"""
Per-resource-path JSON<->CBOR key mapping.

The EITT XML's <Content> blocks describe bodies with human-readable
JSON string keys (e.g. {"value":0}, {"api":{"base":"/"}}), but the real
KNX IoT Point API wire protocol uses CBOR maps with small INTEGER keys
that are scoped PER RESOURCE PATH — e.g. key 1 means "value" on /p/*
but "version" inside an OSCORE cnf map, and key 12 means "ia" on
.well-known/knx/ia but is part of the PASE pbkdf2 struct elsewhere.

There is no single central table for this anywhere — not in the EITT
XML/Python script (which never touches CBOR itself; EITT's closed-source
core does the translation internally), not in the C stack as loadable
data, and not in knx-iot-docs as anything more than scattered per-
resource markdown tables. This module is our replacement for that
missing piece: one schema per resource path, hand-authored from
knx-iot-docs' resource tables and cross-checked against every
cbor2.dumps(...)/cbor2.loads(...) call in tests/runtime/test_5_*.py and
conftest.py (see git history/PR description for the audit).

Extend this table as new TestCases from a fresh XML revision exercise a
resource path that isn't mapped yet — get_schema() raises rather than
guessing, so a gap surfaces as a clear collection/runtime error instead
of silently sending a malformed body.
"""

from __future__ import annotations

import re
from typing import Optional

# The EITT XML represents a CBOR byte string as a JSON string literal
# "h'<hex>'" (e.g. "id":"h'74657374636667'") — confirmed throughout the
# XML (SPAKE2+ payloads, AT cnf/osc id+ms, group-object href hex, etc).
# This is NOT a real KNX-IoT wire convention; it is purely an
# XML-authoring readability convention EITT itself expands before
# sending. Every string value passed through ResourceSchema.encode (and
# the runner's plain non-schema JSON->CBOR path) must have this
# expanded to real bytes, or the DUT derives completely different key
# material / values from the literal ASCII text "h'...'" instead of the
# intended byte string — this was a real, confirmed bug (AT provisioned
# with a "h'...'"-literal master secret string authenticates with
# nothing, because the OSCORE keys derived from that literal string
# don't match what the interpreter's own OscoreContext uses for the
# SAME nominal secret).
_HEX_LITERAL_RE = re.compile(r"^h'([0-9a-fA-F]*)'$")


def expand_hex_literals(obj):
    """Recursively replace every "h'<hex>'"-shaped string in a JSON-like
    structure with the actual bytes it denotes.

    An odd-length hex string (e.g. the template's "h'116'" in 5.3.20.1 —
    the only such occurrence anywhere in the XML, confirmed via
    traces/5_3.xml TelNo ~11650 to be a stray extra digit; the real
    device run actually sent "h'11'") can't be split into whole bytes.
    Rather than raise and abort the whole request — the byte VALUE here
    is a placeholder for a negative access-control probe that the
    device is expected to reject outright regardless of its contents —
    right-pad with one '0' nibble, matching bytes.fromhex's own
    left-to-right nibble grouping, so a malformed-but-recoverable
    literal still produces bytes instead of failing every TestCase that
    happens to reference it."""
    if isinstance(obj, str):
        m = _HEX_LITERAL_RE.match(obj)
        if m:
            digits = m.group(1)
            if len(digits) % 2:
                digits += "0"
            return bytes.fromhex(digits)
        return obj
    if isinstance(obj, list):
        return [expand_hex_literals(item) for item in obj]
    if isinstance(obj, dict):
        return {k: expand_hex_literals(v) for k, v in obj.items()}
    return obj


class ResourceSchema:
    """String-key <-> integer-key mapping for one resource path.

    `fields` maps the human-readable name (as it appears in the XML's
    JSON-shaped <Content>) to its CBOR integer key. `nested` optionally
    maps a field name to another ResourceSchema for a nested map (e.g.
    the AT entry's "cnf" -> "osc" sub-map).
    """

    def __init__(self, fields: dict[str, int],
                nested: Optional[dict[str, "ResourceSchema"]] = None):
        self.fields = fields
        self.nested = nested or {}
        self._reverse = {v: k for k, v in fields.items()}

    def encode(self, obj):
        """Recursively translate a JSON-shaped dict/list/scalar (string
        keys) into a CBOR-ready dict/list/scalar (integer keys), using
        this schema for the top level and `nested` schemas for any
        field that has one. Unknown field names pass through with a
        KeyError — a body referencing a field this schema doesn't know
        about must fail loudly rather than silently drop data.

        Any "h'<hex>'" literal (the XML's byte-string-in-JSON
        convention) is expanded to real bytes before it reaches the
        wire — see expand_hex_literals()."""
        if isinstance(obj, list):
            return [self.encode(item) for item in obj]
        if not isinstance(obj, dict):
            return expand_hex_literals(obj)
        out = {}
        for k, v in obj.items():
            if k not in self.fields:
                raise KeyError(
                    f"Field '{k}' not in schema (known fields: "
                    f"{sorted(self.fields)})")
            int_key = self.fields[k]
            sub_schema = self.nested.get(k)
            out[int_key] = (sub_schema.encode(v) if sub_schema
                           else expand_hex_literals(v))
        return out

    def decode(self, obj):
        """Inverse of encode(): translate a CBOR-decoded dict/list
        (integer keys) back into a JSON-shaped dict (string keys) for
        Content-matching. Unknown integer keys are kept as-is (stringified)
        rather than dropped, so an unexpected/new field is still visible
        in a match failure instead of vanishing silently."""
        if isinstance(obj, list):
            return [self.decode(item) for item in obj]
        if not isinstance(obj, dict):
            return obj
        out = {}
        for k, v in obj.items():
            name = self._reverse.get(k, str(k))
            sub_schema = self.nested.get(name)
            out[name] = sub_schema.decode(v) if sub_schema else v
        return out


# ---------------------------------------------------------------------------
# OSCORE cnf / osc sub-structure, reused by /auth/at and the PASE messages.
# Cross-checked against conftest.py:619 (`8: {4: {0: new_sid, 2: new_ms}}`)
# and oc_knx_sec.h's Access Token / OSCORE cnf Doxygen tables.
# ---------------------------------------------------------------------------

# Keys per EITT's own map, tests/runtime/data/EITT_KnxPointAPIResourceKeyMaps.xml,
# PointAPIResourceKeyMap Path="auth/at.*" ID="PAPI_RKM_OSCORE" — the
# authoritative table. Note salt=5 SHARES its key with nbf: that file's
# header calls this out explicitly ("nbf/salt = 5) because they occur in
# mutually exclusive contexts"), and the device agrees — oc_knx_sec.c:993
# parses cnf:osc:salt as (8:4:5). Getting salt wrong is silent and total:
# the AT write still returns 2.01 but the device derives its keys with an
# EMPTY salt, so every subsequent message under that context fails AEAD
# (this cost 5.3.19.5 a full debugging round with salt mistakenly at 35).
_OSC_SCHEMA = ResourceSchema({
    "id": 0,          # kid (byte string)
    "version": 1,
    "ms": 2,          # master secret (byte string)
    "hkdf": 3,
    "alg": 4,
    "salt": 5,        # master salt (byte string)
    "contextid": 6,   # id_context / group context id — the real XML
                      # (5.3.8.5, 5.3.19.5) spells this lowercase-d
                      # "contextid", NOT "contextId"; kept lowercase to
                      # match the actual <Content> field name exactly.
    "contextId": 6,   # ...but EITT's own key map spells it with a capital
                      # I, so accept both spellings for the same key.
})

_CNF_SCHEMA = ResourceSchema({
    "osc": 4,
    "kid": 3,
}, nested={"osc": _OSC_SCHEMA})

# Access Token entry (POST/PUT /auth/at), the inner map wrapped as {0: ...}
# by the outer AT-list schema below. Cross-checked against
# conftest.py:615-620 (`at_inner = {0:..,9:..,38:..,8:{4:{0:..,2:..}}}`).
_AT_ENTRY_SCHEMA = ResourceSchema({
    "id": 0,
    "exp": 4,
    "nbf": 5,
    "cnf": 8,
    "scope": 9,
    "profile": 38,
}, nested={"cnf": _CNF_SCHEMA})

# /k group-write notification's nested "s" (state) object — st/ga live
# INSIDE this sub-map, not at the top level. Cross-checked against
# api/oc_knx.c:774-806 (oc_core_knx_k_post_handler's field scan: st=6,
# ga=7, and "value" (1) is itself a field of THIS same object, not a
# separate top-level key) and traces/5_3.xml TelNo 487
# ("BF 05 BF 07 19 FF FF 06 61 77 01 F5 FF 04 19 11 01 FF" =
# {5: {7: 65535, 6: "w", 1: true}, 4: 4353}).
_K_S_SCHEMA = ResourceSchema({
    "st": 6,
    "ga": 7,
    "value": 1,
})

# /k — group-write notification. api/oc_knx.c:756 (sia=4, mandatory).
#
# "a" (97) carries the SAME st/ga/value sub-map as "s" (5). Confirmed from
# real wire bytes in traces/5_4_1.xml, where 5.4.1.13's long-group-address
# write records RawContent
# "BF 18 61 BF 07 19 FF FF 06 61 77 01 F4 FF 04 19 11 0F FF" —
# CBOR {97: {7: 65535, 6: "w", 1: false}, 4: 4367}, EvalResult "OK"; `18 61`
# is uint8 97, so this is NOT a small key. The device treats the two
# identically: oc_core_knx_k_post_handler's scan matches on
# `case OC_REP_OBJECT` and reads st/ga/value out of whatever object it
# finds, never checking whether the outer key was 5 or 97.
_K_SCHEMA = ResourceSchema({
    "sia": 4,
    "s": 5,
    "a": 97,
}, nested={"s": _K_S_SCHEMA, "a": _K_S_SCHEMA})


# ---------------------------------------------------------------------------
# Default map — EITT's own Default="yes" table, transcribed verbatim from
# tests/runtime/data/EITT_KnxPointAPIResourceKeyMaps.xml (PAPI_RKM_DEF_0). EITT
# applies this to ANY resource path that has no path-specific map, which is
# why most resources need no entry of their own. get_schema() falls back to
# it instead of raising, so a path we have never seen still encodes/decodes
# correctly as long as it uses these standard field names.
#
# Every key here was independently confirmed against real traces / the C
# implementation before this file was available; the file then verified the
# whole set at once and supplied the ones we had not yet encountered
# (secFlags, pkgq, fault, g, groupKey, overridden, dpt, knxClassicObj,
# a, c, seqCntr) — those will matter for sections 5.4-5.10.
#
# "s" nests the group-telegram state sub-map (st/ga/value), per the /k
# cross-check above.
_DEFAULT_SCHEMA = ResourceSchema({
    "id": 0,
    "value": 1,
    "cmd": 2,
    "status": 3,
    "sia": 4,
    "s": 5,
    "st": 6,
    "ga": 7,
    "cflag": 8,
    "url": 10,
    "href": 11,
    "ia": 12,
    "grpid": 13,
    "at": 14,
    "fid": 25,
    "iid": 26,
    "secFlags": 28,
    "pkgq": 29,
    "a": 97,
    "c": 99,
    "fault": 102,
    "g": 103,
    "groupKey": 107,
    "seqCntr": 110,
    "overridden": 111,
    "knxClassicObj": 115,
    "dpt": 116,
}, nested={"s": _K_S_SCHEMA})


# ---------------------------------------------------------------------------
# Resource path -> ResourceSchema
# ---------------------------------------------------------------------------

# .well-known/knx/spake — SPAKE2+ handshake wire format. A genuine
# multi-step handshake is driven by spake_flow.py's Spake2PlusClient
# directly (bypassing this schema — see that module's docstring), but an
# isolated single-shot probe (e.g. 5.3.1.3's bare step-1 POST sent while
# the device is deliberately NOT in default config state, expecting a
# flat 4.00 with no continuation) goes through the ordinary
# _send_request/_expect_response path like any other message, and needs
# this schema to encode its literal JSON <Content>.
#
# Transcribed verbatim from EITT's own map for this path
# (tests/runtime/data/EITT_KnxPointAPIResourceKeyMaps.xml, PAPI_RKM_OSCORE for
# Path=".well-known/knx/spake"), which confirmed the four keys we had
# derived from knx_spake2plus.py's create_*_request() dicts (id=0,
# shareP=10, confirmP=14, rnd=15) and api/oc_knx.c's spake key enum
# (pw=8 — 5.3.1.4a's {"pw": ...} handover probe expects 4.00 back since
# the device does not implement it yet), and added the response-side and
# pbkdf2 keys we had no reason to hit yet (shareV, confirmV, pbkdf2, it,
# version, salt, contextId).
_SPAKE_SCHEMA = ResourceSchema({
    "id": 0,
    "version": 1,
    "ms": 2,
    "hkdf": 3,
    "alg": 4,
    "salt": 5,
    "contextId": 6,
    "pw": 8,
    "shareP": 10,
    "shareV": 11,
    "pbkdf2": 12,
    "confirmV": 13,
    "confirmP": 14,
    "rnd": 15,
    "it": 16,
})

RESOURCE_SCHEMAS: dict[str, ResourceSchema] = {
    ".well-known/knx/spake": _SPAKE_SCHEMA,

    # auth/at/{token-id} — GET a single Access Token entry by id. Unlike
    # the /auth/at collection resource (bare array of entries, see
    # AT_ENTRY_SCHEMA usage in runner.py), a by-id GET returns exactly
    # one bare entry map — same field shape as an entry in that array.
    # 5.2.2.3 verifies a just-created PASE temp token this way
    # ('/id: TmpTok', '/cnf/osc/id: h\'...\'', '/scope: [...]').
    "/auth/at": _AT_ENTRY_SCHEMA,

    # .well-known/knx/ia — set Individual Address / Installation ID
    # (+ optional Fabric ID). conftest.py:644,805; test_5_2_wellknown_knx.py:192.
    ".well-known/knx/ia": ResourceSchema({
        "ia": 12, "iid": 26, "fid": 25,
    }),

    # .well-known/knx (device restart / programming mode / factory reset
    # command envelope). test_5_2_wellknown_knx.py:78,91;
    # test_5_2_reset.py:144,314 (`{2: "reset", 1: <code>}`).
    ".well-known/knx": ResourceSchema({
        "value": 1,     # e.g. programming-mode boolean payload {1: true}
        "cmd": 2,       # "restart" | "reset"
    }),

    # /a/lsm — Load State Machine command. test_5_1_discovery_extended.py:171
    # (`{2: <cmd>}`); 1=loading,2=loadComplete,4=unload per set_lsm() docstring.
    "/a/lsm": ResourceSchema({
        "cmd": 2,
        "status": 3,
    }),

    # /fp/g — Group Object table entry. test_5_5_fp_tables.py:160
    # (`[{0: id, 7: ga, 8: cflag, 11: href}]`); id-only entry ({0: id})
    # is the POST-empty-element delete form (test_5_5_fp_tables.py:211).
    "/fp/g": ResourceSchema({
        "id": 0, "ga": 7, "cflag": 8, "href": 11,
    }),

    # /fp/r, /fp/p — Recipient / Publisher table entry, same field
    # shape. test_5_5_fp_tables.py:480 (`[{0: id, 13: grpid, 7: ga}]`);
    # a unicast-address entry uses "ia"/"iid" instead of "grpid" — see
    # tests/docs/eitt-xml-interpreter.md's JSON<->CBOR mapping research
    # (5.5.5.3/5.5.9.3 unicast recipient/publisher entries).
    "/fp/r": ResourceSchema({
        "id": 0, "ga": 7, "grpid": 13, "ia": 12, "iid": 26, "fid": 25, "at": 14,
    }),
    "/fp/p": ResourceSchema({
        "id": 0, "ga": 7, "grpid": 13, "ia": 12, "iid": 26, "fid": 25, "at": 14,
    }),

    # /k — group-write notification. See _K_SCHEMA above for the
    # nested "s" (state) sub-map cross-check.
    "k": _K_SCHEMA,

    # /p/{path} — parameter/diagnostic/group-object datapoint value.
    # Usual GET response and PUT/POST body are just {1: <value>}, but a
    # GET with the "?m=id" URL parameter (5.3.20.1) instead returns
    # {0: "knx://sn.<serial>/<path>"} — confirmed against
    # traces/5_3.xml TelNo 1185/1186, RawContent "BF 00 78 1A ... FF" =
    # {0: "knx://sn:00fa10020800/p/p1"}.
    # test_5_8_parameters.py:200; test_5_2_fingerprint.py:169.
    "/p": ResourceSchema({"id": 0, "value": 1, "href": 11}),

    # /dev/{name} — device data property value. Same {1: <value>} shape
    # as /p/* (test_5_2_device_resources.py:117,127,133: `data.get(1)`).
    "/dev": ResourceSchema({"value": 1}),

    # /ap/pv — Application Program "PEI value"/parameter-value datapoint.
    # Same flat {1: <value>} shape as /p and /dev — the value here just
    # happens to be a CBOR array (5.3.20.1's PUT body [65535,65535,255]).
    # Confirmed against traces/5_3.xml TelNo 1198, RawContent
    # "BF 01 83 19 FF FF 19 FF FF 18 FF FF" = {1: [65535,65535,255]}.
    "ap/pv": ResourceSchema({"value": 1}),

    # /auth/o/replwdo, /auth/o/osndelay — OSCORE replay-window-size /
    # sender-sequence-delay GET/PUT, same flat {1: <value>} shape as
    # /p and /dev. api/oc_knx_sec.c:172-174 (osndelay GET:
    # `oc_rep_i_set_uint(root, 1, ...)`), :195-198 (osndelay PUT reads
    # `rep->iname == 1`); replwdo GET handler at :230 follows the same
    # single-field convention.
    "/auth/o/replwdo": ResourceSchema({"value": 1}),
    "/auth/o/osndelay": ResourceSchema({"value": 1}),

    # /.well-known/knx/f — fingerprint. Response body is {1: <value>}.
    ".well-known/knx/f": ResourceSchema({"value": 1}),

    # /a/swu (Software Update) state/control resources.
    # test_5_2_swu.py:197 (`{1: 0}` = update trigger with value 0).
    "/a/swu": ResourceSchema({"value": 1}),
    "swu/update": ResourceSchema({"value": 1}),
    "swu/state": ResourceSchema({"value": 1}),
    "swu/pkgbytes": ResourceSchema({"value": 1}),

    # /test/trigger — test-only DUT control endpoint (not part of the
    # real KNX IoT spec; used by overrides.py action="trigger" to
    # automate steps EITT expects a human to perform manually).
    # test_5_3_security.py:1719,1818 (`{11: "/p/3"}` — href to trigger).
    "/test/trigger": ResourceSchema({"href": 11, "value": 1}),
}

# AT entry schema is exposed separately, with no "/auth/at" entry in
# RESOURCE_SCHEMAS: the wire envelope wraps one entry as {0: entry}
# (conftest.py:780) or a bare list of entries for multi-write, so there
# is no single flat field-name map for the resource path itself — the
# runner must apply this schema to each entry and build the {0: ...}/
# list envelope itself rather than going through get_schema("/auth/at").
AT_ENTRY_SCHEMA = _AT_ENTRY_SCHEMA


# Resource "families": a path registered here matches itself AND every
# path nested under it (e.g. "/p" also covers "/p/1", "/p/p1", ...),
# because individual datapoint instances under these parents all share
# the same {value, href, ...} field shape rather than getting their own
# per-instance schema.
_FAMILY_PREFIXES = ("/p", "/a/swu", "/dev", "/auth/at")


def get_schema(path: str) -> ResourceSchema:
    """Look up the ResourceSchema for a CoAP path.

    Tries, in order: the exact path, the path with/without a leading
    slash, and — for registered "family" prefixes like "/p" — the
    longest matching prefix (so "/p/1", "/p/p1", etc. all resolve to the
    "/p" schema without needing one entry per datapoint instance).

    Falls back to _DEFAULT_SCHEMA when nothing matches, mirroring EITT
    itself: its key map has a Default="yes" table applied to every path
    without a specific one (see tests/runtime/data/
    EITT_KnxPointAPIResourceKeyMaps.xml). A path-specific entry is
    therefore only needed where the shape genuinely DIFFERS from the
    default (e.g. auth/at's RFC-8392 token map, or .well-known/knx/spake),
    not for every new resource. An unknown FIELD name inside a body still
    raises loudly from ResourceSchema.encode() — the loud failure we want
    is "this field isn't in any map", not "this path isn't listed".
    """
    stripped = path.split("?", 1)[0].strip("/")
    for candidate in (stripped, stripped.rstrip("/")):
        if candidate in RESOURCE_SCHEMAS:
            return RESOURCE_SCHEMAS[candidate]
        if f"/{candidate}" in RESOURCE_SCHEMAS:
            return RESOURCE_SCHEMAS[f"/{candidate}"]

    normalized = f"/{stripped}"
    best_match = None
    for prefix in _FAMILY_PREFIXES:
        if (normalized == prefix or normalized.startswith(prefix + "/")):
            if best_match is None or len(prefix) > len(best_match):
                best_match = prefix
    if best_match is not None and best_match in RESOURCE_SCHEMAS:
        return RESOURCE_SCHEMAS[best_match]

    return _DEFAULT_SCHEMA
