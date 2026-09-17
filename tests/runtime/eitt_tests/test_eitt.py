"""
Dynamic EITT XML-driven conformance tests.

Unlike the runtime tier's behaviour-named test modules, this file does
NOT hand-code individual test bodies. Instead, `pytest_generate_tests`
below reads tests/runtime/data/KnxConformanceTestTemplate-PointAPI.xml at
collection time and generates one parametrized test item per TestCase,
named after its qualified name (collection::suite::case) so `pytest
-k "5_1_1_2"`-style selection still works. Swapping to a new EITT XML
revision changes what gets collected on the next run — nothing here
needs to be hand-edited or regenerated to source code.

See tests/docs/eitt-xml-interpreter.md for the full design and
eitt/ for the interpreter modules this drives:
  model            XML parser / document hierarchy
  variables        #VAR substitution + Fields-scoped variable table
  content_match    <Content> matching mini-language
  hooks            ported EvalFn/EvalResponseFn/PatchXxxFn callbacks
  provisioning     "@@[pah..." comment-command engine
  resource_schema  per-resource-path JSON<->CBOR key mapping
  spake_flow       real SPAKE2+ handshake (bypasses the XML's own
                   <TOOL.SHARE_P>/<TOOL.CONFIRM_P> placeholders)
  overrides        maintainer exceptions for manual-interaction /
                   not-yet-reliable TestCases (eitt/overrides.yaml)
  runner           the sequence player tying all of the above together

Preparation/state-sharing model (per the user, who authors and runs
these sequences directly in the real EITT tool): EITT inserts
'@#"Copy of AUTH PREPARATION"' / '@#"Copy of IA PREPARATION"' /
'@#"Copy of TABLE PREPARATION"' once, at the START of whatever unit was
selected to run — a single TestCase, a TestSuite, or (as recorded in
this XML) a whole TestCollection — against a freshly factory-reset
device. Every TestCase inside that run shares the resulting
device/OSCORE/variable state; state is NEVER shared across separate
runs. Concretely, 124 of 187 TestCases in this v40 XML have NO
preparation reference of their own at all — they were authored/recorded
as part of a bundled TestCollection-level run and rely on state left
behind by an earlier sibling TestCase (in document order, sometimes in
a different TestSuite within the same collection — e.g. 5.2.5.1,
5.2.6.1 and 5.2.7.1 all lack their own prep and depend on state from
5.2.2.x/5.2.4.x run earlier in the same "5.2. /.well-known/knx"
collection). A TestCase WITH its own prep reference re-authenticates
for real when the XML places it there (see e.g. 5.2.2.1, which
factory-resets the device again mid-sequence and references AUTH
PREPARATION a second time).

So: this file bundles at the TestCollection level — one factory-reset
per collection (via the `collection_runner` fixture below), then every
TestCase in that collection runs, in XML document order, sharing one
SequenceRunner/VariableTable/ProvisioningEngine. Each TestCase is still
its own independently-named, independently-reported pytest item (so
`pytest -k "5_2_6_1"` still selects exactly one), but they are NOT
independently RUNNABLE in isolation — running only `5.2.6.1` without
everything that precedes it in the collection will fail exactly as it
would on real hardware, because it depends on that earlier state. This
mirrors real EITT: selecting a smaller unit (e.g. a single TestCase)
gets its own fresh prep inserted for that run; selecting a bundle
shares one prep across everything in it.

Coverage status: this collects and can run EVERY TestCase in the XML.
CBOR bodies are encoded/decoded via eitt/resource_schema.py, which
falls back to a default key map for an unregistered PATH (mirroring
EITT's own Default="yes" table) — so a fresh section only needs an
entry where its body shape differs from that default. An unknown FIELD
name does fail loudly (KeyError from ResourceSchema.encode) rather than
silently sending garbage.
See tests/docs/eitt-xml-interpreter.md for the interpreter design and
suite's per-ID coverage in the meantime; the two suites are intended to
run side-by-side during migration (see project notes), not compete.
"""

import os
import re
import time
from pathlib import Path

import pytest

from eitt.model import parse_eitt_xml
from eitt.overrides import OverrideTable
from eitt.provisioning import ProvisioningEngine
from eitt.runner import PreparationRegistry, SequenceRunner
from eitt.variables import VariableTable

# The conformance template. RUNTIME_TEST_EITT_XML points the interpreter at
# a DIFFERENT copy — the supported way to check that an assertion really
# bites (mutate a scratch copy, confirm the suite goes red) without ever
# editing the master template, which must not be modified.
XML_PATH = Path(os.environ.get(
    "RUNTIME_TEST_EITT_XML",
    Path(__file__).resolve().parent.parent / "data"
    / "KnxConformanceTestTemplate-PointAPI.xml"))

_PROJECT = parse_eitt_xml(XML_PATH)
_OVERRIDES = OverrideTable.load()

# The operator's per-message Activate choices, applied over the read-only
# template (which must never be edited). This is how a device CAPABILITY
# encoded in the XML as alternative response branches — 5.10's "pagination
# supported" 2.05 vs "not supported" 4.00 pairs — gets selected. A stale key
# is a hard error: leaving the template's own branch active would otherwise
# fail somewhere far from the cause.
_UNMATCHED_ACTIVATION = _OVERRIDES.apply_message_activation(_PROJECT)
if _UNMATCHED_ACTIVATION:
    raise RuntimeError(
        "eitt/overrides.yaml 'message_activation' entries matched no "
        f"CoAPMessage in {XML_PATH.name}: {_UNMATCHED_ACTIVATION!r} — check "
        "for a typo or a stale entry from a previous XML revision.")

_PREPARATIONS = PreparationRegistry(_PROJECT)

# TestCases that are themselves preparation sequences (invoked via the
# "@#"..."" inline-reference mechanism, not meant to be collected as
# their own top-level pytest items) or template TestCases the
# CreationMessage instructs testers to duplicate per-project (the
# "Upload Block" loop bodies) rather than run directly.
_EXCLUDED_CASE_NAMES = {
    "NEW PROJECT SETUP", "AUTH PREPARATION", "IA PREPARATION",
    "TABLE PREPARATION",
    "Upload Block - First Half", "Upload Block - Second Half",
}


def _collect_cases():
    """Returns a list of (collection_name, TestCase) in XML document
    order — document order matters here because state flows forward
    from one TestCase to the next within a collection (see module
    docstring)."""
    cases = []
    for coll, suite, tc in _PROJECT.iter_test_cases():
        if tc.name in _EXCLUDED_CASE_NAMES:
            continue
        cases.append((coll.name, tc))
    return cases


_ALL_CASES = _collect_cases()


def _check_overrides_match_real_cases():
    """A typo in eitt/overrides.yaml's qualified-name keys is otherwise a
    silent no-op (OverrideTable.get() just returns None for an unknown
    key) — fail collection loudly instead, naming the exact bad key,
    so a stale/misspelled override can't quietly stop applying."""
    known_names = {tc.qualified_name for _coll_name, tc in _ALL_CASES}
    # Excluded (preparation/template) TestCases are legitimate override
    # targets too (e.g. a future override on "AUTH PREPARATION" itself),
    # so check against the full unfiltered set, not just _ALL_CASES.
    known_names |= {tc.qualified_name for _c, _s, tc in _PROJECT.iter_test_cases()}
    unmatched = [name for name in _OVERRIDES.keys() if name not in known_names]
    if unmatched:
        raise RuntimeError(
            "eitt/overrides.yaml has key(s) that don't match any "
            f"TestCase.qualified_name in {XML_PATH.name}: {unmatched!r} "
            "— check for typos or a stale entry from a previous XML "
            "revision.")


_check_overrides_match_real_cases()


def _section_of(collection_name: str) -> str:
    """The section number a TestCollection belongs to, e.g.
    "5.6. Application Program Object Resource" -> "5.6"."""
    m = re.match(r"\s*(\d+(?:\.\d+)*)", collection_name)
    return m.group(1) if m else ""


def _in_section(collection_name: str, wanted: str) -> bool:
    """True if `collection_name`'s section is `wanted` or nested under it,
    comparing NUMERIC COMPONENTS rather than characters. "5.6" therefore
    matches section "5.6" and a hypothetical "5.6.1", but never "5.5" —
    whereas a plain substring test ("5.6" in "5.5.5.6") is exactly the
    trap this exists to avoid.
    """
    have = _section_of(collection_name).split(".")
    want = wanted.strip().rstrip(".").split(".")
    return have[:len(want)] == want


def pytest_generate_tests(metafunc):
    if "eitt_case" not in metafunc.fixturenames:
        return

    cases = _ALL_CASES
    sections = metafunc.config.getoption("--section")
    if sections:
        cases = [(cn, tc) for cn, tc in cases
                 if any(_in_section(cn, s) for s in sections)]
        if not cases:
            known = sorted({_section_of(cn) for cn, _tc in _ALL_CASES})
            raise pytest.UsageError(
                f"--section {sections!r} matched no TestCases. "
                f"Known sections: {known}")

    ids = [tc.qualified_name for _coll_name, tc in cases]
    metafunc.parametrize("eitt_case", [tc for _coll_name, tc in cases],
                        ids=ids)


def pytest_collection_modifyitems(items):
    """Force strict XML-document order within this file's tests, so a
    collection's TestCases run in the exact order state flows between
    them (a `pytest -k` selection that happens to reorder them would
    otherwise silently break the state-dependency chain). Only reorders
    items generated by THIS file — other test modules keep
    whatever order conftest.py's own pytest_collection_modifyitems
    already gave them.
    """
    order = {tc.qualified_name: i for i, (_coll_name, tc) in enumerate(_ALL_CASES)}
    eitt_items = [it for it in items if it.name.startswith("test_eitt[")]
    other_items = [it for it in items if not it.name.startswith("test_eitt[")]
    eitt_items.sort(key=lambda it: order.get(
        it.name[len("test_eitt["):-1], len(order)))
    items[:] = other_items + eitt_items


@pytest.fixture(scope="session")
def _collection_runners(coap):
    """One shared (VariableTable, ProvisioningEngine, SequenceRunner)
    per TestCollection, created lazily the first time a TestCase from
    that collection runs and reused for every subsequent TestCase in
    the same collection (see module docstring for why this is the
    correct state-sharing boundary)."""
    return {}


def _runner_for_collection(collection_name: str, coap, cache: dict
                           ) -> SequenceRunner:
    if collection_name in cache:
        return cache[collection_name]

    coap.post("/test/factory-reset", timeout=5)
    time.sleep(2)
    coap.drain_socket(timeout=0.5)

    vt = VariableTable.for_collection(_PROJECT, collection_name,
                                      overrides=_OVERRIDES.variables)
    engine = ProvisioningEngine(vt)
    runner = SequenceRunner(coap=coap, project=_PROJECT, variables=vt,
                            provisioning=engine, preparations=_PREPARATIONS,
                            overrides=_OVERRIDES)
    cache[collection_name] = runner
    return runner


def test_eitt(eitt_case, coap, _collection_runners):
    """One dynamically generated test per XML TestCase — see
    pytest_generate_tests above for how `eitt_case` is parametrized.

    Runs against a SequenceRunner shared with every other TestCase in
    the same TestCollection, in document order (see module docstring)."""
    override = _OVERRIDES.get(eitt_case.qualified_name)
    if override is not None and override.action == "skip":
        pytest.skip(override.reason or "excluded via eitt/overrides.yaml")

    runner = _runner_for_collection(eitt_case.collection_name, coap,
                                    _collection_runners)

    # F5: the variable table is shared by every TestCase in the collection,
    # so snapshot first and diff — otherwise one case's unresolved name would
    # be re-reported against every case that follows it.
    seen_unresolved = set(runner.variables.unresolved)

    result = runner.run(eitt_case)

    new_unresolved = runner.variables.unresolved - seen_unresolved

    if override is not None and override.action == "xfail":
        if not result.ok:
            pytest.xfail(f"{override.reason}: "
                        f"{result.failure.reason if result.failure else ''}")
        return

    # F5: an unresolved #VAR is substituted as the literal token, so it is
    # compared against an expectation holding the SAME token and matches
    # itself — the assertion silently becomes a tautology. Append it to a
    # real failure as context; report it on its own when the case otherwise
    # "passed", because that pass cannot be trusted.
    unresolved_note = ""
    if new_unresolved:
        unresolved_note = (
            "\n  unresolved #VAR(s): "
            + ", ".join(sorted(new_unresolved))
            + "\n  (an unresolved token matches itself, so any step "
              "asserting on it passed vacuously — check for a renamed "
              "Field in the XML or a missing entry in overrides.yaml)")

    if not result.ok:
        reason = result.failure.reason if result.failure else "unknown failure"
        item = result.failure.item if result.failure else None
        pytest.fail(f"{reason}\n  at sequence item: {item!r}"
                    f"{unresolved_note}")

    if new_unresolved:
        pytest.fail(f"TestCase reported OK but used unresolved variables."
                    f"{unresolved_note}")
