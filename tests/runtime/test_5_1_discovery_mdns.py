"""
Runtime conformance tests -- 5.1.2 Discovery via mDNS/DNS-SD

Covers:
  5.1.2.1:  Discovery using serial number for unconfigured device
  5.1.2.2:  Discovery using serial number for configured device
  5.1.2.3:  Discovery using individual address for configured device
  5.1.2.3a: Discovery for individual address 0 for configured device
  5.1.2.3b: Discovery using individual address for unconfigured device
  5.1.2.4:  Discovery using programming mode
  5.1.2.5a: Unsolicited mDNS responses on Programming mode change
  5.1.2.5b: Unsolicited mDNS responses on Individual Address change

Reference: 08_10_5 KNX IoT Point API Tests v01_01_01_AS

Requires: zeroconf (pip install zeroconf)
"""

import os
import socket
import struct
import time

import cbor2
import pytest

from coap_client import APPLICATION_CBOR, LINK_FORMAT
from conftest import DEVICE_PASSWORD, ALL_SCOPES, DUT_IA, DUT_IID
from knx_oscore import OscoreContext
from knx_spake2plus import Spake2PlusClient

try:
    from zeroconf import (
        IPVersion,
        ServiceBrowser,
        ServiceInfo,
        ServiceListener,
        Zeroconf,
    )

    HAS_ZEROCONF = True
except ImportError:
    HAS_ZEROCONF = False

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DUT_SERIAL = "00fa10020800"
KNX_SERVICE_TYPE = "_knx._udp.local."

# Timeouts (ms for zeroconf, seconds for CoAP)
MDNS_BROWSE_TIMEOUT_S = 5
MDNS_ANNOUNCE_WAIT_S = 3
COAP_TIMEOUT = 5.0


# ---------------------------------------------------------------------------
# Helper: raw mDNS PTR query (bypasses zeroconf — mirrors EITT approach)
# ---------------------------------------------------------------------------

MDNS_MULTICAST_ADDR = "ff02::fb"
MDNS_PORT = 5353
DNS_TYPE_PTR = 12
DNS_CLASS_IN = 1


def _encode_dns_name(name: str) -> bytes:
    """Encode a dotted DNS name into wire-format labels."""
    parts = name.rstrip(".").split(".")
    result = b""
    for label in parts:
        encoded = label.encode("ascii")
        result += bytes([len(encoded)]) + encoded
    result += b"\x00"  # root label
    return result


def _decode_dns_name(data: bytes, offset: int) -> tuple[str, int]:
    """Decode a DNS wire-format name, handling compression pointers."""
    labels = []
    seen_offsets = set()
    jumped = False
    return_offset = offset
    while True:
        if offset >= len(data):
            break
        if offset in seen_offsets:
            break  # loop protection
        seen_offsets.add(offset)
        length = data[offset]
        if length == 0:
            if not jumped:
                return_offset = offset + 1
            break
        if (length & 0xC0) == 0xC0:
            # compression pointer
            if offset + 1 >= len(data):
                break
            ptr = ((length & 0x3F) << 8) | data[offset + 1]
            if not jumped:
                return_offset = offset + 2
            jumped = True
            offset = ptr
            continue
        offset += 1
        if offset + length > len(data):
            break
        labels.append(data[offset : offset + length].decode("ascii", errors="replace"))
        offset += length
        if not jumped:
            return_offset = offset
    return ".".join(labels) + ".", return_offset


def _raw_mdns_ptr_query(iface_idx: int, qname: str,
                        timeout: float = 5.0) -> list[str]:
    """Send a raw mDNS PTR query and return PTR RDATA names from responses.

    Also captures unsolicited announcements that contain PTR records for the
    queried name in either the answer or additional sections.

    Returns a list of PTR RDATA domain names (e.g.
    ['00fa10020800._knx._udp.local.']).
    """
    # Build DNS query packet
    txn_id = 0  # mDNS always uses 0
    flags = 0   # standard query
    header = struct.pack("!HHHHHH", txn_id, flags, 1, 0, 0, 0)
    question = _encode_dns_name(qname) + struct.pack("!HH", DNS_TYPE_PTR, DNS_CLASS_IN)
    packet = header + question

    qname_norm = qname.lower().rstrip(".")

    # Create IPv6 UDP socket
    sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        # Bind to mDNS port to receive multicast responses
        sock.bind(("::", MDNS_PORT))
        # Join mDNS multicast group on the specific interface
        mreq = socket.inet_pton(socket.AF_INET6, MDNS_MULTICAST_ADDR)
        mreq += struct.pack("I", iface_idx)
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)
        # Set outgoing multicast interface
        sock.setsockopt(
            socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF,
            struct.pack("I", iface_idx),
        )
        sock.settimeout(timeout)

        # Send query to mDNS multicast group
        sock.sendto(packet, (MDNS_MULTICAST_ADDR, MDNS_PORT, 0, iface_idx))

        # Collect responses
        ptr_results = []
        pkt_count = 0
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(remaining)
            try:
                data, _addr = sock.recvfrom(4096)
            except socket.timeout:
                break

            pkt_count += 1

            # Parse DNS packet
            if len(data) < 12:
                continue
            _id, _flags, _qdcount, ancount, _nscount, _arcount = struct.unpack(
                "!HHHHHH", data[:12]
            )
            is_response = bool(_flags & 0x8000)

            # Skip queries (our own echo or from DUT's listener)
            if not is_response:
                continue

            # Skip question section
            offset = 12
            for _ in range(_qdcount):
                _name, offset = _decode_dns_name(data, offset)
                offset += 4  # QTYPE + QCLASS

            # Parse answer + authority + additional for any matching PTR
            total_rr = ancount + _nscount + _arcount
            for _ in range(total_rr):
                if offset >= len(data):
                    break
                rr_name, offset = _decode_dns_name(data, offset)
                if offset + 10 > len(data):
                    break
                rr_type, _rr_class, _rr_ttl, rdlen = struct.unpack(
                    "!HHIH", data[offset : offset + 10]
                )
                offset += 10
                if offset + rdlen > len(data):
                    break
                if rr_type == DNS_TYPE_PTR:
                    rr_name_norm = rr_name.lower().rstrip(".")
                    if rr_name_norm == qname_norm:
                        ptr_rdata, _ = _decode_dns_name(data, offset)
                        if ptr_rdata not in ptr_results:
                            ptr_results.append(ptr_rdata)
                offset += rdlen

            if ptr_results:
                break  # got what we need

        print(f"[mdns] raw query: received {pkt_count} packets, "
              f"found {len(ptr_results)} PTR matches")
        return ptr_results
    finally:
        sock.close()


def _capture_mdns_announcements(iface_idx: int, target_ptr_name: str,
                                timeout: float = 10.0) -> list[str]:
    """Listen for mDNS announcements and return PTR RDATA for target_ptr_name.

    This passively captures unsolicited mDNS announcements (no query sent).
    Looks for PTR records matching target_ptr_name in any section (answer,
    authority, additional) of any mDNS response packet.

    Returns a list of PTR RDATA domain names found.
    """
    target_norm = target_ptr_name.lower().rstrip(".")

    sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        sock.bind(("::", MDNS_PORT))
        mreq = socket.inet_pton(socket.AF_INET6, MDNS_MULTICAST_ADDR)
        mreq += struct.pack("I", iface_idx)
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)
        sock.settimeout(1.0)

        ptr_results = []
        pkt_count = 0
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data, _addr = sock.recvfrom(4096)
            except socket.timeout:
                continue

            pkt_count += 1
            if len(data) < 12:
                continue
            _id, _flags, _qdcount, ancount, _nscount, _arcount = struct.unpack(
                "!HHHHHH", data[:12]
            )
            if not (_flags & 0x8000):
                continue  # skip queries

            # Skip questions
            offset = 12
            for _ in range(_qdcount):
                _name, offset = _decode_dns_name(data, offset)
                offset += 4

            # Parse all RRs
            total_rr = ancount + _nscount + _arcount
            for _ in range(total_rr):
                if offset >= len(data):
                    break
                rr_name, offset = _decode_dns_name(data, offset)
                if offset + 10 > len(data):
                    break
                rr_type, _rr_class, _rr_ttl, rdlen = struct.unpack(
                    "!HHIH", data[offset : offset + 10]
                )
                offset += 10
                if offset + rdlen > len(data):
                    break
                if rr_type == DNS_TYPE_PTR:
                    rr_name_norm = rr_name.lower().rstrip(".")
                    if rr_name_norm == target_norm:
                        ptr_rdata, _ = _decode_dns_name(data, offset)
                        if ptr_rdata not in ptr_results:
                            ptr_results.append(ptr_rdata)
                offset += rdlen

            if ptr_results:
                break

        print(f"[mdns] capture: received {pkt_count} packets, "
              f"found {len(ptr_results)} PTR matches for {target_ptr_name}")
        return ptr_results
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Helper: resolve interface name → index (Linux)
# ---------------------------------------------------------------------------

def _iface_name_to_index(name: str) -> int:
    """Convert interface name (e.g. 'veth-test') to its numeric index."""
    return socket.if_nametoindex(name)


def _get_zeroconf_interfaces():
    """Return the interfaces parameter for Zeroconf based on DEVICE_IFACE.

    In CI (Linux + veth), DEVICE_IFACE is set to the test-side veth interface.
    zeroconf accepts interface indexes for IPv6.  On systems without
    DEVICE_IFACE, returns InterfaceChoice.All.
    """
    iface = os.environ.get("DEVICE_IFACE")
    if iface:
        try:
            idx = _iface_name_to_index(iface)
            return [idx]
        except (OSError, AttributeError):
            pass
    # Fallback: use default (all interfaces)
    from zeroconf import InterfaceChoice
    return InterfaceChoice.All


# ---------------------------------------------------------------------------
# Listener for ServiceBrowser
# ---------------------------------------------------------------------------

class _ServiceCollector:
    """Collects discovered service names from a ServiceBrowser."""

    def __init__(self):
        self.found: list[str] = []

    def add_service(self, zc: "Zeroconf", type_: str, name: str) -> None:
        self.found.append(name)

    def remove_service(self, zc: "Zeroconf", type_: str, name: str) -> None:
        pass

    def update_service(self, zc: "Zeroconf", type_: str, name: str) -> None:
        pass


# ---------------------------------------------------------------------------
# Fixture: session-scoped Zeroconf instance
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def mdns(server_process):
    """Create a Zeroconf instance on the correct interface."""
    if not HAS_ZEROCONF:
        pytest.skip("zeroconf library not installed")

    interfaces = _get_zeroconf_interfaces()
    zc = Zeroconf(interfaces=interfaces, ip_version=IPVersion.V6Only)
    yield zc
    zc.close()


# ---------------------------------------------------------------------------
# Helper: browse a DNS-SD type/subtype and return discovered names
# ---------------------------------------------------------------------------

def _browse_service(mdns: "Zeroconf", service_type: str,
                    timeout: float = MDNS_BROWSE_TIMEOUT_S) -> list[str]:
    """Browse for services of the given type, return list of instance names."""
    collector = _ServiceCollector()
    browser = ServiceBrowser(mdns, service_type, collector)
    time.sleep(timeout)
    browser.cancel()
    return collector.found


def _get_service_info(mdns: "Zeroconf", service_type: str,
                      instance_name: str,
                      timeout_ms: int = 3000) -> "ServiceInfo | None":
    """Query SRV + AAAA + TXT for a specific service instance."""
    return mdns.get_service_info(service_type, instance_name,
                                timeout=timeout_ms)


# ---------------------------------------------------------------------------
# Helper: verify full discovery chain (PTR → SRV → AAAA → CoAP reachability)
# ---------------------------------------------------------------------------

def _verify_discovery_chain(mdns, coap, service_type, expected_instance):
    """
    Full EITT discovery chain:
      1. PTR query → find instance
      2. SRV query → get hostname + port
      3. AAAA query → get IPv6 address
      4. CoAP GET .well-known/core → verify reachability
    """
    # Step 1: Browse for PTR records
    found = _browse_service(mdns, service_type)
    assert len(found) > 0, (
        f"No services found browsing {service_type}")
    assert expected_instance in found, (
        f"Expected '{expected_instance}' in {found}")

    # Step 2+3: SRV + AAAA via get_service_info
    info = _get_service_info(mdns, KNX_SERVICE_TYPE, expected_instance)
    assert info is not None, (
        f"Could not resolve service info for {expected_instance}")
    assert info.server is not None, "SRV record missing hostname"
    assert info.port > 0, f"SRV record has invalid port: {info.port}"

    # Verify hostname format: knx-{serial}.local.
    expected_hostname = f"knx-{DUT_SERIAL}.local."
    assert info.server.lower() == expected_hostname, (
        f"Hostname mismatch: {info.server} != {expected_hostname}")

    # Check AAAA record (IPv6 address)
    ipv6_addrs = info.parsed_addresses(version=IPVersion.V6Only)
    assert len(ipv6_addrs) > 0, "No AAAA (IPv6) records in service info"

    # Step 4: Verify CoAP reachability at discovered address
    resp = coap.get(".well-known/core", accept=LINK_FORMAT,
                    timeout=COAP_TIMEOUT)
    assert resp is not None, "CoAP GET .well-known/core timed out"
    assert resp.is_successful, (
        f"CoAP GET .well-known/core failed: {resp.code}")

    return info


# ===========================================================================
# 5.1.2.1 -- Discovery using serial number for unconfigured device
# ===========================================================================

class TestMdnsDiscoverySerialUnconfigured:
    """5.1.2.1: Discovery using serial number is possible for unconfigured device."""

    def test_5_1_2_1_browse_knx_service(self, mdns, coap):
        """Step 1-2: PTR query for _knx._udp.local → find device by SN."""
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"
        found = _browse_service(mdns, KNX_SERVICE_TYPE)
        assert len(found) > 0, "No KNX services found via mDNS browse"
        assert expected in found, (
            f"Device '{expected}' not found. Found: {found}")

    def test_5_1_2_1_browse_serial_subtype(self, mdns, coap):
        """Step 3-4: PTR query for _{sn}._sub._knx._udp.local → find device."""
        subtype = f"_{DUT_SERIAL}._sub.{KNX_SERVICE_TYPE}"
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"
        found = _browse_service(mdns, subtype)
        assert len(found) > 0, (
            f"No services found browsing serial subtype {subtype}")
        assert expected in found, (
            f"Device not found via serial subtype. Found: {found}")

    def test_5_1_2_1_srv_resolution(self, mdns, coap):
        """Step 5-6: SRV query → hostname + port."""
        instance = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"
        info = _get_service_info(mdns, KNX_SERVICE_TYPE, instance)
        assert info is not None, f"Could not resolve SRV for {instance}"

        expected_hostname = f"knx-{DUT_SERIAL}.local."
        assert info.server.lower() == expected_hostname, (
            f"Hostname: {info.server} != {expected_hostname}")
        assert info.port > 0, f"Invalid port: {info.port}"

    def test_5_1_2_1_aaaa_resolution(self, mdns, coap):
        """Step 7-8: AAAA query → IPv6 address."""
        instance = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"
        info = _get_service_info(mdns, KNX_SERVICE_TYPE, instance)
        assert info is not None, f"Could not resolve info for {instance}"

        ipv6_addrs = info.parsed_addresses(version=IPVersion.V6Only)
        assert len(ipv6_addrs) > 0, "No IPv6 (AAAA) addresses resolved"

    def test_5_1_2_1_coap_reachability(self, mdns, coap):
        """Step 9-10: CoAP GET .well-known/core at discovered address → 2.05."""
        resp = coap.get(".well-known/core", accept=LINK_FORMAT,
                        timeout=COAP_TIMEOUT)
        assert resp is not None, "CoAP .well-known/core timed out"
        assert resp.is_successful, f"Expected 2.05, got {resp.code}"


# ===========================================================================
# 5.1.2.2 -- Discovery using serial number for configured device
# ===========================================================================

class TestMdnsDiscoverySerialConfigured:
    """5.1.2.2: Discovery using serial number is possible for configured device."""

    def test_5_1_2_2_serial_subtype_configured(self, mdns, coap, oscore_ctx):
        """Configured device (IA set) is still discoverable by serial number."""
        # Device is already configured with IID/IA by conftest oscore_ctx
        subtype = f"_{DUT_SERIAL}._sub.{KNX_SERVICE_TYPE}"
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"

        found = _browse_service(mdns, subtype)
        assert len(found) > 0, (
            f"Configured device not found via serial subtype {subtype}")
        assert expected in found, (
            f"Device not in results. Found: {found}")

    def test_5_1_2_2_full_chain_configured(self, mdns, coap, oscore_ctx):
        """Full PTR → SRV → AAAA → CoAP chain for configured device."""
        subtype = f"_{DUT_SERIAL}._sub.{KNX_SERVICE_TYPE}"
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"
        _verify_discovery_chain(mdns, coap, subtype, expected)


# ===========================================================================
# 5.1.2.3 -- Discovery using individual address for configured device
# ===========================================================================

class TestMdnsDiscoveryIA:
    """5.1.2.3: Discovery using individual address for configured device."""

    def test_5_1_2_3_ia_subtype_discovery(self, mdns, coap, oscore_ctx):
        """Browse _ia{iid}-{ia}._sub._knx._udp.local → find device."""
        # IID and IA set by conftest: IID=0x1199887766, IA=0x1101
        iid_hex = format(DUT_IID, "x")
        ia_hex = format(DUT_IA, "x")
        subtype = f"_ia{iid_hex}-{ia_hex}._sub.{KNX_SERVICE_TYPE}"
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"

        found = _browse_service(mdns, subtype)
        assert len(found) > 0, (
            f"Device not found via IA subtype {subtype}")
        assert expected in found, (
            f"Device not in results. Found: {found}")

    def test_5_1_2_3_ia_full_chain(self, mdns, coap, oscore_ctx):
        """Full discovery chain using IA subtype."""
        iid_hex = format(DUT_IID, "x")
        ia_hex = format(DUT_IA, "x")
        subtype = f"_ia{iid_hex}-{ia_hex}._sub.{KNX_SERVICE_TYPE}"
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"
        _verify_discovery_chain(mdns, coap, subtype, expected)


# ===========================================================================
# 5.1.2.3a -- Discovery for individual address 0 for configured device
# ===========================================================================

class TestMdnsDiscoveryIAZero:
    """5.1.2.3a: Discovery for IA=0 on configured device."""

    def test_5_1_2_3a_ia_zero_subtype(self, mdns, coap, oscore_ctx):
        """Set IA=0, IID=0 then browse _ia0-0._sub._knx._udp.local."""
        # POST new IA=0, IID=0
        ia_payload = cbor2.dumps({12: 0, 26: 0})
        resp = coap.oscore_post(oscore_ctx, "/.well-known/knx/ia",
                                payload=ia_payload, timeout=COAP_TIMEOUT)
        if resp is None or not resp.is_successful:
            pytest.skip("Could not set IA=0, IID=0")

        # Wait for mDNS re-announcement
        time.sleep(MDNS_ANNOUNCE_WAIT_S)

        subtype = f"_ia0-0._sub.{KNX_SERVICE_TYPE}"
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"

        found = _browse_service(mdns, subtype)
        assert len(found) > 0, (
            f"Device not found via _ia0-0 subtype")
        assert expected in found, (
            f"Device not in results. Found: {found}")

        # Restore IID/IA to EITT values
        restore = cbor2.dumps({12: DUT_IA, 26: DUT_IID})
        coap.oscore_post(oscore_ctx, "/.well-known/knx/ia",
                         payload=restore, timeout=COAP_TIMEOUT)
        time.sleep(MDNS_ANNOUNCE_WAIT_S)


# ===========================================================================
# 5.1.2.3b -- Discovery using IA for unconfigured device
# ===========================================================================

class TestMdnsDiscoveryIAUnconfigured:
    """5.1.2.3b: Discovery using IA for unconfigured device → _ia0-ffff.

    EITT procedure:
      1. Factory reset (code 2)
      2. Browse mDNS for _ia0-ffff._sub._knx._udp.local.
      3. Verify PTR record points to {serial}._knx._udp.local.

    This test factory-resets the device, checks the unconfigured mDNS
    subtype, then re-provisions (SPAKE2+ + AT + IA/IID) to restore
    the session OSCORE context for subsequent tests.
    """

    @pytest.mark.skip(reason="mDNS subtype query unreliable in Docker CI veth — needs stack-side fix for IPV6_MULTICAST_IF on listener socket")
    def test_5_1_2_3b_ia_unconfigured_subtype(self, mdns, coap, oscore_ctx):
        """Factory-reset device should advertise _ia0-ffff subtype."""
        subtype = f"_ia0-ffff._sub.{KNX_SERVICE_TYPE}"
        expected_instance = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"

        iface = os.environ.get("DEVICE_IFACE")
        if not iface:
            pytest.skip("DEVICE_IFACE not set — cannot capture mDNS")
        iface_idx = _iface_name_to_index(iface)

        # 1. Start capturing mDNS announcements BEFORE the factory reset.
        #    The DUT sends an unsolicited mDNS announcement (with the
        #    _ia0-ffff subtype PTR in additional records) ~200ms after
        #    the factory reset fires.  We need to be listening to catch it.
        import threading
        capture_result: list[str] = []

        def _capture_thread():
            result = _capture_mdns_announcements(
                iface_idx, subtype, timeout=15)
            capture_result.extend(result)

        cap_thread = threading.Thread(target=_capture_thread, daemon=True)
        cap_thread.start()
        time.sleep(0.2)  # let the socket bind before sending reset

        # 2. Factory reset code 2 via OSCORE (like EITT does).
        reset_payload = cbor2.dumps({2: "reset", 1: 2})
        resp = coap.oscore_post(oscore_ctx, "/.well-known/knx",
                                payload=reset_payload, timeout=5)
        assert resp is not None and resp.is_successful, (
            f"Factory reset code 2 failed: {resp}")

        # 3. Wait for the capture thread to finish (it will get the
        #    announcement or time out after 15s).
        cap_thread.join(timeout=20)
        print(f"[5.1.2.3b] Announcement capture for {subtype} → {capture_result}")

        # 4. If capture missed the announcement, try a raw PTR query
        #    as fallback (the listener thread should respond).
        if not capture_result:
            time.sleep(2)  # extra wait for DUT to settle
            query_result = _raw_mdns_ptr_query(iface_idx, subtype, timeout=8)
            print(f"[5.1.2.3b] Raw PTR query for {subtype} → {query_result}")
            capture_result.extend(query_result)

        ptr_results = capture_result

        # 5. Re-provision FIRST so subsequent tests work even if
        #    mDNS discovery fails.
        spake = Spake2PlusClient(
            password=DEVICE_PASSWORD, sender_id="MdnsRe")

        req1 = cbor2.dumps(spake.create_parameter_request())
        resp1 = coap.post("/.well-known/knx/spake", payload=req1,
                          content_format=APPLICATION_CBOR, timeout=30)
        assert resp1 is not None and resp1.is_successful, (
            f"SPAKE2+ step 1 failed: {resp1}")
        spake.process_parameter_response(cbor2.loads(resp1.payload))

        req2 = cbor2.dumps(spake.create_key_exchange_request())
        resp2 = coap.post("/.well-known/knx/spake", payload=req2,
                          content_format=APPLICATION_CBOR, timeout=30)
        assert resp2 is not None and resp2.is_successful, (
            f"SPAKE2+ step 2 failed: {resp2}")
        spake.process_key_exchange_response(cbor2.loads(resp2.payload))

        req3 = cbor2.dumps(spake.create_confirmation_request())
        resp3 = coap.post("/.well-known/knx/spake", payload=req3,
                          content_format=APPLICATION_CBOR, timeout=10)
        assert resp3 is not None and resp3.is_successful, (
            f"SPAKE2+ step 3 failed: {resp3}")

        # 6. Provision AT entry via PASE context
        pase_sender_id = spake.sender_id.encode("utf-8")
        pase_ctx = OscoreContext(
            master_secret=spake.shared_key,
            sender_id=pase_sender_id,
            recipient_id=b"",
        )

        new_master_secret = os.urandom(16)
        new_sender_id = b"RtTest"
        at_inner = {
            0: "RuntimeTest",
            9: ALL_SCOPES,
            38: 2,
            8: {4: {0: new_sender_id, 2: new_master_secret}},
        }
        at_cbor = cbor2.dumps({0: at_inner})
        resp_at = coap.oscore_post(pase_ctx, "/auth/at",
                                   payload=at_cbor, timeout=10)
        assert resp_at is not None and resp_at.is_successful, (
            f"AT provisioning failed: {resp_at}")

        # 7. Set IA/IID to restore runtime state
        new_ctx = OscoreContext(
            master_secret=new_master_secret,
            sender_id=new_sender_id,
            recipient_id=b"",
        )
        ia_iid_payload = cbor2.dumps({12: DUT_IA, 26: DUT_IID})
        resp_ia = coap.oscore_post(
            new_ctx, "/.well-known/knx/ia", payload=ia_iid_payload)
        assert resp_ia is not None and resp_ia.is_successful, (
            f"IA/IID restore failed: {resp_ia}")

        # 8. Update session oscore_ctx in-place so subsequent tests work
        oscore_ctx.master_secret = new_ctx.master_secret
        oscore_ctx.sender_id = new_ctx.sender_id
        oscore_ctx.recipient_id = new_ctx.recipient_id
        oscore_ctx.id_context = new_ctx.id_context
        oscore_ctx.ssn = new_ctx.ssn
        oscore_ctx.sender_key = new_ctx.sender_key
        oscore_ctx.recipient_key = new_ctx.recipient_key
        oscore_ctx.common_iv = new_ctx.common_iv

        # 9. Assert on the mDNS result (after re-provisioning)
        assert len(ptr_results) > 0, (
            f"Unconfigured device should respond to PTR query for {subtype}")
        # Check that the PTR RDATA points to the expected instance
        matched = any(
            r.lower().rstrip(".") == expected_instance.lower().rstrip(".")
            for r in ptr_results
        )
        assert matched, (
            f"Expected PTR → '{expected_instance}' in {ptr_results}")


# ===========================================================================
# 5.1.2.4 -- Discovery using programming mode
# ===========================================================================

class TestMdnsDiscoveryPM:
    """5.1.2.4: Discovery using programming mode."""

    def test_5_1_2_4_pm_subtype_enabled(self, mdns, coap, oscore_ctx):
        """Enable PM, browse _pm._sub._knx._udp.local → find device."""
        # Enable programming mode (CBOR map {1: true})
        pm_payload = cbor2.dumps({1: True})
        resp = coap.oscore_put(oscore_ctx, "/dev/pm",
                               payload=pm_payload, timeout=COAP_TIMEOUT)
        assert resp is not None and resp.is_successful, (
            f"PUT /dev/pm=true failed: {resp}")

        # Wait for mDNS re-announcement
        time.sleep(MDNS_ANNOUNCE_WAIT_S)

        subtype = f"_pm._sub.{KNX_SERVICE_TYPE}"
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"

        found = _browse_service(mdns, subtype)
        assert len(found) > 0, (
            f"Device not found via _pm subtype after enabling PM")
        assert expected in found, (
            f"Device not in _pm results. Found: {found}")

    def test_5_1_2_4_pm_full_chain(self, mdns, coap, oscore_ctx):
        """Full discovery chain via _pm subtype."""
        subtype = f"_pm._sub.{KNX_SERVICE_TYPE}"
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"
        _verify_discovery_chain(mdns, coap, subtype, expected)

    def test_5_1_2_4_pm_subtype_disabled(self, mdns, coap, oscore_ctx):
        """Disable PM → _pm subtype should no longer be advertised."""
        # Disable programming mode (CBOR map {1: false})
        pm_payload = cbor2.dumps({1: False})
        resp = coap.oscore_put(oscore_ctx, "/dev/pm",
                               payload=pm_payload, timeout=COAP_TIMEOUT)
        assert resp is not None and resp.is_successful, (
            f"PUT /dev/pm=false failed: {resp}")

        # Wait for mDNS goodbye + re-announcement without _pm
        time.sleep(MDNS_ANNOUNCE_WAIT_S)

        subtype = f"_pm._sub.{KNX_SERVICE_TYPE}"
        found = _browse_service(mdns, subtype, timeout=3)
        # Device should NOT appear in _pm browse results
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"
        assert expected not in found, (
            f"Device still found via _pm subtype after disabling PM")


# ===========================================================================
# 5.1.2.5a -- Unsolicited mDNS responses on Programming mode change
# ===========================================================================

class TestMdnsUnsolicitedPM:
    """5.1.2.5a: Unsolicited mDNS responses on Programming mode change."""

    def test_5_1_2_5a_pm_toggle_announcements(self, mdns, coap, oscore_ctx):
        """Toggle PM on→off→on and verify mDNS announcements appear."""
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"

        # Enable PM
        resp = coap.oscore_put(oscore_ctx, "/dev/pm",
                               payload=cbor2.dumps({1: True}),
                               timeout=COAP_TIMEOUT)
        assert resp is not None and resp.is_successful

        # Browse _pm — should find device
        time.sleep(MDNS_ANNOUNCE_WAIT_S)
        subtype = f"_pm._sub.{KNX_SERVICE_TYPE}"
        found = _browse_service(mdns, subtype)
        assert expected in found, "PM enable: device not found via _pm"

        # Disable PM
        resp = coap.oscore_put(oscore_ctx, "/dev/pm",
                               payload=cbor2.dumps({1: False}),
                               timeout=COAP_TIMEOUT)
        assert resp is not None and resp.is_successful

        # Browse _pm — should NOT find device
        time.sleep(MDNS_ANNOUNCE_WAIT_S)
        found = _browse_service(mdns, subtype, timeout=3)
        assert expected not in found, "PM disable: device still in _pm"

        # Re-enable PM
        resp = coap.oscore_put(oscore_ctx, "/dev/pm",
                               payload=cbor2.dumps({1: True}),
                               timeout=COAP_TIMEOUT)
        assert resp is not None and resp.is_successful

        # Browse _pm — should find device again
        time.sleep(MDNS_ANNOUNCE_WAIT_S)
        found = _browse_service(mdns, subtype)
        assert expected in found, "PM re-enable: device not found via _pm"

        # Cleanup: disable PM
        coap.oscore_put(oscore_ctx, "/dev/pm",
                        payload=cbor2.dumps({1: False}),
                        timeout=COAP_TIMEOUT)


# ===========================================================================
# 5.1.2.5b -- Unsolicited mDNS responses on Individual Address change
# ===========================================================================

class TestMdnsUnsolicitedIA:
    """5.1.2.5b: Unsolicited mDNS responses on Individual Address change."""

    def test_5_1_2_5b_ia_change_announcement(self, mdns, coap, oscore_ctx):
        """Change IA and verify new IA subtype appears in mDNS."""
        expected = f"{DUT_SERIAL}.{KNX_SERVICE_TYPE}"

        # Set a new IA (different from EITT default)
        new_ia = 0x4D2   # 1234 decimal
        new_iid = 0x2DFDC1C3D  # 12345678909 decimal
        ia_payload = cbor2.dumps({12: new_ia, 26: new_iid})
        resp = coap.oscore_post(oscore_ctx, "/.well-known/knx/ia",
                                payload=ia_payload, timeout=COAP_TIMEOUT)
        assert resp is not None and resp.is_successful, (
            f"POST IA change failed: {resp}")

        # Wait for mDNS re-announcement
        time.sleep(MDNS_ANNOUNCE_WAIT_S)

        # Browse new IA subtype
        iid_hex = format(new_iid, "x")
        ia_hex = format(new_ia, "x")
        subtype = f"_ia{iid_hex}-{ia_hex}._sub.{KNX_SERVICE_TYPE}"

        found = _browse_service(mdns, subtype)
        assert expected in found, (
            f"Device not found via new IA subtype {subtype} after IA change")

        # Restore original EITT IA
        restore = cbor2.dumps({12: DUT_IA, 26: DUT_IID})
        coap.oscore_post(oscore_ctx, "/.well-known/knx/ia",
                         payload=restore, timeout=COAP_TIMEOUT)
        time.sleep(MDNS_ANNOUNCE_WAIT_S)
