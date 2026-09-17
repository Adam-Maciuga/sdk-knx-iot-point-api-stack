"""PySide6 front-end for the EITT conformance interpreter.

A SECOND front-end onto the same engine, never a parallel implementation:
it drives `eitt.runner.SequenceRunner` through the `eitt.events` sink, so a
GUI run and a pytest run execute identical code. Keeping that true is the
main design constraint here (GUI plan, "Risks": two front-ends diverging).

Nothing in this package may be imported by the conformance suite — Qt is
not installed in the Docker image, by design.
"""
