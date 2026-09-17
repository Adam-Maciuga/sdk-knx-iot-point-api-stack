"""
EITT XML interpreter — dynamically drives KNX IoT conformance tests
straight from the EITT conformance-test-tool XML project file, instead
of hand-porting each EITT revision into static pytest source.

Package layout:
    model.py          Dataclasses mirroring the XML document hierarchy,
                       and the parser that builds them from an .xml file.
    variables.py       #VAR substitution and the Fields/variable-table model.
    content_match.py   The <Content> matching mini-language (literal,
                       wildcard, /path: value, all:/opt:-prefixed regex).
    hooks.py           Ported EvalFn/EvalResponseFn/PatchXxxFn callbacks
                       from KnxConformanceTestTemplate-PointAPI.py.
    provisioning.py    The "@@[pahxxx" comment-command engine (device,
                       multicast, OSCORE-context, discovery provisioning).
    runner.py          The sequence player: walks a TestCase's Sequence
                       and drives coap_client.CoapClient / knx_oscore.
                       OscoreContext accordingly.

See tests/docs/eitt-xml-interpreter.md for the full design notes and the
EITT semantics this package is reverse-engineered from.
"""
