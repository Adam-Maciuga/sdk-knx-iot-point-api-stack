---
name: feedback-changelog-only-on-releases
description: "Never add entries to CHANGELOG.md in the stack repo as part of a feature/cleanup change — the changelog is written only at release time"
metadata:
  type: feedback
---

**Do not touch `CHANGELOG.md` in `knx-iot-point-api-stack` when implementing
a change.** The changelog is updated only when a release is cut, by the
maintainer, from the commit history.

**Why:** stated 2026-09-03 after the TCP/TLS transport removal added an
"Unreleased" section listing the removed public API. The user reverted it:
"the changelog is only updated on releases". Per-change entries create
merge noise and duplicate what git history already records.

**How to apply:**

- Put the description of removed/changed public API in the commit message,
  not in the changelog.
- If a change is release-worthy, mention it in the final report so the
  maintainer can pick it up at release time; do not write it yourself.
- Applies to agents you dispatch too: tell implementer agents explicitly
  "do not edit CHANGELOG.md".
