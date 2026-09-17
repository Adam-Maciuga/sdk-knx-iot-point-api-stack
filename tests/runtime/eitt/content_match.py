"""
The <Content> matching mini-language.

Per tests/docs/eitt-xml-interpreter.md §4, an expected-response <Content>
block is interpreted line-by-line (lines separated by literal '\\n', i.e.
the XML's &#10;) against the response body:

  *                     wildcard — accept any body without checking it.
  (well-formed JSON)    literal structural match against the decoded body.
  /path: value          check one field of the decoded (CBOR/JSON) body;
                         `value` may be a literal, a #VAR, or a regex, and
                         `a|b` is alternation ("either literal is ok").
  all: <regex>          every comma-split link-format entry in the body
                         must match this regex (universality).
  opt: <regex>          if any entry matches, fine; if none do, also fine
                         (optional existence — manufacturer-optional
                         resources).
  <bare regex>          at least one entry must match (mandatory
                         existence) — the default shape for repeated
                         resource-presence checks in a .well-known/core
                         response.
  (empty / <Content/>)  no body expected — used for bodyless
                         GET/DELETE requests and 2.02/4.00-style
                         responses with no payload.

Link-format bodies are split the same way conftest.py's
parse_link_format() treats them: comma-separated entries, each looking
like '<href>;attr="val";...'. This module does the split/match; it does
not decode CBOR/JSON on its own — callers pass in the already-decoded
Python value (dict/list) for '/path:' checks, or the raw text for
link-format existence checks.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from typing import Any, Optional

from .variables import VariableTable


@dataclass
class MatchResult:
    ok: bool
    reason: str = ""

    def __bool__(self) -> bool:
        return self.ok


def _split_link_format_entries(text: str) -> list[str]:
    """Split a CoRE Link Format (RFC 6690) body into its comma-separated
    entries, the same way conftest.parse_link_format does."""
    return [e.strip() for e in text.split(",") if e.strip()]


def _match_line_against_entries(pattern: str, entries: list[str],
                                mode: str) -> MatchResult:
    """mode is 'all' (universality), 'opt' (optional existence), or
    'exists' (mandatory existence)."""
    try:
        rx = re.compile(pattern)
    except re.error as e:
        return MatchResult(False, f"invalid regex {pattern!r}: {e}")

    matches = [e for e in entries if rx.search(e)]

    if mode == "all":
        # An 'all:' rule over an EMPTY entry list is vacuously true, and a
        # device answering 2.05 with an empty body would therefore satisfy
        # every link-format assertion in the template. EITT's 'all:' means
        # "the response is a list of links and every one of them looks like
        # this" — a response with no links at all does not satisfy that, it
        # fails to be the thing being described. So require at least one.
        if not entries:
            return MatchResult(
                False,
                f"'all:' rule {pattern!r} — response contained no "
                f"link-format entries at all (empty body)")
        non_matching = [e for e in entries if not rx.search(e)]
        if non_matching:
            return MatchResult(
                False,
                f"'all:' rule {pattern!r} — entries not matching: "
                f"{non_matching}")
        return MatchResult(True)

    if mode == "opt":
        # 'opt:' marks a manufacturer-OPTIONAL resource: absent is fine, and
        # so (today) is present-but-malformed. See finding F4 in
        # tests/docs/eitt-interpreter-findings.md — tightening this to
        # "if present it must match" needs the resource's IDENTITY, and the
        # only identity available is the pattern itself. The template's 35
        # active 'opt:' patterns embed the link target behind arbitrary
        # lookaheads (e.g. '^(?=.*;ct=40)<.*\\/auth>.*'), so extracting it
        # means heuristically parsing a regex — fragile enough to risk
        # inventing failures the template never asked for. Left permissive
        # ON PURPOSE until there is a non-guessing way to identify the
        # target; documented rather than silently weak.
        return MatchResult(True)

    # mandatory existence
    if not matches:
        return MatchResult(
            False, f"no response entry matched required pattern {pattern!r}")
    return MatchResult(True)


def match_link_format_content(expected: str, response_text: str,
                              variables: Optional[VariableTable] = None
                              ) -> MatchResult:
    """Evaluate a link-format <Content> block (all:/opt:/bare-regex lines)
    against a decoded application/link-format response body."""
    entries = _split_link_format_entries(response_text)
    lines = expected.split("\n")

    i = 0
    while i < len(lines):
        raw_line = lines[i]
        line = raw_line.strip()
        i += 1
        if not line:
            continue

        # A bare '.*' continuation line right after an 'all:' line extends
        # the same block rather than being its own separate rule — see
        # e.g. "all: .*rt=\"...\".*\n.*" (spec §4 item 5). We simply treat
        # a lone '.*' continuation as always-true and skip it.
        if line == ".*":
            continue

        if variables is not None:
            line = variables.substitute(line)

        if line.startswith("all:"):
            pattern = line[len("all:"):].strip()
            result = _match_line_against_entries(pattern, entries, "all")
        elif line.startswith("opt:"):
            pattern = line[len("opt:"):].strip()
            result = _match_line_against_entries(pattern, entries, "opt")
        else:
            result = _match_line_against_entries(line, entries, "exists")

        if not result.ok:
            return result

    return MatchResult(True)


def _get_json_path(value: Any, path: str) -> tuple[bool, Any]:
    """Resolve a simple '/a/b/c' pointer-like path against a decoded
    dict/list. Returns (found, value_at_path)."""
    cur = value
    for part in [p for p in path.split("/") if p]:
        if isinstance(cur, dict):
            if part in cur:
                cur = cur[part]
                continue
            return False, None
        if isinstance(cur, list):
            try:
                idx = int(part)
            except ValueError:
                return False, None
            if 0 <= idx < len(cur):
                cur = cur[idx]
                continue
            return False, None
        return False, None
    return True, cur


_HEX_LITERAL_RE = re.compile(r"^h'([0-9A-Fa-f](?:[0-9A-Fa-f ])*)'$")


def _normalize_hex_literal(s: str) -> Optional[str]:
    """If `s` is an XML "h'<hex>'" byte-string literal made up only of hex
    digits and spaces (e.g. "h'54 6D 70 54 6F 6B'" — the space-separated,
    uppercase form a Field with Format="Hex" expands to, vs. the compact
    lowercase form bytes.hex() produces), return its hex digits lowercased
    with whitespace stripped so both spellings compare equal. Returns None
    if `s` isn't a pure-hex literal (e.g. it embeds a regex like
    "h'[0-9,a-f]{32}'"), so callers fall back to their other comparison
    strategies for those."""
    m = _HEX_LITERAL_RE.match(s.strip())
    if m is None:
        return None
    return m.group(1).replace(" ", "").lower()


def _value_matches(expected: str, actual: Any) -> MatchResult:
    """Match one '/path: value' rule's RHS against the actual field value.

    Supports literal equality (after str() coercion), 'a|b' alternation,
    and inline regex (tried when the literal/alternation forms fail).

    A byte-string actual value (e.g. an AT entry's cnf/osc id or ms) is
    rendered in the XML's own "h'<hex>'" convention before comparison —
    the expected side commonly embeds a regex INSIDE that convention,
    e.g. "h'[0-9,a-f]{32}'", to match a value of a given length without
    pinning the exact bytes. A Field with Format="Hex" instead expands to
    a space-separated, uppercase hex literal (e.g. "h'54 6D 70 54 6F 6B'"),
    so pure-hex literals on both sides are additionally compared digit by
    digit, ignoring case and whitespace, rather than only as exact strings.
    """
    if isinstance(actual, bool):
        # JSON/CBOR booleans decode to Python True/False; the XML writes
        # them lowercase ("/value: true"). Compare in the XML's spelling
        # so an expected "true"/"false" matches, not Python's "True".
        actual_str = "true" if actual else "false"
    elif isinstance(actual, bytes):
        actual_str = f"h'{actual.hex()}'"
    elif isinstance(actual, str):
        actual_str = actual
    else:
        try:
            actual_str = json.dumps(actual)
        except TypeError:
            actual_str = str(actual)

    actual_hex = _normalize_hex_literal(actual_str)

    alternatives = expected.split("|") if "|" in expected else [expected]
    for alt in alternatives:
        alt = alt.strip()
        if alt == str(actual):
            return MatchResult(True)
        if alt == actual_str:
            return MatchResult(True)
        if actual_hex is not None:
            alt_hex = _normalize_hex_literal(alt)
            if alt_hex is not None and alt_hex == actual_hex:
                return MatchResult(True)

    # Fall back to regex match (covers e.g. '\d*[1-9]\d*', '^[1-9][0-9]*$',
    # and the "h'<regex>'" byte-string-with-embedded-regex convention).
    #
    # FULLMATCH, not search: every expected value in this template describes
    # the WHOLE field, so an unanchored search turns each one into a substring
    # test — '/value: 1' would accept 21, '/value: 0' would accept 304, 'true'
    # would accept 'untrue', and '2.05' would accept '12.056' (the '.' is a
    # wildcard too). Checked against all 105 distinct expected values in the
    # template: every genuine regex among them ('[0-9]{1,5}', '\d*[1-9]\d*',
    # "h'[0-9a-fA-F]{32}'", the '^\[(?:,\s*|(?:1)|...)*\]' array forms) is
    # self-contained and still matches under fullmatch. See finding F3 in
    # tests/docs/eitt-interpreter-findings.md.
    try:
        rx = re.compile(expected)
        if rx.fullmatch(actual_str) or rx.fullmatch(str(actual)):
            return MatchResult(True)
    except re.error:
        pass

    return MatchResult(
        False, f"field value {actual!r} does not match expected {expected!r}")


def _structural_match(expected: Any, actual: Any) -> bool:
    """Structural equality for literal-JSON <Content> blocks, except
    every string leaf on the expected side is matched via
    _value_matches() rather than plain '==' — so a Field default like
    "Windows|Linux" substituted into a literal '{"value": "#FIELD"}'
    block accepts either value, exactly like a '/path: value' rule
    would, instead of only ever matching one exact string."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict) or expected.keys() != actual.keys():
            return False
        return all(_structural_match(v, actual[k]) for k, v in expected.items())
    if isinstance(expected, list):
        if not isinstance(actual, list) or len(expected) != len(actual):
            return False
        return all(_structural_match(e, a) for e, a in zip(expected, actual))
    if isinstance(expected, str):
        return bool(_value_matches(expected, actual))
    return expected == actual


def match_field_content(expected: str, decoded_body: Any,
                        variables: Optional[VariableTable] = None
                        ) -> MatchResult:
    """Evaluate a '/path: value' (one or more lines) <Content> block
    against an already-decoded (dict/list) response body."""
    for raw_line in expected.split("\n"):
        line = raw_line.strip()
        if not line:
            continue
        if variables is not None:
            line = variables.substitute(line)
        if not line.startswith("/"):
            # A bare (non-path-prefixed) line inside an otherwise
            # path-prefixed block is treated as a whole-body regex.
            try:
                rx = re.compile(line)
            except re.error:
                return MatchResult(False, f"invalid regex {line!r}")
            body_str = (decoded_body if isinstance(decoded_body, str)
                       else json.dumps(decoded_body))
            if not rx.search(body_str):
                return MatchResult(False, f"body does not match {line!r}")
            continue

        path, _, rhs = line[1:].partition(":")
        rhs = rhs.strip()
        found, actual = _get_json_path(decoded_body, path)
        if not found:
            return MatchResult(False, f"field '/{path}' not present in response")
        result = _value_matches(rhs, actual)
        if not result.ok:
            return MatchResult(False, f"/{path}: {result.reason}")

    return MatchResult(True)


def looks_like_link_format(expected: str) -> bool:
    """True when the <Content> text uses the link-format DSL (all:/opt:
    prefixes, or link-entry-shaped bare regex lines like '<...>...')."""
    for raw_line in expected.split("\n"):
        line = raw_line.strip()
        if not line or line == ".*":
            continue
        if line.startswith(("all:", "opt:")):
            return True
        if line.startswith("<") or "<.*" in line or line.startswith("^"):
            return True
    return False


def looks_like_field_content(expected: str) -> bool:
    """True when the <Content> text uses the '/path: value' DSL."""
    return any(line.strip().startswith("/")
               for line in expected.split("\n") if line.strip())


def match_content(expected: Optional[str], response_payload: bytes,
                  decoded_body: Any = None,
                  variables: Optional[VariableTable] = None) -> MatchResult:
    """Top-level entry point: dispatch to the right matching strategy for
    an expected <Content> block.

    Args:
        expected: the raw <Content> text from the XML (already stripped
            of the outer element — None/"" means an empty/self-closing
            <Content/>, i.e. "no body expected").
        response_payload: the raw response bytes (used for text/wildcard
            and link-format matching).
        decoded_body: the response payload already decoded to a Python
            value (dict/list/str) by the caller, when applicable — used
            for '/path: value' and literal-JSON matching. May be None if
            decoding failed or wasn't attempted; field/JSON matching
            against None will simply fail with a clear reason.
        variables: the active VariableTable, for #VAR substitution inside
            the expected text.
    """
    if expected is None or expected == "":
        if response_payload:
            return MatchResult(
                False,
                f"expected empty body, got {len(response_payload)} byte(s)")
        return MatchResult(True)

    text = variables.substitute(expected) if variables else expected

    # A bare wildcard — either EITT's own literal "*" or a lone ".*"
    # regex (e.g. 5.3.20.1's expected <Content>.*</Content>, meaning
    # "accept any link-format body without checking it") — accepts any
    # body without further checks. ".*" only counts as a *whole-body*
    # wildcard here; the same token appearing as a continuation line
    # inside a larger 'all:'-prefixed link-format block is handled
    # separately by match_link_format_content, which needs to keep
    # matching the OTHER lines in that block.
    if text.strip() in ("*", ".*"):
        return MatchResult(True)

    if looks_like_field_content(text):
        if decoded_body is None:
            return MatchResult(
                False, "expected '/path: value' content but response body "
                "could not be decoded")
        return match_field_content(expected, decoded_body, variables)

    if looks_like_link_format(text):
        response_text = response_payload.decode("utf-8", errors="replace")
        return match_link_format_content(expected, response_text, variables)

    # Literal match: try structural JSON-equality first (order-insensitive
    # for dict keys, and every string leaf may use the same 'a|b'
    # alternation / regex convention '/path: value' checks support — a
    # Field default like "Windows|Linux" substituted into a literal
    # '{"value": "#FIELD"}' block must accept either, not just an exact
    # string), fall back to exact string comparison.
    try:
        expected_json = json.loads(text)
        if decoded_body is not None and _structural_match(expected_json, decoded_body):
            return MatchResult(True)
        actual_text = response_payload.decode("utf-8", errors="replace")
        try:
            actual_json = json.loads(actual_text)
            if _structural_match(expected_json, actual_json):
                return MatchResult(True)
        except (ValueError, TypeError):
            pass
        return MatchResult(
            False, f"literal JSON mismatch: expected {expected_json!r}, "
            f"got body {decoded_body!r}")
    except (ValueError, TypeError):
        pass

    actual_text = response_payload.decode("utf-8", errors="replace")
    if text.strip() == actual_text.strip():
        return MatchResult(True)
    return MatchResult(
        False, f"literal text mismatch: expected {text!r}, got {actual_text!r}")
