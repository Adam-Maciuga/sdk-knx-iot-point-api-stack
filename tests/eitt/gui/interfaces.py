"""Enumerating IPv6 interfaces and addresses, for the live-DUT picker.

Windows is why this exists. `scope_id=0` lets the OS choose, and on a
developer machine it routinely picks a Hyper-V or WSL adapter that cannot
see the device at all — so a live DUT usually has to be told which
interface AND which source address to use. `fixtures.discover_dut` already
works around this by sweeping every interface; the GUI can do better by
letting the operator pick once.

Qt-free so the enumeration is testable without a window.

## Cross-platform notes

Everything here uses the `socket` module only — no `netifaces`, no
`ip`/`ipconfig` parsing, no ctypes into platform APIs. That keeps one code
path across Windows, Linux and macOS, which matters because the harness
already runs on Windows hosts and inside Linux containers.

Behaviour that genuinely differs, and how it is handled:

- **Interface names.** Linux/macOS give `eth0`, `en0`. Windows gives
  synthetic names like `ethernet_32776`. Both are only ever DISPLAYED —
  the interface INDEX is what is passed to the socket layer, and that is
  portable.
- **`getaddrinfo(interface_name)`** resolves on Linux and macOS but is a
  DNS query on Windows that blocks for seconds per adapter. Never used;
  addresses come from the hostname lookup plus a per-interface route
  probe instead.
- **`IPV6_MULTICAST_IF`** exists on all three, but the value is an
  interface index everywhere except some BSDs. The probe is wrapped in
  `try/except OSError` so an unsupported combination degrades to "no
  link-local address discovered" rather than failing the enumeration.
- **Loopback** is index 1 on Linux and Windows, but the NAME varies
  (`lo`, `lo0`, `Loopback`), so both checks are applied.
"""

from __future__ import annotations

import socket
from dataclasses import dataclass, field


@dataclass
class Interface:
    """One network interface and the IPv6 addresses bound to it."""

    index: int
    name: str
    addresses: list[str] = field(default_factory=list)

    @property
    def link_local(self) -> list[str]:
        return [a for a in self.addresses if a.lower().startswith("fe80")]

    @property
    def global_addresses(self) -> list[str]:
        """Routable and ULA addresses — anything that is not link-local or
        loopback. ULA (fd00::/8) counts: the Docker topology uses it."""
        return [a for a in self.addresses
                if not a.lower().startswith(("fe80", "::1"))]

    @property
    def is_loopback(self) -> bool:
        # Index 1 on Linux and Windows, but the name varies by platform
        # (`lo`, `lo0`, `Loopback`), so check both rather than trust either.
        return (self.index == 1
                or self.name.lower() in ("lo", "lo0", "loopback")
                or "::1" in self.addresses)

    def describe(self) -> str:
        preferred = (self.global_addresses or self.link_local or
                     self.addresses)
        suffix = f"  —  {preferred[0]}" if preferred else "  —  (no IPv6)"
        return f"{self.name} [{self.index}]{suffix}"


def list_interfaces(include_loopback: bool = False) -> list[Interface]:
    """Every interface with at least one IPv6 address.

    Interfaces WITHOUT an IPv6 address are dropped: this tool speaks IPv6
    only, so offering them would just be a way to pick something that
    cannot work. Loopback is excluded by default for the same reason —
    though the Docker profile legitimately uses `::1`, which is why the
    flag exists.
    """
    found: dict[int, Interface] = {}

    try:
        entries = socket.if_nameindex()
    except OSError:
        return []
    for index, name in entries:
        found[index] = Interface(index=index, name=name)

    # Resolve the HOST once. Deliberately NOT getaddrinfo(interface_name):
    # on Windows the names are GUID-like, that lookup is a DNS query, and it
    # blocks for seconds PER INTERFACE — enough to hang the dialog on a
    # machine with a dozen virtual adapters.
    #
    # Only LINK-LOCAL addresses carry a usable scope_id. Global and ULA
    # addresses come back with scope_id=0 because they need no scope to be
    # unambiguous — so filing every address under its scope_id (which this
    # did first) silently discarded every routable address and left the
    # picker offering nothing but fe80::. On this machine that was 4 of 6.
    unscoped: list[str] = []
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None,
                                       socket.AF_INET6):
            address, scope_id = info[4][0].split("%")[0], info[4][3]
            if scope_id:
                interface = found.get(scope_id)
                if interface is not None and \
                        address not in interface.addresses:
                    interface.addresses.append(address)
            elif address not in unscoped:
                unscoped.append(address)
    except (socket.gaierror, UnicodeError, OSError):
        pass

    # Link-local addresses rarely come back from the hostname lookup, but
    # they are what a KNX device is usually reached on. Ask each interface
    # directly, which is a local table read rather than a name lookup.
    for index, interface in found.items():
        try:
            probe = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            probe.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF,
                             index)
            # ff02::1 is all-nodes link-local: connect() sets a route
            # without sending anything, and getsockname() then reports the
            # source address the OS would use on THIS interface.
            probe.connect(("ff02::1", 5683, 0, index))
            address = probe.getsockname()[0].split("%")[0]
            probe.close()
            if address and address not in interface.addresses:
                interface.addresses.append(address)
        except OSError:
            pass

    # The unscoped ones (global, ULA) become a pseudo-interface rather than
    # being guessed onto an adapter — see UNSCOPED. Built HERE, after the
    # probe loop above, or that loop would iterate it too.
    #
    # NO FILTERING. Show every address the host reports and let the
    # operator choose: this module cannot reliably tell which one reaches a
    # given device, and each guess it made was wrong in some real case —
    # dropping unscoped addresses hid all four routable ones, and the
    # later fe80 filter was another rule invented on the same bad premise.
    if unscoped:
        found[UNSCOPED] = Interface(index=UNSCOPED, name="(no scope id)",
                                    addresses=list(unscoped))

    usable = [i for i in found.values()
              if i.addresses and (include_loopback or not i.is_loopback)]
    # Routable first — the KNX stack uses site-local (scope 5), so those are
    # usually what is wanted, and they should not sit under five link-local
    # adapters. Ordering only; nothing is removed.
    usable.sort(key=lambda i: (not i.global_addresses, i.index))
    return usable


#: Interface index for addresses the OS will not attribute to one.
#:
#: A global or ULA address genuinely HAS no scope id — that is the point of
#: it. Three approaches were tried and all return 0: reading `getaddrinfo`'s
#: scope_id, binding to the address and reading `getsockname()`, and
#: connecting to it and reading the source. The socket layer simply does
#: not expose the owning adapter for an unscoped address, and the portable
#: alternatives (parsing `ip -6 addr` / `Get-NetIPAddress`) differ per
#: platform and per locale.
#:
#: It does not matter for what the picker is for. Binding a socket to a
#: global or ULA address needs no scope, so the address is usable exactly
#: as offered — only link-local ones require the adapter to be named, and
#: those DO carry their scope id.
UNSCOPED = 0


def default_interface() -> int:
    """Interface index the OS would pick, or 0 when it cannot be told.

    0 means "let the OS decide" — correct on Linux, and the thing that goes
    wrong on Windows, which is exactly why the picker exists.
    """
    try:
        probe = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        # UDP connect() only sets a route — no packet leaves. The timeout
        # is belt and braces: this must never stall the dialog.
        probe.settimeout(0.5)
        probe.connect(("2001:4860:4860::8888", 53))
        scope_id = probe.getsockname()[3]
        probe.close()
        return scope_id
    except OSError:
        return 0
