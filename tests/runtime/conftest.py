"""
Pytest fixtures for KNX IoT runtime conformance tests.

Starts the headless runtime_test_server as a subprocess, waits for the
"RUNTIME_TEST_SERVER_READY" sentinel, then yields the device's CoAP endpoint.

Performs SPAKE2+ handshake and OSCORE provisioning so that tests can
make authenticated requests to the DUT.

After all tests, sends SIGINT to shut it down.
"""

import os
import platform
import re
import signal
import socket
import subprocess
import threading
import time
from pathlib import Path

import cbor2
import pytest

from coap_client import CoapClient, APPLICATION_CBOR
from knx_spake2plus import Spake2PlusClient
from knx_oscore import OscoreContext


# ---------------------------------------------------------------------------
# Test ordering — strict numeric EITT order
# ---------------------------------------------------------------------------

def _eitt_sort_key(item):
    """Extract numeric EITT test ID from function name for sorting.

    Function names encode EITT IDs as: test_5_X_Y_Z[letter][_stepN]_description
    Examples:
      test_5_1_1_1_site_local     → (5, 1, 1, 1, 0, 0)
      test_5_1_1_3_step2_rt       → (5, 1, 1, 3, 0, 2)
      test_5_1_1_3b_invalid       → (5, 1, 1, 3, 2, 0)
      test_5_3_17_6_receive       → (5, 3, 17, 6, 0, 0)
    """
    m = re.match(
        r'test_(\d+)_(\d+)_(\d+)_(\d+)([a-z]?)(?:_step(\d+))?_',
        item.name)
    if m:
        return (
            int(m.group(1)),
            int(m.group(2)),
            int(m.group(3)),
            int(m.group(4)),
            ord(m.group(5)) - ord('a') + 1 if m.group(5) else 0,
            int(m.group(6)) if m.group(6) else 0,
        )
    # Tests without EITT ID in name sort to the end
    return (999, 999, 999, 999, 999, 999)


def pytest_collection_modifyitems(items):
    """Sort ALL tests by their numeric EITT ID.

    Enforces strict numeric execution order matching the EITT reference
    project. Tests are sorted by their parsed ID: (major, section,
    subsection, number, letter_suffix, step). Python's sort is stable,
    so tests with identical keys preserve their definition order.
    """
    items.sort(key=_eitt_sort_key)


# ---------------------------------------------------------------------------
# Early environment check
# ---------------------------------------------------------------------------

def _check_ipv6_loopback():
    """Verify IPv6 is available. Binds to the configured host or ::1."""
    host = os.environ.get("DEVICE_HOST", "::1")
    try:
        s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        s.bind((host, 0))
        s.close()
        print(f"[conftest] IPv6 ({host}) is available")
    except OSError as e:
        # If we can't bind to the specific host, at least check loopback
        try:
            s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            s.bind(("::1", 0))
            s.close()
            print(f"[conftest] IPv6 loopback (::1) is available "
                  f"(DEVICE_HOST={host} not bindable: {e})")
        except OSError as e2:
            pytest.exit(
                f"IPv6 is NOT available: {e2}\n"
                "The runtime tests require IPv6. If running in Docker, "
                "try: docker run --sysctl net.ipv6.conf.all.disable_ipv6=0 ..."
            )


_check_ipv6_loopback()

# ---------------------------------------------------------------------------
# Multicast scope — must match the C stack's KNX_MULTICAST_SCOPE
# ---------------------------------------------------------------------------
# Default: 5 (site-local, production).
# Set KNX_MULTICAST_SCOPE=2 for link-local (same-machine dev/test on Windows).
MC_SCOPE = int(os.environ.get("KNX_MULTICAST_SCOPE", "5"))


# ---------------------------------------------------------------------------
# External DUT mode detection
# ---------------------------------------------------------------------------

# When no RUNTIME_TEST_SERVER env var is set AND no binary is found in standard
# build dirs, the suite runs in "external DUT mode": it expects an already-
# running DUT on the network.  If DEVICE_HOST is set, it connects directly;
# otherwise, it performs KNX multicast discovery using the serial number.
#
# Set DUT_SERIAL_NUMBER env var to override the default serial number.

def _is_external_dut_mode() -> bool:
    """Return True if we should skip starting a local server process."""
    if os.environ.get("EXTERNAL_DUT", "").lower() in ("1", "true", "yes"):
        return True
    return False


_external_dut = _is_external_dut_mode()


# ---------------------------------------------------------------------------
# Locate the test server binary
# ---------------------------------------------------------------------------

def _find_server_binary() -> Path:
    """Search common build directories for the runtime_test_server binary."""
    repo = Path(__file__).resolve().parents[2]  # tests/runtime -> repo root

    candidates = []
    if platform.system() == "Windows":
        exe = "runtime_test_server.exe"
    else:
        exe = "runtime_test_server"

    # Check preset build dirs
    for preset in ("linux-test-gcc", "linux-test-asan",
                   "windows-test-gcc", "windows-test-msvc"):
        p = repo / "build" / preset / "tests" / exe
        candidates.append(p)

    # Also check relative to CWD (for CI)
    candidates.append(Path("tests") / exe)
    candidates.append(Path(exe))

    # Allow override via env var
    env = os.environ.get("RUNTIME_TEST_SERVER")
    if env:
        candidates.insert(0, Path(env))

    for c in candidates:
        if c.exists():
            return c

    # If nothing found, try which / where
    return Path(exe)


# ---------------------------------------------------------------------------
# Multicast DUT discovery
# ---------------------------------------------------------------------------

LINK_FORMAT = 40  # application/link-format

def _get_ipv6_interface_indices() -> list:
    """Return a list of interface indices that have IPv6 addresses.

    Filters out loopback (index 1) and deduplicates.
    """
    indices = set()
    try:
        for info in socket.getaddrinfo(
                socket.gethostname(), None, socket.AF_INET6):
            # info = (family, type, proto, canonname, (host, port, flow, scope))
            scope_id = info[4][3]
            if scope_id and scope_id != 1:  # skip loopback
                indices.add(scope_id)
    except OSError:
        pass
    return sorted(indices)


def _try_discover(client, path: str, scope: int,
                  interface: str = None, scope_id: int = 0,
                  timeout: float = 2.0) -> tuple:
    """Single discovery attempt.  Returns (host, port) or None."""
    try:
        responses = client.multicast_get(
            path, scope=scope, accept=LINK_FORMAT,
            interface=interface, collect_timeout=timeout)
    except OSError as e:
        print(f"[conftest] multicast on scope={scope} failed: {e}")
        return None
    for resp in responses:
        if resp.is_successful and resp.source_addr:
            host = resp.source_addr[0]
            port = resp.source_addr[1]
            if "%" in host:
                host = host.split("%")[0]
            return (host, port)
    return None


def discover_dut(serial_number: str,
                 interface: str = None,
                 timeout: float = 5.0,
                 scope: int = MC_SCOPE) -> tuple:
    """Discover a KNX IoT device by serial number via multicast.

    Sends a multicast GET to the KNX multicast address for the configured
    scope with path /.well-known/core?ep=knx://sn.<serial> and returns the
    first responder's (host, port, iface_index).

    When no interface is specified, tries the OS default first, then
    iterates through all available IPv6 interfaces (important on Windows
    where scope_id=0 often picks the wrong adapter).

    Args:
        serial_number: KNX serial number (e.g. "00fa10020800")
        interface: Network interface name (e.g. "eth0")
        timeout: How long to wait for a response
        scope: IPv6 multicast scope (2=link, 5=site)

    Returns:
        (host, port, iface_index) tuple of the discovered device.
        iface_index is 0 when the OS default route was used.

    Raises:
        RuntimeError: if no device responds within timeout
    """
    client = CoapClient(host="::1", port=5683)
    try:
        path = f".well-known/core?ep=knx://sn.{serial_number}"

        # If a specific interface was given, just try that
        if interface or os.environ.get("DEVICE_IFACE"):
            result = _try_discover(client, path, scope,
                                   interface=interface, timeout=timeout)
            if result:
                print(f"[conftest] Discovered DUT at [{result[0]}]:{result[1]} "
                      f"(serial={serial_number})")
                return (result[0], result[1], 0)
            iface = interface or os.environ.get("DEVICE_IFACE")
            raise RuntimeError(
                f"No KNX device with serial '{serial_number}' responded "
                f"to multicast discovery within {timeout}s on scope {scope}. "
                f"Interface: {iface}")

        # No interface specified — try OS default first (fast path for Linux)
        result = _try_discover(client, path, scope, timeout=min(timeout, 2.0))
        if result:
            print(f"[conftest] Discovered DUT at [{result[0]}]:{result[1]} "
                  f"(serial={serial_number}, scope_id=0)")
            return (result[0], result[1], 0)

        # OS default didn't work — iterate through real IPv6 interfaces
        # trying both site-local (5) and link-local (2) scopes per interface
        # (needed on Windows where scope_id=0 often picks a Hyper-V adapter)
        iface_indices = _get_ipv6_interface_indices()
        scopes_to_try = [scope]
        if scope != 2:
            scopes_to_try.append(2)  # also try link-local

        if iface_indices:
            attempts = len(iface_indices) * len(scopes_to_try)
            print(f"[conftest] OS default multicast failed, trying "
                  f"{len(iface_indices)} interfaces x {len(scopes_to_try)} "
                  f"scopes ({attempts} attempts)")
            per_attempt = max(0.5, (timeout - 2.0) / attempts)
            for idx in iface_indices:
                for try_scope in scopes_to_try:
                    os.environ["_DISCOVERY_SCOPE_ID"] = str(idx)
                    result = _try_discover(
                        client, path, try_scope, scope_id=idx,
                        timeout=per_attempt)
                    os.environ.pop("_DISCOVERY_SCOPE_ID", None)
                    if result:
                        print(f"[conftest] Discovered DUT at [{result[0]}]"
                              f":{result[1]} (serial={serial_number}, "
                              f"iface={idx}, scope={try_scope})")
                        return (result[0], result[1], idx)

        raise RuntimeError(
            f"No KNX device with serial '{serial_number}' responded "
            f"to multicast discovery within {timeout}s. "
            f"Tried OS default + {len(iface_indices)} interfaces x "
            f"{len(scopes_to_try)} scopes")
    finally:
        client.close()


# ---------------------------------------------------------------------------
# Early discovery for external DUT mode
# ---------------------------------------------------------------------------
# When EXTERNAL_DUT is set and no DEVICE_HOST is given, run discovery NOW
# (at import time) so that discovered host/port/interface are available
# before test modules' pytestmark skipif checks run.

_discovered_host: str = ""
_discovered_port: int = 0
_discovered_iface: int = 0

DUT_SERIAL_NUMBER = os.environ.get("DUT_SERIAL_NUMBER", "00fa10020800")

if _external_dut and not os.environ.get("DEVICE_HOST"):
    try:
        _iface = os.environ.get("DEVICE_IFACE")
        _dh, _dp, _di = discover_dut(
            DUT_SERIAL_NUMBER, interface=_iface, timeout=15.0)
        _discovered_host = _dh
        _discovered_port = _dp
        _discovered_iface = _di
        # Auto-set DEVICE_IFACE so multicast test modules see it at
        # import time (their pytestmark checks os.environ).
        if not os.environ.get("DEVICE_IFACE"):
            iface_idx = _di
            # If discovery used OS default (idx=0), resolve the actual
            # interface from the response source address
            if not iface_idx:
                try:
                    # Link-local responses include scope_id directly
                    probe = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
                    probe.connect((_dh, _dp))
                    local_addr = probe.getsockname()
                    iface_idx = local_addr[3]  # scope_id from local addr
                    probe.close()
                except OSError:
                    pass
            if not iface_idx:
                # Global address: find the outgoing interface via routing.
                # Use the first IPv6 interface that has a routable address.
                iface_indices = _get_ipv6_interface_indices()
                if len(iface_indices) == 1:
                    iface_idx = iface_indices[0]
                else:
                    # Multiple interfaces — try a link-local probe
                    try:
                        probe = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
                        probe.connect(("ff02::fd", 5683, 0, 0))
                        iface_idx = probe.getsockname()[3]
                        probe.close()
                    except OSError:
                        if iface_indices:
                            iface_idx = iface_indices[0]
            if iface_idx:
                try:
                    for idx, name in socket.if_nameindex():
                        if idx == iface_idx:
                            os.environ["DEVICE_IFACE"] = name
                            print(f"[conftest] Auto-set DEVICE_IFACE={name} "
                                  f"(index {iface_idx})")
                            break
                except OSError:
                    os.environ["DEVICE_IFACE"] = str(iface_idx)
    except RuntimeError as e:
        print(f"[conftest] Early discovery failed: {e}")


# ---------------------------------------------------------------------------
# Server process management
# ---------------------------------------------------------------------------

SERVER_READY_SENTINEL = "RUNTIME_TEST_SERVER_READY"
SERVER_STARTUP_TIMEOUT = int(os.environ.get("SERVER_STARTUP_TIMEOUT", "15"))

# Shared state: the port detected from the server's startup output.
_detected_port: int = 0


@pytest.fixture(scope="session")
def server_process():
    """Start the runtime_test_server and wait for it to be ready.
    In external DUT mode, yields None (no subprocess to manage)."""
    global _detected_port

    if _external_dut:
        print("\n[conftest] External DUT mode — skipping local server")
        yield None
        return

    binary = _find_server_binary()
    print(f"\n[conftest] Starting server: {binary}")

    # Clean up any leftover storage from previous runs so the AT table
    # is empty and SPAKE2+ handshake can succeed.
    import glob
    import shutil
    for d in glob.glob("runtime_test_storage_*"):
        print(f"[conftest] Removing stale storage: {d}")
        shutil.rmtree(d, ignore_errors=True)
    # Also check the binary's directory
    bin_dir = binary.parent
    for d in glob.glob(str(bin_dir / "runtime_test_storage_*")):
        print(f"[conftest] Removing stale storage: {d}")
        shutil.rmtree(d, ignore_errors=True)

    proc = subprocess.Popen(
        [str(binary)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,  # line-buffered
    )

    # Wait for the ready sentinel
    deadline = time.monotonic() + SERVER_STARTUP_TIMEOUT
    ready = False
    while time.monotonic() < deadline:
        line = proc.stdout.readline()
        if not line:
            if proc.poll() is not None:
                stderr = proc.stderr.read()
                pytest.fail(
                    f"Server exited prematurely (code {proc.returncode}).\n"
                    f"stderr:\n{stderr}"
                )
            time.sleep(0.1)
            continue
        print(f"[server] {line.rstrip()}")
        if SERVER_READY_SENTINEL in line:
            ready = True
            # Parse "RUNTIME_TEST_SERVER_READY port=NNNNN"
            m = re.search(r"port=(\d+)", line)
            if m:
                _detected_port = int(m.group(1))
                print(f"[conftest] Detected server port: {_detected_port}")
            break

    if not ready:
        proc.kill()
        pytest.fail("Server did not become ready within timeout")

    # Drain stdout in a background thread so the server never blocks
    # on a full pipe buffer (classic subprocess deadlock).
    # In CI (RUNTIME_TEST_QUIET=1), suppress server output to avoid log overflow.
    _quiet = os.environ.get("RUNTIME_TEST_QUIET", "0") == "1"

    def _drain(stream, prefix="server", always_print=False):
        for line in stream:
            if always_print or not _quiet:
                print(f"[{prefix}] {line.rstrip()}")

    drain_thread = threading.Thread(target=_drain, args=(proc.stdout, "server"),
                                    daemon=True)
    drain_thread.start()
    # Always print stderr — it carries ASan/UBSan reports and crash diagnostics
    stderr_thread = threading.Thread(target=_drain,
                                     args=(proc.stderr, "server-err"),
                                     kwargs={"always_print": True},
                                     daemon=True)
    stderr_thread.start()

    # Give the network layer a moment to bind
    time.sleep(0.5)

    yield proc

    # Teardown: send SIGINT / terminate
    print("\n[conftest] Stopping server...")
    if platform.system() == "Windows":
        proc.terminate()
    else:
        proc.send_signal(signal.SIGINT)

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

    print(f"[conftest] Server exited with code {proc.returncode}")


# ---------------------------------------------------------------------------
# CoAP client helpers
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def coap_port(server_process):
    """The CoAP port the DUT listens on.

    Priority: COAP_PORT env -> auto-detected from server stdout ->
    early discovery result -> default 5683."""

    env = os.environ.get("COAP_PORT")
    if env:
        return int(env)
    if _detected_port:
        return _detected_port
    if _discovered_port:
        return _discovered_port

    return 5683


@pytest.fixture(scope="session")
def device_host(coap_port):
    """The IPv6 address of the DUT.

    Priority: DEVICE_HOST env -> early discovery result -> loopback."""
    env = os.environ.get("DEVICE_HOST")
    if env:
        return env
    if _discovered_host:
        return _discovered_host
    return "::1"


@pytest.fixture(scope="session")
def device_iface():
    """The network interface name for multicast (e.g. 'veth-test').
    When set, multicast tests send via this interface.
    Defaults to None (OS picks the interface)."""
    return os.environ.get("DEVICE_IFACE")


@pytest.fixture(scope="session")
def mc_scope():
    """IPv6 multicast scope matching the stack's KNX_MULTICAST_SCOPE.
    2 = link-local (dev/test), 5 = site-local (production)."""
    return MC_SCOPE


@pytest.fixture(scope="session")
def coap(server_process, device_host, coap_port):
    """A shared CoapClient instance for the entire test session.
    Uses a single UDP socket to avoid aiocoap's context exhaustion bug.
    Set COAP_TIMEOUT env var to increase timeouts (e.g. under ASan)."""
    timeout = float(os.environ.get("COAP_TIMEOUT", "5.0"))
    client = CoapClient(host=device_host, port=coap_port, timeout=timeout)
    yield client
    client.close()


# ---------------------------------------------------------------------------
# SPAKE2+ and OSCORE provisioning
# ---------------------------------------------------------------------------

# Device password — must match app_get_password() in runtime_test_server.c
DEVICE_PASSWORD = os.environ.get("DEVICE_PASSWORD", "2X4W3TE0DFLLS19Y1FCH")

# DUT identity — matches EITT configuration
DUT_IA = 0x1101
DUT_IID = 0x1199887766
DUT_IA_HEX = "1101"
DUT_IID_HEX = "1199887766"

# Full-scope AT entry credentials
TEST_TOKEN_ID = "RuntimeTest"
TEST_SENDER_ID = b"RtTest"
TEST_MASTER_SECRET = os.urandom(16)

# All scopes needed to access all resource types
ALL_SCOPES = [
    "if.i", "if.o", "if.g.s", "if.p", "if.d",
    "if.a", "if.s", "if.c", "if.sec", "if.swu",
]


# ---------------------------------------------------------------------------
# Shared helpers — used by multiple test files
# ---------------------------------------------------------------------------

def auth_prepare(coap, oscore_ctx):
    """AUTH PREPARATION — matches EITT 'Copy of AUTH PREPARATION'.

    1. Factory reset (erase-code 2)
    2. SPAKE2+ (PASE) handshake
    3. Create session access token via PASE context
    4. Delete temporary PASE token (implicit — replaced by session AT)
    5. Update oscore_ctx to use the new session credentials

    After this, the device is in factory-reset state with a fresh
    session AT installed.  IA/IID are NOT set yet.
    """
    coap.post("/test/factory-reset", timeout=5)
    time.sleep(2)
    coap.drain_socket(timeout=0.5)

    spake = Spake2PlusClient(password=DEVICE_PASSWORD, sender_id="Repro")
    req1 = cbor2.dumps(spake.create_parameter_request())
    resp1 = coap.post("/.well-known/knx/spake", payload=req1,
                      content_format=APPLICATION_CBOR, timeout=30)
    assert resp1 is not None and resp1.is_successful
    spake.process_parameter_response(cbor2.loads(resp1.payload))

    req2 = cbor2.dumps(spake.create_key_exchange_request())
    resp2 = coap.post("/.well-known/knx/spake", payload=req2,
                      content_format=APPLICATION_CBOR, timeout=30)
    assert resp2 is not None and resp2.is_successful
    spake.process_key_exchange_response(cbor2.loads(resp2.payload))

    req3 = cbor2.dumps(spake.create_confirmation_request())
    resp3 = coap.post("/.well-known/knx/spake", payload=req3,
                      content_format=APPLICATION_CBOR, timeout=10)
    assert resp3 is not None and resp3.is_successful

    pase_ctx = OscoreContext(
        master_secret=spake.shared_key,
        sender_id=spake.sender_id.encode("utf-8"),
        recipient_id=b"",
    )
    new_ms = os.urandom(16)
    new_sid = b"RtTest"
    at_inner = {
        0: "RuntimeTest",
        9: ALL_SCOPES,
        38: 2,
        8: {4: {0: new_sid, 2: new_ms}},
    }
    resp = coap.oscore_post(pase_ctx, "/auth/at",
                            payload=cbor2.dumps({0: at_inner}), timeout=10)
    assert resp is not None and resp.is_successful

    new_ctx = OscoreContext(
        master_secret=new_ms, sender_id=new_sid, recipient_id=b"")

    oscore_ctx.master_secret = new_ctx.master_secret
    oscore_ctx.sender_id = new_ctx.sender_id
    oscore_ctx.recipient_id = new_ctx.recipient_id
    oscore_ctx.id_context = new_ctx.id_context
    oscore_ctx.ssn = new_ctx.ssn
    oscore_ctx.sender_key = new_ctx.sender_key
    oscore_ctx.recipient_key = new_ctx.recipient_key
    oscore_ctx.common_iv = new_ctx.common_iv


def ia_prepare(coap, oscore_ctx, ia=DUT_IA, iid=DUT_IID):
    """IA PREPARATION — matches EITT 'Copy of IA PREPARATION'.

    Sets the individual address and installation ID, then the device
    restarts.  Must be called after auth_prepare().
    """
    ia_iid = cbor2.dumps({12: ia, 26: iid})
    resp = coap.oscore_post(oscore_ctx, "/.well-known/knx/ia",
                            payload=ia_iid)
    assert resp is not None and resp.is_successful, (
        f"IA preparation failed: {resp.code if resp else 'timeout'}")



def set_lsm(coap, oscore_ctx, cmd):
    """Set LSM state: 1=loading, 2=loaded, 4=unload. Returns response."""
    resp = coap.oscore_post(
        oscore_ctx, "/a/lsm", payload=cbor2.dumps({2: cmd}))
    assert resp is not None and resp.is_successful, (
        f"LSM cmd={cmd} failed: {resp.code if resp else 'timeout'}")
    return resp


def parse_link_format(payload: bytes) -> list[dict]:
    """Parse a CoRE Link Format (RFC 6690) payload into a list of link dicts."""
    text = payload.decode("utf-8", errors="replace")
    links = []
    for entry in text.split(","):
        entry = entry.strip()
        if not entry:
            continue
        m = re.match(r"<([^>]*)>", entry)
        if not m:
            continue
        link = {"href": m.group(1)}
        for attr in re.finditer(r';(\w+)=("([^"]*)"|([\w.:/*-]+))', entry):
            key = attr.group(1)
            val = attr.group(3) if attr.group(3) is not None else attr.group(4)
            link[key] = val
        links.append(link)
    return links


@pytest.fixture(scope="session")
def oscore_ctx(coap):
    """Perform SPAKE2+ handshake, provision a full-scope AT entry, and return
    an OscoreContext that can make authenticated requests to any resource."""

    # ---- Step 1: SPAKE2+ handshake ----
    spake = Spake2PlusClient(password=DEVICE_PASSWORD, sender_id="PaseTmp")

    # Step 1a: Send parameter request
    req1_cbor = cbor2.dumps(spake.create_parameter_request())
    resp1 = coap.post("/.well-known/knx/spake", payload=req1_cbor,
                      content_format=APPLICATION_CBOR, timeout=30)
    assert resp1 is not None, "SPAKE2+ step 1 timed out"

    # If the device already has AT entries (e.g. from a previous test run),
    # SPAKE2+ returns 4.00.  The EITT does a factory reset (code 2) via
    # OSCORE before each test section; we use /test/factory-reset instead
    # (unprotected test-control endpoint registered by both
    # runtime_test_server and eitt_virtual_gui).
    if not resp1.is_successful:
        print("[conftest] SPAKE2+ step 1 returned "
              f"{resp1.code} — attempting /test/factory-reset")
        rst = coap.post("/test/factory-reset", timeout=5)
        assert rst is not None and rst.is_successful, (
            f"Factory reset failed ({rst.code if rst else 'timeout'}); "
            "device AT table is not empty and /test/factory-reset is "
            "unavailable — restart the DUT manually")
        time.sleep(2)
        coap.drain_socket(timeout=0.5)
        # Retry SPAKE2+ with a fresh client
        spake = Spake2PlusClient(password=DEVICE_PASSWORD, sender_id="PaseTmp")
        req1_cbor = cbor2.dumps(spake.create_parameter_request())
        resp1 = coap.post("/.well-known/knx/spake", payload=req1_cbor,
                          content_format=APPLICATION_CBOR, timeout=30)
        assert resp1 is not None, "SPAKE2+ step 1 timed out after reset"
        assert resp1.is_successful, (
            f"SPAKE2+ step 1 still fails after reset: {resp1.code}")

    spake.process_parameter_response(cbor2.loads(resp1.payload))

    # Step 2: Send shareP, get shareV + confirmV
    req2_cbor = cbor2.dumps(spake.create_key_exchange_request())
    resp2 = coap.post("/.well-known/knx/spake", payload=req2_cbor,
                      content_format=APPLICATION_CBOR, timeout=30)
    assert resp2 is not None, "SPAKE2+ step 2 timed out"
    assert resp2.is_successful, f"SPAKE2+ step 2 failed: {resp2.code}"
    spake.process_key_exchange_response(cbor2.loads(resp2.payload))

    # Step 3: Send confirmP
    req3_cbor = cbor2.dumps(spake.create_confirmation_request())
    resp3 = coap.post("/.well-known/knx/spake", payload=req3_cbor,
                      content_format=APPLICATION_CBOR, timeout=10)
    assert resp3 is not None, "SPAKE2+ step 3 timed out"
    assert resp3.is_successful, f"SPAKE2+ step 3 failed: {resp3.code}"

    print(f"[conftest] SPAKE2+ handshake complete,"
          f" shared_key={spake.shared_key.hex()}")

    # ---- Step 2: Create PASE OSCORE context (if.sec scope) ----
    pase_sender_id = spake.sender_id.encode("utf-8")
    pase_ctx = OscoreContext(
        master_secret=spake.shared_key,
        sender_id=pase_sender_id,
        recipient_id=b"",  # server has no sender ID for PASE
    )

    # ---- Step 3: Provision full-scope AT entry via /auth/at ----
    at_inner = {
        0: TEST_TOKEN_ID,  # id (string)
        9: ALL_SCOPES,     # scope (string array)
        38: 2,             # profile = coap_oscore
        8: {               # cnf
            4: {           # osc
                0: TEST_SENDER_ID,      # id (byte string)
                2: TEST_MASTER_SECRET,  # ms (byte string)
            }
        }
    }
    # Server expects AT entry wrapped in an outer map (oc_parse_rep unwraps
    # the top-level map, then the handler looks for OC_REP_OBJECT entries)
    at_cbor = cbor2.dumps({0: at_inner})
    resp_at = coap.oscore_post(pase_ctx, "/auth/at", payload=at_cbor,
                               timeout=10)
    assert resp_at is not None, "AT provisioning timed out"
    assert resp_at.is_successful, f"AT provisioning failed: {resp_at.code}"

    print(f"[conftest] Provisioned AT entry '{TEST_TOKEN_ID}' with "
          f"sender_id={TEST_SENDER_ID.hex()}")

    # ---- Step 4: Create full-scope OSCORE context ----
    full_ctx = OscoreContext(
        master_secret=TEST_MASTER_SECRET,
        sender_id=TEST_SENDER_ID,
        recipient_id=b"",
    )

    # ---- Step 5: Set IID and IA so the device enters runtime state ----
    # oc_is_device_in_runtime() requires iid != 0 && lsm_s == LSM_S_LOADED.
    # Without this, multicast handlers (/k, .well-known/core GA) silently
    # ignore requests.
    # Values match EITT configuration:
    #   IID = 0x1199887766 (75599452006 decimal)
    #   IA  = 0x1101 (4353 decimal)
    # Both must be set together via POST to /.well-known/knx/ia
    # (CBOR keys: 12=ia, 26=iid, 25=fid optional)
    ia_iid_payload = cbor2.dumps({12: 0x1101, 26: 0x1199887766})
    resp_ia = coap.oscore_post(
        full_ctx, "/.well-known/knx/ia", payload=ia_iid_payload)
    assert resp_ia is not None, "IA/IID POST timed out"
    assert resp_ia.is_successful, f"IA/IID POST failed: {resp_ia.code}"
    print("[conftest] Set IID=0x1199887766, IA=0x1101 for runtime state (matches EITT)")

    return full_ctx


# ---------------------------------------------------------------------------
# Per-test pass/fail output
# ---------------------------------------------------------------------------

def pytest_runtest_logreport(report):
    """Write PASSED / FAILED / ERROR / SKIPPED on its own line via the terminal reporter."""
    if report.when == "setup" and report.skipped:
        status = "SKIPPED"
    elif report.when == "setup" and report.failed:
        status = "ERROR"
    elif report.when == "call" and report.passed:
        status = "PASSED"
    elif report.when == "call" and report.failed:
        status = "FAILED"
    elif report.when == "call" and report.skipped:
        status = "SKIPPED"
    else:
        return
    import sys
    sys.stdout.write(f"\n[{status}] {report.nodeid}\n")
    sys.stdout.flush()
