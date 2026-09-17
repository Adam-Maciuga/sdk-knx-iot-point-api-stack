"""Running a working copy against a DUT, off the GUI thread.

Phase 3. The single hard rule of this module: **Qt widgets are touched only
from the GUI thread.** The runner executes on a QThread and every event
crosses back as a signal — never a direct call into a widget or model. Qt
crashes rather than warns when that rule is broken, so the sink here does
nothing but emit.

The second rule, from the plan's risk list: the GUI drives
`eitt.runner.SequenceRunner` through the phase-0 event sink and nothing
else. No parallel execution path, or a GUI run and a CI run would
eventually disagree about what conformance means.
"""

from __future__ import annotations

import os
import time
import traceback
from dataclasses import dataclass
from typing import Optional

from PySide6.QtCore import QObject, Qt, QThread, Signal

from eitt.events import CaseEvent, EventBus, StepEvent, Telegram


@dataclass
class ConnectionProfile:
    """Where the DUT is and how to reach it.

    Mirrors the environment the pytest harness already understands, so a
    GUI run and a headless run configure the device identically:

    - **docker** — the container topology `run-in-docker.ps1` builds: a
      veth pair with the DUT in its own netns. `DEVICE_HOST` is injected
      and `RUNTIME_TEST_SERVER_PREFIX` carries the `ip netns exec` prefix.
    - **live** — a real device found by multicast discovery
      (`conftest.discover_dut`), which on Windows also needs the interface
      picked explicitly because scope_id=0 chooses the wrong adapter.
    """

    # Defaults to LIVE, because that is the case where the address is not
    # knowable up front — discovery finds it. Docker's `::1:5683` looks
    # like a safer default but is actively misleading on a developer
    # machine, where EITT's own listener also binds [::]:5683 and answers
    # every write 4.03.
    kind: str = "live"
    host: str = "::1"
    port: int = 5683
    interface: Optional[str] = None
    #: Interface index (scope_id). Windows needs this explicitly: scope_id=0
    #: lets the OS choose and it routinely picks a Hyper-V or WSL adapter
    #: that cannot see the device.
    interface_index: int = 0
    #: Source address to bind to. A machine often has several IPv6
    #: addresses on the chosen interface, and only one of them can reach
    #: the device's link.
    bind_address: Optional[str] = None
    serial_number: str = "00fa10020800"
    #: (host, port) once discovery has found the device, so the sweep runs
    #: ONCE per session rather than before every Play. Cleared by Reset.
    discovered: Optional[tuple] = None
    #: True when the device is driven BY HAND — it has buttons/a GUI but no
    #: /test/... control endpoints. `@@!` prompts then go to a modal, and
    #: the pre-run factory reset is skipped (it would only 4.04).
    manual_device: bool = False
    multicast_scope: int = 5
    #: Seconds to wait for a response before calling it missing. The
    #: template declares AckChkTimeOut on ZERO of its 1457 OUT rows, so
    #: every one falls back to this. 5s matches the CoAP client's own
    #: default and the pytest harness; raise it for slow hardware, where
    #: declaring a telegram missing too early is the worst thing this tool
    #: can do — it reports a conformance failure that did not happen.
    response_timeout: float = 5.0
    time_scale: float = 1.0

    def as_environment(self) -> dict:
        """The env vars conftest reads, so both front-ends agree."""
        env = {
            "COAP_PORT": str(self.port),
            "DUT_SERIAL_NUMBER": self.serial_number,
            "KNX_MULTICAST_SCOPE": str(self.multicast_scope),
            "RUNTIME_TEST_TIME_SCALE": str(self.time_scale),
            "COAP_TIMEOUT": str(self.response_timeout),
        }
        if self.kind == "live":
            env["EXTERNAL_DUT"] = "1"
            # Deliberately NO DEVICE_HOST: conftest skips discovery when it
            # is set, and a live profile's `host` is still the placeholder
            # default until discovery runs. Exporting it pinned the client
            # to ::1 and discovery then reported the unspecified address.
        else:
            env["DEVICE_HOST"] = self.host
        if self.interface:
            env["DEVICE_IFACE"] = self.interface
        return env

    def describe(self) -> str:
        where = f"[{self.host}]:{self.port}"
        if self.kind == "live":
            return f"live DUT {where} (sn {self.serial_number})"
        return f"docker {where}"


class _SignalSink(QObject):
    """An `eitt.events` sink that only emits Qt signals.

    Runs on the WORKER thread. Because the signals are connected across a
    thread boundary, Qt queues them and delivers on the GUI thread — which
    is the whole mechanism keeping widget access legal.
    """

    telegram = Signal(object)
    step = Signal(object)
    case = Signal(object)

    def on_telegram(self, telegram: Telegram) -> None:
        self.telegram.emit(telegram)

    def on_step(self, event: StepEvent) -> None:
        self.step.emit(event)

    def on_case(self, event: CaseEvent) -> None:
        self.case.emit(event)


class OperatorPrompt(QObject):
    """Carries an `@@!` prompt from the worker thread to a human.

    The worker BLOCKS while the operator acts, which is the whole point:
    the next step assumes the physical action has happened. A
    BlockingQueuedConnection does exactly that — the modal is raised on the
    GUI thread, the worker waits, and `answer` carries the result back.

    Not a plain signal: a normal queued emit returns immediately and the
    sequence would race ahead of the human.
    """

    #: (prompt text) -> the GUI shows a modal and sets `answer`.
    asked = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self.answer: Optional[bool] = None

    def ask(self, prompt: str) -> Optional[bool]:
        self.answer = None
        self.asked.emit(prompt)     # blocking: returns once the GUI answers
        return self.answer


class RunWorker(QObject):
    """Executes selected TestCases and reports progress by signal."""

    started_case = Signal(str)
    finished_case = Signal(str, bool, str)
    stopped_at = Signal(str, int)        # case name, row to resume from
    note = Signal(str)                   # advisory, not a failure
    finished = Signal(int, int)          # passed, failed
    failed = Signal(str)                 # setup blew up entirely

    def __init__(self, project, cases, profile: ConnectionProfile,
                 sink: _SignalSink, start_index: int = 0,
                 reset_first: bool = True,
                 prompter: Optional["OperatorPrompt"] = None):
        super().__init__()
        # Routes `@@!` prompts to a human. When absent the runner falls
        # back to overrides.yaml's /test/... mapping (headless).
        self.prompter = prompter
        self.project = project
        self.cases = list(cases)
        self.profile = profile
        self.sink = sink
        # Row to begin the FIRST case at — the GUI's "play from the
        # selected row". Later cases always start at 0.
        self.start_index = start_index
        # Factory-reset before the run. On by default because a stale
        # OSCORE context is the single most common cause of a run that
        # 4.03s its way through every case.
        self.reset_first = reset_first
        self._stop = False

    def stop(self) -> None:
        """Stop at the CURRENT position — after the step in flight.

        Note this can leave the device mid-TestCase: a sequence that has
        written a table but not restored it, say. That is deliberate — you
        asked to stop HERE, and stopping at the next case boundary instead
        would run steps you wanted skipped. Resuming (play) continues from
        where it stopped; Reset returns to the first row and re-runs the
        preparation, which is how you get back to a known state.
        """
        self._stop = True

    def _collection_of(self, case) -> str:
        """The collection a TestCase belongs to, by identity.

        Matching on identity rather than name: a copy can legitimately hold
        two cases with the same Name in different collections (EITT's "Copy
        of ..." duplicates), and a name lookup would pick whichever came
        first and hand the runner the wrong variable scope.
        """
        for collection, _suite, candidate in self.project.iter_test_cases():
            if candidate is case:
                return collection.name
        return case.collection_name or ""

    def run(self) -> None:
        try:
            self._run()
        except Exception:                            # noqa: BLE001
            # A crash in setup must surface in the GUI, not vanish into a
            # dead worker thread.
            self.failed.emit(traceback.format_exc())

    def _run(self) -> None:
        # Imported here, not at module scope: conftest reads the
        # environment AT IMPORT TIME, so the profile has to be applied
        # first or the client would target whatever the last run used.
        # Clear first: os.environ.update() only ADDS, so a DEVICE_HOST left
        # by an earlier Docker run would survive into a live one and make
        # conftest skip discovery entirely.
        if self.profile.kind == "live":
            os.environ.pop("DEVICE_HOST", None)
        os.environ.update(self.profile.as_environment())

        from eitt.overrides import OverrideTable
        from eitt.provisioning import ProvisioningEngine
        from eitt.runner import PreparationRegistry, SequenceRunner
        from eitt.variables import VariableTable
        from transport.coap_client import CoapClient

        # The SAME overrides pytest loads. An empty table (what this passed
        # first) silently drops four variable corrections — including
        # GENERAL_TIME_TO_NEXT and the firmware-update file path — plus the
        # manual-action map that answers `@@!` operator prompts. A GUI run
        # and a CI run have to mean the same thing.
        overrides = OverrideTable.load()

        host, port = self.profile.host, self.profile.port
        if self.profile.kind == "live" and self.profile.discovered:
            # Already found this session. Discovery sweeps every interface
            # on a machine where the OS default does not work, which costs
            # minutes — doing that before every Play would make the tool
            # unusable. `Reset` clears it when the device moves.
            host, port = self.profile.discovered
            os.environ["DEVICE_HOST"] = host
            os.environ["COAP_PORT"] = str(port)
        elif self.profile.kind == "live":
            # A real device's address is not known up front — find it the
            # same way conftest does, by multicast serial-number lookup.
            # On Windows the interface usually has to be named explicitly,
            # because scope_id=0 picks the wrong adapter.
            from conftest import discover_dut
            host, port, _iface = discover_dut(
                self.profile.serial_number,
                interface=self.profile.interface, timeout=15.0)
            os.environ["DEVICE_HOST"] = host
            os.environ["COAP_PORT"] = str(port)
            # Remember it, so the next Play skips the sweep.
            self.profile.discovered = (host, port)

        coap = CoapClient(host=host, port=port,
                          timeout=self.profile.response_timeout)

        # Preflight. Without this, pointing at the wrong address just races
        # through every case in seconds with unhelpful failures — which is
        # indistinguishable from a device that rejects everything. On this
        # developer machine EITT's own listener sits on [::]:5683 alongside
        # the virtual device, so `::1` reaches whichever bound first.
        try:
            probe = coap.get(".well-known/knx", timeout=3.0)
        except OSError:
            # Windows turns an ICMP port-unreachable into
            # ConnectionResetError (WinError 10054) on the next recv, so a
            # closed port RAISES here rather than timing out. That is the
            # ordinary "nothing is listening" case, not a crash.
            probe = None
        if probe is None:
            self.failed.emit(
                f"No CoAP device answered at [{host}]:{port}.\n\n"
                f"Profile: {self.profile.describe()}\n\n"
                f"For a device on this machine use the LIVE DUT profile — "
                f"it finds the device by serial number instead of guessing "
                f"an address. A virtual device binds an ephemeral port, "
                f"not 5683.")
            return
        preparations = PreparationRegistry(self.project)
        bus = EventBus(self.sink)

        # Put the device back to a known state before the run, exactly as
        # the pytest harness does per collection.
        #
        # This is NOT optional housekeeping. Every sequence begins with a
        # preparation that provisions a token and derives an OSCORE
        # context; leaving the previous run's context in place makes the
        # device answer 4.00 to the next SPAKE (a context already exists)
        # and 4.03 to every write (we no longer hold the right one). The
        # recorded certification trace runs SPAKE 27 times with no 4.03 at
        # all, which is what a correctly-reset device looks like.
        if self.reset_first and not self.profile.manual_device:
            try:
                coap.post("/test/factory-reset", timeout=5)
                time.sleep(2)
                coap.drain_socket(timeout=0.5)
            except OSError as exc:
                # A real certification device has no /test endpoints. That
                # is expected — the operator resets it by hand — so this
                # must not stop the run.
                self.note.emit(
                    f"Could not factory-reset the device ({exc}). If this "
                    f"is real hardware, reset it manually before running.")

        passed = failed = 0
        for position, case in enumerate(self.cases):
            if self._stop:
                break
            self.started_case.emit(case.name)

            # `collection_name` is filled in by EittProject.iter_test_cases(),
            # so walk the project once rather than trusting whatever an
            # earlier iteration happened to leave on the dataclass.
            collection = self._collection_of(case)
            variables = VariableTable.for_collection(
                self.project, collection, overrides=overrides.variables)
            runner = SequenceRunner(
                coap=coap, project=self.project, variables=variables,
                provisioning=ProvisioningEngine(variables),
                preparations=preparations, overrides=overrides,
                sink=bus, time_scale=self.profile.time_scale)
            # Polled after each completed step, so Stop lands at the
            # current position rather than at the next case boundary.
            runner.should_stop = lambda: self._stop
            if self.prompter is not None:
                runner.operator_prompt = self.prompter.ask
            # Only the FIRST case honours the start row; the rest of a
            # multi-case selection runs from the top.
            runner.start_index = self.start_index if position == 0 else 0

            try:
                result = runner.run(case)
                ok = result.ok
                reason = ("" if result.failure is None
                          else result.failure.reason)
                if result.stopped:
                    # Stopped by request: not a pass and not a failure.
                    self.stopped_at.emit(case.name,
                                         runner.stopped_at or 0)
                    self.finished.emit(passed, failed)
                    return
            except Exception as exc:                 # noqa: BLE001
                # One TestCase blowing up must not abandon the rest — the
                # operator asked for the whole selection. Keep the
                # TRACEBACK though: a bare "AttributeError: 'int' object
                # has no attribute 'lower'" names neither the file nor the
                # line, which makes a real bug in this tool look like a
                # device problem.
                ok = False
                reason = f"{type(exc).__name__}: {exc}"
                traceback.print_exc()

            passed, failed = (passed + 1, failed) if ok else (passed,
                                                              failed + 1)
            self.finished_case.emit(case.name, ok, reason)

        self.finished.emit(passed, failed)


class RunController(QObject):
    """Owns the worker thread and wires it to the GUI.

    Kept separate from MainWindow so the threading can be reasoned about —
    and tested — without a window.
    """

    telegram = Signal(object)
    step = Signal(object)
    case = Signal(object)
    started_case = Signal(str)
    finished_case = Signal(str, bool, str)
    stopped_at = Signal(str, int)
    note = Signal(str)
    #: An `@@!` prompt needs a human. A slot MUST set `prompter.answer`
    #: before returning — the worker is blocked until it does.
    operator_prompt = Signal(str)
    finished = Signal(int, int)
    failed = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._thread: Optional[QThread] = None
        self._worker: Optional[RunWorker] = None
        self._sink: Optional[_SignalSink] = None
        self._prompter: Optional[OperatorPrompt] = None

    @property
    def prompter(self) -> Optional["OperatorPrompt"]:
        """The live prompt channel, so a slot can set `answer`."""
        return self._prompter

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.isRunning()

    def start(self, project, cases, profile: ConnectionProfile,
              start_index: int = 0, reset_first: bool = True) -> bool:
        if self.running:
            return False

        self._sink = _SignalSink()
        self._prompter = OperatorPrompt()
        # BLOCKING, so the worker waits while the operator acts — the next
        # step assumes the physical action has happened.
        self._prompter.asked.connect(self.operator_prompt,
                                     Qt.BlockingQueuedConnection)
        self._worker = RunWorker(project, cases, profile, self._sink,
                                 start_index=start_index,
                                 reset_first=reset_first,
                                 prompter=self._prompter)
        self._thread = QThread()
        self._worker.moveToThread(self._thread)
        self._sink.moveToThread(self._thread)
        self._prompter.moveToThread(self._thread)

        # Queued across the thread boundary, so these land on the GUI
        # thread and it is safe for a slot to touch widgets.
        self._sink.telegram.connect(self.telegram)
        self._sink.step.connect(self.step)
        self._sink.case.connect(self.case)
        self._worker.started_case.connect(self.started_case)
        self._worker.finished_case.connect(self.finished_case)
        self._worker.stopped_at.connect(self.stopped_at)
        self._worker.note.connect(self.note)
        self._worker.finished.connect(self.finished)
        self._worker.failed.connect(self.failed)

        self._thread.started.connect(self._worker.run)
        self._worker.finished.connect(self._cleanup)
        self._worker.failed.connect(self._cleanup)
        self._thread.start()
        return True

    def stop(self) -> None:
        if self._worker is not None:
            self._worker.stop()

    def _cleanup(self, *_args) -> None:
        if self._thread is not None:
            self._thread.quit()
            self._thread.wait(5000)
            self._thread = None
        self._worker = None
        self._sink = None
        self._prompter = None
