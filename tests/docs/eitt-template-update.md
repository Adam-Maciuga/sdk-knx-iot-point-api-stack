# Adopting a new EITT conformance template revision

How to move the eitt tier from one revision of KNX's conformance template to
the next. The tier generates one pytest item per `TestCase` in the XML, so a
new revision is a drop-in file, not a porting job. What takes the time is
everything around the file: the companions EITT ships with it, the overrides
that are keyed by case name, the reference traces, and the baselines that
record the expected tally.

The current template is version 40 of "8/10/5 - KNX IoT Point API Tests",
schema namespace `http://knx.org/xml/eitt/4.3`.

## What a new revision consists of

EITT installs these files together. Collect all of them from the new EITT
release, not just the XML:

| File in `tests/eitt/data/` | Where it comes from | Who reads it here |
| --- | --- | --- |
| `KnxConformanceTestTemplate-PointAPI.xml` | the template itself, read-only | `interpreter/model.py`, the GUI |
| `KnxConformanceTestTemplate-PointAPI.py` | EITT's hook script (`Eval*`, `Patch*` callbacks) | nobody at run time; `interpreter/hooks.py` is a 1:1 port of it |
| `EITT_KnxPointAPIResourceKeyMaps.xml` | EITT's JSON <-> CBOR key maps | `interpreter/resource_schema.py` |
| `EITT_KnxCommentCommandsScheme.xml` | the grammar of every `@` / `@@[` comment command | `interpreter/comment_scheme.py`, `provisioning.py` |
| `<date>_EittConformanceTestVariableValues.json` | exported from the running EITT tool for the new suite version | nobody at run time; it is the reference for the `variables:` section of `interpreter/overrides.yaml` |
| `traces/5_*.xml` | runs recorded by the real tool against a device | you, when a failure is disputed |

The template is the only one of these that must never be hand-edited. Replacing
it wholesale with the next official revision is the intended way to change it.

## Step by step

### 1. Diff the template first

Before copying anything, diff the old XML against the new one. The
`<History>` block at the top lists every change with a version, a date and a
one-line description. The diff of the `TestCase` elements is the work list for
everything below: renamed or renumbered cases, new cases, changed messages,
new XML attributes, new comment commands.

Check the root element:

- `xmlns` must still be `http://knx.org/xml/eitt/4.3`. If the schema moved,
  `NS` in `interpreter/model.py` has to follow, and the parser needs a review
  against the new schema.
- `Version` and `MinEITTVersion` are worth noting in the commit message.

### 2. Replace the template and its companions

Copy the new XML over `tests/eitt/data/KnxConformanceTestTemplate-PointAPI.xml`.

Then the companions:

- **Hook script.** Diff EITT's new `KnxConformanceTestTemplate-PointAPI.py`
  against the one in `data/` and port every new or changed function into
  `interpreter/hooks.py`. The port is one to one by function name; the runner
  looks the name found on a `<CoAPMessage>` up in `HOOKS`. A hook the
  interpreter does not know makes the case fail, it is never skipped
  silently. Copy the new script into `data/` as well, it is the source of
  truth for the port.
- **Key maps and comment scheme.** Replace both XML files if they changed. A
  new key in the maps needs nothing else. A new comment command is
  behavioural, never a no-op: read its entry in the scheme and implement it
  in `interpreter/provisioning.py`.
- **Variable values.** In the EITT tool, select the new suite and export the
  variable values JSON. The file is layered (`GLOBAL`, then the suite keyed
  by GUID, name and version, then per collection), so the new suite version
  gets its own block. Drop the export into `data/` next to the old one and
  compare the two. Wherever a default changed, update the `variables:`
  section of `interpreter/overrides.yaml`. No code reads the JSON; the
  `variables:` section is our copy of its content. The three-layer model
  behind this is described in
  `tests/docs/memory/reference-eitt-three-layer-variable-model.md`.

### 3. Run the harness tier on the host

```
pytest tests/eitt/harness_tests -q
```

No device, no Docker, a minute or two. This is where the mechanical breakage
shows up:

- Parse errors in the new XML.
- Every `overrides.yaml` key that no longer names a real `TestCase` is a
  collection error naming the key. Renamed or renumbered cases surface here.
  `message_activation` and `message_attributes` entries that match no message
  are hard errors as well, on purpose: a stale entry would leave the
  template's own branch or a corrupt value in place and fail somewhere far
  from the cause.
- Harness tests that pin template facts, for example a case count or a
  specific message in a specific case.

Fix the keys, then re-run until collection is clean.

### 4. Run the eitt tier in Docker

```powershell
.\tests\common\tools\run-in-docker.ps1 -Tier eitt -IncludeUncommitted
```

Always in Docker. A host-native run fails in ways that look like code
regressions. Read the failures per section, never one case in isolation: a
single `TestCase` run alone skips its sibling preparations and fabricates
"access denied" style failures. Use `-Section 5.6`, not `-k 5.6`, because `-k`
is a substring match and also selects 5.5.5.6.

For every failing section, in this order:

1. **Template and trace.** Read the exact `TestCase` in the new XML, its
   `Activate` state, and the reference trace in `data/traces/` for that
   section. A case the tool itself skips or answers differently is not a
   device gap.
2. **Accepted certification deviations.**
   `tests/docs/memory/reference-eitt-certification-accepted-deviations.md`
   lists the official certification setup and the per-section exceptions
   (4.00 versus 4.03, telegram enable/disable, default-value corrections,
   the 5.10 multi-IP setup). Check it before calling anything a bug.
3. **Count it in the template.** Before agreeing with any claim about what the
   template does, count it in the XML. A parser default is not evidence.

Each failure ends in one of four buckets:

| Bucket | What to do |
| --- | --- |
| Device gap | Fix the stack, or file it in `tests/FINDINGS.md` with the case ids. |
| Template data flaw, a wrong attribute value | `message_attributes:` entry in `overrides.yaml` with a mandatory `reason`. Only attributes the interpreter maps onto a message field are accepted. |
| Operator choice between two alternative branches | `message_activation:` entry. |
| Interpreter gap, an attribute or command the fail-closed gate rejects | Implement it in the interpreter, add an entry to `tests/docs/eitt-interpreter-findings.md`, update it in the same commit as the fix. |

The gate that produces the last bucket is `_unsupported_attributes` in
`interpreter/runner.py`: a message carrying an attribute the interpreter cannot
honour fails instead of being sent approximately. Expect new template features
to land here first.

A case that cannot run yet gets an `action: skip` override with a reason; a
case that runs but is known blocked gets `action: xfail`. Both are visible in
the tally, neither is silent.

### 5. Record new traces where the template changed

The traces in `data/traces/` are the arbitration evidence when a failure is
disputed, and the ones there were recorded against version 40. For every
section the new revision changed, record a run with the real EITT tool against
a device and replace the section's trace file. Keep the old file until the new
one exists, a trace of the previous revision beats no trace.

### 6. Check the runtime tier for new overlap

The hand-written tier in `tests/runtime/` exists only for behaviour the template
does not cover. A new revision may cover something a hand-written test also
checks. For every new or changed `TestCase`, look for a runtime test on the same
behaviour. Delete the hand-written one, or keep it with an
`@pytest.mark.eitt_overlap("5.x.y.z")` marker if it asserts materially more than
the template does. `tests/runtime/README.md` has the rule and the marker.

### 7. Update the baselines

When the tier is green, or every remaining failure is a filed finding, update
what records the expected tally:

- The comment on the `eitt-gcc` job in `.gitlab-ci.yml`, which names the
  expected passed, skipped and xfailed counts.
- The status block in `tests/docs/memory/project-eitt-5-3-status.md`.
- The template version and case counts in `tests/docs/eitt-xml-interpreter.md`
  and `tests/docs/runtime-test-inventory.md`.

The strings such as "Copy of 5.2 V40" in the GUI harness tests are sample
names, not template facts, and can stay.

### 8. Commit in two parts

1. The template, its companions, the new variable-values export and the new
   traces, named after the template version, for example "EITT template V41
   with EITT 4.5 companions".
2. Interpreter, hook and override changes, with the tracker and baseline
   updates.

Kept apart, the adoption of the official files stays reviewable on its own and
the interpreter changes can be read against the template diff.

## Checklist

- [ ] Old and new XML diffed, `<History>` read, namespace unchanged
- [ ] Template replaced
- [ ] Hook script diffed and ported, copied into `data/`
- [ ] Key maps and comment scheme replaced if changed, new commands implemented
- [ ] Variable values exported, `variables:` in `overrides.yaml` updated
- [ ] Harness tier green on the host
- [ ] EITT tier run in Docker, every failure in a bucket
- [ ] New traces recorded for changed sections
- [ ] Runtime tier checked for new overlap
- [ ] Baselines updated: CI comment, memory status, design doc, inventory
- [ ] Two commits
