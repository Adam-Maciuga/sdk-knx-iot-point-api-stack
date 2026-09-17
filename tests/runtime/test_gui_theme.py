"""Contrast tests for the grid palettes.

Readability is not a matter of taste here: the first version hardcoded
colours picked against a white background, and on a dark desktop the
"near-black" default text sat at #1e1e1e on #2d2d2d — invisible. These
tests compute real WCAG contrast ratios so that cannot come back.
"""

from __future__ import annotations

import os

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

try:
    from PySide6.QtGui import QColor, QPalette
    from PySide6.QtWidgets import QApplication
except ImportError as exc:
    pytest.skip(f"GUI extra not usable: {exc}", allow_module_level=True)

from gui.theme import (_DARK, _LIGHT, alternate_base,  # noqa: E402
                       banner_style, grid_theme)

# WCAG 2.1 AA for normal text. Everything here is either body text or a
# short coloured token that has to be readable at a glance.
AA = 4.5

TINTS = ("default", "inactive", "comment", "connection", "type_out",
         "type_in", "method", "error", "uri", "content",
         "coap_row", "mdns_row")


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


def _luminance(colour: QColor) -> float:
    def channel(value: int) -> float:
        v = value / 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return (0.2126 * channel(colour.red())
            + 0.7152 * channel(colour.green())
            + 0.0722 * channel(colour.blue()))


def _contrast(fg: QColor, bg: QColor) -> float:
    a, b = _luminance(fg), _luminance(bg)
    lo, hi = min(a, b), max(a, b)
    return (hi + 0.05) / (lo + 0.05)


def _palette(dark: bool) -> QPalette:
    palette = QPalette()
    base = QColor(45, 45, 45) if dark else QColor(255, 255, 255)
    text = QColor(255, 255, 255) if dark else QColor(0, 0, 0)
    palette.setColor(QPalette.ColorRole.Base, base)
    palette.setColor(QPalette.ColorRole.Text, text)
    return palette


def test_contrast_sanity_check():
    """The measuring stick itself: black on white is 21:1."""
    assert _contrast(QColor(0, 0, 0), QColor(255, 255, 255)) == pytest.approx(
        21.0, abs=0.1)


@pytest.mark.parametrize("dark,values,backgrounds", [
    (True, _DARK, [QColor(45, 45, 45), QColor(38, 38, 40)]),
    (False, _LIGHT, [QColor(255, 255, 255), QColor(246, 246, 249)]),
])
@pytest.mark.parametrize("tint", TINTS)
def test_every_tint_clears_aa(dark, values, backgrounds, tint):
    """Against BOTH the base and the alternating-row background — a colour
    that passes on one and fails on the other is unreadable every second
    row."""
    for background in backgrounds:
        ratio = _contrast(values[tint], background)
        assert ratio >= AA, (
            f"{'dark' if dark else 'light'} {tint} is {ratio:.2f}:1 on "
            f"{background.name()} — below AA ({AA}:1)")


@pytest.mark.parametrize("values", [_DARK, _LIGHT])
def test_protocol_hues_are_clearly_different(values):
    """EITT colours a telegram row by its connection so a block of mDNS
    steps is spottable without reading a word. Two hues that look alike
    would defeat the point."""
    coap, mdns = values["coap_row"], values["mdns_row"]
    assert coap != mdns
    # Different enough in hue, not merely in brightness — the two blocks
    # are usually adjacent and equal-luminance neighbours blur together.
    assert abs(coap.hue() - mdns.hue()) > 60


@pytest.mark.parametrize("values", [_DARK, _LIGHT])
def test_error_stands_out_against_the_protocol_hue(values):
    """A 4.xx has to be findable inside a green COAP block."""
    assert values["error"] != values["coap_row"]
    assert abs(values["error"].hue() - values["coap_row"].hue()) > 60


@pytest.mark.parametrize("values", [_DARK, _LIGHT])
def test_case_break_band_is_readable(values):
    assert _contrast(values["case_break_fg"],
                     values["case_break_bg"]) >= AA


@pytest.mark.parametrize("values", [_DARK, _LIGHT])
def test_inactive_is_distinguishable_from_default(values):
    """Deactivated rows must read as muted WITHOUT becoming unreadable —
    the whole point is telling them apart from live ones at a glance."""
    assert values["inactive"] != values["default"]
    separation = abs(_luminance(values["inactive"])
                     - _luminance(values["default"]))
    assert separation > 0.05, "inactive is too close to normal text"


def test_theme_follows_the_palette(app):
    assert grid_theme(_palette(dark=True)).dark is True
    assert grid_theme(_palette(dark=False)).dark is False


def test_dark_and_light_differ(app):
    """Same meanings, different luminance — if they were identical one of
    the two would be the unreadable one."""
    dark = grid_theme(_palette(dark=True))
    light = grid_theme(_palette(dark=False))
    for tint in TINTS:
        assert getattr(dark, tint) != getattr(light, tint)


def test_alternate_base_is_never_the_opposite_of_base(app):
    """Windows' dark palette reports AlternateBase as pure WHITE, so
    trusting it flashes every other row white behind pale text. Ours must
    stay on the same side of the fence as Base."""
    for dark in (True, False):
        palette = _palette(dark)
        base = palette.color(QPalette.ColorRole.Base)
        alternate = alternate_base(palette)
        assert abs(alternate.lightness() - base.lightness()) < 40, (
            f"alternate row {alternate.name()} is nothing like base "
            f"{base.name()}")


@pytest.mark.parametrize("kind", ["readonly", "edit"])
def test_banner_style_is_theme_specific(app, kind):
    dark = banner_style(kind, _palette(dark=True))
    light = banner_style(kind, _palette(dark=False))
    assert dark != light
    assert "background:" in dark and "color:" in dark
