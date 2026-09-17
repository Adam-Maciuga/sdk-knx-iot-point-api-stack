"""Tests for the EITT comment-command scheme reader.

Pure parsing — no DUT, no Docker. The behaviour worth pinning down is the
longest-prefix resolution: the command strings share prefixes (`@`, `@!`,
`@@`, `@@!`, `@@[`, `@@[w`, `@@[pahdev`), so a naive scan returns `@` for
every comment in the template.
"""

from __future__ import annotations

import os

import pytest

from eitt.comment_scheme import get_comment_scheme, parse_comment_scheme


@pytest.fixture(scope="module")
def scheme():
    return get_comment_scheme()


def test_scheme_parses(scheme):
    assert len(scheme) == 27
    assert scheme.category_name("0") == "Common"
    assert scheme.category_name("5") == "Point API"


@pytest.mark.parametrize("text,expected", [
    # The ambiguous family — each must resolve to its OWN command, not to
    # the shorter prefix that also matches.
    ("@!beep only", "@!"),
    ("@@ acknowledge", "@@"),
    ("@@!Press the button now", "@@!"),
    ("@[w\"00:00:05\"", "@[w"),
    ("@@[w\"00:01:00\"", "@@[w"),
    ("@@[pahdev\"BDUT;serialnumber=1\"", "@@[pahdev"),
    # The forms the runner implements (F7/F8).
    ("@[t narrative text", "@[t"),
    ("@#\"AUTH PREPARATION\"", "@#"),
    ("@>w\"loop;3\"", "@>w"),
])
def test_lookup_resolves_longest_prefix(scheme, text, expected):
    found = scheme.lookup(text)
    assert found is not None, f"{text!r} matched no command"
    assert found.command == expected


def test_template_only_spelling_resolves_to_its_scheme_entry(scheme):
    """The template writes `@@[MDNS"rqcheck=..."` (16 uses) where the
    scheme documents `@@[pahmdns`. Without the alias the prefix search
    falls back to bare `@@` and the properties pane would describe an mDNS
    request-check toggle as a modal operator prompt.

    ProvisioningEngine already treats the two as one command
    (`_apply_pahmdns = _apply_mdns`); this keeps DISPLAY in step with
    EXECUTION.
    """
    found = scheme.lookup('@@[MDNS"rqcheck=off"')
    assert found is not None
    assert found.command == "@@[pahmdns"
    assert found is scheme.lookup('@@[pahmdns"rqcheck=off"')


def test_alias_does_not_shadow_other_double_at_commands(scheme):
    """An alias is checked before the length-ordered scan, so verify it did
    not capture its own prefix family."""
    assert scheme.lookup("@@ acknowledge").command == "@@"
    assert scheme.lookup("@@!prompt").command == "@@!"
    assert scheme.lookup('@@[pahctx"a"').command == "@@[pahctx"


def test_plain_prose_is_not_a_command(scheme):
    assert scheme.lookup("Preparation: set the device to loading state") is None
    assert scheme.lookup("") is None


def test_commands_carry_description_and_format(scheme):
    """Every command the properties pane will display needs at least a
    format string to show — that pane is the whole reason this file is
    parsed rather than hard-coded."""
    for cmd in scheme:
        assert cmd.command, "a command with no Command attribute"
        assert cmd.format, f"{cmd.command} has no Format"


def test_modal_prompt_is_documented_as_suspending(scheme):
    """`@@` and `@@!` are the operator-prompt forms the runner routes
    through overrides.yaml (finding F7). Confirm the scheme really does
    describe both as suspending evaluation — that claim is what justifies
    treating a bare `@@` as a manual prompt rather than a no-op."""
    for command in ("@@", "@@!"):
        found = scheme.lookup(command + "x")
        assert found is not None
        assert "suspend" in found.description.lower()


def test_explicit_path_is_accepted():
    # Relative to THIS file — pytest may be invoked from the repo root.
    scheme = parse_comment_scheme(os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "data", "EITT_KnxCommentCommandsScheme.xml"))
    assert len(scheme) == 27
