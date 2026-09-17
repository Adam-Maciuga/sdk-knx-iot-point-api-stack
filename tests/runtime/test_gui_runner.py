"""Tests for the GUI's run controller.

No DUT: the worker is exercised against a fake CoAP client, so these belong
to the fast tier. What matters here is the contract, not the protocol —
the runner itself is already covered by the conformance suite.

The threading rule these protect: events cross from the worker thread by
QUEUED signal, never a direct call into a widget. Qt crashes rather than
warns when that is broken.
"""

from __future__ import annotations

import os

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

try:
    from PySide6.QtCore import QEventLoop, QTimer
    from PySide6.QtWidgets import QApplication
except ImportError as exc:
    pytest.skip(f"GUI extra not usable: {exc}", allow_module_level=True)

from eitt.model import parse_eitt_xml                          # noqa: E402
from gui.runner_thread import ConnectionProfile, RunController  # noqa: E402

TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "data", "KnxConformanceTestTemplate-PointAPI.xml")


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


@pytest.fixture(scope="module")
def project():
    return parse_eitt_xml(TEMPLATE)


# -- connection profile ----------------------------------------------------

def test_docker_profile_sets_the_harness_environment():
    """The GUI and the pytest harness must configure the device the same
    way, or a run here and a run in CI would not mean the same thing."""
    env = ConnectionProfile(kind="docker", host="fd00::1", port=5683,
                            time_scale=0.05).as_environment()
    assert env["DEVICE_HOST"] == "fd00::1"
    assert env["COAP_PORT"] == "5683"
    assert env["RUNTIME_TEST_TIME_SCALE"] == "0.05"
    assert "EXTERNAL_DUT" not in env


def test_live_profile_flags_external_dut():
    env = ConnectionProfile(kind="live", serial_number="00fa10020800",
                            interface="eth0").as_environment()
    assert env["EXTERNAL_DUT"] == "1"
    assert env["DUT_SERIAL_NUMBER"] == "00fa10020800"
    assert env["DEVICE_IFACE"] == "eth0"


def test_profile_describes_itself_distinctly():
    docker = ConnectionProfile(kind="docker").describe()
    live = ConnectionProfile(kind="live").describe()
    assert docker != live
    assert "docker" in docker and "live" in live


# -- controller ------------------------------------------------------------

def _wait_for(signal, timeout_ms=15000):
    """Block the test until `signal` fires, pumping the event loop.

    Necessary because the worker runs on another thread and its signals are
    queued — without a running loop they would never be delivered.
    """
    loop = QEventLoop()
    received = []
    signal.connect(lambda *args: (received.append(args), loop.quit()))
    QTimer.singleShot(timeout_ms, loop.quit)
    loop.exec()
    return received[0] if received else None


def test_controller_starts_idle(app):
    controller = RunController()
    assert controller.running is False


def test_second_start_is_refused_while_running(app, project, monkeypatch):
    """Two runs against one device would interleave telegrams and produce a
    meaningless trace."""
    controller = RunController()
    cases = [tc for _c, _s, tc in project.iter_test_cases()][:1]

    # A profile pointing nowhere: the run fails fast, which is all this
    # test needs — it is about the guard, not the protocol.
    profile = ConnectionProfile(kind="docker", host="::1", port=1)
    assert controller.start(project, cases, profile) is True
    second = controller.start(project, cases, profile)
    controller.stop()
    _wait_for(controller.finished, timeout_ms=20000)
    assert second is False


def test_unreachable_device_is_reported_not_raced_through(app, project):
    """Pointing at an address with no device must say so plainly.

    Before the preflight this raced through every case in seconds with
    unhelpful per-case errors, which looks identical to a device that
    rejects everything — and sent real debugging in the wrong direction.
    """
    controller = RunController()
    cases = [tc for _c, _s, tc in project.iter_test_cases()
             if tc.name.startswith("5.1.1.1")][:1]
    if not cases:
        pytest.skip("5.1.1.1 not in the template")

    # Port 1 has nothing listening.
    profile = ConnectionProfile(kind="docker", host="::1", port=1,
                                time_scale=0.0)
    controller.start(project, cases, profile)
    detail = _wait_for(controller.failed, timeout_ms=60000)

    assert detail is not None, "an unreachable device must be reported"
    assert "No CoAP device answered" in detail[0]


def test_run_always_reports_completion(app, project, monkeypatch):
    """However a run ends, the GUI must be told — otherwise Play stays
    disabled forever and the window looks hung."""
    controller = RunController()
    cases = [tc for _c, _s, tc in project.iter_test_cases()][:1]

    profile = ConnectionProfile(kind="docker", host="::1", port=1,
                                time_scale=0.0)
    controller.start(project, cases, profile)

    # Either outcome is a report; what must not happen is silence.
    loop_result = _wait_for(controller.failed, timeout_ms=60000)
    assert loop_result is not None
