"""Tests for IPv6 interface enumeration.

No Qt. These run on whatever machine executes them, so they assert on
STRUCTURE rather than on a particular adapter — an assertion like "eth0
exists" would pass on CI and fail on a laptop.

The cross-platform contract matters here: the same code path serves Windows
hosts and Linux containers, and the one thing that must never happen is a
blocking call. `getaddrinfo(interface_name)` is a DNS query on Windows and
stalls for seconds per adapter, which is enough to freeze the dialog.
"""

from __future__ import annotations

import time

import pytest

from gui.interfaces import Interface, default_interface, list_interfaces


def test_enumeration_is_fast():
    """A blocking name lookup here freezes the connection dialog. This
    caught a real hang: getaddrinfo() on Windows adapter names took long
    enough that the first version never returned."""
    start = time.monotonic()
    list_interfaces()
    assert time.monotonic() - start < 2.0


def test_enumeration_returns_interfaces_with_addresses():
    """Interfaces without IPv6 are dropped — this tool speaks IPv6 only, so
    offering them would just be a way to pick something that cannot work."""
    for interface in list_interfaces():
        assert interface.addresses
        # >= 0, not > 0: UNSCOPED (index 0) is the legitimate pseudo-entry
        # holding global/ULA addresses, which carry no scope id at all.
        assert interface.index >= 0
        assert interface.name


def test_routable_addresses_are_offered():
    """The KNX stack uses site-local (scope 5), so a run normally binds to
    a ULA or global address. Filing every address under its scope_id — as
    this did first — silently dropped ALL of them, because only link-local
    carries a scope id. On the dev machine that lost 4 of 6.

    Skipped where the host genuinely has none (a CI container often does).
    """
    routable = [a for i in list_interfaces() for a in i.global_addresses]
    if not routable:
        pytest.skip("host has no routable IPv6 address")
    assert all(not a.lower().startswith("fe80") for a in routable)


def test_nothing_is_filtered_out():
    """Every address the host reports must be offered.

    This module cannot reliably tell which address reaches a given device,
    and every filter it tried was wrong in some real case: filing by
    scope_id hid all four routable addresses, and a later fe80 exclusion
    was the same mistake again. Order them, never drop them.
    """
    import socket

    try:
        reported = {info[4][0].split("%")[0] for info in
                    socket.getaddrinfo(socket.gethostname(), None,
                                       socket.AF_INET6)}
    except (socket.gaierror, UnicodeError, OSError):
        pytest.skip("host name does not resolve to IPv6")

    offered = {a for i in list_interfaces(include_loopback=True)
               for a in i.addresses}
    missing = reported - offered
    assert not missing, f"addresses dropped by the picker: {missing}"


def test_routable_interfaces_are_listed_first():
    """They should not be buried under half a dozen link-local adapters."""
    interfaces = list_interfaces()
    with_routable = [n for n, i in enumerate(interfaces)
                     if i.global_addresses]
    without = [n for n, i in enumerate(interfaces) if not i.global_addresses]
    if with_routable and without:
        assert max(with_routable) < min(without)


def test_loopback_is_excluded_by_default():
    assert all(not i.is_loopback for i in list_interfaces())
    # ...but available when asked for: the Docker profile uses ::1.
    with_loopback = list_interfaces(include_loopback=True)
    assert len(with_loopback) >= len(list_interfaces())


def test_default_interface_never_raises():
    """Returns 0 ("let the OS choose") when it cannot tell — which is the
    correct answer on Linux and the problem case on Windows."""
    assert default_interface() >= 0


# -- address classification -------------------------------------------------

def test_link_local_and_global_are_separated():
    """The picker offers routable addresses first: link-local works but
    needs a scope, so it is the fallback, not the default."""
    interface = Interface(index=3, name="eth0", addresses=[
        "fe80::1", "fd00::5", "2001:db8::1", "::1"])

    assert interface.link_local == ["fe80::1"]
    assert interface.global_addresses == ["fd00::5", "2001:db8::1"]


def test_ula_counts_as_global():
    """fd00::/8 is what the Docker topology uses; treating it as
    non-routable would hide the only usable address there."""
    interface = Interface(index=3, name="veth", addresses=["fd00::1"])
    assert interface.global_addresses == ["fd00::1"]


def test_loopback_detected_by_index_name_or_address():
    """Index 1 on Linux and Windows, but the NAME varies by platform
    (lo, lo0, Loopback) — so all three signals are checked."""
    assert Interface(index=1, name="anything").is_loopback
    assert Interface(index=9, name="lo").is_loopback
    assert Interface(index=9, name="lo0").is_loopback
    assert Interface(index=9, name="Loopback").is_loopback
    assert Interface(index=9, name="eth0", addresses=["::1"]).is_loopback
    assert not Interface(index=9, name="eth0",
                         addresses=["fe80::1"]).is_loopback


def test_describe_is_human_readable():
    interface = Interface(index=20, name="ethernet_32776",
                          addresses=["fe80::1"])
    described = interface.describe()
    assert "ethernet_32776" in described
    assert "[20]" in described
    assert "fe80::1" in described


def test_describe_survives_an_interface_with_no_addresses():
    assert Interface(index=4, name="ppp0").describe()
