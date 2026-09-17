---
name: feedback-memory-lives-in-the-repo
description: "Project memory MUST be written to tests/docs/memory/ in the repo, never only to ~/.claude — the user works from several machines and a local-only note is invisible everywhere else"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 4799ccc5-4c9c-4bdd-98d6-09eef940b849
  modified: 2026-07-29T00:00:00.000Z
---

**Write project memory to `tests/docs/memory/` in the
`knx-iot-point-api-stack` repo, and commit it.** The per-machine store under
`~/.claude/projects/…/memory/` is a MIRROR, not the source of truth.

**Why:** the user works from several machines. A note written only to
`~/.claude` exists on exactly one of them — so the next session on a
different machine starts blind, re-derives what was already settled, and
risks re-litigating decisions that were explicitly closed. Stated
2026-07-29, after 16 files had accumulated in the local-only store.

The repo is also where this project's durable knowledge already lives
(`tests/docs/eitt-*.md`), so memory sitting outside it was the odd one out.

**How to apply:**

- New or updated memory → write it under `tests/docs/memory/` and commit it
  in the same change as the work it describes.
- Keep `tests/docs/memory/MEMORY.md` as the index; one line per file.
- The `~/.claude` copy may be kept in sync for the local recall mechanism,
  but it is never the only copy. If the two disagree, **the repo wins** —
  it is the one that has seen other machines.
- Same rule as the findings tracker
  ([[reference-eitt-interpreter-findings]]): record it in the repo, in the
  same commit as the fix. Nothing important should live only in a chat
  reply or only on one laptop.
