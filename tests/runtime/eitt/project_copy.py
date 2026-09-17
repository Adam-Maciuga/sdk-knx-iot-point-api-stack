"""Build a runnable working project from a selection of template cases.

Phase 1b of the GUI plan. The master template is a read-only SOURCE; you
copy a scope out of it to get something editable and launchable, mirroring
EITT's own "Copy of 5.4.1. POST /k V40" alongside the original.

Qt-free on purpose: the copy rules are the part worth testing, and they are
useful from a script (generating a project for a single collection is a
reasonable CLI thing to want).

## Preparation is REFERENCED, not prepended

Verified against the template 2026-07-27, and the reason this module is more
than a `deepcopy`:

- The 4 preparation sequences live in ONE dedicated collection; no 5.x
  collection carries its own.
- 63 of 193 TestCases reach them through an inline `@#"..."` comment
  **inside their own sequence**, at the point the preparation is needed —
  only 7 of 131 references sit near the top, and one is at index 371 of 429.
  Concatenating preparation at the front would run it at the wrong moment
  and change what the test does.
- The referenced name is `"Copy of AUTH PREPARATION"`, which does NOT exist
  in the template — only `AUTH PREPARATION` does. That is EITT telling us
  its workflow: template cases are written to reference the OPERATOR's
  copies. `PreparationRegistry.resolve()` compensates by stripping the
  `Copy of ` prefix.

So a copy takes the selected cases **verbatim** and brings whichever
preparations they reference along **as siblings**, names unchanged —
resolution is by name, so renaming one breaks every reference to it.
"""

from __future__ import annotations

import copy
import re
from dataclasses import dataclass
from typing import Iterable, Optional

from .model import Comment, EittProject, TestCase, TestCollection, TestSuite

# `@#"NAME"` — inline call of another sequence.
# `@>w"NAME;count"` — parallel/loop invocation of one.
# Both create a by-name dependency a copy has to satisfy.
_INLINE_REF_RE = re.compile(r'^@#"([^"]+)"')
_LOOP_REF_RE = re.compile(r'^@>w"([^;"]+)[;"]')

_COPY_PREFIX = "Copy of "


def iter_references(test_case: TestCase) -> Iterable[str]:
    """Names of the sequences this TestCase calls into."""
    for item in test_case.sequence:
        if not isinstance(item, Comment):
            continue
        text = item.text.strip()
        match = _INLINE_REF_RE.match(text) or _LOOP_REF_RE.match(text)
        if match:
            yield match.group(1)


def _candidate_names(reference: str) -> list[str]:
    """The names a reference may resolve to, most specific first.

    Mirrors `PreparationRegistry.resolve()`: the XML says
    `Copy of AUTH PREPARATION` but only `AUTH PREPARATION` exists, so the
    bare name is the fallback. Keeping the two in step matters — a copy that
    satisfied a different set of names than the runner resolves would look
    complete and then fail at run time.
    """
    if reference.startswith(_COPY_PREFIX):
        return [reference, reference[len(_COPY_PREFIX):]]
    return [reference]


@dataclass
class CopyResult:
    """A working project plus what had to be pulled in to make it run."""

    project: EittProject
    #

    # Preparation TestCase names brought along to satisfy references.
    dependencies: list[str]
    # References nothing in the template could satisfy. Not fatal — the
    # runner reports it per-step — but the GUI should surface it, because
    # the usual cause is copying a scope out of a project that was already
    # incomplete.
    unresolved: list[str]
    case_count: int


def _find_case(project: EittProject, name: str) -> Optional[tuple]:
    for coll, suite, case in project.iter_test_cases():
        if case.name == name:
            return coll, suite, case
    return None


def copy_scope(project: EittProject, selected: Iterable[TestCase],
               name: str = "") -> CopyResult:
    """Build a new EittProject containing `selected` plus the preparations
    they reference.

    Structure is preserved: a copied case keeps its suite, and that suite
    keeps its collection, so the copy's tree looks like the region of the
    template it came from. Dependencies land in whatever collection/suite
    they occupied in the template (the "Test Sequence Preparation"
    collection), NOT merged into the copied scope — they are siblings.
    """
    selected = list(selected)
    wanted: dict[int, TestCase] = {id(c): c for c in selected}

    # Resolve references transitively: a preparation may itself call another.
    # `seen` guards the self-referential case (a sequence that calls itself
    # would otherwise recurse forever).
    dependencies: list[str] = []
    unresolved: list[str] = []
    seen: set[str] = {c.name for c in selected}
    queue = list(selected)
    while queue:
        case = queue.pop()
        for reference in iter_references(case):
            for candidate in _candidate_names(reference):
                if candidate in seen:
                    break
                found = _find_case(project, candidate)
                if found is None:
                    continue
                seen.add(candidate)
                dependencies.append(candidate)
                wanted[id(found[2])] = found[2]
                queue.append(found[2])
                break
            else:
                if reference not in unresolved:
                    unresolved.append(reference)

    # Walk the template in document order and keep the wanted cases, so the
    # copy's ordering matches the source rather than selection order.
    collections: list[TestCollection] = []
    for coll in project.collections:
        suites: list[TestSuite] = []
        for suite in coll.test_suites:
            cases = [copy.deepcopy(c) for c in suite.test_cases
                     if id(c) in wanted]
            if cases:
                suites.append(TestSuite(id=suite.id, name=suite.name,
                                        comment=suite.comment,
                                        test_cases=cases))
        if suites:
            collections.append(TestCollection(
                id=coll.id, name=coll.name, comment=coll.comment,
                # Fields carry the collection's variable scope
                # (VariableTable.for_collection reads them), so a copy
                # without them would lose every #VAR default.
                fields=list(coll.fields), test_suites=suites))

    copied = EittProject(
        id=project.id, name=name or f"Copy of {project.name}",
        version=project.version,
        global_fields=list(project.global_fields),
        collections=collections)

    return CopyResult(project=copied, dependencies=dependencies,
                      unresolved=unresolved,
                      case_count=sum(1 for _ in copied.iter_test_cases()))


def cases_in(node) -> list[TestCase]:
    """Every TestCase under a tree node, whatever its level.

    Lets the GUI treat "right-clicked a collection" and "right-clicked a
    single case" as the same operation.
    """
    if isinstance(node, TestCase):
        return [node]
    if isinstance(node, TestSuite):
        return list(node.test_cases)
    if isinstance(node, TestCollection):
        return [c for s in node.test_suites for c in s.test_cases]
    if isinstance(node, EittProject):
        return [c for _coll, _s, c in node.iter_test_cases()]
    return []
