"""Tests for building a working project out of a template selection.

Pure parsing/graph work — no DUT, no Qt. The behaviour that matters is
dependency resolution: a copy that silently omits a referenced preparation
looks complete in the tree and then fails at run time, which is exactly the
failure mode this module exists to prevent.
"""

from __future__ import annotations

import os

import pytest

from eitt.model import parse_eitt_xml
from eitt.project_copy import cases_in, copy_scope, iter_references

TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "data", "KnxConformanceTestTemplate-PointAPI.xml")


@pytest.fixture(scope="module")
def project():
    return parse_eitt_xml(TEMPLATE)


def _collection(project, name):
    return next(c for c in project.collections if c.name == name)


def test_references_are_found_where_they_sit(project):
    """The reference is a comment INSIDE the sequence, not a header — one
    lives at index 371 of 429. Finding them means scanning the whole
    sequence, not peeking at the first few items."""
    case = next(tc for _c, _s, tc in project.iter_test_cases()
                if tc.name.startswith("5.1.1.1."))
    assert list(iter_references(case)) == [
        "Copy of AUTH PREPARATION",
        "Copy of IA PREPARATION",
        "Copy of TABLE PREPARATION",
    ]


def test_reference_resolves_through_the_copy_of_prefix(project):
    """The XML references "Copy of AUTH PREPARATION"; only "AUTH
    PREPARATION" exists. Copy resolution must strip the prefix exactly as
    PreparationRegistry.resolve() does, or every copy would report its
    preparations as unresolved."""
    names = {tc.name for _c, _s, tc in project.iter_test_cases()}
    assert "AUTH PREPARATION" in names
    assert "Copy of AUTH PREPARATION" not in names

    case = next(tc for _c, _s, tc in project.iter_test_cases()
                if tc.name.startswith("5.1.1.1."))
    result = copy_scope(project, [case])
    assert result.unresolved == []
    assert "AUTH PREPARATION" in result.dependencies


@pytest.mark.parametrize("collection,expected_deps", [
    ("5.1. Discovery", {"AUTH PREPARATION", "IA PREPARATION",
                        "TABLE PREPARATION"}),
    ("5.3. Security", {"AUTH PREPARATION", "IA PREPARATION",
                       "TABLE PREPARATION"}),
    # 5.2 additionally pulls the two upload-block sequences, which is the
    # transitive case: they are reached from the firmware-update TestCases.
    ("5.2. /.well-known/knx", {"AUTH PREPARATION", "IA PREPARATION",
                               "TABLE PREPARATION",
                               "Upload Block - First Half",
                               "Upload Block - Second Half"}),
])
def test_copying_a_collection_pulls_its_preparations(project, collection,
                                                     expected_deps):
    source = _collection(project, collection)
    result = copy_scope(project, cases_in(source))

    assert set(result.dependencies) == expected_deps
    assert result.unresolved == []
    assert result.case_count == len(cases_in(source)) + len(expected_deps)


def test_preparations_land_as_siblings_not_prepended(project):
    """Preparation is REFERENCED at the point the test needs it, so a copy
    must not concatenate it into the copied cases. It belongs in its own
    collection, exactly where the template keeps it."""
    source = _collection(project, "5.3. Security")
    original_lengths = {c.name: len(c.sequence) for c in cases_in(source)}

    result = copy_scope(project, cases_in(source))

    names = [c.name for c in result.project.collections]
    assert "Test Sequence Preparation" in names
    assert "5.3. Security" in names

    # No copied case grew — nothing was spliced into a sequence.
    for _c, _s, case in result.project.iter_test_cases():
        if case.name in original_lengths:
            assert len(case.sequence) == original_lengths[case.name]


def test_copy_preserves_document_order_and_structure(project):
    source = _collection(project, "5.3. Security")
    result = copy_scope(project, cases_in(source))

    copied = _collection(result.project, "5.3. Security")
    assert [s.name for s in copied.test_suites] == \
        [s.name for s in source.test_suites]


def test_copy_carries_collection_fields(project):
    """Fields hold the collection's variable scope — VariableTable
    .for_collection() reads them. A copy without them loses every #VAR
    default and would fail on the first substitution."""
    source = _collection(project, "5.3. Security")
    result = copy_scope(project, cases_in(source))
    copied = _collection(result.project, "5.3. Security")

    assert len(copied.fields) == len(source.fields)
    assert result.project.global_fields == project.global_fields


def test_copy_is_deep(project):
    """Edits to a copy must never reach the master — that is the whole
    point of the read-only template."""
    source = _collection(project, "5.1. Discovery")
    original = cases_in(source)[0]
    result = copy_scope(project, [original])

    copied = next(c for _a, _b, c in result.project.iter_test_cases()
                  if c.name == original.name)
    assert copied is not original

    before = original.sequence[0].activate
    copied.sequence[0].activate = not before
    assert original.sequence[0].activate is before


def test_copying_a_single_case_is_the_same_operation(project):
    """cases_in() flattens any node, so right-clicking a collection and
    right-clicking one case go through one code path."""
    case = next(tc for _c, _s, tc in project.iter_test_cases()
                if tc.name.startswith("5.1.1.1."))
    result = copy_scope(project, cases_in(case))
    assert result.case_count == 1 + len(result.dependencies)


def test_unresolved_reference_is_reported_not_raised(project):
    """A dangling reference is data about an incomplete project, not a
    crash — the runner reports it per-step, and the GUI should surface it."""
    from eitt.model import Comment, TestCase

    orphan = TestCase(id="x", name="ORPHAN", version_suffix="",
                      sequence=[Comment(id="c", activate=True,
                                        text='@#"NO SUCH SEQUENCE"')])
    result = copy_scope(project, [orphan])
    assert result.unresolved == ["NO SUCH SEQUENCE"]
    assert result.dependencies == []


def test_self_reference_terminates(project):
    """A sequence that calls itself must not recurse forever.

    Built by mutating a REAL template case: copy_scope assembles the result
    by walking the template, so a synthetic TestCase that is not in it
    resolves to nothing and would not exercise the cycle at all.
    """
    from eitt.model import Comment

    victim = next(tc for _c, _s, tc in project.iter_test_cases()
                  if tc.name.startswith("5.1.1.1."))
    marker = Comment(id="cycle", activate=True,
                     text=f'@#"{victim.name}"')
    victim.sequence.append(marker)
    try:
        result = copy_scope(project, [victim])       # must terminate
    finally:
        victim.sequence.remove(marker)

    # The case is present once, and did not list itself as a dependency.
    assert victim.name not in result.dependencies
    assert result.case_count == 1 + len(result.dependencies)


def test_case_outside_the_template_is_not_invented(project):
    """copy_scope assembles the copy by walking the template, so a case
    that is not in it contributes nothing. Documenting this because the
    GUI always passes real nodes — and a future caller that does not
    should see the behaviour stated rather than be surprised."""
    from eitt.model import Comment, TestCase

    stranger = TestCase(id="x", name="NOT IN TEMPLATE", version_suffix="",
                        sequence=[Comment(id="c", activate=True, text="hi")])
    assert copy_scope(project, [stranger]).case_count == 0
