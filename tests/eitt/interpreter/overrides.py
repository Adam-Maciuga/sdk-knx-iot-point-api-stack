"""
Maintainer-supplied overrides for TestCases the XML alone can't fully
automate, or that aren't trusted yet.

Two problems this solves, both independent of the XML itself so they
survive swapping to a new EITT revision:

1. Manual-interaction steps. Some EITT steps assume a human is present
   to push a button, restart the DUT, or otherwise trigger behavior the
   XML has no way to request programmatically (the only XML-native hint
   is things like Cycles="*" plus a narrative "trigger manually" comment
   — see tests/docs/eitt-xml-interpreter.md §7/§8). The DUT firmware
   exposes test-only control endpoints for exactly this
   (POST /test/trigger, /test/restart, /test/factory-reset; see
   runtime_test_server.c). An override with action="trigger" tells the
   runner to call one of those instead of waiting for a human.

   EITT's own marker for such a step is the `@@!<text>` comment command —
   "display comment in toolbar, SUSPEND EVALUATION and beep", i.e. a modal
   the operator dismisses after acting. Those are mapped by TEXT via the
   "manual_actions:" list rather than per TestCase, because one TestCase can
   contain several different prompts (5.3.1.3 has both a power cycle and a
   Local Master Reset) and because `@@!` alone does NOT imply "restart" —
   5.2's "Please check LAST UPDATE value" is a pure human observation.

2. Not-yet-reliable telegrams. A TestCase you don't trust yet (new XML
   revision, flaky step, known stack gap) can be marked action="skip"
   (excluded, with a reason) or action="xfail" (run, but a failure
   doesn't fail the suite).

3. Corrupt template data. A single attribute value in the shipped
   template can be plain wrong — a truncated fragment left behind by an
   editing accident — and the master template must not be edited to fix
   it. "message_attributes:" corrects one attribute of one message over
   the read-only template, the same way "message_activation:" corrects
   its Activate state (see MessageAttributes).

Overrides are keyed by TestCase.qualified_name (collection::suite::case)
so they keep matching after an XML regeneration that preserves test
names, and show up as a clean diff in code review — see
overrides.yaml alongside this module for the on-disk format.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

try:
    import yaml
except ImportError:  # pragma: no cover
    yaml = None  # type: ignore[assignment]

_DEFAULT_PATH = Path(__file__).resolve().parent / "overrides.yaml"

_VALID_ACTIONS = {"trigger", "skip", "xfail"}


@dataclass
class ManualAction:
    """One `@@!<text>` operator prompt mapped to a DUT control endpoint.

    `@@!` is EITT's "display comment in toolbar, SUSPEND EVALUATION and
    beep" command (`EITT_KnxCommentCommandsScheme.xml`) — a modal the
    operator dismisses after doing something physical. The TEXT is the only
    thing that says what, so each prompt has to be matched individually:
    `@@!` on its own means "a human does something", not "restart".

    `match` is a case-insensitive regex against the prompt text (with the
    `@@!` stripped). `endpoint` is None for a prompt that needs no device
    action at all — a pure human observation such as 5.2's "Please check
    LAST UPDATE value", which has no automatable equivalent and is simply
    acknowledged.
    """
    match: str
    endpoint: Optional[str] = None
    params: dict = None
    reason: str = ""

    def __post_init__(self):
        if self.params is None:
            self.params = {}


@dataclass
class Override:
    action: str                       # "trigger" | "skip" | "xfail"
    reason: str = ""
    endpoint: Optional[str] = None    # for action="trigger"
    params: dict = None               # for action="trigger" (CBOR payload)

    def __post_init__(self):
        if self.action not in _VALID_ACTIONS:
            raise ValueError(
                f"Unknown override action {self.action!r} — must be one "
                f"of {sorted(_VALID_ACTIONS)}")
        if self.action == "trigger" and not self.endpoint:
            raise ValueError("action='trigger' requires an 'endpoint'")
        if self.params is None:
            self.params = {}


@dataclass
class MessageActivation:
    """One CoAPMessage whose Activate state we flip after parsing.

    EITT templates encode a device CAPABILITY as alternative response
    branches: the same request is followed by both a success and a refusal
    response, and the operator activates whichever matches the DUT. Our
    checked-in template ships 5.10's pagination pairs with the REFUSAL
    (4.00) active, i.e. "pagination not supported" — but this device does
    support it, and the certification run's own trace shows 2.05.

    Rather than edit the read-only master template (see the project notes:
    it must not be modified), the operator's activation choice lives here.

    Keyed by `case` (TestCase.qualified_name suffix — the case NAME) plus
    the message's XML `id` (a GUID). Both survive template regeneration;
    a telegram NUMBER would not, since EITT renumbers per generated project.
    `index` disambiguates the rare duplicate-ID case (5.10.1.2's two `auth`
    alternatives literally share one GUID): it is the 0-based occurrence of
    that id within the case.
    """
    case: str
    id: str
    active: bool
    index: Optional[int] = None
    reason: str = ""


# XML attribute name -> CoAPMessage field (see model.py CoAPMessage.from_xml).
# Deliberately a closed list rather than a generic setattr: an unknown name
# would otherwise create a stray attribute on the dataclass and the override
# would silently do nothing, which is the exact failure mode this section
# exists to prevent.
_MESSAGE_ATTRIBUTE_FIELDS = {
    "AcceptedCF": "accepted_cf",
    "CF": "cf",
    "ContentFormat": "cf",
}


@dataclass
class MessageAttributes:
    """One CoAPMessage whose XML attribute values we correct after parsing.

    Unlike MessageActivation this is not an operator CHOICE — it is template
    data corruption. The shipped template carries at least one attribute whose
    value is a truncated fragment of the real one (5.5.4.1's `AcceptedCF="r"`,
    the tail of "application/cbor"), which the fail-closed Content-Format
    parsing rejects rather than silently dropping.

    The master template is read-only (see the project notes), so the
    correction lives here, keyed the same way MessageActivation is: `case`
    (TestCase NAME) plus the message's XML `id` (a GUID), both of which
    survive template regeneration, with `index` for the rare duplicate-ID
    case. `set` maps XML attribute names to their corrected values; only
    names in _MESSAGE_ATTRIBUTE_FIELDS are accepted, and an unknown one is a
    load-time ValueError so a typo cannot quietly be a no-op.
    """
    case: str
    id: str
    set: dict
    index: Optional[int] = None
    reason: str = ""


class OverrideTable:
    """Loaded overrides.yaml, keyed by TestCase.qualified_name."""

    def __init__(self, overrides: dict[str, Override]):
        self._overrides = overrides
        self._variables: dict = {}
        self._manual_actions: list[ManualAction] = []
        self._message_activation: list[MessageActivation] = []
        self._message_attributes: list[MessageAttributes] = []
        self._accepted_response_codes: dict[str, set] = {}

    @property
    def accepted_response_codes(self) -> dict:
        """Response-code substitutions the certification body accepts.

        Maps a PLANNED code ("4.01") to the set of codes that may stand in
        for it ({"4.03"}). This exists because the KSG "IoT" class agreed
        that where a test asks for "Unauthorized" a device answering
        "Forbidden" is also correct — and the reference certification runs
        record exactly that: traces/5_3.xml has five
        "Message code differs, planned: 4.01, received: 4.03" evaluations
        and traces/5_4_1.xml one "planned: 4.00, received: 4.03", and both
        runs were ACCEPTED.

        Deliberately global rather than per-TestCase: it is a property of
        the device's error-reporting, not of any one test, and EITT's own
        telegram numbering (which the certification notes use to identify
        the affected steps) does not survive template regeneration.
        """
        return self._accepted_response_codes

    @property
    def variables(self) -> dict:
        """Variable values that win over the template's Fields defaults —
        EITT's variable-values JSON / generated-copy layer. Applied by
        VariableTable.for_collection(..., overrides=...)."""
        return self._variables

    @classmethod
    def load(cls, path: Optional[Path] = None) -> "OverrideTable":
        path = path or _DEFAULT_PATH
        if not path.exists():
            return cls({})
        if yaml is None:
            raise RuntimeError(
                "PyYAML is required to load eitt/overrides.yaml — "
                "add pyyaml to requirements.txt")
        with open(path, encoding="utf-8") as f:
            raw = yaml.safe_load(f) or {}

        # "variables:" is not a TestCase key — it is the variable layer
        # EITT applies over the read-only template's Fields defaults (its
        # variable-values JSON, plus operator edits to a generated copy).
        variables = raw.pop("variables", None) or {}
        if not isinstance(variables, dict):
            raise ValueError(
                "eitt/overrides.yaml 'variables' must be a mapping of "
                "variable name -> value")

        # "manual_actions:" is not a TestCase key either — it maps `@@!`
        # operator prompts to DUT control endpoints by TEXT. Ordered: the
        # first matching pattern wins.
        manual_raw = raw.pop("manual_actions", None) or []
        if not isinstance(manual_raw, list):
            raise ValueError(
                "eitt/overrides.yaml 'manual_actions' must be a LIST of "
                "{match, endpoint, ...} mappings (order matters)")
        manual_actions = []
        for spec in manual_raw:
            if not isinstance(spec, dict) or "match" not in spec:
                raise ValueError(
                    "each eitt/overrides.yaml 'manual_actions' entry must be "
                    "a mapping with at least a 'match' key")
            try:
                re.compile(spec["match"], re.I)
            except re.error as exc:
                raise ValueError(
                    f"manual_actions 'match' {spec['match']!r} is not a valid "
                    f"regex: {exc}") from exc
            manual_actions.append(ManualAction(
                match=spec["match"],
                endpoint=spec.get("endpoint"),
                params=spec.get("params"),
                reason=spec.get("reason", ""),
            ))

        # "message_activation:" is not a TestCase key either — it is the
        # operator's per-message Activate choice, applied over the read-only
        # template (see MessageActivation).
        act_raw = raw.pop("message_activation", None) or []
        if not isinstance(act_raw, list):
            raise ValueError(
                "eitt/overrides.yaml 'message_activation' must be a LIST of "
                "{case, id, active} mappings")
        message_activation = []
        for spec in act_raw:
            if (not isinstance(spec, dict) or "case" not in spec
                    or "id" not in spec or "active" not in spec):
                raise ValueError(
                    "each eitt/overrides.yaml 'message_activation' entry must "
                    "be a mapping with 'case', 'id' and 'active' keys")
            message_activation.append(MessageActivation(
                case=spec["case"],
                id=spec["id"],
                active=bool(spec["active"]),
                index=spec.get("index"),
                reason=spec.get("reason", ""),
            ))

        # "message_attributes:" is not a TestCase key either — it corrects
        # corrupt attribute VALUES over the read-only template (see
        # MessageAttributes).
        attr_raw = raw.pop("message_attributes", None) or []
        if not isinstance(attr_raw, list):
            raise ValueError(
                "eitt/overrides.yaml 'message_attributes' must be a LIST of "
                "{case, id, set, reason} mappings")
        message_attributes = []
        for spec in attr_raw:
            if (not isinstance(spec, dict) or "case" not in spec
                    or "id" not in spec or "set" not in spec):
                raise ValueError(
                    "each eitt/overrides.yaml 'message_attributes' entry must "
                    "be a mapping with 'case', 'id' and 'set' keys")
            assignments = spec["set"]
            if not isinstance(assignments, dict) or not assignments:
                raise ValueError(
                    f"message_attributes entry case={spec['case']!r} "
                    f"id={spec['id']!r}: 'set' must be a NON-EMPTY mapping of "
                    "XML attribute name -> value")
            for attr_name, value in assignments.items():
                if attr_name not in _MESSAGE_ATTRIBUTE_FIELDS:
                    raise ValueError(
                        f"message_attributes entry case={spec['case']!r} "
                        f"id={spec['id']!r}: unknown XML attribute "
                        f"{attr_name!r} — must be one of "
                        f"{sorted(_MESSAGE_ATTRIBUTE_FIELDS)}")
                if not isinstance(value, str):
                    raise ValueError(
                        f"message_attributes entry case={spec['case']!r} "
                        f"id={spec['id']!r}: value for {attr_name!r} must be a "
                        f"string, got {type(value).__name__}")
            if not str(spec.get("reason", "")).strip():
                raise ValueError(
                    f"message_attributes entry case={spec['case']!r} "
                    f"id={spec['id']!r} needs a non-empty 'reason' — it "
                    "overrides the certified template, so why must be written "
                    "down")
            message_attributes.append(MessageAttributes(
                case=spec["case"],
                id=spec["id"],
                set=dict(assignments),
                index=spec.get("index"),
                reason=spec["reason"],
            ))

        # "accepted_response_codes:" is not a TestCase key either — see the
        # property above. Shape: {planned: [accepted, ...]}.
        codes_raw = raw.pop("accepted_response_codes", None) or {}
        if not isinstance(codes_raw, dict):
            raise ValueError(
                "eitt/overrides.yaml 'accepted_response_codes' must be a "
                "mapping of planned-code -> list of accepted codes")
        accepted_codes = {}
        for planned, accepted in codes_raw.items():
            if isinstance(accepted, str):
                accepted = [accepted]
            accepted_codes[str(planned).strip()] = {
                str(a).strip() for a in accepted}

        overrides = {}
        for qualified_name, spec in raw.items():
            if not isinstance(spec, dict) or "action" not in spec:
                raise ValueError(
                    f"eitt/overrides.yaml entry {qualified_name!r} must be "
                    "a mapping with at least an 'action' key")
            overrides[qualified_name] = Override(
                action=spec["action"],
                reason=spec.get("reason", ""),
                endpoint=spec.get("endpoint"),
                params=spec.get("params"),
            )
        table = cls(overrides)
        table._variables = {k: str(v) for k, v in variables.items()}
        table._manual_actions = manual_actions
        table._message_activation = message_activation
        table._message_attributes = message_attributes
        table._accepted_response_codes = accepted_codes
        return table

    def apply_message_activation(self, project) -> list[str]:
        """Flip CoAPMessage.activate per the 'message_activation:' entries.

        Called once, right after the template is parsed. Returns the list of
        entries that matched nothing — the caller raises on a non-empty
        result, because a stale key here would otherwise silently leave the
        template's own (wrong) branch active and the failure would surface
        far away from its cause.
        """
        by_key = self._messages_by_key(project)

        unmatched = []
        for entry in self._message_activation:
            targets, problem = self._select(by_key, entry)
            if problem:
                unmatched.append(problem)
                continue
            for msg in targets:
                msg.activate = entry.active
        return unmatched

    def apply_message_attributes(self, project) -> list[str]:
        """Correct CoAPMessage attribute values per 'message_attributes:'.

        Called once, right after apply_message_activation. Returns the list of
        entries that matched nothing, exactly as apply_message_activation does
        — the caller raises on a non-empty result, because a stale entry would
        otherwise leave the template's corrupt value in place and the failure
        would surface far away from its cause.
        """
        by_key = self._messages_by_key(project)

        unmatched = []
        for entry in self._message_attributes:
            targets, problem = self._select(by_key, entry)
            if problem:
                unmatched.append(problem)
                continue
            for msg in targets:
                for attr_name, value in entry.set.items():
                    setattr(msg, _MESSAGE_ATTRIBUTE_FIELDS[attr_name], value)
        return unmatched

    @staticmethod
    def _messages_by_key(project) -> dict:
        """Every CoAPMessage in the project, keyed by (case name, XML id).

        A list per key: 5.10.1.2's two `auth` alternatives literally share one
        GUID, which is what `index` disambiguates.
        """
        by_key: dict[tuple[str, str], list] = {}
        for _coll, _suite, tc in project.iter_test_cases():
            for item in tc.sequence:
                mid = getattr(item, "id", None)
                # Comments have an id too; only CoAPMessages have `activate`.
                if mid is None or not hasattr(item, "activate"):
                    continue
                by_key.setdefault((tc.name, mid), []).append(item)
        return by_key

    @staticmethod
    def _select(by_key: dict, entry) -> tuple[list, Optional[str]]:
        """The messages one entry addresses, or an empty list plus a string
        saying why it matched nothing — shared by both apply_* methods so a
        stale entry reads the same however it went stale."""
        targets = by_key.get((entry.case, entry.id))
        if not targets:
            return [], f"case={entry.case!r} id={entry.id!r} (no such message)"
        if entry.index is not None:
            if entry.index >= len(targets):
                return [], (
                    f"case={entry.case!r} id={entry.id!r} "
                    f"index={entry.index} (only {len(targets)} occurrence"
                    f"{'s' if len(targets) != 1 else ''})")
            targets = [targets[entry.index]]
        return targets, None

    def manual_action(self, prompt: str) -> Optional[ManualAction]:
        """The mapping for one `@@!` prompt text, or None when no pattern
        matches. A None result is a HARD ERROR at the call site, not a
        silent skip: an unrecognised operator prompt means the test needs a
        physical action nobody performed, so passing would be meaningless."""
        for entry in self._manual_actions:
            if re.search(entry.match, prompt, re.I):
                return entry
        return None

    def get(self, qualified_name: str) -> Optional[Override]:
        return self._overrides.get(qualified_name)

    def keys(self):
        """The qualified names every loaded override is keyed by — used
        to cross-check against real TestCase.qualified_name values at
        collection time (see test_eitt.py), since a typo here
        would otherwise be a silent no-op."""
        return self._overrides.keys()

    def __contains__(self, qualified_name: str) -> bool:
        return qualified_name in self._overrides

    def __len__(self) -> int:
        return len(self._overrides)
