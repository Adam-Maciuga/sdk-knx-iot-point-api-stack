"""Tests for MainWindow interaction rules.

Specifically the properties-pane switching: clicking a telegram — in the
sequence grid now, in the trace later — has to bring the Telegram
Properties pane forward, and the pane must follow whichever view was
clicked LAST.
"""

from __future__ import annotations

import os

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

try:
    from PySide6.QtCore import QModelIndex
    from PySide6.QtWidgets import QApplication
except ImportError as exc:
    pytest.skip(f"GUI extra not usable: {exc}", allow_module_level=True)

from eitt.model import CoAPMessage                     # noqa: E402
from gui.main_window import MainWindow                 # noqa: E402
from gui.models import CaseBreak                       # noqa: E402


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


@pytest.fixture
def window(app):
    return MainWindow()


@pytest.fixture
def project(window):
    """The window's own parsed template — the same objects its models hold,
    so a lookup here matches what the grid is showing."""
    return window.project


def _open_a_case(window, prefix="5.3"):
    model = window.tree_model
    for row in range(model.rowCount(QModelIndex())):
        collection = model.index(row, 0, QModelIndex())
        if model.node_at(collection).name.startswith(prefix):
            suite = model.index(0, 0, collection)
            case = model.index(0, 0, suite)
            window._on_template_activated(case)
            return
    pytest.skip(f"no {prefix} collection")


def _first_telegram_row(window):
    model = window.grid_model
    return next(r for r in range(model.rowCount())
                if isinstance(model.item_at(r), CoAPMessage))


def test_tabs_use_eitts_names(window):
    """"Conformance Tests" is the MASTER and "CTL" the copies — the
    opposite of what the words suggest, and deliberately EITT's."""
    labels = [window.left_tabs.tabText(i)
              for i in range(window.left_tabs.count())]
    assert labels[0] == "Conformance Tests"
    assert labels[1].startswith("CTL")
    assert labels[2] == "Telegram Properties"


def test_clicking_a_telegram_switches_to_properties(window):
    _open_a_case(window)
    assert window.left_tabs.currentIndex() != window._properties_tab_index

    row = _first_telegram_row(window)
    window.grid.setCurrentIndex(window.grid_model.index(row, 0))

    assert window.left_tabs.currentIndex() == window._properties_tab_index
    assert window.properties_source == "sequence"
    assert window.properties.tree.topLevelItemCount() > 0


def test_last_clicked_view_owns_the_pane(window):
    """The sequence shows the PLANNED telegram and the trace what actually
    happened. Keeping the older one on screen would leave you reading the
    wrong half without noticing."""
    _open_a_case(window)
    row = _first_telegram_row(window)
    item = window.grid_model.item_at(row)

    window.grid.setCurrentIndex(window.grid_model.index(row, 0))
    assert window.properties_source == "sequence"

    # Phase 2 will call this from the trace pane; the rule is tested now so
    # the trace side cannot land with different behaviour.
    window.show_properties_for(item, source="trace")
    assert window.properties_source == "trace"

    window.grid.setCurrentIndex(window.grid_model.index(row, 0))
    window._on_row_selected(window.grid_model.index(row, 0), QModelIndex())
    assert window.properties_source == "sequence"


# -- transport controls ----------------------------------------------------

def test_transport_controls_exist_and_start_idle(window):
    assert window.run_action.text() == "Play"
    assert window.stop_action.text() == "Stop"
    assert window.reset_action.text() == "Reset"
    # Stop is meaningless before anything runs.
    assert window.run_action.isEnabled()
    assert not window.stop_action.isEnabled()


def test_play_starts_at_the_selected_row(window):
    """"Play from here" is the point of the control — starting at the top
    regardless of selection would make it a plain Run button."""
    _open_a_case(window)
    row = _first_telegram_row(window)
    window.grid.setCurrentIndex(window.grid_model.index(row, 0))

    # The grid holds a CaseBreak before the steps, so the view row and the
    # sequence index are NOT the same number.
    assert window._selected_step_index() < row


def test_step_index_is_relative_to_its_own_case(window, project):
    """With several cases concatenated, a step index only means something
    relative to its CaseBreak — otherwise "play from here" would start at
    the wrong step in every case but the first."""
    collection = next(c for c in project.collections
                      if c.name.startswith("5.2"))
    window._open_sequence(collection, editable=False)
    model = window.grid_model

    breaks = [r for r in range(model.rowCount())
              if isinstance(model.item_at(r), CaseBreak)]
    assert len(breaks) > 1

    # First row of the SECOND case is step 0 of that case, not step N.
    window.grid.setCurrentIndex(model.index(breaks[1] + 1, 0))
    assert window._selected_step_index() == 0


def test_reset_clears_the_trace_and_returns_to_the_top(window):
    _open_a_case(window)
    window.trace_buffer.on_case(
        __import__("eitt.events", fromlist=["CaseEvent"]).CaseEvent(
            name="x", phase="start"))
    assert window.trace_buffer.lines

    window._on_reset_clicked()

    assert window.trace_buffer.lines == []
    assert window.grid_model.current_row is None
    assert window._resume_index is None


def test_stop_records_where_to_resume(window):
    """Play after a Stop continues from that row rather than restarting —
    otherwise stopping mid-sequence would lose the position."""
    _open_a_case(window)
    case_name = window.grid_model.case_at(1)

    window._on_run_stopped(case_name, 4)

    assert window._resume_index == 4
    assert window.grid_model.current_row is not None


def test_current_step_highlight_moves_and_clears(window):
    _open_a_case(window)
    model = window.grid_model

    model.set_current_step(3)
    assert model.current_row == 3
    model.set_current_step(5)
    assert model.current_row == 5
    model.set_current_step(None)
    assert model.current_row is None


def test_row_for_step_maps_back_into_the_grid(window, project):
    """The highlight needs the VIEW row for a step index, and the two only
    line up inside one case."""
    collection = next(c for c in project.collections
                      if c.name.startswith("5.2"))
    window._open_sequence(collection, editable=False)
    model = window.grid_model

    breaks = [r for r in range(model.rowCount())
              if isinstance(model.item_at(r), CaseBreak)]
    second = model.item_at(breaks[1]).name
    assert model.row_for_step(second, 0) == breaks[1] + 1
    assert model.row_for_step("no such case", 0) is None


def test_case_separator_does_not_hijack_the_pane(window):
    """A separator is view furniture, not a telegram — selecting one must
    not blank a pane the user is reading."""
    _open_a_case(window)
    row = _first_telegram_row(window)
    window.grid.setCurrentIndex(window.grid_model.index(row, 0))
    before = window.properties.tree.topLevelItemCount()

    separator = next(r for r in range(window.grid_model.rowCount())
                     if window.grid_model.is_full_width_row(
                         window.grid_model.item_at(r)))
    window._on_row_selected(window.grid_model.index(separator, 0),
                            QModelIndex())

    assert window.properties.tree.topLevelItemCount() == before
