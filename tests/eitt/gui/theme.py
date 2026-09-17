"""Colours for the sequence grid, derived from the running Qt palette.

Hardcoding a palette is what this replaces. The first version picked colours
against a white background; on a dark theme the "near-black" default text
sat at #1e1e1e on #2d2d2d and was effectively invisible, and Windows'
dark palette reports `AlternateBase` as pure WHITE — so alternating rows
flashed white behind pale text.

So: detect the theme once from `QPalette.Base`, and return a palette tuned
for it. Both variants keep the same MEANING per colour (teal = connection,
red = error, ...) so the two themes read the same way; only the luminance
moves. Dark-theme values are lifted toward pastel because saturated hues
that work on white lose contrast on a dark ground.

Qt's own `Highlight` is used for selection rather than a colour of ours —
matching the desktop is the one thing a user notices immediately.
"""

from __future__ import annotations

from dataclasses import dataclass

from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication


@dataclass(frozen=True)
class GridTheme:
    """Every colour the telegram grid draws with."""

    dark: bool

    default: QColor          # ordinary cell text
    inactive: QColor         # Activate="no" rows
    comment: QColor          # @-command / prose rows
    connection: QColor       # COAP / mDNS
    type_out: QColor         # CON / NON — going out
    type_in: QColor          # ACK — coming back
    method: QColor           # GET / POST / 2.05
    error: QColor            # 4.xx / 5.xx
    uri: QColor
    content: QColor

    case_break_fg: QColor
    case_break_bg: QColor
    alternate_base: QColor   # safe alternating-row tint

    # Per-PROTOCOL row tints. EITT colours a telegram row by its connection
    # — COAP green, mDNS violet — so a block of mDNS steps is spottable
    # without reading a word. We keep the per-column tints for the
    # meaning-bearing cells and use these as the row's base hue, which is a
    # bit more information than EITT shows.
    coap_row: QColor
    mdns_row: QColor

    # The step executing right now. EITT marks the current telegram and
    # scrolls to follow it, which is the only way to see where a long run
    # has got to.
    current_step_bg: QColor
    current_step_fg: QColor


def _is_dark(palette: QPalette) -> bool:
    """Base, not Window: the grid is drawn on Base, and a few themes style
    the two differently."""
    return palette.color(QPalette.ColorRole.Base).lightness() < 128


_LIGHT = dict(
    default=QColor(30, 30, 30),
    # Muted, but still READABLE: at #969696 this was 2.74:1 on white, and
    # the 97 deactivated telegrams are rows you have to be able to read to
    # know what was switched off. #6e6e6e clears AA.
    inactive=QColor(110, 110, 110),
    comment=QColor(0, 70, 190),
    connection=QColor(0, 110, 90),
    type_out=QColor(0, 100, 170),
    type_in=QColor(120, 70, 160),
    method=QColor(20, 120, 20),
    error=QColor(190, 30, 30),
    uri=QColor(0, 90, 160),
    content=QColor(120, 80, 20),
    case_break_fg=QColor(255, 255, 255),
    case_break_bg=QColor(90, 95, 130),
    alternate_base=QColor(246, 246, 249),
    coap_row=QColor(20, 120, 20),
    mdns_row=QColor(140, 40, 170),
    current_step_bg=QColor(255, 225, 120),
    current_step_fg=QColor(20, 20, 20),
)

# Same meanings, lifted for a dark ground. Every one of these clears ~4.5:1
# against #2d2d2d; the saturated light-theme values sit closer to 2:1 there,
# which is what made the first attempt unreadable.
_DARK = dict(
    default=QColor(222, 222, 226),
    # Same reasoning as the light theme's: muted but readable. At #787880
    # this was 3.15:1 on #2d2d2d.
    inactive=QColor(150, 150, 158),
    comment=QColor(120, 170, 255),
    connection=QColor(90, 210, 190),
    type_out=QColor(110, 180, 255),
    type_in=QColor(200, 150, 250),
    method=QColor(120, 220, 120),
    error=QColor(255, 120, 110),
    uri=QColor(130, 190, 255),
    content=QColor(230, 190, 130),
    case_break_fg=QColor(240, 240, 250),
    case_break_bg=QColor(58, 62, 86),
    alternate_base=QColor(38, 38, 40),
    coap_row=QColor(120, 220, 120),
    mdns_row=QColor(215, 155, 255),
    current_step_bg=QColor(120, 95, 0),
    current_step_fg=QColor(255, 248, 220),
)


def grid_theme(palette: QPalette | None = None) -> GridTheme:
    """The palette for the current theme.

    Not cached: a user can switch light/dark while the app runs, and
    re-reading a dozen QColors per repaint is free next to laying out the
    grid.
    """
    if palette is None:
        app = QApplication.instance()
        palette = app.palette() if app is not None else QPalette()
    dark = _is_dark(palette)
    return GridTheme(dark=dark, **(_DARK if dark else _LIGHT))


def banner_style(kind: str, palette: QPalette | None = None) -> str:
    """Stylesheet for a header/banner strip.

    `kind` is "readonly" (the master template — amber) or "edit" (a working
    copy — green). The two were hardcoded to dark-theme values, which left
    them muddy on a light desktop; pick per theme instead.
    """
    dark = grid_theme(palette).dark
    palettes = {
        ("readonly", True): ("#3a3a20", "#e8e0a0"),
        ("readonly", False): ("#fdf6d8", "#5a4b00"),
        ("edit", True): ("#203a20", "#b8e0b8"),
        ("edit", False): ("#e4f5e4", "#1c5220"),
    }
    background, foreground = palettes[(kind, dark)]
    return (f"background: {background}; color: {foreground}; "
            f"padding: 5px; border-radius: 3px;")


def alternate_base(palette: QPalette | None = None) -> QColor:
    """A safe alternating-row colour.

    Windows' dark palette reports `AlternateBase` as pure WHITE, so trusting
    it makes every other row flash white behind pale text. Derive from
    `Base` instead: nudge toward the text colour by a few percent, which is
    all an alternating row should ever be.
    """
    if palette is None:
        app = QApplication.instance()
        palette = app.palette() if app is not None else QPalette()
    return grid_theme(palette).alternate_base
