"""
The "@@[pahxxx" comment-command engine.

Per tests/docs/eitt-xml-interpreter.md §3, EITT embeds tool commands in
<Comment Text="..."> using a small DSL:

    @@[pahdev"Alias;key=value;..."       device (endpoint) provisioning
    @@[pahmcast"Alias;key=value;..."     multicast group provisioning
    @@[pahctx"Alias;key=value;..."       OSCORE security-context building
    @@[pahsnc"on"/"off"                  sequence-number-check toggle
    @@[pahdiscover                       trigger a discovery cycle
    @@[MDNS"rqcheck=on"/"off"            mDNS request-check strictness
    @@[w"#FIELD"                        named wait (distinct from
                                          TimeToNext)

Other embedded-mini-language comment forms handled elsewhere:
    @#"TestCaseName"                     inline test-case reference —
                                          see runner.py (preparation
                                          sequences AUTH/IA/TABLE).
    @>w"TestCaseName;unlimited"          repeatable-subsequence
                                          invocation — see runner.py
                                          (SWU block-upload loop).

`pahctx` in particular is built incrementally: a context alias
accumulates fields (senderid, recipientid, mastersecret, contextid,
salt, remove) across several successive comments before enough material
exists to derive an OscoreContext. This module tracks that accumulation
and exposes the materialized OscoreContext once senderid/recipientid +
mastersecret are known (recipientid defaults to b"" when never set,
matching the unicast-context shape used throughout the suite).

NOT the same thing as the XML attribute `Activate="yes"/"no"` (3614 uses,
the per-step enable/disable flag handled by `model.py`/`runner.py`). The
`pahctx` PARAMETER `activate=` is a different namespace with 1 use.

`activate=` / `updatesession=` / `destination=` are deliberately IGNORED,
and that is correct rather than a gap. Per EITT's own grammar
(`EITT_KnxCommentCommandsScheme.xml` Pos=10..12) they are EITT's
device<->context ASSOCIATION commands — "activates/updates security
context for given destination device" — carrying no crypto material.
All 3 uses are in 5.4.1.3/.4, and none needs an implementation here:

- Our runner resolves a context PER STEP from each telegram's own
  `SecContext="..."` attribute, so it never needs a device binding;
  EITT needs one because it keeps per-device session state.
- `updatesession=` terminates an accumulation block, and the
  `mastersecret=` line just before it has already materialized a fresh
  OscoreContext with `ssn = 0` — exactly the "session refresh" EITT
  means. 5.4.1.3 proves it: the very next step is commented "read
  request 1st time (echo chanllange)", i.e. a fresh session is expected.
  (5.4.1.4's `updatesession` has 0 steps after it at all.)
- 5.4.1.4's `activate=` switches between two aliases that are mirror
  views of ONE OSCORE pair: `MessagingTestUnicast01` gets
  `senderid=102a3b4c5d6e7f`, `...02` gets the same value as
  `recipientid`, sharing a mastersecret — so `01.sender_key ==
  02.recipient_key` and vice versa, with a shared common_iv. In the
  recorded run (`data/traces/5_4_1.xml`) the comment produced NO telegram
  and EITT relabelled the DUT's response from the scripted `...02` to
  `...01` (`KeyID=10 2A 3B 4C 5D 6E 7F`) — the KEY identifies the
  context, not the alias. Decryption works here without the binding.

`txseq=`/`rxseq=` (Pos=7/8, "change TX/RX sequence number") are likewise
unimplemented but appear NOWHERE in the Point-API template.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Callable, Optional

from .variables import VariableTable

try:
    from transport.knx_oscore import OscoreContext
except ImportError:  # pragma: no cover - exercised only outside the test tree
    OscoreContext = None  # type: ignore[assignment,misc]


def _hex_or_ascii(value: Optional[str], default: bytes = b"") -> Optional[bytes]:
    """Decode a pahctx senderid/recipientid/contextid/salt value.

    These appear in the XML in two shapes: pure even-length hex
    (e.g. "102a3b4c5d6e7f" for group/multicast contexts) and short
    literal ASCII identifiers (e.g. "TmpTok", "testcfg", "AccCtrl" for
    unicast/PASE-derived contexts) — matching fixtures.py's own
    convention of literal ASCII sender ids (TEST_SENDER_ID = b"RtTest").
    A string only decodes as hex when every character is a hex digit
    AND the length is even; anything else (odd length, or containing a
    non-hex letter like 'g'-'z') is treated as literal ASCII bytes.
    """
    if value is None:
        return default
    is_hex_shaped = (len(value) % 2 == 0
                     and all(c in "0123456789abcdefABCDEF" for c in value))
    if is_hex_shaped:
        return bytes.fromhex(value)
    return value.encode("utf-8")


_COMMAND_RE = re.compile(r'^@@\[([A-Za-z]+)(?:"([^"]*)")?')

# Commands whose quoted argument is a single bare value with no leading
# "Alias;" component and no 'key=value' shape either — as opposed to
# pahdev/pahmcast/pahctx ("Alias;key=value;...") and MDNS ("key=value"
# with no alias). Anything not listed here is assumed to use the aliased
# "Alias;key=value;..." form.
_BARE_VALUE_COMMANDS = {"pahsnc", "w"}

# Commands whose quoted argument is 'key=value;...' with NO leading
# alias component at all (unlike pahdev/pahmcast/pahctx).
_NO_ALIAS_COMMANDS = {"mdns"}


@dataclass
class ParsedCommand:
    command: str                     # "pahdev" | "pahmcast" | "pahctx" | ...
    params: dict[str, str] = field(default_factory=dict)
    alias: Optional[str] = None      # the leading "Alias" before ';key=val'


def parse_comment_command(text: str) -> Optional[ParsedCommand]:
    """Parse a Comment/@Text into a ParsedCommand, or None if it isn't a
    recognized "@@[..." tool command (plain "@[t..." narrative comments,
    "@#"..."" test-case references, and "@>w"..."" loop invocations are
    NOT handled here — see runner.py)."""
    m = _COMMAND_RE.match(text)
    if not m:
        return None
    command, arg_string = m.group(1), m.group(2)
    if arg_string is None:
        return ParsedCommand(command=command)

    if command in _BARE_VALUE_COMMANDS:
        # e.g. 'pahsnc"on"' / 'w"#SWU_WAIT_AFTER_DOWNLOAD"' — the whole
        # quoted string is a single value, never "Alias;key=value".
        return ParsedCommand(command=command, params={"_value": arg_string})

    if command.lower() in _NO_ALIAS_COMMANDS:
        # e.g. 'MDNS"rqcheck=off"' — 'key=value' with no leading alias.
        params = {}
        for part in arg_string.split(";"):
            if "=" in part:
                k, _, v = part.partition("=")
                params[k.strip()] = v.strip()
        return ParsedCommand(command=command, params=params, alias=None)

    parts = arg_string.split(";")
    alias = parts[0].strip() if parts else None
    params: dict[str, str] = {}
    for part in parts[1:]:
        if "=" in part:
            k, _, v = part.partition("=")
            params[k.strip()] = v.strip()
        elif part.strip():
            # Rare bare (non key=value) continuation part with no '=' —
            # keep it visible under a positional key rather than
            # silently dropping it.
            params.setdefault("_extra", part.strip())
    return ParsedCommand(command=command, params=params, alias=alias)


@dataclass
class DeviceEndpoint:
    """A "@@[pahdev"-provisioned logical device alias."""
    name: str
    serial_number: Optional[str] = None
    password: Optional[str] = None
    ip_address: Optional[str] = None  # "[ipv6]:port" or "invalid"


@dataclass
class MulticastGroup:
    """A "@@[pahmcast"-provisioned multicast group/well-known scope alias.

    Per EITT's own command grammar (tests/eitt/data/
    EITT_KnxCommentCommandsScheme.xml), pahmcast accepts `context=`
    (RX context), and also the explicit `rxcontext=` / `txcontext=`
    spellings, plus `join=<true/false>` ("Joins or leaves multicast
    group") and `remove=true`.
    """
    name: str
    installation_id: Optional[str] = None
    group_id: Optional[str] = None
    scope: Optional[str] = None
    context_alias: Optional[str] = None      # from `context=` / `rxcontext=`
    tx_context_alias: Optional[str] = None   # from `txcontext=`
    joined: bool = True


@dataclass
class ContextBuilder:
    """Accumulates "@@[pahctx" fields for one alias until an OscoreContext
    can be materialized."""
    name: str
    sender_id: Optional[str] = None
    recipient_id: Optional[str] = None
    master_secret: Optional[str] = None
    context_id: Optional[str] = None
    salt: Optional[str] = None
    removed: bool = False

    def apply(self, params: dict[str, str]) -> None:
        if params.get("remove", "").lower() == "true":
            self.sender_id = None
            self.recipient_id = None
            self.master_secret = None
            self.context_id = None
            self.salt = None
            self.removed = True
            return
        self.removed = False
        if "senderid" in params:
            self.sender_id = params["senderid"]
        if "recipientid" in params:
            self.recipient_id = params["recipientid"]
        if "mastersecret" in params:
            self.master_secret = params["mastersecret"]
        if "contextid" in params:
            self.context_id = params["contextid"]
        if "salt" in params:
            self.salt = params["salt"]

    def is_ready(self, spake_secret: Optional[bytes] = None) -> bool:
        return not self.removed and (
            self.master_secret is not None or spake_secret is not None)

    def build(self, spake_secret: Optional[bytes] = None):
        """Materialize an OscoreContext from the accumulated fields.

        sender_id defaults to b"" (empty) when never set — this is the
        PASE/server-role shape (SecContext="" contexts derived purely
        from a SPAKE2+ result use the tool-supplied sender_id set
        separately by the runner, not through pahctx params).

        When this context never received an explicit "mastersecret="
        param (e.g. the real XML's '@@[pahctx"Temporary Key;
        senderid=TmpTok"' — no mastersecret at all), `spake_secret` is
        used instead: the shared key the immediately preceding SPAKE2+
        exchange derived for this same sender_id (see runner.py's
        _run_spake_span / ProvisioningEngine.pending_spake_result).
        """
        if OscoreContext is None:
            raise RuntimeError(
                "knx_oscore module not importable — run with "
                "tests/common on sys.path")
        sender_id = _hex_or_ascii(self.sender_id)
        recipient_id = _hex_or_ascii(self.recipient_id)
        if self.master_secret is not None:
            master_secret = bytes.fromhex(self.master_secret)
        elif spake_secret is not None:
            master_secret = spake_secret
        else:
            raise ValueError(
                f"pahctx '{self.name}' has no mastersecret and no "
                "preceding SPAKE2+ result to derive one from")
        id_context = _hex_or_ascii(self.context_id, default=None)
        master_salt = _hex_or_ascii(self.salt)
        return OscoreContext(
            master_secret=master_secret,
            sender_id=sender_id,
            recipient_id=recipient_id,
            id_context=id_context,
            master_salt=master_salt,
        )


class ProvisioningEngine:
    """Tracks the side effects of "@@[pah..." comment commands for one
    TestCase run: device endpoints, multicast groups, OSCORE contexts,
    and the sequence-number-check / mDNS-strictness toggles.

    The runner calls `apply(text)` for every Comment it encounters and
    then resolves an IP="Alias"/SecContext="Alias" attribute via
    `resolve_device`/`resolve_context` when building/matching a
    CoAPMessage.
    """

    def __init__(self, variables: VariableTable,
                on_discover: Optional[Callable[[], None]] = None):
        self.variables = variables
        self.on_discover = on_discover
        self.devices: dict[str, DeviceEndpoint] = {}
        self.multicast_groups: dict[str, MulticastGroup] = {}
        self._context_builders: dict[str, ContextBuilder] = {}
        self.contexts: dict[str, object] = {}  # alias -> OscoreContext
        self.seq_no_check_enabled = True
        self.mdns_request_check_enabled = True
        # Set by runner.py's _run_spake_span right after a SPAKE2+
        # exchange completes; consumed by the NEXT pahctx comment that
        # names the same sender_id and has no explicit mastersecret.
        self.pending_spake_result = None

    # -- dispatch -------------------------------------------------------

    def apply(self, text: str) -> Optional[ParsedCommand]:
        """Parse and apply one Comment's @@[... command, if it is one.
        Returns the ParsedCommand for callers that want to log/assert on
        it, or None if `text` wasn't a recognized tool command."""
        cmd = parse_comment_command(text)
        if cmd is None:
            return None

        handler = getattr(self, f"_apply_{cmd.command.lower()}", None)
        if handler is None:
            raise KeyError(
                f"No provisioning handler for '@@[{cmd.command}' — this "
                "XML revision uses a comment command that hasn't been "
                "implemented yet in provisioning.py")
        handler(cmd)
        return cmd

    # -- pahdev -----------------------------------------------------------

    def _apply_pahdev(self, cmd: ParsedCommand) -> None:
        dev = self.devices.setdefault(cmd.alias, DeviceEndpoint(name=cmd.alias))
        params = {k: self.variables.substitute(v) for k, v in cmd.params.items()}
        if "serialnumber" in params:
            dev.serial_number = params["serialnumber"]
        if "password" in params:
            dev.password = params["password"]
        if "ipaddress" in params:
            dev.ip_address = params["ipaddress"]

    def resolve_device(self, alias: str) -> Optional[DeviceEndpoint]:
        return self.devices.get(alias)

    # -- pahmcast -----------------------------------------------------------

    def _apply_pahmcast(self, cmd: ParsedCommand) -> None:
        grp = self.multicast_groups.setdefault(
            cmd.alias, MulticastGroup(name=cmd.alias))
        params = {k: self.variables.substitute(v) for k, v in cmd.params.items()}
        if "installationid" in params:
            grp.installation_id = params["installationid"]
        if "groupid" in params:
            grp.group_id = params["groupid"]
        if "scope" in params:
            grp.scope = params["scope"]
        # `context=` and `rxcontext=` are the same thing per EITT's
        # grammar (both documented as "Name of RX security context").
        if "context" in params:
            grp.context_alias = params["context"]
        if "rxcontext" in params:
            grp.context_alias = params["rxcontext"]
        if "txcontext" in params:
            grp.tx_context_alias = params["txcontext"]
        if "join" in params:
            grp.joined = params["join"].lower() == "true"
        if params.get("remove", "").lower() == "true":
            self.multicast_groups.pop(cmd.alias, None)

    def resolve_multicast_group(self, alias: str) -> Optional[MulticastGroup]:
        return self.multicast_groups.get(alias)

    def group_for_rx_context(self, context_alias: str
                             ) -> Optional[MulticastGroup]:
        """Find the JOINED multicast group whose RX security context is
        `context_alias`. This is the binding that tells the runner which
        group address to listen on when the XML expects a telegram the DUT
        originates (an `OUT` message on a `Multicast*Rx` SecContext) —
        EITT establishes it via '@@[pahmcast"<grp>;context=<RxCtx>"'.
        """
        if not context_alias:
            return None
        for grp in self.multicast_groups.values():
            if grp.context_alias == context_alias and grp.joined:
                return grp
        return None

    def joined_group_with_context(self) -> Optional[MulticastGroup]:
        """The joined multicast group that has an RX security context bound,
        for an `OUT` telegram whose own SecContext attribute is EMPTY.

        5.4.1.7 and 5.4.1.9 both expect a DUT-originated group telegram but
        leave `SecContext=""` on the step, so `group_for_rx_context` can't
        find anything. The binding still exists — `@@[pahmcast"MCastGrp01;
        context=MulticastRx"` — and it PERSISTS ACROSS TestCases in the
        collection (5.4.1.9 issues only `join=true`, inheriting grpid /
        installationid / context from an earlier case, exactly as EITT does
        with its shared project state).

        That the telegram really is protected under that inherited context
        is not a guess: traces/5_4_1.xml TelNo 185 — 5.4.1.9's telegram —
        records `SecContext="MulticastRx"` with KeyID 10 2A 3B 4C 5D 6E 7F,
        PartialIV 01 E0 and KidContext 10 2A 4B, i.e. EITT resolved the
        empty attribute to the bound context too.

        Prefers a group whose context is actually materialized, so a
        half-provisioned group can't shadow a usable one.
        """
        fallback = None
        for grp in self.multicast_groups.values():
            if not (grp.joined and grp.context_alias):
                continue
            if self.resolve_context(grp.context_alias) is not None:
                return grp
            fallback = fallback or grp
        return fallback

    def any_unicast_context(self):
        """A materialized context NOT bound to a multicast group, for a
        DUT-originated unicast telegram whose step names no SecContext.

        5.4.1.15's telegram step carries `SecContext=""` and the trace
        (traces/5_4_1.xml TelNo 323) records it with an empty SecContext too,
        so there is no alias to resolve — but the telegram IS protected
        (KeyID 18 2A 3B 4C 5D 6E 7F). The only unicast context the TestCase
        provisions is the right one; group RX/TX contexts are excluded so a
        multicast context cannot be picked by accident.

        Prefers the most recently provisioned, since a TestCase that sets up
        several has the live one last.
        """
        group_aliases = {g.context_alias for g in self.multicast_groups.values()
                         if g.context_alias}
        for alias in reversed(list(self.contexts)):
            if alias in group_aliases:
                continue
            ctx = self.contexts.get(alias)
            if ctx is not None:
                return ctx
        return None

    # -- pahctx -----------------------------------------------------------

    def _apply_pahctx(self, cmd: ParsedCommand) -> None:
        alias = cmd.alias
        if alias is None:
            return
        params = {k: self.variables.substitute(v) for k, v in cmd.params.items()}
        builder = self._context_builders.setdefault(alias, ContextBuilder(name=alias))
        # Snapshot the builder's identity-defining fields BEFORE applying
        # the new params, so a re-provisioning call with THE SAME
        # sender_id/master_secret/etc. (e.g. AUTH PREPARATION's
        # '@@[pahctx"TokenTest;senderid=testcfg"' + a fixed mastersecret,
        # re-run every time the preparation is invoked, as it is 3x in
        # "5.3.1.3 ... across various device reset types") can be told
        # apart from a genuine change of identity.
        before = (builder.sender_id, builder.recipient_id,
                 builder.master_secret, builder.context_id, builder.salt)
        builder.apply(params)
        after = (builder.sender_id, builder.recipient_id,
                builder.master_secret, builder.context_id, builder.salt)

        # Only invalidate the materialized context when its identity
        # actually changed (or it was explicitly removed). The DEVICE's
        # own OSCORE replay window for this kid is keyed on sender_id and
        # persists across re-preparation (it doesn't reset just because
        # EITT re-runs the same pahctx params) — so rebuilding a fresh
        # OscoreContext here would silently reset OUR sender sequence
        # number back to 0 and immediately collide with a PIV the device
        # already saw as a replay, breaking every request sent through
        # this context after the first re-preparation.
        if builder.removed or before != after:
            self.contexts.pop(alias, None)
        elif alias in self.contexts:
            return  # identical re-provisioning — keep the existing ctx/ssn

        spake_secret = None
        pending = self.pending_spake_result
        if (pending is not None and builder.master_secret is None
                and builder.sender_id == pending.sender_id):
            spake_secret = pending.shared_key
            self.pending_spake_result = None  # one-shot consumption

        if builder.is_ready(spake_secret):
            self.contexts[alias] = builder.build(spake_secret)

    def resolve_context(self, alias: str):
        """Return the materialized OscoreContext for a SecContext alias,
        or None for SecContext="" (unsecured) / not-yet-ready contexts."""
        if not alias:
            return None
        return self.contexts.get(alias)

    def set_context(self, alias: str, ctx) -> None:
        """Install an already-built OscoreContext directly (used by the
        runner for the PASE/"Temporary Key" context, which is derived
        from the SPAKE2+ handshake result rather than from pahctx
        parameters alone)."""
        self.contexts[alias] = ctx

    # -- pahsnc / MDNS toggles --------------------------------------------

    def _apply_pahsnc(self, cmd: ParsedCommand) -> None:
        value = cmd.params.get("_value", "").lower()
        self.seq_no_check_enabled = value != "off"

    def _apply_mdns(self, cmd: ParsedCommand) -> None:
        """`@@[MDNS"rqcheck=on|off"` — EITT's own grammar documents this as
        `@@[pahmdns`, "Check if the answered service is equal to the
        requested service" (EITT_KnxCommentCommandsScheme.xml). The
        template uses the older `MDNS` spelling; both are accepted.

        This is NOT cosmetic. With the check ON (the default) an mDNS
        answer only counts when its owner name equals the name that was
        queried. 5.1.2.x turns it OFF precisely because it queries
        `_knx._udp.local` and then matches records for a DIFFERENT service
        (`_00fa10020800._sub._knx._udp.local`, `_pm._sub…`) out of the
        same answer — see the "mDNS discovery" section of
        tests/docs/eitt-xml-interpreter.md.
        """
        value = cmd.params.get("rqcheck", "").lower()
        if value:
            self.mdns_request_check_enabled = value != "off"

    # EITT's scheme file spells this command `pahmdns`; the conformance
    # template still uses the older `MDNS`. Same command, same handler.
    _apply_pahmdns = _apply_mdns

    # -- pahdiscover --------------------------------------------------------

    def _apply_pahdiscover(self, cmd: ParsedCommand) -> None:
        if self.on_discover is not None:
            self.on_discover()

    # -- w (named wait) -----------------------------------------------------

    def _apply_w(self, cmd: ParsedCommand) -> Optional[float]:
        """'@@[w"#FIELD"' — resolve the named wait duration (seconds).
        Returns the wait duration so the runner can actually sleep;
        recorded on the engine too for introspection/tests."""
        raw = cmd.params.get("_value", "")
        resolved = self.variables.substitute(raw)
        try:
            seconds = float(resolved) / 1000.0 if "." not in resolved and resolved.isdigit() else float(resolved)
        except ValueError:
            seconds = None
        self.last_wait_seconds = seconds
        return seconds
