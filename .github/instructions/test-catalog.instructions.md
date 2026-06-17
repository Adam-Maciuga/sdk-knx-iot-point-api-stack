---
description: "Always keep tests/TEST_CATALOG.md up-to-date when adding, removing, or renaming tests."
applyTo: "tests/unit/**,tests/runtime/**"
---

`tests/TEST_CATALOG.md` is the single source of truth for the test inventory.
Whenever you add, remove, rename, or re-scope a test file or test function,
update the catalog in the same change (before committing):

- Add a section (or table rows) for any new test file, listing every test
  function with its EITT reference or spec clause. Mark non-EITT,
  spec-backed approximations clearly.
- Adjust the totals in the header (`N unit tests`, `M runtime tests`, file
  counts, and the combined total) and the Part 2 intro counts so they stay
  accurate.
- Record any `xfail`/`skip` and the reason in the relevant table cell and, if
  durable, in the "Known Skips" / "Known Bugs" sections.
- Keep test names in the catalog byte-for-byte identical to the source.
