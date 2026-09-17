# Packet capture as an independent test oracle — plan

**Status: PLAN ONLY. Nothing implemented.** Written 2026-07-26 after the
transport audits (see `eitt-interpreter-findings.md`). Pick this up when there is
appetite; nothing here is urgent, but it is the highest-value structural
improvement identified so far.

## Why this exists

The audits found real defects in the CoAP/OSCORE/mDNS transport, and fixing them
raises an uncomfortable question: **how would we know if they came back?**

Today every check runs *through* the code being audited. If `_parse_options` is
wrong, the sending path and the checking path are wrong in the same way, and the
suite still goes green. That circularity is the actual structural weakness — not
any individual finding.

There are also **zero unit tests for 3,311 lines of protocol code**
(`coap_client.py` 1931, `knx_oscore.py` 692, `mdns_client.py` 347,
`knx_spake2plus.py` 334). The only thing exercising the transport is the
conformance suite itself, which passes when the transport is good enough for the
paths *this* template and *this* device happen to take. That is how 176 negative
tests asserted nothing for months.

A packet capture breaks the circularity: it observes the wire, not our model of
the wire.

## Why the conditions here are unusually favourable

- **The veth pair is the only link** between the test tool and the DUT (the DUT
  lives in its own netns — see the topology comment in `run-in-docker.ps1`). So
  `veth-test` sees 100% of the relevant traffic and *zero* foreign noise. No BPF
  filter guesswork, no other hosts.
- The container already runs `--privileged`, so capture needs no permission work.
- The CI image is under our control (confirmed by the user), so tooling can be
  baked in rather than installed per-run.
- We already own **nine reference certification traces** (`data/traces/5_*.xml`)
  with `RawContent` hex to diff against.
- We own the OSCORE keys, so the capture can be decrypted offline — a passive
  observer with the key material sees everything.

## Image changes

Verified against the current image (Debian 13 trixie, Python 3.13.5): **none** of
`tcpdump`, `tshark`, `dumpcap`, `ip` are present today.

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
        iproute2 tcpdump \
    && rm -rf /var/lib/apt/lists/*
```

Rationale:

- **`iproute2` is already installed on every run** by `run-in-docker.ps1`.
  Baking it in removes an `apt-get update` from the hot path — latency we pay
  today for nothing.
- **`tcpdump` is only 2 packages** (~1 MB with `libpcap0.8t64`). Measured.
- **Skip `tshark` — it pulls 30 packages.** Measured. We do not want its
  dissectors anyway: the point is an oracle *independent* of our client, and a
  generic dissector would not understand KNX's OSCORE deviations regardless.
- Baking both in also makes runs reproducible — no mid-run dependency on Debian
  mirrors.

## Wrapper changes (`tools/run-in-docker.ps1`)

1. Drop the runtime `apt-get install iproute2` once the image provides it.
2. Start capture before the suite, stop it after:

   ```sh
   tcpdump -i veth-test -w /w/run.pcap -U -s 0 &
   TCPDUMP_PID=$!
   # ... run pytest ...
   kill -INT $TCPDUMP_PID
   ```

   `-U` (unbuffered) matters: a killed `tcpdump` with a full buffer loses the
   tail, which is exactly the part you want after a failure.
3. Copy the pcap out alongside the pytest output.

**Open decision — always-on vs `-Capture` opt-in.** Recommendation: **always
on.** Traffic is small (the largest section is ~2300 messages) and the single
most frustrating thing in the audit session was intermittent failures with no
record of what was actually on the wire. T2 (the CON-ACK ordering bug) was
*invisible* except as flakiness; a pcap would have made it obvious immediately.

## Analysis: keep it independent, keep it small

**Use `dpkt` or hand-rolled `struct` parsing. Do NOT use `scapy`** (heavy, slow),
and **do NOT import from `transport/`**. The moment the analyser shares code with
`coap_client.py` it stops being an independent oracle and becomes a second copy
of the same assumptions. Ethernet → IPv6 → UDP → CoAP header is ~50 lines.

### Checks, in value order

1. **O1 — no OSCORE nonce reuse.** Extract every `(kid, piv)` from the OSCORE
   option (9) on the wire; assert no pair repeats. **This converts O1 from an
   argument about the code into a measurement of what actually happened** — the
   single most valuable check on the list, and it settles an open finding.

   **O5 rides along here for free.** Partition the same `(kid, piv)` set by
   direction: tool→DUT repeats are O1 (our nonce reuse), DUT→tool repeats are
   O5 (the device replaying). O5 was closed as WONTFIX *in the client* because a
   receive-side replay window would reject telegrams the `SeqNoOffset` tests
   need and would hide a real replay behind a timeout — see the O5 entry in
   `eitt-interpreter-findings.md`. On the wire the same fact becomes a reported
   finding with the offending packet attached, which is what we actually wanted.
   Cost: one extra `if` on a set the analyser already builds.
2. **F12 — response type.** The pcap has the true ACK/NON/CON bit. Compare
   against the template's `Type=` per step, independently of our parser. 1324 of
   1457 active response rows say `ACK`.
3. **T2 — ACK correctness.** Did the ACK go to the address the CON came from?
   Was every CON eventually ACKed? Did the DUT retransmit (implying our ACK was
   missed)?
4. **T4 — token/MID correlation.** The wire shows the true pairing; our client's
   view is the thing under suspicion.
5. **T3 — repeated Uri-Path.** The wire has every path segment; our parser keeps
   only the last.
6. **M12 — foreign query parsed as a response.** The QR bit is right there.
7. **Diff a fresh capture against the matching reference trace**
   (`data/traces/5_X.xml` `RawContent`) — ground truth we already own.

### What this does NOT cover

- **OSCORE plaintext**, unless the analyser is given the keys. Doable (we own
  them) but adds coupling — keep it a separate opt-in step if built at all.
- **Interpreter-level findings** (F5 `#VAR`, F6 ignored attributes, F8 unknown
  input): those are XML-semantics bugs, invisible on the wire.
- It is **a second thing that can be wrong.** Keep the analyser dumb and
  declarative so it does not become its own maintenance burden.

## Suggested order

1. Image change + always-on capture, no analysis. Immediately useful for
   debugging; zero risk. **Worth doing on its own merits even if nothing below
   ever happens.**
2. The O1 nonce-reuse check — small, self-contained, settles an open finding.
3. F12 / T2 / T4 checks as the corresponding fixes land, so each fix has an
   independent witness.
4. Trace diffing last — most work, least incremental value once 1–3 exist.

## Related

- `eitt-interpreter-findings.md` — the findings this is meant to keep fixed.
- `knx-protocol-deviations.md` — what KNX changes vs. stock CoAP/OSCORE/
  SPAKE2+/DNS-SD, and therefore what a pcap analyser must NOT assume from an
  RFC reading (the fixed 2.04 outer response code, the 7-byte sender-ID cap, the
  subtype scheme, …).
- **Library replacement was considered and rejected** (2026-07-26). CoAP/OSCORE
  because 175 active steps require deliberately non-conformant behaviour
  (`SeqNoOffset` ×138, `Echo` ×15, `SeqNoFn` ×14, `InvalidAuthTag` ×4,
  `RxKid`/`RndKid` ×3, `CritOptNo` ×1) — a conformance library's job is to
  prevent exactly those. mDNS was initially recommended for `zeroconf` and then
  **withdrawn**: 16 of 41 `DnsMessage` steps target `_sub` subtypes, and
  `zeroconf` normalises a subtype away (`_pm._sub._knx._udp.local.` resolves to
  `_knx._udp.local.`) because it returns resolved *services*, while 5.1 asserts
  on the raw PTR records, on dynamic subtypes that change mid-run
  (`_ia<iid>-<ia>`), and on ttl=0 goodbyes that `zeroconf` consumes as internal
  cache evictions. A narrower option survives: use `zeroconf`'s `DNSIncoming`
  wire parser as a decoder only, retiring M6/M9/M10/M11/M12 while keeping our own
  listener and matching.
