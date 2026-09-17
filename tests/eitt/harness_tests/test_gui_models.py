"""Headless tests for the GUI's model layer (phase 1).

Runs with QT_QPA_PLATFORM=offscreen — a real QApplication and real Qt
models, but no visible window, so this works over SSH and in CI. Skipped
entirely when PySide6 is absent, which is the normal state inside the
Docker conformance image (Qt is deliberately not installed there).

What is worth testing here is the stuff that is easy to get silently wrong:
tree parent/child navigation, and the telegram numbering rule.
"""

from __future__ import annotations

import os

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

# NOT pytest.importorskip: that only turns ModuleNotFoundError into a skip,
# so an INSTALLED-BUT-BROKEN Qt (missing system libs — the usual failure
# inside a slim container) raises ImportError and breaks collection for the
# whole session, taking the conformance suite down with it. Catching
# ImportError covers both "absent" and "unusable", which are the same thing
# from this file's point of view.
try:
    from PySide6.QtCore import QModelIndex, Qt
    from PySide6.QtWidgets import QApplication
except ImportError as exc:
    pytest.skip(f"GUI extra not usable: {exc}", allow_module_level=True)

from interpreter.model import CoAPMessage, Comment, parse_eitt_xml  # noqa: E402
from interpreter.project_copy import cases_in, copy_scope  # noqa: E402
from gui.models import (CaseBreak, CopiesTreeModel,  # noqa: E402
                        ProjectTreeModel, SequenceTableModel)

# Relative to THIS file — pytest may be invoked from the repo root.
TEMPLATE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "data", "KnxConformanceTestTemplate-PointAPI.xml")


def _numbers_of(model):
    """The telegram numbers as bare strings, one per row.

    The `Nr` cell renders as `12:` (EITT's trailing colon), and on a
    full-width row it holds prose instead. Both are display concerns, so
    strip them here rather than teaching every assertion the format.
    """
    out = []
    for row in range(model.rowCount()):
        if model.is_full_width_row(model.item_at(row)):
            out.append("")
            continue
        out.append(model.data(model.index(row, SequenceTableModel.NR))
                   .rstrip(":"))
    return out


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


@pytest.fixture(scope="module")
def project():
    return parse_eitt_xml(TEMPLATE)


# -- tree ------------------------------------------------------------------

def test_tree_exposes_collections(app, project):
    model = ProjectTreeModel(project)
    assert model.rowCount(QModelIndex()) == len(project.collections)


def test_tree_navigates_down_to_test_cases(app, project):
    model = ProjectTreeModel(project)
    collection = model.index(0, 0, QModelIndex())
    assert collection.isValid()
    suite = model.index(0, 0, collection)
    assert suite.isValid()
    case = model.index(0, 0, suite)
    assert case.isValid()
    # A TestCase is a leaf and carries the sequence the grid renders.
    assert model.rowCount(case) == 0
    assert hasattr(model.node_at(case), "sequence")


def test_tree_parent_round_trips(app, project):
    """parent() has to walk UP, but the parser's dataclasses only point
    down — this is the link the model builds by hand, so verify it."""
    model = ProjectTreeModel(project)
    collection = model.index(0, 0, QModelIndex())
    suite = model.index(0, 0, collection)
    case = model.index(0, 0, suite)

    assert model.parent(case).internalPointer() is suite.internalPointer()
    assert model.parent(suite).internalPointer() is collection.internalPointer()
    assert not model.parent(collection).isValid()


# -- grid ------------------------------------------------------------------

def test_grid_numbers_only_active_telegrams(app):
    """EITT numbers telegrams sequentially over the ACTIVE, non-comment
    rows. Reproducing that exactly is what makes our grid comparable to a
    recorded trace (traces/5_4_1.xml numbers 1..325 with gaps at 99/100
    where a deactivated pair sits)."""
    from interpreter.model import CoAPMessage

    def message(active):
        return CoAPMessage(
            id=f"m{active}", activate=active, time_to_next="", cway="IN",
            wait=True, type="CON", code="GET", cf="", accepted_cf="", ip="",
            path="/p/1", sec_context="", secure=None, comment_attr="",
            content=None, cbor=None)

    sequence = [
        Comment(id="c1", activate=True, text="@[t narrative"),
        message(True),      # -> 1
        message(False),     # -> no number
        message(True),      # -> 2
    ]
    model = SequenceTableModel(sequence)
    assert model.rowCount() == 4

    assert _numbers_of(model) == ["", "1", "", "2"]
    #                             ^comment  ^inactive: both unnumbered


def test_grid_renders_a_real_test_case(app, project):
    _coll, _suite, case = next(iter(project.iter_test_cases()))
    model = SequenceTableModel(case.sequence)
    assert model.rowCount() == len(case.sequence)
    assert model.columnCount() == len(SequenceTableModel.COLUMNS)
    # Every cell must render without raising, for every column.
    for row in range(model.rowCount()):
        for col in range(model.columnCount()):
            assert model.data(model.index(row, col)) is not None


def test_grid_set_sequence_resets(app, project):
    cases = [tc for _c, _s, tc in project.iter_test_cases()]
    model = SequenceTableModel(cases[0].sequence)
    model.set_sequence(cases[1].sequence)
    assert model.rowCount() == len(cases[1].sequence)


# -- checkboxes / copy-over ------------------------------------------------

def test_template_tree_is_checkable_only_when_asked(app, project):
    assert ProjectTreeModel(project)._checkable is False
    assert ProjectTreeModel(project, checkable=True)._checkable is True


def test_check_propagates_down_and_derives_up(app, project):
    """Checking a collection checks its cases; unchecking one case must
    put the collection into the PARTIAL state. Parent state is derived
    rather than stored, so the two can never drift apart."""
    from PySide6.QtCore import Qt

    model = ProjectTreeModel(project, checkable=True)
    collection = model.index(0, 0, QModelIndex())

    model.setData(collection, Qt.Checked, Qt.CheckStateRole)
    assert model.data(collection, Qt.CheckStateRole) == Qt.Checked
    total = len(model.checked_cases())
    assert total > 0

    suite = model.index(0, 0, collection)
    case = model.index(0, 0, suite)
    model.setData(case, Qt.Unchecked, Qt.CheckStateRole)

    assert model.data(collection, Qt.CheckStateRole) == Qt.PartiallyChecked
    assert len(model.checked_cases()) == total - 1


def test_checked_cases_come_back_in_document_order(app, project):
    from PySide6.QtCore import Qt

    model = ProjectTreeModel(project, checkable=True)
    model.setData(model.index(1, 0, QModelIndex()), Qt.Checked,
                  Qt.CheckStateRole)
    checked = model.checked_cases()
    order = [c.name for _a, _b, c in project.iter_test_cases()
             if c in checked]
    assert [c.name for c in checked] == order


def test_grid_active_is_editable_only_on_a_copy(app):
    """Structural read-only guarantee: the template's model is built
    without editable=True, so no code path can toggle it."""
    from PySide6.QtCore import Qt

    from interpreter.model import CoAPMessage

    def message():
        return CoAPMessage(
            id="m", activate=True, time_to_next="", cway="IN", wait=True,
            type="CON", code="GET", cf="", accepted_cf="", ip="",
            path="/p/1", sec_context="", secure=None, comment_attr="",
            content=None, cbor=None)

    readonly = SequenceTableModel([message()])
    index = readonly.index(0, SequenceTableModel.ACTIVE)
    assert not (readonly.flags(index) & Qt.ItemIsUserCheckable)
    assert readonly.setData(index, Qt.Unchecked, Qt.CheckStateRole) is False
    assert readonly.item_at(0).activate is True

    editable = SequenceTableModel([message()], editable=True)
    index = editable.index(0, SequenceTableModel.ACTIVE)
    assert editable.flags(index) & Qt.ItemIsUserCheckable
    assert editable.setData(index, Qt.Unchecked, Qt.CheckStateRole) is True
    assert editable.item_at(0).activate is False


def test_disabling_a_row_renumbers(app):
    """Nr is assigned over the ACTIVE rows, so disabling one has to shift
    every number after it — that is what makes the grid comparable to a
    recorded trace."""
    from PySide6.QtCore import Qt

    from interpreter.model import CoAPMessage

    def message():
        return CoAPMessage(
            id="m", activate=True, time_to_next="", cway="IN", wait=True,
            type="CON", code="GET", cf="", accepted_cf="", ip="",
            path="/p/1", sec_context="", secure=None, comment_attr="",
            content=None, cbor=None)

    model = SequenceTableModel([message(), message(), message()],
                               editable=True)
    assert _numbers_of(model) == ["1", "2", "3"]

    model.setData(model.index(0, SequenceTableModel.ACTIVE), Qt.Unchecked,
                  Qt.CheckStateRole)
    assert _numbers_of(model) == ["", "1", "2"]


# -- columns ---------------------------------------------------------------

def test_columns_match_eitt(app):
    """EITT's columns, in EITT's order. RF Info and SN / DoA stay even
    though the Point API never fills them: the operator's eye learns column
    POSITIONS, and dropping two would shift everything after them."""
    assert SequenceTableModel.COLUMNS == [
        "Nr", "Active", "Connection", "Security", "DPT", "Direction",
        "RF Info", "SN / DoA", "Type", "Method",
        "URI Endpoint", "URI Parameter", "CoAP Content", "Wait"]


def test_last_three_headers_relabel_for_mdns(app, project):
    """Same columns, different meaning per protocol — EITT relabels rather
    than adding more."""
    dns = next(c for c in project.collections if c.name.startswith("5.1"))
    model = SequenceTableModel()
    model.set_cases(cases_in(dns))
    assert [model.headerData(i, Qt.Horizontal) for i in (10, 11, 12)] == \
        ["Service (OUT)", "Service (IN)", "Record Data"]

    coap = next(c for c in project.collections if c.name.startswith("5.8"))
    model = SequenceTableModel()
    model.set_cases(cases_in(coap))
    assert [model.headerData(i, Qt.Horizontal) for i in (10, 11, 12)] == \
        ["URI Endpoint", "URI Parameter", "CoAP Content"]


def test_comments_are_full_width_starting_at_nr(app, project):
    """Comments do NOT follow the columns. EITT starts them at `Nr` and
    lets them overrun to the right; confining one to the content column
    buries it where the eye never goes."""
    collection = next(c for c in project.collections
                      if c.name.startswith("5.1"))
    model = SequenceTableModel()
    model.set_cases(cases_in(collection))

    row = next(r for r in range(model.rowCount())
               if isinstance(model.item_at(r), Comment)
               and model.item_at(r).text.strip())

    assert model.is_full_width_row(model.item_at(row))
    assert model.data(model.index(row, SequenceTableModel.NR))
    # ...and nothing in any other column, or the span would overlap text.
    for column in range(1, model.columnCount()):
        assert not model.data(model.index(row, column))


def test_case_breaks_are_full_width_too(app, project):
    _coll, _suite, case = next(iter(project.iter_test_cases()))
    model = SequenceTableModel()
    model.set_cases([case])
    assert model.is_full_width_row(model.item_at(0))
    assert model.data(model.index(0, SequenceTableModel.NR))


def test_telegram_rows_are_not_full_width(app, project):
    """The span is cleared per row, so a telegram must never claim it —
    setSpan is sticky and a stale span hides every column but the first."""
    collection = next(c for c in project.collections
                      if c.name.startswith("5.8"))
    model = SequenceTableModel()
    model.set_cases(cases_in(collection))
    for row in range(model.rowCount()):
        item = model.item_at(row)
        if isinstance(item, CoAPMessage):
            assert not model.is_full_width_row(item)


def test_connection_column_names_the_protocol(app, project):
    from interpreter.model import DnsMessage

    collection = next(c for c in project.collections
                      if c.name.startswith("5.1"))
    model = SequenceTableModel()
    model.set_cases(cases_in(collection))

    conn = SequenceTableModel.CONNECTION
    coap = next(r for r in range(model.rowCount())
                if isinstance(model.item_at(r), CoAPMessage))
    dns = next(r for r in range(model.rowCount())
               if isinstance(model.item_at(r), DnsMessage))
    assert model.data(model.index(coap, conn)) == "COAP"
    assert model.data(model.index(dns, conn)) == "mDNS"


def test_row_takes_its_protocol_colour(app, project):
    """EITT colours a telegram ROW by its connection — COAP green, mDNS
    violet — so a block of mDNS steps is spottable without reading a word.
    Only `Type` keeps its own tint within the row."""
    from interpreter.model import DnsMessage
    from gui.theme import grid_theme

    theme = grid_theme()
    collection = next(c for c in project.collections
                      if c.name.startswith("5.1"))
    model = SequenceTableModel()
    model.set_cases(cases_in(collection))

    def colour(row, column):
        return model.data(model.index(row, column), Qt.ForegroundRole)

    coap = next(r for r in range(model.rowCount())
                if isinstance(model.item_at(r), CoAPMessage)
                and model.item_at(r).activate)
    dns = next(r for r in range(model.rowCount())
               if isinstance(model.item_at(r), DnsMessage)
               and model.item_at(r).activate)

    others = [c for c in range(model.columnCount())
              if c != SequenceTableModel.TYPE]
    assert all(colour(coap, c) == theme.coap_row for c in others)
    assert all(colour(dns, c) == theme.mdns_row for c in others)


def test_inactive_row_beats_the_protocol_colour(app):
    """A deactivated telegram must read as OFF first and as COAP second —
    97 of them exist and mistaking one for live has cost real time."""
    from gui.theme import grid_theme

    message = CoAPMessage(
        id="m", activate=False, time_to_next="", cway="IN", wait=True,
        type="CON", code="GET", cf="", accepted_cf="", ip="", path="/p/1",
        sec_context="", secure=None, comment_attr="", content=None,
        cbor=None)
    model = SequenceTableModel([message])
    assert model.data(model.index(0, SequenceTableModel.CONNECTION),
                      Qt.ForegroundRole) == grid_theme().inactive


def test_error_codes_are_coloured_differently(app):
    """A 4.xx has to be findable by colour in a wall of 2.05s."""
    def message(code):
        return CoAPMessage(
            id="m", activate=True, time_to_next="", cway="OUT", wait=True,
            type="ACK", code=code, cf="", accepted_cf="", ip="",
            path="/p/1", sec_context="", secure=None, comment_attr="",
            content=None, cbor=None)

    model = SequenceTableModel([message("2.05"), message("4.00")])
    method = SequenceTableModel.METHOD
    ok = model.data(model.index(0, method), Qt.ForegroundRole)
    err = model.data(model.index(1, method), Qt.ForegroundRole)
    assert ok != err


# -- Active is a telegram-only column --------------------------------------

def test_active_column_belongs_to_telegrams_only(app, project):
    """`Activate` is a TELEGRAM attribute, and EITT shows the column for
    telegrams only. Measured on the template: all 3109 CoAPMessages and all
    41 DnsMessages carry it, but only 2 of 3409 Comments do.

    The parser defaults a missing `Activate` to yes — correct for
    EXECUTION, since a comment without one must still run — but the view
    must not read that default back as though the XML had stated it, or
    every comment grows a checkbox that means nothing.
    """
    from PySide6.QtCore import Qt

    from interpreter.model import CoAPMessage, DnsMessage

    collection = next(c for c in project.collections
                      if c.name.startswith("5.1"))
    model = SequenceTableModel(editable=True)
    model.set_cases(cases_in(collection))

    active = SequenceTableModel.ACTIVE
    seen = {"telegram": 0, "other": 0}
    for row in range(model.rowCount()):
        item = model.item_at(row)
        checkable = bool(model.flags(model.index(row, active))
                         & Qt.ItemIsUserCheckable)
        if isinstance(item, (CoAPMessage, DnsMessage)):
            assert checkable, f"row {row}: telegram must be toggleable"
            seen["telegram"] += 1
        else:
            assert not checkable, \
                f"row {row}: {type(item).__name__} must have no Active box"
            seen["other"] += 1

    assert seen["telegram"] > 0 and seen["other"] > 0


def test_comment_active_cell_is_blank(app, project):
    """No text either — a comment has no Active state to report."""
    collection = next(c for c in project.collections
                      if c.name.startswith("5.2"))
    for editable in (False, True):
        model = SequenceTableModel(editable=editable)
        model.set_cases(cases_in(collection))
        for row in range(model.rowCount()):
            if isinstance(model.item_at(row), Comment):
                assert not model.data(
                    model.index(row, SequenceTableModel.ACTIVE))


def test_setdata_refuses_a_comment(app, project):
    """Writing to a Comment's `activate` would invent state the template
    never carried."""
    from PySide6.QtCore import Qt

    collection = next(c for c in project.collections
                      if c.name.startswith("5.2"))
    model = SequenceTableModel(editable=True)
    model.set_cases(cases_in(collection))

    row = next(r for r in range(model.rowCount())
               if isinstance(model.item_at(r), Comment))
    before = model.item_at(row).activate
    assert model.setData(model.index(row, SequenceTableModel.ACTIVE),
                         Qt.Unchecked, Qt.CheckStateRole) is False
    assert model.item_at(row).activate is before


def test_deactivated_comment_still_reads_as_inactive(app, project):
    """The two `Activate="no"` comments in the template are switched-off
    `@@!` operator prompts. They are not editable here, but they must still
    LOOK disabled — a live-looking prompt that never fires is worse than a
    missing one."""
    from gui.theme import grid_theme

    collection = next(c for c in project.collections
                      if c.name.startswith("5.2"))
    model = SequenceTableModel(editable=True)
    model.set_cases(cases_in(collection))

    off = [r for r in range(model.rowCount())
           if isinstance(model.item_at(r), Comment)
           and not model.item_at(r).activate]
    assert off, "expected the deactivated @@! prompts in 5.2"
    for row in off:
        assert model.data(model.index(row, SequenceTableModel.CONTENT),
                          Qt.ForegroundRole) == grid_theme().inactive


# -- opening a whole category ----------------------------------------------

def test_opening_a_collection_concatenates_every_case(app, project):
    """Double-clicking "5.2" opens the whole collection, as EITT does —
    one grid, every case, a separator row before each."""
    collection = next(c for c in project.collections
                      if c.name.startswith("5.2"))
    cases = cases_in(collection)

    model = SequenceTableModel()
    model.set_cases(cases)

    breaks = [r for r in range(model.rowCount())
              if isinstance(model.item_at(r), CaseBreak)]
    assert len(breaks) == len(cases)
    assert model.rowCount() == sum(len(c.sequence) for c in cases) + len(cases)


def test_numbering_is_continuous_across_concatenated_cases(app, project):
    """EITT numbers telegrams over the generated PROJECT, not per case, so
    the count must not restart at each boundary."""
    collection = next(c for c in project.collections
                      if c.name.startswith("5.2"))
    model = SequenceTableModel()
    model.set_cases(cases_in(collection))

    numbers = [int(n) for n in _numbers_of(model) if n]
    assert numbers == list(range(1, len(numbers) + 1))


def test_case_at_maps_a_row_back_to_its_case(app, project):
    collection = next(c for c in project.collections
                      if c.name.startswith("5.2"))
    cases = cases_in(collection)
    model = SequenceTableModel()
    model.set_cases(cases)

    # The row right after the second break belongs to the second case.
    breaks = [r for r in range(model.rowCount())
              if isinstance(model.item_at(r), CaseBreak)]
    assert model.case_at(breaks[1] + 1) == cases[1].name
    assert model.case_at(breaks[0] + 1) == cases[0].name


def test_a_single_case_still_gets_one_break_row(app, project):
    """Uniform layout regardless of how many cases were opened — no second
    code path that only runs in the one-case situation."""
    _coll, _suite, case = next(iter(project.iter_test_cases()))
    model = SequenceTableModel()
    model.set_cases([case])

    assert model.rowCount() == len(case.sequence) + 1
    assert isinstance(model.item_at(0), CaseBreak)
    assert model.item_at(0).name == case.name


def test_case_break_rows_are_inert(app, project):
    """A separator has no `activate` to toggle. Without an explicit guard
    an editable grid would draw a checkbox on it that does nothing."""
    from PySide6.QtCore import Qt

    _coll, _suite, case = next(iter(project.iter_test_cases()))
    model = SequenceTableModel(editable=True)
    model.set_cases([case])

    index = model.index(0, SequenceTableModel.ACTIVE)
    assert not (model.flags(index) & Qt.ItemIsUserCheckable)
    assert model.setData(index, Qt.Unchecked, Qt.CheckStateRole) is False


# -- copies tree -----------------------------------------------------------

def _copy_of(project, prefix):
    collection = next(c for c in project.collections
                      if c.name.startswith(prefix))
    return copy_scope(project, cases_in(collection),
                      name=f"Copy of {collection.name}")


def test_copies_tree_holds_several_projects_as_roots(app, project):
    """Each copy-over is a separate project, listed beside the others —
    EITT's "Copy of 5.4.1. POST /k V40" alongside the original."""
    model = CopiesTreeModel()
    assert model.rowCount(QModelIndex()) == 0

    first = _copy_of(project, "5.3")
    second = _copy_of(project, "5.4")
    model.add_copy(first)
    model.add_copy(second)

    assert model.rowCount(QModelIndex()) == 2
    labels = [model.data(model.index(i, 0, QModelIndex())) for i in range(2)]
    assert "Copy of 5.3. Security" in labels[0]
    assert f"[{first.case_count} cases]" in labels[0]
    assert "Copy of 5.4. Messaging" in labels[1]


def test_copies_tree_navigates_down_to_cases(app, project):
    model = CopiesTreeModel()
    model.add_copy(_copy_of(project, "5.3"))

    root = model.index(0, 0, QModelIndex())
    collection = model.index(0, 0, root)
    suite = model.index(0, 0, collection)
    case = model.index(0, 0, suite)

    assert model.rowCount(case) == 0
    assert hasattr(model.node_at(case), "sequence")
    assert model.parent(case).internalPointer() is suite.internalPointer()
    assert not model.parent(root).isValid()


def test_result_for_walks_up_to_the_owning_copy(app, project):
    """Removing a copy from a context menu needs the ROOT the clicked node
    belongs to, whatever depth was clicked."""
    model = CopiesTreeModel()
    first = _copy_of(project, "5.3")
    second = _copy_of(project, "5.4")
    model.add_copy(first)
    model.add_copy(second)

    root = model.index(1, 0, QModelIndex())
    deep = model.index(0, 0, model.index(0, 0, model.index(0, 0, root)))
    assert model.result_for(deep) is second
    assert model.result_for(root) is second


def test_removing_a_copy_drops_only_that_one(app, project):
    model = CopiesTreeModel()
    first = _copy_of(project, "5.3")
    second = _copy_of(project, "5.4")
    model.add_copy(first)
    model.add_copy(second)

    model.remove_copy(first)
    assert model.rowCount(QModelIndex()) == 1
    assert model.result_for(model.index(0, 0, QModelIndex())) is second


def test_inactive_rows_are_visually_distinct(app):
    """97 deactivated CoAPMessages exist in the template and mistaking one
    for live has cost real debugging time — so this styling is a
    requirement, not decoration."""
    from PySide6.QtCore import Qt

    from interpreter.model import CoAPMessage

    inactive = CoAPMessage(
        id="m", activate=False, time_to_next="", cway="IN", wait=True,
        type="CON", code="GET", cf="", accepted_cf="", ip="", path="/p/1",
        sec_context="", secure=None, comment_attr="", content=None, cbor=None)
    model = SequenceTableModel([inactive])
    assert model.data(model.index(0, 0), Qt.ForegroundRole) is not None
