"""The EITT replacement main window — phase 1, read-only viewer.

Layout mirrors EITT's own (see the GUI plan): a tabbed left pane, the
telegram grid top-right, the tracebuffer bottom-right. Execution is phase 3;
nothing here runs anything against a device yet.

    python -m gui.eitt_gui            # from tests/runtime/
"""

from __future__ import annotations

import os
import time
from typing import Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction, QColor, QFont, QPalette
from PySide6.QtWidgets import (QAbstractItemView, QCheckBox,
                               QComboBox, QDialog,
                               QDialogButtonBox, QFileDialog,
                               QFormLayout, QHBoxLayout,
                               QHeaderView, QLabel, QLineEdit, QMainWindow,
                               QMenu, QMessageBox, QPlainTextEdit,
                               QPushButton, QSplitter, QTableView,
                               QTabWidget, QTreeView, QTreeWidget,
                               QTreeWidgetItem, QVBoxLayout, QWidget)

from eitt.comment_scheme import get_comment_scheme
from eitt.events import TX, Telegram
from eitt.model import CoAPMessage, Comment, DnsMessage, parse_eitt_xml
from eitt.project_copy import cases_in, copy_scope

from .interfaces import list_interfaces
from .models import CaseBreak, ProjectTreeModel, SequenceTableModel
from .runner_thread import ConnectionProfile, RunController
from .theme import alternate_base, banner_style, grid_theme
from .trace import TraceBuffer
from .trace_export import export as export_trace
from .trace_view import TraceView

DEFAULT_TEMPLATE = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data", "KnxConformanceTestTemplate-PointAPI.xml")


_MSG_TYPE_NAMES = {0: "CON", 1: "NON", 2: "ACK", 3: "RST"}


def _format_peer(peer) -> str:
    if not peer:
        return ""
    host = peer[0]
    return f"[{host}]:{peer[1]}" if len(peer) > 1 else f"[{host}]"


def _format_time(when: float) -> str:
    local = time.localtime(when)
    return (f"{local.tm_hour:02d}:{local.tm_min:02d}:{local.tm_sec:02d}"
            f".{int((when % 1) * 1000):03d}")


def _unescape(text: str) -> str:
    r"""Render `<\/fp\/g\/1>;ct=60` as `</fp/g/1>;ct=60`.

    The template escapes forward slashes inside link-format payloads (a
    JSON-ism the XML author's tooling left in). EITT displays them
    unescaped. DISPLAY ONLY — the matcher must keep seeing the raw text,
    since that is what the comparison is written against.
    """
    return text.replace("\\/", "/") if text else text


class PropertiesPane(QWidget):
    """EITT's Telegram Properties tab, as a two-column property grid.

    A table rather than a text block because that is what EITT shows and it
    is genuinely easier to scan: fixed label column, values aligned, and
    collapsible group headers ("CoAP Message", "Sequence Properties").

    Presentation rules taken from EITT:
      - inapplicable fields are shown GREYED, not hidden, so the field list
        is stable and you learn where to look;
      - a field the XML omits reads "not set", distinguishing "absent" from
        "present but empty";
      - the resolved request line sits at the bottom (EITT's
        `CON: GET BDUT/fp/g?pn=1`).

    A QTreeWidget rather than QTableWidget purely for the collapsible
    groups; there is no hierarchy below a group.
    """

    #: Fields that only apply to a message this interpreter does not send.
    #: Listed anyway — EITT greys them, and a stable field list is easier to
    #: navigate than one that changes shape per row.
    _NOT_APPLICABLE = ("Source Address", "Group Address", "Service Type")

    #: EITT labels a response code with its RFC 7252 name — `2.05 [Content]`,
    #: not a bare `2.05`. Only the codes this template actually uses.
    _CODE_NAMES = {
        "2.01": "Created", "2.02": "Deleted", "2.03": "Valid",
        "2.04": "Changed", "2.05": "Content",
        "4.00": "Bad Request", "4.01": "Unauthorized",
        "4.02": "Bad Option", "4.03": "Forbidden", "4.04": "Not Found",
        "4.05": "Method Not Allowed", "4.06": "Not Acceptable",
        "4.08": "Request Entity Incomplete",
        "4.13": "Request Entity Too Large",
        "4.15": "Unsupported Content-Format",
        "5.00": "Internal Server Error", "5.01": "Not Implemented",
        "5.03": "Service Unavailable", "5.05": "Proxying Not Supported",
    }

    @classmethod
    def _code_label(cls, code: str) -> str:
        name = cls._CODE_NAMES.get((code or "").strip())
        return f"{code} [{name}]" if name else code

    def __init__(self, parent=None):
        super().__init__(parent)
        self.tree = QTreeWidget()
        self.tree.setColumnCount(2)
        self.tree.setHeaderLabels(["Property", "Value"])
        self.tree.setRootIsDecorated(False)
        self.tree.setIndentation(0)
        self.tree.setAlternatingRowColors(True)
        self.tree.setSelectionMode(QAbstractItemView.NoSelection)
        self.tree.setFocusPolicy(Qt.NoFocus)
        self.tree.header().setSectionResizeMode(0, QHeaderView.Interactive)
        self.tree.header().setSectionResizeMode(1, QHeaderView.Stretch)
        self.tree.setColumnWidth(0, 190)

        # EITT's footer: the resolved telegram (and its data) in a fixed
        # area BELOW the grid, not as a row inside it — a multi-line value
        # in the grid would wreck the column alignment.
        self.footer = QPlainTextEdit()
        self.footer.setReadOnly(True)
        self.footer.setMaximumHeight(90)
        footer_colour = grid_theme().uri.name()
        self.footer.setStyleSheet(
            f"font-family: Consolas, monospace; color: {footer_colour}; "
            f"border: none;")

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)
        layout.addWidget(self.tree)
        layout.addWidget(self.footer)

        self._scheme = get_comment_scheme()

    # -- building blocks ---------------------------------------------------

    def _footer(self, headline: str, detail: str = "") -> None:
        self.footer.setPlainText(
            f"{headline}\n\n{detail}" if detail else headline)

    def _group(self, title: str) -> QTreeWidgetItem:
        node = QTreeWidgetItem(self.tree, [title, ""])
        font = node.font(0)
        font.setBold(True)
        node.setFont(0, font)
        node.setFirstColumnSpanned(True)
        theme = grid_theme()
        node.setBackground(0, theme.case_break_bg)
        node.setForeground(0, theme.case_break_fg)
        node.setExpanded(True)
        return node

    @staticmethod
    def _row(parent, label: str, value, *, enabled: bool = True) -> None:
        if value is None or value == "":
            text = "not set"
            muted = True
        elif value is True:
            text, muted = "yes", False
        elif value is False:
            text, muted = "no", False
        else:
            text, muted = str(value), False

        row = QTreeWidgetItem(parent, [label, text])
        # `inactive` doubles as the greyed-out colour: it is the theme's
        # "present but not in play" tone and reads correctly on both.
        grey = grid_theme().inactive
        if not enabled:
            row.setForeground(0, grey)
            row.setForeground(1, grey)
        elif muted:
            row.setForeground(1, grey)

    def _text_block(self, title: str, body: str) -> None:
        """A multi-line value (Content/Cbor/a long description) — its own
        group so the grid's alignment is not wrecked by a wide cell."""
        group = self._group(title)
        item = QTreeWidgetItem(group, ["", body])
        item.setFirstColumnSpanned(True)
        item.setToolTip(1, body)

    # -- entry point -------------------------------------------------------

    def show_item(self, item) -> None:
        self.tree.clear()
        self.footer.clear()
        if item is None:
            return
        if isinstance(item, Comment):
            self._show_comment(item)
        elif isinstance(item, CoAPMessage):
            self._show_coap(item)
        elif isinstance(item, DnsMessage):
            self._show_dns(item)
        elif isinstance(item, Telegram):
            self._show_telegram(item)
        else:
            self._row(self._group(type(item).__name__), "repr", repr(item))

    def show_missing(self, line) -> None:
        """A telegram that was expected but never arrived — or one that
        could not be sent.

        Shows what SHOULD have happened (there is nothing else to show) and
        puts the error assessment in **Evaluation Result**, which is where
        an operator looks for a verdict. Rendered before anything else so
        the failure is the first thing read, not something to scroll for.
        """
        self.tree.clear()
        self.footer.clear()

        group = self._group("Evaluation Result")
        self._row(group, "Result", "FAILED — not received")
        self._row(group, "Assessment",
                  line.reason or "no response within the expected time")

        expected = getattr(line, "expected", None)
        if isinstance(expected, CoAPMessage):
            self._show_coap(expected)
        elif isinstance(expected, DnsMessage):
            self._show_dns(expected)
        else:
            # Built from the step summary when the template object is not
            # to hand — still says WHAT was expected.
            group = self._group("Expected")
            self._row(group, "Step", line.text.replace(
                "!  NOT RECEIVED: expected ", ""))

        # LAST: _show_coap/_show_dns set their own footer, and the failure
        # is what belongs there.
        self._footer("NOT RECEIVED",
                     line.reason or "nothing arrived within the timeout")

    def _show_telegram(self, item: Telegram) -> None:
        """A telegram that ACTUALLY went on the wire, selected in the trace.

        Distinct from the CoAPMessage view above, which describes what the
        template PLANNED. Showing the same field names for both would
        invite reading a recorded value as a specification.
        """
        group = self._group("Telegram (actual)")
        self._row(group, "Direction",
                  "TX (tool -> DUT)" if item.direction == TX
                  else "RX (DUT -> tool)")
        self._row(group, "Message Type", _MSG_TYPE_NAMES.get(item.msg_type))
        self._row(group, "Code", self._code_label(item.code))
        self._row(group, "Endpoint", item.path)
        self._row(group, "Peer", _format_peer(item.peer))
        self._row(group, "Content Format", item.content_format)

        group = self._group("Security")
        self._row(group, "Secured (OSCORE)", item.secured)
        self._row(group, "Security Context", item.sec_context)
        self._row(group, "Kid", item.kid.hex() if item.kid else "")
        self._row(group, "PIV", item.piv.hex() if item.piv else "")

        group = self._group("Wire")
        self._row(group, "Message ID", item.mid)
        self._row(group, "Token", item.token.hex() if item.token else "")
        self._row(group, "Payload bytes",
                  len(item.raw) if item.raw is not None else None)
        self._row(group, "Timestamp", _format_time(item.timestamp))
        if item.eval_ok is not None:
            self._row(group, "Evaluation Result",
                      "OK" if item.eval_ok else f"FAILED — {item.eval_reason}")

        if item.body not in (None, "", {}):
            self._text_block("Decoded body", str(item.body))
        if item.raw:
            self._text_block("Raw (hex)", item.raw.hex())

    def _show_comment(self, item: Comment) -> None:
        """EITT shows a comment as four rows — Command / Description /
        Format / Text — under a "Comment" header, with the long description
        repeated as a footer. Deliberately much smaller than the telegram
        view; anything more is noise on a row that carries no protocol."""
        command = self._scheme.lookup(item.text)

        group = self._group("Comment")
        self._row(group, "Command", command.command if command else "")
        self._row(group, "Description", command.description if command
                  else "plain text (not an @-command)")
        self._row(group, "Format", command.format if command else "")
        self._row(group, "Text", " ".join(item.text.split()))
        # `Activate` is a telegram attribute; a Comment's value is a parser
        # default, so surface it only when the XML really carried one (the
        # 2 deactivated @@! prompts).
        if not item.activate:
            self._row(group, "Activation", "inactive")

        if command is not None and command.description_long:
            self._footer(command.description, command.description_long)
        elif command is not None:
            self._footer(command.description, "")

    def _show_coap(self, item: CoAPMessage) -> None:
        """EITT greys fields PER MESSAGE, not from a fixed list.

        Comparing a request against a response in the same sequence: on an
        `OUT 2.05` the Invalid Auth Tag, Critical Option, response timeout,
        Random Kid and Last RX Token rows are greyed, while all of them are
        live on the `IN GET` above it. They are things you ask the TOOL to
        do when SENDING, so they mean nothing on a row that only describes
        what the DUT is expected to send back.
        """
        sending = item.is_request

        group = self._group("CoAP Message")
        self._row(group, "Message Type", item.type)
        # EITT labels the same cell Method on a request (GET) and Message
        # Type on a response (2.05 [Content]); keep both rows and let the
        # inapplicable one grey out.
        self._row(group, "Method", self._code_label(item.code))
        self._row(group, "Destination" if sending else "Source", item.ip)
        self._row(group, "Endpoint", item.path)
        self._row(group, "Parameter", " ".join(
            f"{p.key} = {p.value}" for p in item.url_parameters))
        self._row(group, "Security Context", item.sec_context)
        self._row(group, "Content Format", item.cf)
        for label in self._NOT_APPLICABLE:
            self._row(group, label, "", enabled=False)

        group = self._group("Security / Options")
        self._row(group, "Accepted Format", item.accepted_cf, enabled=sending)
        self._row(group, "Secured Message", item.secure)
        self._row(group, "Sequence Number", item.seq_no or item.seq_no_fn)
        self._row(group, "Sequence Number Offset", item.seq_no_offset)
        self._row(group, "Echo Option [hex]", item.echo_value)
        self._row(group, "Observe Option", item.observe_value or item.observe)
        # Send-side knobs — see the docstring.
        self._row(group, "Invalid Auth Tag [hex]", item.invalid_auth_tag,
                  enabled=sending)
        self._row(group, "Critical Option Number [dec]", item.crit_opt_no,
                  enabled=sending)
        self._row(group, "Critical Option Value [hex]", item.crit_opt_value,
                  enabled=sending)
        self._row(group, "Wait for Response Timeout [sec]",
                  item.ack_chk_timeout if item.ack_chk_timeout else "Default",
                  enabled=sending)
        self._row(group, "Use Random Kid Context", item.rnd_kid,
                  enabled=sending)
        self._row(group, "Use Last RX Token", item.use_last_rx_token,
                  enabled=sending)
        # ...and the receive-side one, greyed on a request for the mirror
        # reason: there is no incoming kid to check when you are sending.
        self._row(group, "Expected Kid Context",
                  item.rx_kid or ("<no check>" if not sending else ""),
                  enabled=not sending)

        hooks = [(label, value) for label, value in (
            ("EvalFn", item.eval_fn),
            ("EvalResponseFn", item.eval_response_fn),
            ("PatchAddressFn", item.patch_address_fn),
            ("PatchContentFn", item.patch_content_fn),
            ("PatchParameterFn", item.patch_parameter_fn)) if value]
        group = self._group("Script Functions")
        self._row(group, "Depends on Script Function",
                  "yes" if hooks else "no")
        for label, value in hooks:
            self._row(group, label, value)

        group = self._group("Sequence Properties")
        self._row(group, "ID", item.id)
        self._row(group, "Activation", "active" if item.activate
                  else "inactive")
        self._row(group, "Transmit Mode", item.cway.lower())
        self._row(group, "Time to next telegram", item.time_to_next)
        self._row(group, "Wait end time", "wait" if item.wait else "no wait")
        self._row(group, "Cycles", item.cycles)
        self._row(group, "Comment", item.comment_attr)

        if item.content:
            self._text_block("Content", _unescape(item.content))
        if item.cbor:
            self._text_block("Content (CBOR)", _unescape(item.cbor))

        self._footer(*self._resolved(item))

    @classmethod
    def _resolved(cls, item: CoAPMessage) -> tuple:
        """EITT's footer: the telegram as it goes on the wire, with its
        payload underneath — `ACK: 2.05 [Content] BDUT` / `[Data:] ...`."""
        if item.is_request:
            query = "&".join(f"{p.key}={p.value}"
                             for p in item.url_parameters)
            target = f"{item.ip or 'BDUT'}/{item.path.lstrip('/')}"
            if query:
                target += f"?{query}"
            headline = f"{item.type}: {item.code} {target}"
        else:
            headline = (f"{item.type}: {cls._code_label(item.code)} "
                        f"{item.ip or 'BDUT'}")

        body = _unescape(item.content or item.cbor or "")
        if body:
            lines = [ln.strip() for ln in body.splitlines() if ln.strip()]
            body = "[Data:] " + ("\n        ".join(lines))
        return headline, body

    def _show_dns(self, item: DnsMessage) -> None:
        group = self._group("DNS Message")
        self._row(group, "Type", item.type)
        self._row(group, "Record Type", item.record_type)
        self._row(group, "Direction", item.cway)
        self._row(group, "Service", item.service)
        self._row(group, "Host", item.host)
        self._row(group, "Port", item.port)

        group = self._group("Sequence Properties")
        self._row(group, "ID", item.id)
        self._row(group, "Activation", "active" if item.activate
                  else "inactive")
        self._row(group, "Time to next telegram", item.time_to_next)
        self._row(group, "Wait end time", "wait" if item.wait else "no wait")

        self._footer(f"{item.cway}: {item.record_type} {item.service}".strip())


class OperatorPromptDialog(QMessageBox):
    """EITT's `@@!` modal: show the instruction, wait for the human.

    These steps gate on a PHYSICAL action — power-cycling the device,
    pressing a reset button, triggering a sensor. A device with its own GUI
    (or real certification hardware) is driven by hand, so this is the
    normal path there, not a fallback.

    "I did it" continues; "Skip" fails the step rather than pretending. A
    sequence that carried on as though the action had happened would test
    the wrong thing and report a pass for it, which is the failure mode
    finding F7 was about.
    """

    def __init__(self, prompt: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Operator action required")
        self.setIcon(QMessageBox.Warning)
        self.setText(prompt)
        self.setInformativeText(
            "The sequence is paused. Perform the action on the device, then "
            "press “I did it”.")
        self.done_button = self.addButton("I did it",
                                          QMessageBox.AcceptRole)
        self.skip_button = self.addButton("Skip (fails the step)",
                                          QMessageBox.RejectRole)
        self.setDefaultButton(self.done_button)

    def performed(self) -> bool:
        return self.clickedButton() is self.done_button


class ConnectionDialog(QDialog):
    """Pick where the DUT is: the Docker topology, or a real device.

    Both, not either/or — the plan's decision. The fields mirror the
    environment variables `conftest` reads, so a GUI run and a headless run
    configure the device identically rather than drifting apart.
    """

    def __init__(self, profile: ConnectionProfile, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Connection")
        self.setMinimumWidth(420)

        self.kind = QComboBox()
        self.kind.addItem("Docker (veth + dut netns)", "docker")
        self.kind.addItem("Live DUT (mDNS discovery)", "live")
        self.kind.setCurrentIndex(
            self.kind.findData(profile.kind if profile.kind in
                               ("docker", "live") else "live"))
        self.kind.currentIndexChanged.connect(self._sync_enabled)

        self.host = QLineEdit(profile.host)
        self.port = QLineEdit(str(profile.port))
        self.serial = QLineEdit(profile.serial_number)

        # A live DUT usually has to be told WHICH adapter and WHICH source
        # address: scope_id=0 lets the OS choose and on Windows that
        # routinely lands on a Hyper-V or WSL adapter with no route to the
        # device. Enumerating real interfaces beats asking for a name
        # nobody can remember (Windows reports them as `ethernet_32776`).
        self.interface = QComboBox()
        self.interface.addItem("(let the OS choose)", None)
        for found in list_interfaces():
            self.interface.addItem(found.describe(), found)
            if found.name == profile.interface:
                self.interface.setCurrentIndex(self.interface.count() - 1)
        self.interface.currentIndexChanged.connect(self._sync_addresses)

        self.bind_address = QComboBox()
        self.bind_address.setToolTip(
            "Source address to send from. One interface can hold several; "
            "only the one on the device's link will reach it.")

        # A device with buttons but no /test endpoints — a virtual device
        # with its own GUI, or real certification hardware. `@@!` prompts
        # then go to a modal instead of a control endpoint, and the pre-run
        # factory reset is skipped because it would only ever 4.04.
        self.manual_device = QCheckBox(
            "Driven by hand (no /test endpoints)")
        self.manual_device.setChecked(profile.manual_device)
        self.manual_device.setToolTip(
            "Tick for a device you operate yourself. Operator prompts open "
            "a dialog and wait for you, rather than firing /test/... .")

        self.response_timeout = QLineEdit(str(profile.response_timeout))
        self.response_timeout.setToolTip(
            "Seconds to wait for a response before calling it MISSING.\n"
            "The template declares no per-message timeout, so every OUT "
            "row uses this. Raise it for slow hardware — declaring a "
            "telegram missing too early reports a failure that did not "
            "happen.")

        self.time_scale = QLineEdit(str(profile.time_scale))
        self.time_scale.setToolTip(
            "Multiplies every inter-telegram wait (TimeToNext).\n"
            "1.0 = exactly what a real EITT run does — keep it there for a "
            "certification run.\n"
            "Below 1.0 only over a loopback, where the device answers in "
            "microseconds and the waits are pure idling.\n"
            "Above 1.0 for hardware that needs longer to settle between "
            "telegrams.")

        form = QFormLayout()
        form.addRow("Connect to", self.kind)
        form.addRow("Host", self.host)
        form.addRow("Port", self.port)
        form.addRow("Serial number", self.serial)
        form.addRow("Interface", self.interface)
        form.addRow("Bind address", self.bind_address)
        form.addRow("", self.manual_device)
        form.addRow("Response timeout [s]", self.response_timeout)
        form.addRow("Time scale", self.time_scale)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok
                                   | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(buttons)
        self._sync_addresses()
        self._sync_enabled()

    def _sync_addresses(self) -> None:
        """Refill the address list for the chosen interface."""
        self.bind_address.clear()
        self.bind_address.addItem("(default for the interface)", None)
        found = self.interface.currentData()
        if found is None:
            return
        # EVERY address the interface holds. Routable ones are listed first
        # because they are usually what is wanted, but nothing is dropped —
        # `global_addresses + link_local` silently discarded anything that
        # was neither, which is exactly the kind of quiet filtering that
        # made the picker useless in the first place.
        ordered = found.global_addresses + found.link_local
        ordered += [a for a in found.addresses if a not in ordered]
        for address in ordered:
            self.bind_address.addItem(address, address)

    def _sync_enabled(self) -> None:
        live = self.kind.currentData() == "live"
        # A live DUT is FOUND, not addressed: discovery fills in host/port
        # from the serial number, so offering them for editing would
        # promise something the run then overwrites.
        self.host.setEnabled(not live)
        self.port.setEnabled(not live)
        self.serial.setEnabled(live)
        # Docker reaches the DUT over a veth the harness sets up, so
        # picking an adapter there would be meaningless.
        self.interface.setEnabled(live)
        self.bind_address.setEnabled(live)

    def profile(self) -> ConnectionProfile:
        def number(field, fallback):
            try:
                return type(fallback)(field.text().strip())
            except (TypeError, ValueError):
                return fallback

        found = self.interface.currentData()
        return ConnectionProfile(
            kind=self.kind.currentData(),
            host=self.host.text().strip() or "::1",
            port=number(self.port, 5683),
            interface=found.name if found is not None else None,
            interface_index=found.index if found is not None else 0,
            bind_address=self.bind_address.currentData(),
            serial_number=self.serial.text().strip() or "00fa10020800",
            manual_device=self.manual_device.isChecked(),
            response_timeout=number(self.response_timeout, 5.0),
            time_scale=number(self.time_scale, 1.0))


class MainWindow(QMainWindow):
    def __init__(self, template: Optional[str] = None, parent=None):
        super().__init__(parent)
        self._template = template or DEFAULT_TEMPLATE
        self.project = parse_eitt_xml(self._template)

        self.setWindowTitle(
            f"KNX-IoT Point API — conformance runner  [{os.path.basename(self._template)}]")
        self.resize(1500, 900)

        # -- CTL: the master template. READ-ONLY SOURCE. Nothing here edits
        # or launches anything; checking rows and copying them over is the
        # only way to get something runnable (GUI plan, phase 1b).
        self.tree = QTreeView()
        self.tree_model = ProjectTreeModel(self.project, checkable=True)
        self.tree.setModel(self.tree_model)
        self.tree.setHeaderHidden(False)
        self.tree.expandToDepth(0)
        self.tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self._on_tree_context_menu)
        self.tree.doubleClicked.connect(self._on_template_activated)

        self.properties = PropertiesPane()

        # -- Copies: the editable side. Each copy-over adds a root to this
        # ONE tree, as EITT lists "Copy of 5.4.1. POST /k V40" beside the
        # original in its project tree.
        self.copies: list = []
        self.copies_tree = QTreeView()
        self.copies_model = None
        self.copies_tree.setHeaderHidden(True)
        self.copies_tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self.copies_tree.customContextMenuRequested.connect(
            self._on_copies_context_menu)
        self.copies_tree.doubleClicked.connect(self._on_copy_activated)

        # Both trees live on the LEFT, as tabs. Double-clicking a TestCase in
        # either one opens its sequence in the single grid on the right —
        # which is how EITT works, and it keeps one grid to reason about
        # rather than a grid per copy.
        # Tab strip along the BOTTOM, and EITT's OWN names for these panes:
        #
        #   "Conformance Tests" = the read-only master template (our source)
        #   "CTL"               = the working copies (what you edit and run)
        #
        # That mapping is the opposite of what the names suggest in
        # isolation, which is exactly why it is spelled out here — using our
        # own words would cost every EITT user their muscle memory.
        # Interfaces/Filter/Trigger are deliberately not reproduced.
        left = QTabWidget()
        left.setTabPosition(QTabWidget.South)
        left.addTab(self._template_pane(), "Conformance Tests")
        left.addTab(self._copies_pane(), "CTL")
        left.addTab(self.properties, "Telegram Properties")
        self.left_tabs = left
        self._copies_tab_index = 1
        self._properties_tab_index = 2
        # Which view last drove the properties pane: "sequence" (the planned
        # telegram) or "trace" (what actually happened). Phase 2 wires the
        # trace side; the switching rule lives here so both go through one
        # place. See show_properties_for().
        self.properties_source = None

        self.grid = QTableView()
        self.grid_model = SequenceTableModel()
        self.grid.setModel(self.grid_model)
        self.grid.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.grid.setAlternatingRowColors(True)
        self.grid.verticalHeader().setVisible(False)
        self.grid.setShowGrid(False)
        self.grid.setWordWrap(False)
        # Windows' DARK palette reports AlternateBase as pure WHITE, so
        # trusting it flashes every other row white behind pale text.
        # Override it with a tint derived from Base — see gui/theme.py.
        grid_palette = self.grid.palette()
        grid_palette.setColor(QPalette.ColorRole.AlternateBase,
                              alternate_base())
        self.grid.setPalette(grid_palette)
        # Readability: the sequence window is the thing you stare at, and a
        # 6,559-row grid at the default metrics is a wall. A slightly larger
        # monospaced face keeps CBOR, hex and paths legible and columns
        # visually aligned; the taller rows stop lines from merging.
        grid_font = QFont("Consolas")
        if not grid_font.exactMatch():
            grid_font = QFont("Courier New")
        grid_font.setPointSize(10)
        self.grid.setFont(grid_font)
        self.grid.verticalHeader().setDefaultSectionSize(22)
        self.grid.horizontalHeader().setSectionResizeMode(
            SequenceTableModel.CONTENT, QHeaderView.Stretch)
        self.grid.selectionModel().currentChanged.connect(self._on_row_selected)

        # Says whose sequence is on screen and whether it can be edited.
        # Without it there is no way to tell a read-only template case from
        # an editable copy of the same case.
        self.grid_header = QLabel()
        self.grid_header.setWordWrap(True)
        self._set_grid_source(None, editable=False)

        # Live trace of a run, inspectable afterwards in the same session.
        # Saving/reloading a trace is a separate topic, along with
        # persisting the CTL screen and its selection.
        self.trace_buffer = TraceBuffer()
        self.tracebuffer = TraceView()
        self.tracebuffer.attach(self.trace_buffer)
        self.tracebuffer.telegram_selected.connect(
            lambda telegram: self.show_properties_for(telegram,
                                                      source="trace"))
        self.tracebuffer.missing_selected.connect(self._show_missing)

        grid_box = QWidget()
        grid_layout = QVBoxLayout(grid_box)
        grid_layout.setContentsMargins(0, 0, 0, 0)
        grid_layout.setSpacing(2)
        grid_layout.addWidget(self.grid_header)
        grid_layout.addWidget(self.grid)

        right = QSplitter(Qt.Vertical)
        right.addWidget(grid_box)
        right.addWidget(self._titled("Tracebuffer", self.tracebuffer))
        right.setSizes([600, 250])

        split = QSplitter(Qt.Horizontal)
        split.addWidget(left)
        split.addWidget(right)
        split.setSizes([420, 1080])
        self.setCentralWidget(split)

        # Execution (phase 3). The controller owns the worker thread; every
        # event arrives here as a QUEUED signal, so these slots run on the
        # GUI thread and may touch widgets.
        self.profile = ConnectionProfile()
        # Row to resume from after a Stop; None means "start where the
        # selection says".
        self._resume_index: Optional[int] = None
        # Name of the case currently executing, so a StepEvent can be
        # matched to a row in the open grid.
        self._running_case: Optional[str] = None
        self.controller = RunController(self)
        self.controller.telegram.connect(self._on_run_telegram)
        self.controller.step.connect(self._on_run_step)
        self.controller.case.connect(self._on_run_case)
        self.controller.started_case.connect(self._on_run_case_started)
        self.controller.finished_case.connect(self._on_run_case_finished)
        self.controller.stopped_at.connect(self._on_run_stopped)
        self.controller.note.connect(
            lambda text: self.statusBar().showMessage(text, 15000))
        self.controller.operator_prompt.connect(self._on_operator_prompt)
        self.controller.finished.connect(self._on_run_finished)
        self.controller.failed.connect(self._on_run_failed)
        self._build_toolbar()

        cases = sum(1 for _ in self.project.iter_test_cases())
        self.statusBar().showMessage(
            f"{len(self.project.collections)} collections · {cases} test cases "
            f"· double-click a test case to open its sequence")

    # -- running -----------------------------------------------------------

    def _build_toolbar(self) -> None:
        bar = self.addToolBar("Run")
        bar.setMovable(False)

        # Transport controls, in the order they are reached for.
        self.run_action = QAction("Play", self)
        self.run_action.setToolTip(
            "Run from the SELECTED row of the open sequence (or from the "
            "top when nothing is selected)")
        self.run_action.triggered.connect(self._on_run_clicked)
        bar.addAction(self.run_action)

        self.stop_action = QAction("Stop", self)
        self.stop_action.setEnabled(False)
        self.stop_action.setToolTip(
            "Stop at the current position, after the step in flight.\n"
            "This can leave the device mid-sequence — use Reset to return "
            "to a known state.")
        self.stop_action.triggered.connect(self.controller.stop)
        bar.addAction(self.stop_action)

        self.reset_action = QAction("Reset", self)
        self.reset_action.setToolTip(
            "Go back to the first row of the sequence (also clears the "
            "trace)")
        self.reset_action.triggered.connect(self._on_reset_clicked)
        bar.addAction(self.reset_action)

        bar.addSeparator()
        # Separate from Reset: clearing the log without losing your place
        # in the sequence is a different intention — usually "I have read
        # this, show me only what happens next".
        self.clear_trace_action = QAction("Clear trace", self)
        self.clear_trace_action.setToolTip(
            "Empty the tracebuffer, leaving the sequence position alone")
        self.clear_trace_action.triggered.connect(self._on_clear_trace)
        bar.addAction(self.clear_trace_action)

        self.export_trace_action = QAction("Export trace", self)
        self.export_trace_action.setToolTip(
            "Save the trace as text, EITT trace XML, or JSON")
        self.export_trace_action.triggered.connect(self._on_export_trace)
        bar.addAction(self.export_trace_action)

        bar.addSeparator()
        self.profile_action = QAction(self._profile_label(), self)
        self.profile_action.triggered.connect(self._on_pick_profile)
        bar.addAction(self.profile_action)

    def _profile_label(self) -> str:
        return f"Connection: {self.profile.describe()}"

    def _on_pick_profile(self) -> None:
        dialog = ConnectionDialog(self.profile, self)
        if dialog.exec():
            self.profile = dialog.profile()
            self.profile_action.setText(self._profile_label())
            self.statusBar().showMessage(
                f"Connection set to {self.profile.describe()}")

    def _cases_to_run(self) -> tuple:
        """(project, cases) for the current selection, or (None, []).

        A copy is preferred over the template: the template is a read-only
        SOURCE and running it directly would sidestep the whole copy-over
        model. Falls back to the ticked template cases only to say so.
        """
        index = self.copies_tree.currentIndex()
        result = (self.copies_model.result_for(index)
                  if index.isValid() else None)
        if result is None and len(self.copies) == 1:
            result = self.copies[0]     # the obvious one, unambiguously
        if result is not None:
            node = self.copies_model.node_at(index) if index.isValid() else None
            cases = cases_in(node) if node is not None and cases_in(node) \
                else cases_in(result.project)
            return result.project, cases
        return None, []

    def _on_run_clicked(self) -> None:
        if self.controller.running:
            return
        project, cases = self._cases_to_run()
        if not cases:
            QMessageBox.information(
                self, "Nothing to run",
                "Runs happen on a COPY. Tick what you need in Conformance "
                "Tests, copy it to CTL, then select it there.")
            return

        # Play resumes from where a Stop left off, or from the row the
        # operator selected, or the top.
        start_index = self._resume_index if self._resume_index is not None \
            else self._selected_step_index()
        self._resume_index = None

        if start_index == 0:
            # A fresh run gets a fresh trace; resuming appends to the one
            # already on screen, which is the point of resuming.
            self.trace_buffer.clear()
            self.tracebuffer.attach(self.trace_buffer)
        self.tracebuffer.set_follow(True)
        self.run_action.setEnabled(False)
        self.stop_action.setEnabled(True)
        where = f" from row {start_index + 1}" if start_index else ""
        self.statusBar().showMessage(
            f"Running {len(cases)} test case(s){where} against "
            f"{self.profile.describe()} …")
        self.controller.start(project, cases, self.profile,
                              start_index=start_index)

    def _selected_step_index(self) -> int:
        """Index into the open TestCase's sequence for the selected row.

        The grid can hold several concatenated cases, each behind a
        CaseBreak, so the view row is NOT the sequence index. Count back to
        the owning break and subtract — otherwise "play from here" would
        start at the wrong step in every case but the first.
        """
        index = self.grid.currentIndex()
        if not index.isValid():
            return 0
        row = index.row()
        offset = 0
        for candidate in range(row, -1, -1):
            item = self.grid_model.item_at(candidate)
            if isinstance(item, CaseBreak):
                offset = candidate + 1
                break
        return max(0, row - offset)

    def _on_export_trace(self) -> None:
        """Save the trace. Format follows the chosen extension."""
        if not self.trace_buffer.lines:
            QMessageBox.information(self, "Nothing to export",
                                    "The tracebuffer is empty.")
            return

        suggested = time.strftime("trace-%Y%m%d-%H%M%S.xml")
        path, _selected = QFileDialog.getSaveFileName(
            self, "Export trace", suggested,
            "EITT trace XML (*.xml);;Text (*.txt);;JSON (*.json)")
        if not path:
            return

        # Name the copy the run came from, so an exported trace says what
        # it is a trace OF — EITT puts the same thing in PlanSource.
        plan_source = ""
        index = self.copies_tree.currentIndex()
        result = (self.copies_model.result_for(index)
                  if index.isValid() else None)
        if result is not None:
            plan_source = result.project.name

        try:
            export_trace(self.trace_buffer, path, plan_source)
        except OSError as exc:
            QMessageBox.critical(self, "Export failed", str(exc))
            return
        self.statusBar().showMessage(
            f"Trace exported to {os.path.basename(path)} "
            f"({len(self.trace_buffer.lines)} lines)")

    def _on_clear_trace(self) -> None:
        """Empty the tracebuffer WITHOUT touching the sequence position.

        Distinct from Reset: clearing the log is "I have read this, show me
        what happens next", and works mid-run. Reset is "put the sequence
        back to the start", which does not.
        """
        self.trace_buffer.clear()
        self.tracebuffer.attach(self.trace_buffer)
        self.statusBar().showMessage("Trace cleared")

    def _on_reset_clicked(self) -> None:
        if self.controller.running:
            QMessageBox.information(
                self, "Run in progress",
                "Stop the run before resetting.")
            return
        self._resume_index = None
        # Forget the discovered address too: Reset means "start clean", and
        # the device may well have moved (a virtual one gets a new
        # ephemeral port every restart).
        self.profile.discovered = None
        self.trace_buffer.clear()
        self.tracebuffer.attach(self.trace_buffer)
        self.grid_model.set_current_step(None)
        if self.grid_model.rowCount():
            self.grid.setCurrentIndex(self.grid_model.index(0, 0))
            self.grid.scrollToTop()
        self.statusBar().showMessage("Reset to the first row")

    def _on_run_telegram(self, telegram) -> None:
        self.trace_buffer.on_telegram(telegram)
        self.tracebuffer.append_new()

    def _on_run_step(self, event) -> None:
        self.trace_buffer.on_step(event)
        self.tracebuffer.append_new()
        self._highlight_step(event)

    def _highlight_step(self, event) -> None:
        """Mark the executing row and scroll it into view.

        Only for the case currently on screen: a StepEvent from a nested
        preparation carries the PREPARATION's own indices, and applying
        those to the open sequence would jump the highlight to an unrelated
        row (see StepEvent's docstring).
        """
        if not self._running_case:
            return
        row = self.grid_model.row_for_step(self._running_case, event.index)
        if row is None:
            return
        self.grid_model.set_current_step(row)
        self.grid.scrollTo(self.grid_model.index(row, 0),
                           QAbstractItemView.PositionAtCenter)

    def _on_run_case(self, event) -> None:
        self.trace_buffer.on_case(event)
        self.tracebuffer.append_new()

    def _on_run_case_started(self, name: str) -> None:
        self._running_case = name
        self.statusBar().showMessage(f"Running {name} …")

    def _on_run_case_finished(self, name: str, ok: bool, reason: str) -> None:
        if not ok:
            self.statusBar().showMessage(f"FAILED {name} — {reason[:120]}")

    def _on_operator_prompt(self, prompt: str) -> None:
        """An `@@!` step needs a human. Runs on the GUI thread with the
        WORKER BLOCKED, so the answer must be set before returning."""
        prompter = self.controller.prompter
        if prompter is None:
            return

        if not self.profile.manual_device:
            # The device has /test endpoints; let the runner drive them
            # rather than making the operator do by hand what can be
            # automated. `None` falls through to overrides.yaml.
            prompter.answer = None
            return

        # Show it in the trace too — an action the operator took is part of
        # what happened, and a trace that omitted it would not explain why
        # the device changed state.
        self.trace_buffer.add_operator_action(prompt)
        self.tracebuffer.append_new()

        dialog = OperatorPromptDialog(prompt, self)
        dialog.exec()
        prompter.answer = dialog.performed()
        if not dialog.performed():
            self.statusBar().showMessage(
                f"Operator skipped: {prompt[:80]}")

    def _on_run_stopped(self, name: str, row: int) -> None:
        """Remember where to resume, so Play continues rather than restarts."""
        self._resume_index = row
        view_row = self.grid_model.row_for_step(name, row)
        if view_row is not None:
            self.grid_model.set_current_step(view_row)
            self.grid.setCurrentIndex(self.grid_model.index(view_row, 0))
        self.statusBar().showMessage(
            f"Stopped at row {row + 1} of {name} — Play resumes here, "
            f"Reset returns to the top")

    def _on_run_finished(self, passed: int, failed: int) -> None:
        self.run_action.setEnabled(True)
        self.stop_action.setEnabled(False)
        self._running_case = None
        self.tracebuffer.append_new()
        if self._resume_index is None:
            self.grid_model.set_current_step(None)
            self.statusBar().showMessage(
                f"Run finished — {passed} passed, {failed} failed")

    def _on_run_failed(self, detail: str) -> None:
        self.run_action.setEnabled(True)
        self.stop_action.setEnabled(False)
        self.statusBar().showMessage("Run could not start")
        QMessageBox.critical(self, "Run failed", detail[-2000:])

    def _template_pane(self) -> QWidget:
        """The master template tree plus its copy-over button.

        EITT calls this pane "Conformance Tests".

        The banner is not decoration: this pane is a SOURCE, and every
        affordance on it has to make that obvious, or someone will look for
        a Run button here and conclude the tool is broken.
        """
        box = QWidget()
        layout = QVBoxLayout(box)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        banner = QLabel("Read-only master template — tick items and copy "
                        "them to CTL to run or edit.")
        banner.setWordWrap(True)
        banner.setStyleSheet(banner_style("readonly"))
        layout.addWidget(banner)
        layout.addWidget(self.tree)

        buttons = QWidget()
        row = QHBoxLayout(buttons)
        row.setContentsMargins(0, 0, 0, 0)
        self.copy_button = QPushButton("Copy checked to new project")
        self.copy_button.clicked.connect(self._on_copy_checked)
        clear = QPushButton("Clear")
        clear.clicked.connect(self.tree_model.clear_checks)
        row.addWidget(self.copy_button)
        row.addWidget(clear)
        layout.addWidget(buttons)
        return box

    def _on_tree_context_menu(self, point) -> None:
        index = self.tree.indexAt(point)
        if not index.isValid():
            return
        node = self.tree_model.node_at(index)
        cases = cases_in(node)
        if not cases:
            return

        label = getattr(node, "name", "selection")
        menu = QMenu(self)
        action = QAction(
            f'Copy "{label[:40]}" to new project  ({len(cases)} cases)', self)
        action.triggered.connect(
            lambda: self._create_copy(cases, f"Copy of {label}"))
        menu.addAction(action)
        menu.exec(self.tree.viewport().mapToGlobal(point))

    def _on_copy_checked(self) -> None:
        cases = self.tree_model.checked_cases()
        if not cases:
            QMessageBox.information(
                self, "Nothing selected",
                "Tick one or more collections, suites or test cases first, "
                "or right-click a node to copy just that scope.")
            return
        self._create_copy(cases, f"Copy of {len(cases)} selected")

    def _create_copy(self, cases, name: str) -> None:
        """Build a working project and add it as a root of the Copies tree.

        The copy carries the preparations its cases REFERENCE (via the
        inline @#"..." comments), pulled in as siblings — never spliced into
        a sequence. See eitt/project_copy.py for why.
        """
        result = copy_scope(self.project, cases, name=name)
        if result.case_count == 0:
            QMessageBox.warning(self, "Nothing copied",
                                "That selection produced no test cases.")
            return

        self.copies.append(result)
        self.copies_model.add_copy(result)
        self.copies_tree.expandToDepth(0)
        self.left_tabs.setTabText(self._copies_tab_index,
                                  f"CTL ({len(self.copies)})")
        self.left_tabs.setCurrentIndex(self._copies_tab_index)

        message = (f"{name}: {result.case_count} cases "
                   f"({len(cases)} selected + {len(result.dependencies)} "
                   f"preparation) — double-click a test case to open it")
        if result.unresolved:
            message += f" — UNRESOLVED: {', '.join(result.unresolved)}"
        self.statusBar().showMessage(message)

    # -- opening a sequence in the grid ------------------------------------

    def _on_template_activated(self, index) -> None:
        """Double-click in CTL: open the case READ-ONLY."""
        self._open_sequence(self.tree_model.node_at(index), editable=False)

    def _on_copy_activated(self, index) -> None:
        """Double-click in Copies: open the case EDITABLE."""
        self._open_sequence(self.copies_model.node_at(index), editable=True)

    def _open_sequence(self, node, *, editable: bool) -> None:
        """Open whatever was double-clicked.

        A collection or suite opens EVERY case beneath it, concatenated with
        a separator row before each — the way EITT presents a whole 5.2 — so
        double-clicking a category is useful rather than inert. A copy root
        opens the entire working project.
        """
        cases = cases_in(getattr(node, "project", None) or node)
        if not cases:
            return
        # A fresh model per open, because editability is fixed at
        # construction: that is what makes "the template cannot be edited" a
        # structural property rather than a flag someone must remember to
        # re-check.
        self.grid_model = SequenceTableModel(editable=editable)
        self.grid_model.set_cases(cases)
        self.grid.setModel(self.grid_model)
        self.grid.selectionModel().currentChanged.connect(self._on_row_selected)
        self._apply_grid_layout()
        self._set_grid_source(node, editable=editable, cases=cases)

    def _apply_grid_layout(self) -> None:
        """Column widths, and the row spanning that makes comments readable.

        Comments and case separators start at `Nr` and run the full width,
        as EITT renders them — they are prose, not telegrams, and confining
        one to a column buries it.
        """
        model = self.grid_model
        for row in range(model.rowCount()):
            span = model.is_full_width_row(model.item_at(row))
            # setSpan is sticky across setModel, so clear it explicitly
            # rather than only setting it — a telegram row inheriting a
            # previous view's span would hide every column but the first.
            self.grid.setSpan(row, 0, 1, model.columnCount() if span else 1)

        self.grid.resizeColumnsToContents()
        header = self.grid.horizontalHeader()
        # Give the wide, information-bearing columns room; the flag columns
        # only ever hold a word.
        for column, width in ((SequenceTableModel.NR, 60),
                              (SequenceTableModel.ACTIVE, 52),
                              (SequenceTableModel.CONNECTION, 78),
                              (SequenceTableModel.SECURITY, 110),
                              (SequenceTableModel.DPT, 46),
                              (SequenceTableModel.DIRECTION, 68),
                              (SequenceTableModel.RF_INFO, 54),
                              (SequenceTableModel.SN_DOA, 62),
                              (SequenceTableModel.TYPE, 58),
                              (SequenceTableModel.METHOD, 62),
                              (SequenceTableModel.URI, 200),
                              (SequenceTableModel.PARAM, 150),
                              (SequenceTableModel.WAIT, 90)):
            self.grid.setColumnWidth(column, width)
        header.setSectionResizeMode(SequenceTableModel.CONTENT,
                                    QHeaderView.Stretch)

    def _set_grid_source(self, node, *, editable: bool, cases=None) -> None:
        if node is None:
            self.grid_header.setText(
                "No sequence open — double-click a test case, suite or "
                "collection in Conformance Tests (read-only) or CTL "
                "(editable copies).")
            self.grid_header.setStyleSheet(
                "color: #999; padding: 4px; font-style: italic;")
            return

        name = getattr(node, "name", None) or getattr(
            getattr(node, "project", None), "name", "selection")
        cases = cases or []
        scope = (f"{len(cases)} test cases, "
                 f"{sum(len(c.sequence) for c in cases)} steps"
                 if len(cases) != 1 else f"{len(cases[0].sequence)} steps")

        # ASCII markers, not emoji: this text is read back by tests through
        # a cp1252 console on Windows, where a lock glyph raises
        # UnicodeEncodeError.
        if editable:
            self.grid_header.setText(
                f"[EDIT]  {name}   —   {scope}. Editable copy; toggle "
                f"'Active' to enable/disable a telegram.")
            self.grid_header.setStyleSheet(banner_style("edit"))
        else:
            self.grid_header.setText(
                f"[READ-ONLY]  {name}   —   {scope}. Master template.")
            self.grid_header.setStyleSheet(banner_style("readonly"))
        self.statusBar().showMessage(f"{name} — {scope}")

    # -- copies tree -------------------------------------------------------

    def _copies_pane(self) -> QWidget:
        box = QWidget()
        layout = QVBoxLayout(box)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        banner = QLabel("Working copies (CTL) — editable. Double-click a "
                        "test case to open its sequence.")
        banner.setWordWrap(True)
        banner.setStyleSheet(banner_style("edit"))
        layout.addWidget(banner)

        from .models import CopiesTreeModel
        self.copies_model = CopiesTreeModel()
        self.copies_tree.setModel(self.copies_model)
        layout.addWidget(self.copies_tree)

        remove = QPushButton("Remove selected copy")
        remove.clicked.connect(self._on_remove_copy)
        layout.addWidget(remove)
        return box

    def _on_copies_context_menu(self, point) -> None:
        index = self.copies_tree.indexAt(point)
        if not index.isValid():
            return
        result = self.copies_model.result_for(index)
        if result is None:
            return
        menu = QMenu(self)
        action = QAction(f'Remove "{result.project.name[:40]}"', self)
        action.triggered.connect(lambda: self._remove_copy(result))
        menu.addAction(action)
        menu.exec(self.copies_tree.viewport().mapToGlobal(point))

    def _on_remove_copy(self) -> None:
        index = self.copies_tree.currentIndex()
        result = self.copies_model.result_for(index) if index.isValid() else None
        if result is None:
            QMessageBox.information(self, "No copy selected",
                                    "Select something inside a copy first.")
            return
        self._remove_copy(result)

    def _remove_copy(self, result) -> None:
        if result in self.copies:
            self.copies.remove(result)
        self.copies_model.remove_copy(result)
        self.left_tabs.setTabText(
            self._copies_tab_index,
            f"CTL ({len(self.copies)})" if self.copies else "CTL")
        # The open sequence may have belonged to the copy just removed.
        # Leaving it on screen would offer edits to an object nothing owns.
        self.grid_model = SequenceTableModel()
        self.grid.setModel(self.grid_model)
        self.grid.selectionModel().currentChanged.connect(self._on_row_selected)
        self._set_grid_source(None, editable=False)

    @staticmethod
    def _titled(title: str, widget: QWidget) -> QWidget:
        box = QWidget()
        layout = QVBoxLayout(box)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)
        label = QLabel(title)
        label.setStyleSheet("font-weight: bold; padding: 2px;")
        layout.addWidget(label)
        layout.addWidget(widget)
        return box


    def _on_row_selected(self, current, _previous) -> None:
        """A click in the sequence grid shows that telegram's properties."""
        if not current.isValid():
            return
        item = self.grid_model.item_at(current.row())
        # Case separators are view furniture, not telegrams — selecting one
        # should not blank a pane the user is reading.
        if self.grid_model.is_full_width_row(item) and not isinstance(
                item, Comment):
            return
        self.show_properties_for(item, source="sequence")

    def show_properties_for(self, item, *, source: str) -> None:
        """Show `item` in the properties pane and bring that pane forward.

        `source` is "sequence" or "trace". Whichever was clicked LAST owns
        the pane — the two views describe the same telegram from different
        sides (planned vs actual), so silently keeping the older one would
        leave you reading the wrong half without noticing.
        """
        self.properties_source = source
        self.properties.show_item(item)
        self.left_tabs.setCurrentIndex(self._properties_tab_index)

    def _show_missing(self, line) -> None:
        """A NOT RECEIVED row was clicked in the trace."""
        self.properties_source = "trace"
        self.properties.show_missing(line)
        self.left_tabs.setCurrentIndex(self._properties_tab_index)
