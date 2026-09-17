"""Qt model/view adapters over the EITT parser.

Phase 1 of the GUI plan. These wrap `parse_eitt_xml()` output directly —
no copying into per-row widgets, which is what keeps 6,559 sequence items
scrolling smoothly. Qt's model/view is virtualised natively, so the models
below are thin: they answer `data()` from the dataclasses the parser
already built.

Kept in its own module (rather than beside the window) because the models
are the part worth testing headlessly — a QAbstractItemModel can be
exercised with QT_QPA_PLATFORM=offscreen and no visible window.
"""

from __future__ import annotations

from typing import Any, Optional

from dataclasses import dataclass

from PySide6.QtCore import QAbstractItemModel, QAbstractTableModel, QModelIndex, Qt
from PySide6.QtGui import QColor, QFont

from eitt.model import CoAPMessage, Comment, DnsMessage, TestCase

from .theme import grid_theme

# Row tinting. `Activate="no"` rows MUST be visually distinct: the template
# has 97 deactivated CoAPMessages and mistaking one for live has cost real
# debugging time (GUI plan, phase 1).
# Colours are NOT constants here — they come from gui.theme, derived from
# the running Qt palette. The first version hardcoded values chosen against
# a white background; on a dark theme the "near-black" default text sat at
# #1e1e1e on #2d2d2d and was invisible. See theme.py for both variants.


class ProjectTreeModel(QAbstractItemModel):
    """Collections -> Suites -> TestCases.

    Uses internalPointer to hold the parsed object itself, so a selection
    hands the rest of the GUI the real TestCase rather than an index it has
    to resolve back.

    With `checkable=True` the tree grows tri-state checkboxes, which is how
    a scope is selected for copy-over. Check state lives HERE and never
    touches the parsed objects — the template is read-only, and a check is
    a GUI selection, not a property of the test.
    """

    def __init__(self, project, parent=None, checkable: bool = False):
        super().__init__(parent)
        self._project = project
        self._checkable = checkable
        # id(node) -> Qt.Checked/Unchecked, leaves only. Parent state is
        # DERIVED from children (see _check_state), so the two can never
        # drift out of agreement.
        self._checked: dict[int, Qt.CheckState] = {}
        # Parent links, built once. QAbstractItemModel.parent() has to be
        # able to walk UP, and the parser's dataclasses only point down.
        self._parent_of: dict[int, Any] = {}
        self._children: dict[int, list] = {}
        self._index_in_parent: dict[int, int] = {}
        self._build(project)

    def _build(self, project) -> None:
        self._children[id(project)] = list(project.collections)
        for collection in project.collections:
            suites = list(getattr(collection, "test_suites", None)
                          or getattr(collection, "suites", []))
            self._register(project, collection, suites)
            for suite in suites:
                cases = list(suite.test_cases)
                self._register(collection, suite, cases)
                for case in cases:
                    self._register(suite, case, [])

    def _register(self, parent, node, children) -> None:
        self._children[id(node)] = children
        self._parent_of[id(node)] = parent
        siblings = self._children.get(id(parent), [])
        try:
            self._index_in_parent[id(node)] = siblings.index(node)
        except ValueError:
            self._index_in_parent[id(node)] = 0

    # -- QAbstractItemModel ------------------------------------------------

    def index(self, row, column, parent=QModelIndex()):
        node = parent.internalPointer() if parent.isValid() else self._project
        children = self._children.get(id(node), [])
        if row < 0 or row >= len(children):
            return QModelIndex()
        return self.createIndex(row, column, children[row])

    def parent(self, index):
        if not index.isValid():
            return QModelIndex()
        parent = self._parent_of.get(id(index.internalPointer()))
        if parent is None or parent is self._project:
            return QModelIndex()
        return self.createIndex(self._index_in_parent.get(id(parent), 0), 0,
                                parent)

    def rowCount(self, parent=QModelIndex()):
        node = parent.internalPointer() if parent.isValid() else self._project
        return len(self._children.get(id(node), []))

    def columnCount(self, parent=QModelIndex()):
        return 1

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        node = index.internalPointer()
        if role == Qt.DisplayRole:
            name = getattr(node, "name", str(node))
            steps = getattr(node, "sequence", None)
            if steps is not None:
                return f"{name}  ({len(steps)})"
            return name
        if role == Qt.ToolTipRole:
            return getattr(node, "name", "")
        if role == Qt.CheckStateRole and self._checkable:
            return self._check_state(node)
        return None

    # -- check state -------------------------------------------------------

    def _leaves(self, node) -> list:
        """Every TestCase under a node — the only things whose check state
        is stored. A collection is 'checked' iff all its cases are."""
        children = self._children.get(id(node), [])
        if not children:
            return [node]
        return [leaf for child in children for leaf in self._leaves(child)]

    def _check_state(self, node) -> Qt.CheckState:
        leaves = self._leaves(node)
        if len(leaves) == 1 and leaves[0] is node:
            return self._checked.get(id(node), Qt.Unchecked)
        checked = sum(1 for leaf in leaves
                      if self._checked.get(id(leaf), Qt.Unchecked) == Qt.Checked)
        if checked == 0:
            return Qt.Unchecked
        if checked == len(leaves):
            return Qt.Checked
        return Qt.PartiallyChecked

    def setData(self, index, value, role=Qt.CheckStateRole):
        if role != Qt.CheckStateRole or not self._checkable or not index.isValid():
            return False
        node = index.internalPointer()
        # Qt hands the value through as an int on some bindings.
        state = Qt.Checked if Qt.CheckState(value) == Qt.Checked else Qt.Unchecked
        for leaf in self._leaves(node):
            self._checked[id(leaf)] = state
        # Ancestors' states are derived, so repaint the whole tree rather
        # than trying to work out which parents changed.
        self.dataChanged.emit(QModelIndex(), QModelIndex(),
                              [Qt.CheckStateRole])
        self.layoutChanged.emit()
        return True

    def flags(self, index):
        base = super().flags(index)
        if self._checkable and index.isValid():
            return base | Qt.ItemIsUserCheckable
        return base

    def checked_cases(self) -> list:
        """The TestCases currently ticked, in document order."""
        out = []
        for _coll, _suite, case in self._project.iter_test_cases():
            if self._checked.get(id(case), Qt.Unchecked) == Qt.Checked:
                out.append(case)
        return out

    def clear_checks(self) -> None:
        self._checked.clear()
        self.layoutChanged.emit()

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if orientation == Qt.Horizontal and role == Qt.DisplayRole:
            return "Project"
        return None

    def node_at(self, index) -> Optional[Any]:
        return index.internalPointer() if index.isValid() else None


class CopiesTreeModel(QAbstractItemModel):
    """Several working projects in one tree, each its own root.

    Mirrors EITT listing "Copy of 5.4.1. POST /k V40" alongside the
    original. Roots are `CopyResult`s rather than bare projects so a node
    can report what preparation was pulled in and whether anything is
    unresolved.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self._results: list = []
        self._parent_of: dict[int, Any] = {}
        self._children: dict[int, list] = {}
        self._index_in_parent: dict[int, int] = {}

    def add_copy(self, result) -> None:
        self.beginResetModel()
        self._results.append(result)
        self._rebuild()
        self.endResetModel()

    def remove_copy(self, result) -> None:
        self.beginResetModel()
        if result in self._results:
            self._results.remove(result)
        self._rebuild()
        self.endResetModel()

    @property
    def results(self) -> list:
        return list(self._results)

    def _rebuild(self) -> None:
        self._parent_of.clear()
        self._children.clear()
        self._index_in_parent.clear()
        self._children[id(self)] = list(self._results)
        for result in self._results:
            collections = list(result.project.collections)
            self._register(self, result, collections)
            for collection in collections:
                suites = list(collection.test_suites)
                self._register(result, collection, suites)
                for suite in suites:
                    cases = list(suite.test_cases)
                    self._register(collection, suite, cases)
                    for case in cases:
                        self._register(suite, case, [])

    def _register(self, parent, node, children) -> None:
        self._children[id(node)] = children
        self._parent_of[id(node)] = parent
        siblings = self._children.get(id(parent), [])
        try:
            self._index_in_parent[id(node)] = siblings.index(node)
        except ValueError:
            self._index_in_parent[id(node)] = 0

    def index(self, row, column, parent=QModelIndex()):
        node = parent.internalPointer() if parent.isValid() else self
        children = self._children.get(id(node), [])
        if row < 0 or row >= len(children):
            return QModelIndex()
        return self.createIndex(row, column, children[row])

    def parent(self, index):
        if not index.isValid():
            return QModelIndex()
        parent = self._parent_of.get(id(index.internalPointer()))
        if parent is None or parent is self:
            return QModelIndex()
        return self.createIndex(self._index_in_parent.get(id(parent), 0), 0,
                                parent)

    def rowCount(self, parent=QModelIndex()):
        node = parent.internalPointer() if parent.isValid() else self
        return len(self._children.get(id(node), []))

    def columnCount(self, parent=QModelIndex()):
        return 1

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        node = index.internalPointer()
        if role == Qt.DisplayRole:
            # A CopyResult root shows the project name plus its case count.
            project = getattr(node, "project", None)
            if project is not None:
                return f"{project.name}  [{node.case_count} cases]"
            name = getattr(node, "name", str(node))
            steps = getattr(node, "sequence", None)
            return f"{name}  ({len(steps)})" if steps is not None else name
        if role == Qt.ToolTipRole:
            project = getattr(node, "project", None)
            if project is not None and node.unresolved:
                return "UNRESOLVED: " + ", ".join(node.unresolved)
            return getattr(node, "name", "")
        return None

    def node_at(self, index) -> Optional[Any]:
        return index.internalPointer() if index.isValid() else None

    def result_for(self, index):
        """The CopyResult a node belongs to — walk up to the root."""
        node = self.node_at(index)
        while node is not None and not hasattr(node, "project"):
            node = self._parent_of.get(id(node))
            if node is self:
                return None
        return node


def _has_active(item) -> bool:
    """True for the rows where `Active` is a real, editable property.

    `Activate` is a TELEGRAM attribute. Measured on the template: all 3109
    CoAPMessages and all 41 DnsMessages carry it, but only 2 of 3409
    Comments do. The parser defaults a missing `Activate` to yes — correct
    for EXECUTION, since a comment without one must still run — but that
    default made every comment look like it had a toggle. EITT shows the
    column for telegrams only, so the view must not read the default back
    as if the XML had stated it.
    """
    return isinstance(item, (CoAPMessage, DnsMessage))


@dataclass
class CaseBreak:
    """A separator row naming the TestCase whose steps follow.

    Opening a whole collection concatenates many sequences into one grid, so
    without these the boundaries are invisible and a row's owning case is
    unknowable. Not a sequence item — it exists only in the view.
    """

    name: str
    index: int          # 1-based position among the opened cases
    total: int
    step_count: int


class SequenceTableModel(QAbstractTableModel):
    """A TestCase's sequence as the EITT telegram grid — or several.

    Double-clicking a collection or suite opens every case beneath it, laid
    out end to end with a `CaseBreak` row before each, which is what EITT
    does. Telegram numbering runs CONTINUOUSLY across the whole opened
    scope, matching how EITT numbers a generated project rather than
    restarting per case.

    Columns are EITT's, in EITT's order. `RF Info` and `SN / DoA` are kept
    even though the Point API never fills them: the operator's eye learns
    column POSITIONS, and dropping two would shift everything after them.

    The last three columns are RELABELLED per protocol, as EITT does —
    `URI Endpoint / URI Parameter / CoAP Content` for CoAP becomes
    `Service (OUT) / Service (IN) / Record Data` for mDNS. Same columns,
    different meaning, so the header follows whatever is on screen.

    Comment rows do NOT follow the columns. They start at the `Nr` column
    and run the full width — see `is_full_width_row`.
    """

    COLUMNS = ["Nr", "Active", "Connection", "Security", "DPT", "Direction",
               "RF Info", "SN / DoA", "Type", "Method",
               "URI Endpoint", "URI Parameter", "CoAP Content", "Wait"]

    #: Headers for the same three columns when the grid holds mDNS rows.
    DNS_COLUMNS = {10: "Service (OUT)", 11: "Service (IN)", 12: "Record Data"}

    (NR, ACTIVE, CONNECTION, SECURITY, DPT, DIRECTION, RF_INFO, SN_DOA,
     TYPE, METHOD, URI, PARAM, CONTENT, WAIT) = range(14)

    def __init__(self, sequence=None, parent=None, editable: bool = False):
        super().__init__(parent)
        # Only a COPY is editable. The template model is constructed without
        # this flag, so the read-only guarantee is structural rather than a
        # convention someone has to remember.
        self._editable = editable
        self._items: list = list(sequence or [])
        # Whether the open scope contains mDNS steps, which relabels the
        # last three column headers (see DNS_COLUMNS).
        self._is_dns_view = any(isinstance(i, DnsMessage)
                                for i in self._items)
        # Display numbers, assigned over ACTIVE telegram rows only — this is
        # how EITT numbers them, and it is why the numbers in the
        # certification deviations list can never be mapped onto our copy.
        # Display only: never use this as an identifier (use item.id).
        self._numbers = self._assign_numbers(self._items)
        # Per-row verdict, filled in by a live run. None = not run.
        self._verdicts: dict[int, bool] = {}
        # The step executing right now, highlighted so the operator can see
        # where a run has got to — EITT keeps the current telegram marked
        # and scrolls to follow it.
        self._current_row: Optional[int] = None

    @staticmethod
    def _assign_numbers(items) -> dict[int, int]:
        numbers: dict[int, int] = {}
        counter = 0
        for row, item in enumerate(items):
            if isinstance(item, (Comment, CaseBreak)):
                continue
            if not getattr(item, "activate", True):
                continue
            counter += 1
            numbers[row] = counter
        return numbers

    def set_sequence(self, sequence) -> None:
        self.beginResetModel()
        self._items = list(sequence or [])
        self._is_dns_view = any(isinstance(i, DnsMessage)
                                for i in self._items)
        self._numbers = self._assign_numbers(self._items)
        self._verdicts = {}
        self.endResetModel()

    def set_cases(self, cases) -> None:
        """Open several TestCases end to end, each behind a CaseBreak row.

        A single case gets a break row too: it names what is on screen, and
        making the layout independent of how many cases were opened avoids a
        second code path that only runs in the one-case situation.
        """
        cases = list(cases)
        items: list = []
        for position, case in enumerate(cases, start=1):
            items.append(CaseBreak(name=case.name, index=position,
                                   total=len(cases),
                                   step_count=len(case.sequence)))
            items.extend(case.sequence)

        self.beginResetModel()
        self._items = items
        self._is_dns_view = any(isinstance(i, DnsMessage) for i in items)
        self._numbers = self._assign_numbers(self._items)
        self._verdicts = {}
        self.endResetModel()

    def set_current_step(self, row: Optional[int]) -> None:
        """Mark the row a run is executing, or None to clear it."""
        previous, self._current_row = self._current_row, row
        for changed in (previous, row):
            if changed is not None and 0 <= changed < len(self._items):
                self.dataChanged.emit(
                    self.index(changed, 0),
                    self.index(changed, self.columnCount() - 1))

    @property
    def current_row(self) -> Optional[int]:
        return self._current_row

    def row_for_step(self, case_name: str, step_index: int) -> Optional[int]:
        """View row for `step_index` within the named case.

        The grid can hold several concatenated cases, so a step index is
        only meaningful relative to its CaseBreak.
        """
        offset = None
        for row, item in enumerate(self._items):
            if isinstance(item, CaseBreak):
                offset = row + 1 if item.name == case_name else None
            elif offset is not None and row - offset == step_index:
                return row
        return None

    def case_at(self, row: int) -> str:
        """Name of the TestCase owning `row` — the nearest break above it."""
        for index in range(min(row, len(self._items) - 1), -1, -1):
            item = self._items[index]
            if isinstance(item, CaseBreak):
                return item.name
        return ""

    def set_verdict(self, row: int, ok: bool) -> None:
        if 0 <= row < len(self._items):
            self._verdicts[row] = ok
            left = self.index(row, 0)
            right = self.index(row, self.columnCount() - 1)
            self.dataChanged.emit(left, right)

    def item_at(self, row: int):
        return self._items[row] if 0 <= row < len(self._items) else None

    # -- QAbstractTableModel -----------------------------------------------

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self._items)

    def columnCount(self, parent=QModelIndex()):
        return len(self.COLUMNS)

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if role != Qt.DisplayRole:
            return None
        if orientation == Qt.Horizontal:
            # The last three columns mean different things for mDNS, and
            # EITT relabels them rather than adding more columns.
            if self._is_dns_view and section in self.DNS_COLUMNS:
                return self.DNS_COLUMNS[section]
            return self.COLUMNS[section]
        return None

    def _foreground(self, item, column: int):
        """Protocol drives the ROW hue; a few columns keep their own tint.

        EITT colours a whole telegram row by its connection — COAP green,
        mDNS violet — so a block of mDNS steps is spottable without reading
        a word. That is the property worth having, and an earlier
        per-column-only scheme lost it.

        We keep slightly more than EITT: the columns that carry a verdict at
        a glance (`Type`, and `Method` when it is a 4.xx/5.xx) hold their
        own colour, so an error is still findable inside a green block.
        Everything else takes the protocol hue.
        """
        theme = grid_theme()
        if isinstance(item, CaseBreak):
            return theme.case_break_fg
        if not getattr(item, "activate", True):
            return theme.inactive
        if isinstance(item, Comment):
            return theme.comment

        # An error code outranks the protocol hue — it is the one thing you
        # must never miss in a wall of 2.05s.
        if column == self.METHOD:
            code = str(getattr(item, "code", ""))
            if code[:1] in ("4", "5"):
                return theme.error
        if column == self.TYPE:
            # CON/NON going out vs ACK coming back: worth distinguishing
            # within the row.
            return (theme.type_in
                    if str(getattr(item, "type", "")).upper() == "ACK"
                    else theme.type_out)
        # The row's own colour, by protocol.
        if isinstance(item, DnsMessage):
            return theme.mdns_row
        if isinstance(item, CoAPMessage):
            return theme.coap_row
        return theme.default

    @staticmethod
    def is_full_width_row(item) -> bool:
        """True for rows that ignore the columns and run the whole width.

        Comments and case separators are prose, not telegrams. EITT starts
        them at the `Nr` column and lets them overrun everything to the
        right — putting a comment in the content column instead (which is
        what this did first) buries it far off to the right where the eye
        never goes.
        """
        return isinstance(item, (Comment, CaseBreak))

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        item = self._items[index.row()]

        if role == Qt.DisplayRole:
            return self._display(item, index.row(), index.column())
        if role == Qt.ForegroundRole:
            # On the current-step band the protocol hues lose contrast, so
            # the whole row switches to one high-contrast colour.
            if index.row() == self._current_row:
                return grid_theme().current_step_fg
            return self._foreground(item, index.column())
        if role == Qt.BackgroundRole:
            # The executing step wins over everything: it is the one row
            # the operator is watching during a run.
            if index.row() == self._current_row:
                return grid_theme().current_step_bg
            if isinstance(item, CaseBreak):
                return grid_theme().case_break_bg
        if role == Qt.FontRole:
            if isinstance(item, CaseBreak):
                font = QFont()
                font.setBold(True)
                return font
            if isinstance(item, Comment):
                font = QFont()
                font.setItalic(True)
                return font
        if role == Qt.ToolTipRole:
            return self._tooltip(item)
        if (role == Qt.CheckStateRole and self._editable
                and index.column() == self.ACTIVE and _has_active(item)):
            return Qt.Checked if item.activate else Qt.Unchecked
        return None

    def setData(self, index, value, role=Qt.EditRole):
        """Toggle a step's Active flag — EITT's enable/disable telegram.

        Writes straight to the copied dataclass. Safe because copy_scope
        deep-copies: the master's objects are never reachable from here.
        Renumbering follows, since Nr is assigned over the ACTIVE rows only.
        """
        if (role != Qt.CheckStateRole or not self._editable
                or not index.isValid() or index.column() != self.ACTIVE):
            return False
        item = self._items[index.row()]
        # Telegrams only — see _has_active. A Comment's `activate` is a
        # parser default, not something the XML said, so writing to it here
        # would invent state the template never carried.
        if not _has_active(item):
            return False
        item.activate = Qt.CheckState(value) == Qt.Checked
        self._numbers = self._assign_numbers(self._items)
        top = self.index(0, 0)
        bottom = self.index(self.rowCount() - 1, self.columnCount() - 1)
        self.dataChanged.emit(top, bottom)
        return True

    def flags(self, index):
        base = super().flags(index)
        if not index.isValid():
            return base
        # Separators and comments have no `Activate` of their own; drawing a
        # checkbox on them would offer a control that does nothing.
        if isinstance(self._items[index.row()], CaseBreak):
            return Qt.ItemIsEnabled
        if (self._editable and index.column() == self.ACTIVE
                and _has_active(self._items[index.row()])):
            return base | Qt.ItemIsUserCheckable
        return base

    def _display(self, item, row: int, column: int) -> str:
        # Full-width rows: all the text sits in the FIRST column and the
        # view spans it across the rest (see is_full_width_row).
        if isinstance(item, CaseBreak):
            if column == self.NR:
                scope = (f"  [{item.index}/{item.total}]"
                         if item.total > 1 else "")
                return f"{item.name}{scope}  —  {item.step_count} steps"
            return ""

        if isinstance(item, Comment):
            if column == self.NR:
                return " ".join(item.text.split())
            return ""

        if column == self.NR:
            return str(self._numbers.get(row, "")) + ":" \
                if self._numbers.get(row) else ""
        if column == self.ACTIVE:
            # Text only in the read-only grid; an editable one renders the
            # checkbox instead and a duplicate label beside it is noise.
            if self._editable:
                return ""
            return "yes" if item.activate else "no"
        if column == self.DIRECTION:
            return item.cway
        if column == self.WAIT:
            return item.time_to_next or ""
        # RF Info and SN / DoA stay empty for the Point API — kept so the
        # column POSITIONS match EITT's.
        if column in (self.RF_INFO, self.SN_DOA, self.DPT):
            return ""

        if isinstance(item, DnsMessage):
            if column == self.CONNECTION:
                return "mDNS"
            if column == self.TYPE:
                return item.record_type
            # `Service (OUT)` / `Service (IN)`: EITT puts the queried name
            # on the IN row and the answering name on the OUT row.
            if column == self.URI:
                return item.service if item.cway == "OUT" else ""
            if column == self.PARAM:
                return item.service if item.cway == "IN" else ""
            if column == self.CONTENT:      # Record Data
                return item.host or ""
            return ""

        if isinstance(item, CoAPMessage):
            if column == self.CONNECTION:
                return "COAP"
            if column == self.SECURITY:
                return item.sec_context
            if column == self.TYPE:
                return item.type
            if column == self.METHOD:
                return item.code
            if column == self.URI:
                return item.path
            if column == self.PARAM:
                return "&".join(f"{p.key}={p.value}"
                                for p in item.url_parameters)
            if column == self.CONTENT:
                body = item.content or item.cbor or ""
                # `<\/fp\/g\/1>` -> `</fp/g/1>`: the template escapes
                # forward slashes in link-format payloads and EITT shows
                # them unescaped. Display only — the matcher still compares
                # against the raw text.
                return " ".join(body.split())[:200].replace("\\/", "/")
        return ""

    @staticmethod
    def _tooltip(item) -> str:
        if isinstance(item, CaseBreak):
            return f"{item.name} — {item.step_count} steps"
        if isinstance(item, Comment):
            return item.text
        if isinstance(item, CoAPMessage):
            parts = [f"ID: {item.id}"]
            if item.content:
                parts.append(f"Content: {item.content}")
            if item.eval_fn:
                parts.append(f"EvalFn: {item.eval_fn}")
            if item.eval_response_fn:
                parts.append(f"EvalResponseFn: {item.eval_response_fn}")
            return "\n".join(parts)
        return getattr(item, "id", "")
