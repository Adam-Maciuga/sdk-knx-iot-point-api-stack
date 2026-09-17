---
name: feedback-optional-dep-test-guards
description: "Guarding a test on an optional dep: importorskip is not enough (breaks on a broken install), and never hardcode cwd-relative data paths — the Docker harness runs pytest from the repo root"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 4799ccc5-4c9c-4bdd-98d6-09eef940b849
  modified: 2026-07-27T12:38:07.406Z
---

Two traps when adding a test file that depends on something the Docker
conformance image does not have (hit both while building the GUI, 2026-07-27).

**1. `pytest.importorskip` only handles ABSENT, not BROKEN.**

It converts `ModuleNotFoundError` into a skip. An installed-but-unusable
package — Qt missing system libs, the classic slim-container failure —
raises a plain `ImportError`, which propagates and **fails collection for
the entire session**, taking the whole conformance suite with it. Use:

```python
try:
    from PySide6.QtWidgets import QApplication
except ImportError as exc:
    pytest.skip(f"GUI extra not usable: {exc}", allow_module_level=True)
```

Verified by stubbing a `PySide6` that raises `ImportError`: was
"1 error during collection, exit 2", became "9 passed, 1 skipped, exit 0".

**2. Never hardcode a cwd-relative data path in a test.**

`TEMPLATE = "data/KnxConformanceTestTemplate-PointAPI.xml"` works locally
(pytest run from `tests/runtime/`) and fails in Docker, which invokes pytest
from the **repo root**. Resolve against the test file instead:

```python
TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "data", "KnxConformanceTestTemplate-PointAPI.xml")
```

This cost a full 9-minute Docker run that reported 5 ERRORs — which look
alarming next to a green suite but were pure path breakage.

**How to apply:** after adding any test file, run it BOTH ways before
trusting it — `pytest tests/runtime/test_x.py` from the repo root, and via
[[feedback-eitt-always-run-in-docker]]. A local-only pass proves nothing
about the harness. Related: [[project-eitt-gui-plan]].
