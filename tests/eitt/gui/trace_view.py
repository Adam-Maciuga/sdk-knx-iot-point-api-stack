"""The tracebuffer widget.

Renders `gui.trace.TraceBuffer` lines the way EITT does: a number gutter on
the left, monospaced text on the right.

The gutter is the part that carries meaning and is easy to get wrong:

  BLUE number   a telegram of the sequence itself
  GREEN number  an EXTERNALLY loaded telegram — from a called preparation
  (blank)       captured but never awaited (the pahdiscover probes)

Blue and green run SEPARATE counters, so a preparation numbers 1,2,3… in
green while the outer sequence continues 286,287… in blue.
"""

from __future__ import annotations

from typing import Optional

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor, QFont
from PySide6.QtWidgets import (QAbstractItemView, QHeaderView, QTreeWidget,
                               QTreeWidgetItem)

from .theme import grid_theme
from .trace import LineKind, TraceBuffer, TraceLine

# Gutter colours. Deliberately close to EITT's: the blue/green distinction
# is the one thing a reader relies on to know whose telegram they are
# looking at.
_GUTTER_SEQUENCE = QColor(0, 60, 200)
_GUTTER_EXTERNAL = QColor(0, 130, 40)
_GUTTER_FG = QColor(255, 255, 255)


class TraceView(QTreeWidget):
    """Live trace of a running sequence."""

    #: A telegram line was clicked — carries the `eitt.events.Telegram`.
    telegram_selected = Signal(object)
    #: A NOT RECEIVED line was clicked — carries the whole TraceLine, so the
    #: properties pane can show what was expected AND why it failed.
    missing_selected = Signal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setColumnCount(2)
        self.setHeaderHidden(True)
        self.setRootIsDecorated(False)
        self.setIndentation(0)
        self.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.setUniformRowHeights(True)
        self.header().setSectionResizeMode(0, QHeaderView.Fixed)
        self.header().setSectionResizeMode(1, QHeaderView.Stretch)
        self.setColumnWidth(0, 54)

        font = QFont("Consolas")
        if not font.exactMatch():
            font = QFont("Courier New")
        font.setPointSize(9)
        self.setFont(font)

        self._buffer: Optional[TraceBuffer] = None
        self._follow = True
        # row -> whether it is currently drawn as failed, so _refresh_verdicts
        # only repaints rows whose verdict actually changed.
        self._marked_failed: dict = {}
        self.itemSelectionChanged.connect(self._on_selection_changed)

    # -- content -----------------------------------------------------------

    def attach(self, buffer: TraceBuffer) -> None:
        """Show `buffer` and render whatever it already holds."""
        self._buffer = buffer
        self.clear()
        self._marked_failed.clear()
        for line in buffer.lines:
            self._append(line)

    def append_new(self) -> None:
        """Draw lines the buffer has gained since the last call.

        Appending only the delta, rather than rebuilding: a full run is
        hundreds of lines and a live rebuild per telegram would make the
        pane stutter exactly when it is being watched.
        """
        if self._buffer is None:
            return
        for line in self._buffer.lines[self.topLevelItemCount():]:
            self._append(line)
        self._refresh_verdicts()
        if self._follow:
            self.scrollToBottom()

    def _refresh_verdicts(self) -> None:
        """Recolour telegrams whose verdict landed after they were drawn.

        A telegram is emitted when it arrives, but judged by the OUT step
        that follows — so at append time `eval_ok` is still None and the
        row is drawn as ordinary. Without this pass a failing 4.03 never
        turns red.
        """
        if self._buffer is None:
            return
        theme = grid_theme()
        for row, line in enumerate(self._buffer.lines):
            if line.kind is not LineKind.TELEGRAM or line.telegram is None:
                continue
            failed = line.telegram.eval_ok is False
            if failed == self._marked_failed.get(row):
                continue
            self._marked_failed[row] = failed
            item = self.topLevelItem(row)
            if item is not None:
                item.setForeground(1, theme.error if failed
                                   else theme.default)

    def set_follow(self, follow: bool) -> None:
        """Whether to keep scrolling to the newest line.

        Turned off automatically when the user scrolls up — chasing the
        bottom while someone is reading is the classic log-viewer sin.
        """
        self._follow = follow

    def _append(self, line: TraceLine) -> None:
        number = "" if line.number is None else f"{line.number}:"
        item = QTreeWidgetItem(self, [number, line.text])
        theme = grid_theme()

        if line.kind in (LineKind.TELEGRAM, LineKind.MISSING):
            if line.number is not None:
                item.setBackground(0, _GUTTER_EXTERNAL if line.external
                                   else _GUTTER_SEQUENCE)
                item.setForeground(0, _GUTTER_FG)
                item.setTextAlignment(0, Qt.AlignRight | Qt.AlignVCenter)
            if line.kind is LineKind.MISSING:
                # A whole RED row: nothing arrived, and an absent line
                # would read as "not run yet" rather than "the device went
                # silent" — the most common real failure.
                item.setBackground(0, theme.error)
                item.setBackground(1, theme.error)
                item.setForeground(0, _GUTTER_FG)
                item.setForeground(1, _GUTTER_FG)
                font = item.font(1)
                font.setBold(True)
                item.setFont(1, font)
            else:
                # A failed telegram is the thing you are hunting for.
                item.setForeground(1, theme.error if line.ok is False
                                   else theme.default)
        elif line.kind is LineKind.OPERATOR:
            # Unmissable: the run PAUSED for a human here.
            item.setBackground(1, theme.current_step_bg)
            item.setForeground(1, theme.current_step_fg)
            font = item.font(1)
            font.setBold(True)
            item.setFont(1, font)
        elif line.kind is LineKind.COMMENT:
            item.setForeground(1, theme.comment)
            font = item.font(1)
            font.setItalic(True)
            item.setFont(1, font)
        elif line.kind is LineKind.STRUCTURE:
            item.setForeground(1, theme.type_out)
        elif line.kind is LineKind.HEADER:
            item.setForeground(1, theme.connection)
            font = item.font(1)
            font.setBold(True)
            item.setFont(1, font)
        elif line.kind is LineKind.VERDICT:
            item.setForeground(1, theme.connection)

        item.setData(0, Qt.UserRole, line)

    # -- interaction -------------------------------------------------------

    def _on_selection_changed(self) -> None:
        items = self.selectedItems()
        if not items:
            return
        line = items[0].data(0, Qt.UserRole)
        if line is None:
            return
        if line.kind is LineKind.MISSING:
            # Nothing arrived, so the pane shows what was EXPECTED plus the
            # error assessment in Evaluation Result — which is the only
            # diagnostic a missing telegram has.
            self.missing_selected.emit(line)
        elif line.telegram is not None:
            self.telegram_selected.emit(line.telegram)
