# EITT coverage mapping — moved

This file used to carry a hand-maintained map of EITT test-case IDs to
hand-written runtime tests. It was unreliable: it claimed 179 IDs taken from
`tests/EITT_REFERENCE_PROJECT/EittProject.xml`, a path that does not exist in
this tree, and it omitted both `5.3.14.1` and the whole `5.4.2` suite, which
the template does contain. Do not resurrect it.

**The mapping now lives in two places:**

1. `tests/docs/runtime-test-inventory.md` — the per-test classification
   (COVERED / COVERED-WEAK / KEEP) against the template, section 3.
2. The `eitt_overlap` markers in `tests/runtime/runtime_tests/` — every
   hand-written test that overlaps a template case names that case in code.

The authoritative list of case IDs is the template itself,
`tests/runtime/data/KnxConformanceTestTemplate-PointAPI.xml` (193 `TestCase`
elements, namespace `http://knx.org/xml/eitt/4.3`), which the eitt tier
executes directly — count it there rather than trusting any doc.

On **2026-09-11** every hand-written test whose requirement a template case
already covers was removed from the runtime tier; those requirements are now
exercised by the eitt tier. The full per-test mapping of what was removed and
to which EITT case is in the commit message of
"runtime tier: drop template-covered tests, mark overlaps".

To list the overlaps that remain:

```
pytest tests/runtime/runtime_tests -m eitt_overlap --collect-only -q
```
