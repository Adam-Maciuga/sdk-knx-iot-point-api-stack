---
name: feedback-count-it-in-the-template
description: "When a claim about what EITT does is in play — the user's or mine — count it in the template XML before agreeing or disagreeing; aggregates mislead, one query settles it"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 4799ccc5-4c9c-4bdd-98d6-09eef940b849
  modified: 2026-07-27T14:41:08.385Z
---

When any claim about EITT's behaviour comes up — the user's, mine, or one
inherited from a doc — **count it in the template before responding**. Not
to doubt the user; to get the exact shape, which is usually sharper than
either of us stated it.

Three times in one session (2026-07-27) a single `ElementTree` query
settled a design question in seconds, and **every time it corrected me**:

| Claim | What counting showed |
| --- | --- |
| "a copy gets its preparation prepended" | Wrong. 63 of 193 cases reference it inline via `@#"..."`, one at index **371 of 429** — prepending would run it at the wrong point |
| "copying 5.1 pulls in nothing" (mine) | Wrong. 5.1.1.1, 5.1.2.2, 5.1.2.4 all reference preparations. I had inferred it from aggregate counts |
| "only telegrams have Active" (user's) | **Right, and sharper**: 3109/3109 CoAPMessage + 41/41 DnsMessage carry it, vs **2 of 3409** Comments |

**Why:** the template is 6,559 sequence items. Intuitions formed from
reading a few TestCases, or from totals, do not survive contact with it —
but it is small enough that any structural question is one query away.

**How to apply:** write the query to a scratchpad `.py` file rather than
`python -c` (quoting eats output, and Bash silently returned nothing twice).
**The namespace is `{http://knx.org/xml/eitt/4.3}`** — a `4.2` guess parses
fine and matches zero elements, which reads identically to "the attribute is
never used". Verify the tag counts are non-zero before believing a zero.

Corollary: a parser DEFAULT is not evidence about the XML.
`model.py` defaults a missing `Activate` to yes, which is right for
execution but made 3407 comments look like they carried one. When asking
"does the XML say X", read the XML, not the parsed object.

Related: [[project-eitt-gui-plan]],
[[feedback-eitt-check-template-and-trace]],
[[feedback-eitt-comment-commands-are-behavioural]].
