"""Tests for the Telegram Properties pane.

Presentation, but the kind worth pinning: the pane is how you read a
telegram's security knobs, and a field that is greyed when it should be
live (or vice versa) actively misleads. Rules are taken from EITT's own
screens, not invented.
"""

from __future__ import annotations

import os

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

try:
    from PySide6.QtWidgets import QApplication
except ImportError as exc:
    pytest.skip(f"GUI extra not usable: {exc}", allow_module_level=True)

from eitt.model import CoAPMessage, Comment, parse_eitt_xml  # noqa: E402
from eitt.project_copy import cases_in                       # noqa: E402
from gui.main_window import PropertiesPane, _unescape        # noqa: E402
from gui.theme import grid_theme                             # noqa: E402

TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "data", "KnxConformanceTestTemplate-PointAPI.xml")

# Greyed on a response, live on a request: these ask the TOOL to do
# something while SENDING, so they mean nothing on a row that only
# describes what the DUT should send back. Verified against EITT.
SEND_SIDE = (
    "Accepted Format",
    "Invalid Auth Tag [hex]",
    "Critical Option Number [dec]",
    "Critical Option Value [hex]",
    "Wait for Response Timeout [sec]",
    "Use Random Kid Context",
    "Use Last RX Token",
)


@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])


@pytest.fixture(scope="module")
def project():
    return parse_eitt_xml(TEMPLATE)


def _rows(pane):
    """{label: (value, greyed)} across every group."""
    out = {}
    for i in range(pane.tree.topLevelItemCount()):
        group = pane.tree.topLevelItem(i)
        for j in range(group.childCount()):
            row = group.child(j)
            # Compare against the THEME rather than a literal: the greyed
            # tone differs between light and dark, and hardcoding one made
            # these tests pass only on whichever theme was current.
            greyed = row.foreground(0).color() == grid_theme().inactive
            out[row.text(0)] = (row.text(1), greyed)
    return out


def _groups(pane):
    return [pane.tree.topLevelItem(i).text(0)
            for i in range(pane.tree.topLevelItemCount())]


def _find(project, predicate):
    for _c, _s, case in project.iter_test_cases():
        for item in case.sequence:
            if predicate(item):
                return item
    pytest.skip("no matching sequence item in the template")


def test_send_side_fields_are_live_on_a_request(app, project):
    pane = PropertiesPane()
    request = _find(project, lambda i: isinstance(i, CoAPMessage)
                    and i.is_request)
    pane.show_item(request)

    rows = _rows(pane)
    for label in SEND_SIDE:
        assert label in rows, f"{label} missing"
        assert not rows[label][1], f"{label} must be live on a request"
    # ...and the receive-side one is greyed, for the mirror reason.
    assert rows["Expected Kid Context"][1]


def test_send_side_fields_are_greyed_on_a_response(app, project):
    pane = PropertiesPane()
    response = _find(project, lambda i: isinstance(i, CoAPMessage)
                     and i.is_response)
    pane.show_item(response)

    rows = _rows(pane)
    for label in SEND_SIDE:
        assert rows[label][1], f"{label} must be greyed on a response"
    assert not rows["Expected Kid Context"][1]


def test_inapplicable_fields_are_greyed_not_hidden(app, project):
    """EITT keeps them in the list so the field set is stable and you learn
    where to look."""
    pane = PropertiesPane()
    pane.show_item(_find(project, lambda i: isinstance(i, CoAPMessage)))

    rows = _rows(pane)
    for label in ("Source Address", "Group Address", "Service Type"):
        assert label in rows
        assert rows[label][1]


def test_response_code_carries_its_name(app, project):
    """EITT writes `2.05 [Content]`, not a bare `2.05`."""
    pane = PropertiesPane()
    response = _find(project, lambda i: isinstance(i, CoAPMessage)
                     and i.code == "2.05")
    pane.show_item(response)
    assert _rows(pane)["Method"][0] == "2.05 [Content]"


def test_request_shows_destination_response_shows_source(app, project):
    pane = PropertiesPane()

    pane.show_item(_find(project, lambda i: isinstance(i, CoAPMessage)
                         and i.is_request))
    assert "Destination" in _rows(pane)

    pane.show_item(_find(project, lambda i: isinstance(i, CoAPMessage)
                         and i.is_response))
    assert "Source" in _rows(pane)


def test_footer_shows_the_resolved_telegram(app, project):
    pane = PropertiesPane()
    collection = next(c for c in project.collections
                      if c.name.startswith("5.10"))
    response = next(item for case in cases_in(collection)
                    for item in case.sequence
                    if isinstance(item, CoAPMessage) and item.is_response
                    and item.content)
    pane.show_item(response)

    footer = pane.footer.toPlainText()
    assert footer.startswith("ACK: 2.05 [Content]")
    assert "[Data:]" in footer
    assert "\\/" not in footer          # slashes unescaped for display


def test_comment_pane_is_small_and_command_driven(app, project):
    """A comment carries no protocol, so EITT shows four rows, not the
    telegram treatment."""
    pane = PropertiesPane()
    comment = _find(project, lambda i: isinstance(i, Comment)
                    and i.text.strip().startswith("@[t"))
    pane.show_item(comment)

    assert _groups(pane) == ["Comment"]
    rows = _rows(pane)
    assert set(rows) >= {"Command", "Description", "Format", "Text"}
    assert rows["Command"][0] == "@[t"


def test_plain_prose_comment_says_so(app, project):
    pane = PropertiesPane()
    prose = _find(project, lambda i: isinstance(i, Comment)
                  and i.text.strip() and not i.text.strip().startswith("@"))
    pane.show_item(prose)
    assert "not an @-command" in _rows(pane)["Description"][0]


def test_missing_telegram_shows_its_evaluation_result(app, project):
    """A telegram that never arrived — or could not be sent — carries its
    error assessment in Evaluation Result. That is where an operator looks
    for a verdict, and for a missing telegram it is the only diagnostic
    there is, so it must come FIRST rather than be scrolled to."""
    from gui.trace import LineKind, TraceLine

    expected = _find(project, lambda i: isinstance(i, CoAPMessage)
                     and i.is_response)
    line = TraceLine(kind=LineKind.MISSING, number=7, ok=False,
                     text="!  NOT RECEIVED: expected OUT 2.05 /p/1",
                     expected=expected,
                     reason="no response received within 5.0s")

    pane = PropertiesPane()
    pane.show_missing(line)

    assert _groups(pane)[0] == "Evaluation Result"
    rows = _rows(pane)
    assert "FAILED" in rows["Result"][0]
    assert rows["Assessment"][0] == "no response received within 5.0s"
    # ...and what SHOULD have happened is still shown below it.
    assert "Endpoint" in rows
    assert "NOT RECEIVED" in pane.footer.toPlainText()


def test_missing_without_a_template_object_still_says_what_was_expected(app):
    """The sink carries no reference back to the template, so a missing row
    built from a StepEvent has only its summary — which must still be
    shown rather than leaving the pane blank."""
    from gui.trace import LineKind, TraceLine

    line = TraceLine(kind=LineKind.MISSING, number=3, ok=False,
                     text="!  NOT RECEIVED: expected OUT 2.05 /p/1",
                     reason="timed out")
    pane = PropertiesPane()
    pane.show_missing(line)

    rows = _rows(pane)
    assert rows["Assessment"][0] == "timed out"
    assert rows["Step"][0] == "OUT 2.05 /p/1"


def test_show_none_clears_both_halves(app, project):
    pane = PropertiesPane()
    pane.show_item(_find(project, lambda i: isinstance(i, CoAPMessage)))
    assert pane.tree.topLevelItemCount() > 0

    pane.show_item(None)
    assert pane.tree.topLevelItemCount() == 0
    assert pane.footer.toPlainText() == ""


@pytest.mark.parametrize("raw,expected", [
    (r"<\/fp\/g\/1>;ct=60", "</fp/g/1>;ct=60"),
    ("", ""),
    ("no slashes here", "no slashes here"),
])
def test_unescape(raw, expected):
    assert _unescape(raw) == expected
