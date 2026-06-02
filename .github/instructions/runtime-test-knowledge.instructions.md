---
description: "Keep tests/docs/runtime-test-knowledge.md up-to-date when modifying runtime test infrastructure."
applyTo: "tests/runtime/**,security/oc_oscore_engine.c,security/oc_oscore_crypto.c,security/oc_oscore_context.c"
---

When modifying files in `tests/runtime/`, or OSCORE/SPAKE2+ related files in `security/`,
update `tests/docs/runtime-test-knowledge.md` with any new findings, bug fixes, or
architectural changes before committing.
