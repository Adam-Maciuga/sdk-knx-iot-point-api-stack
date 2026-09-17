"""
SPAKE2+ handshake special-case.

The EITT XML's SPAKE2+ CoAPMessages at Path=".well-known/knx/spake" are
narrative/illustrative: their <Content> text embeds tool-computed
placeholders (<TOOL.SHARE_P>, <TOOL.CONFIRM_P>) that only EITT's own
closed-source PAKE math can fill in — there is nothing in the XML or the
ported hooks.py that could resolve them generically (see
tests/docs/eitt-xml-interpreter.md §4 item 10). knx_spake2plus.py
already implements the real 3-step exchange and, notably, its
create_*_request() methods return dicts ALREADY keyed with the wire
protocol's integer CBOR keys (0/15, 10, 14) — i.e. they bypass
resource_schema.py entirely, since this one resource's shape is
hardcoded in the crypto client rather than being a generic JSON body.

This module recognizes a SPAKE2+ 3-exchange sequence by Path
(".well-known/knx/spake" appearing on 3 consecutive IN/OUT request-
response pairs) and drives it through Spake2PlusClient instead of the
runner's generic _send_request/_expect_response path. On success it
returns the resulting shared_key/sender_id so the runner can install the
"Temporary Key" OSCORE context via ProvisioningEngine.set_context(),
exactly mirroring what the @@[pahctx"Temporary Key;senderid=..." comment
that immediately follows the real exchange in the XML describes.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

import cbor2

from transport.knx_spake2plus import Spake2PlusClient

SPAKE_PATH = ".well-known/knx/spake"


@dataclass
class SpakeResult:
    shared_key: bytes
    sender_id: str


@dataclass
class SpakeStepFailure:
    """A step's response didn't come back 2.xx (or a deliberately-wrong
    step-3 confirmP was rejected, as expected). There is no shared key —
    the runner matches `response` against the XML's own expected-response
    CoAPMessage for this step (Code/Content) instead of raising, since a
    non-2.xx response is exactly what some TestCases (e.g. "SPAKE2+
    across device reset types", "sending an incorrect confirmP fails")
    intentionally provoke and check the shape of."""
    step: int  # 1, 2, or 3 — which step's response this is
    response: object  # CoapResponse, or None on timeout


def is_spake_message(path: str) -> bool:
    return path.strip("/").lower() == SPAKE_PATH.strip("/").lower()


def run_spake_handshake(coap, password: str, sender_id: str = "TmpTok",
                        timeout: float = 30.0,
                        max_steps: int = 3,
                        override_confirm_p: Optional[bytes] = None
                        ) -> Optional[SpakeResult | SpakeStepFailure]:
    """Drive up to `max_steps` of the 3-step SPAKE2+ exchange against the
    DUT (1, 2, or 3 — however many of the exchange's request messages the
    XML actually Activate="yes"s for this TestCase).

    A TestCase like "5.3.1.2 ... incorrect password fails" only wants to
    exercise steps 1-2 (parameter request, key exchange) — it never sends
    a real confirmP, so running step 3 anyway would either send a bogus
    confirmation the XML never asked for, or (worse) silently succeed
    using the wrong-password math without the test ever checking it. So
    `max_steps` must reflect the XML's own activated-message count, not
    always run the full handshake to completion.

    `override_confirm_p`, when given, replaces the mathematically-correct
    confirmP with this exact byte string in step 3's request — this is
    how a "sending an incorrect confirmP fails" TestCase (the XML writes
    a literal/fixed hex value instead of the "<TOOL.CONFIRM_P>"
    placeholder there) actually exercises the device's confirmP check,
    rather than the handshake self-correcting and succeeding for real.

    Any step's response coming back non-2.xx (e.g. a "SPAKE2+ across
    device reset types" TestCase deliberately sends step 1 while the
    device isn't in default configuration state, expecting 4.00) is NOT
    raised as a failure here — a raw 4xx/5xx response is just as
    legitimate an outcome as the caller's own <Content>/Code expectation
    for that step may want to check. Returns a SpakeStepFailure in that
    case (and for the override_confirm_p rejection) so the runner can
    match it against the XML's own expected-response CoAPMessage exactly
    like any ordinary request/response pair, instead of this helper
    silently assuming success is always wanted.

    Returns None (no shared key — there is none yet) when max_steps < 3.
    """
    spake = Spake2PlusClient(password=password, sender_id=sender_id)

    req1 = cbor2.dumps(spake.create_parameter_request())
    resp1 = coap.post(SPAKE_PATH, payload=req1,
                      content_format=60, timeout=timeout)
    if resp1 is None or not resp1.is_successful:
        return SpakeStepFailure(step=1, response=resp1)
    spake.process_parameter_response(cbor2.loads(resp1.payload))
    if max_steps < 2:
        return None

    req2 = cbor2.dumps(spake.create_key_exchange_request())
    resp2 = coap.post(SPAKE_PATH, payload=req2,
                      content_format=60, timeout=timeout)
    if resp2 is None or not resp2.is_successful:
        return SpakeStepFailure(step=2, response=resp2)
    try:
        spake.process_key_exchange_response(cbor2.loads(resp2.payload))
    except ValueError as e:
        # A wrong password (e.g. a "SPAKE2+ ... incorrect password
        # fails" TestCase) makes the LOCAL confirmV check inside
        # process_key_exchange_response fail deterministically — the
        # client can tell the shared key is wrong without a step-3
        # round trip at all, exactly like a real SPAKE2+ implementation
        # would. When the XML never activates a step-3 message (i.e. it
        # only wants steps 1-2 exercised), this is the expected outcome,
        # not a real handshake bug — so it only re-raises as a failure
        # when the caller actually asked for the full 3-step exchange.
        if max_steps >= 3:
            raise AssertionError(str(e)) from e
        return None
    if max_steps < 3:
        return None

    confirmation_request = spake.create_confirmation_request()
    if override_confirm_p is not None:
        confirmation_request = {14: override_confirm_p}
    req3 = cbor2.dumps(confirmation_request)
    resp3 = coap.post(SPAKE_PATH, payload=req3,
                      content_format=60, timeout=timeout)
    if override_confirm_p is not None:
        # A deliberately wrong confirmP is expected to be REJECTED — the
        # caller (a "sending an incorrect confirmP fails" TestCase) wants
        # to see the device's error response itself via its normal
        # response-matching path, not have this helper raise on it.
        return SpakeStepFailure(step=3, response=resp3)
    if resp3 is None or not resp3.is_successful:
        return SpakeStepFailure(step=3, response=resp3)

    return SpakeResult(shared_key=spake.shared_key, sender_id=sender_id)
