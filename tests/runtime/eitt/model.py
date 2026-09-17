"""
Data model + parser for the EITT conformance-test-tool XML format.

Mirrors the document hierarchy documented in
tests/docs/eitt-xml-interpreter.md:

    EITT_KnxConformanceTests
      Fields (global)                         -> list[FieldDef]
      TestCollections
        TestCollection
          Fields (collection-scoped)           -> list[FieldDef]
          TestSuites
            TestSuite
              TestCases
                TestCase
                  Sequence
                    Comment | CoAPMessage | DnsMessage  -> SequenceItem

This module only parses the document into plain dataclasses; it does not
interpret #VAR substitution, Content matching, or @@[pah...] commands —
see variables.py, content_match.py and provisioning.py respectively.
"""

from __future__ import annotations

import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, Union

# The XML declares this namespace on the root element; ElementTree requires
# it to be included in every tag lookup.
NS = "http://knx.org/xml/eitt/4.3"


def _tag(name: str) -> str:
    return f"{{{NS}}}{name}"


def _attr(el: ET.Element, name: str, default: str = "") -> str:
    return el.attrib.get(name, default)


# ---------------------------------------------------------------------------
# Fields (variable declarations)
# ---------------------------------------------------------------------------

@dataclass
class FieldDef:
    """A single <NumberField> or <VariableByteField> variable declaration."""
    name: str
    kind: str  # "NumberField" | "VariableByteField"
    default_value: str
    display_name: str = ""
    comment: str = ""
    format: str = ""
    locked: bool = False
    min_value: Optional[str] = None
    max_value: Optional[str] = None
    min_size: Optional[str] = None
    max_size: Optional[str] = None

    @classmethod
    def from_xml(cls, el: ET.Element) -> "FieldDef":
        kind = el.tag.rsplit("}", 1)[-1]
        return cls(
            name=_attr(el, "Name"),
            kind=kind,
            default_value=_attr(el, "DefaultValue"),
            display_name=_attr(el, "DisplayName"),
            comment=_attr(el, "Comment"),
            format=_attr(el, "Format"),
            locked=_attr(el, "Locked").lower() == "yes",
            min_value=el.attrib.get("MinValue"),
            max_value=el.attrib.get("MaxValue"),
            min_size=el.attrib.get("MinSize"),
            max_size=el.attrib.get("MaxSize"),
        )


def _parse_fields(parent: ET.Element) -> list[FieldDef]:
    fields_el = parent.find(_tag("Fields"))
    if fields_el is None:
        return []
    out = []
    for child in fields_el:
        if child.tag in (_tag("NumberField"), _tag("VariableByteField")):
            out.append(FieldDef.from_xml(child))
    return out


# ---------------------------------------------------------------------------
# Sequence items: Comment, CoAPMessage, DnsMessage
# ---------------------------------------------------------------------------

@dataclass
class URLParameter:
    key: str
    value: str


@dataclass
class Comment:
    id: str
    activate: bool
    text: str

    @classmethod
    def from_xml(cls, el: ET.Element) -> "Comment":
        return cls(
            id=_attr(el, "ID"),
            activate=_attr(el, "Activate", "yes").lower() != "no",
            text=_attr(el, "Text"),
        )


@dataclass
class CoAPMessage:
    id: str
    activate: bool
    time_to_next: str
    cway: str            # "IN" | "OUT"
    wait: bool
    type: str            # "CON" | "ACK" | "NON" | "RST"
    code: str             # method ("GET"/...) or response ("2.05"/...)
    cf: str
    accepted_cf: str
    ip: str
    path: str
    sec_context: str
    secure: Optional[bool]
    comment_attr: str
    content: Optional[str]
    cbor: Optional[str]
    url_parameters: list[URLParameter] = field(default_factory=list)

    # Timing / security knobs
    ack_chk_timeout: Optional[float] = None
    echo: str = "not set"
    echo_value: Optional[str] = None
    seq_no_fn: str = "not set"
    seq_no: Optional[str] = None
    seq_no_offset: Optional[int] = None
    seq_num_method: Optional[str] = None
    rx_kid: Optional[str] = None
    rnd_kid: bool = False
    check_rx_kid: bool = False
    use_last_rx_token: bool = False
    invalid_auth_tag: Optional[str] = None
    observe: Optional[str] = None
    observe_value: Optional[str] = None
    cycles: Optional[str] = None
    crit_opt_no: Optional[str] = None
    crit_opt_value: Optional[str] = None

    # Hook attributes (names of functions in hooks.py)
    eval_fn: Optional[str] = None
    eval_response_fn: Optional[str] = None
    patch_address_fn: Optional[str] = None
    patch_content_fn: Optional[str] = None
    patch_parameter_fn: Optional[str] = None

    @classmethod
    def from_xml(cls, el: ET.Element) -> "CoAPMessage":
        content_el = el.find(_tag("Content"))
        content = content_el.text if content_el is not None else None
        cbor_el = el.find(_tag("Cbor"))
        cbor = cbor_el.text if cbor_el is not None else None

        url_params = [
            URLParameter(key=_attr(p, "Key"), value=_attr(p, "Value"))
            for p in el.findall(_tag("URLParameter"))
        ]

        secure_raw = el.attrib.get("Secure")
        secure = (secure_raw.lower() == "yes") if secure_raw is not None else None

        seq_offset_raw = el.attrib.get("SeqNoOffset")
        seq_offset = int(seq_offset_raw) if seq_offset_raw else None

        ack_timeout_raw = el.attrib.get("AckChkTimeOut")
        ack_timeout = float(ack_timeout_raw) if ack_timeout_raw else None

        return cls(
            id=_attr(el, "ID"),
            activate=_attr(el, "Activate", "yes").lower() != "no",
            time_to_next=_attr(el, "TimeToNext"),
            cway=_attr(el, "CWay"),
            wait=_attr(el, "Wait", "yes").lower() != "no",
            type=_attr(el, "Type"),
            code=_attr(el, "Code"),
            cf=_attr(el, "CF"),
            accepted_cf=_attr(el, "AcceptedCF"),
            ip=_attr(el, "IP"),
            path=_attr(el, "Path"),
            sec_context=_attr(el, "SecContext"),
            secure=secure,
            comment_attr=_attr(el, "Comment"),
            content=content,
            cbor=cbor,
            url_parameters=url_params,
            ack_chk_timeout=ack_timeout,
            echo=_attr(el, "Echo", "not set"),
            echo_value=el.attrib.get("EchoValue"),
            seq_no_fn=_attr(el, "SeqNoFn", "not set"),
            seq_no=el.attrib.get("SeqNo"),
            seq_no_offset=seq_offset,
            seq_num_method=el.attrib.get("SeqNumMethod"),
            rx_kid=el.attrib.get("RxKid"),
            rnd_kid=_attr(el, "RndKid").lower() == "yes",
            check_rx_kid=_attr(el, "CheckRxKid").lower() == "yes",
            use_last_rx_token=_attr(el, "UseLastRxToken").lower() == "yes",
            invalid_auth_tag=el.attrib.get("InvalidAuthTag"),
            observe=el.attrib.get("Observe"),
            observe_value=el.attrib.get("ObserveValue"),
            cycles=el.attrib.get("Cycles"),
            crit_opt_no=el.attrib.get("CritOptNo"),
            crit_opt_value=el.attrib.get("CritOptValue"),
            eval_fn=el.attrib.get("EvalFn") or None,
            eval_response_fn=el.attrib.get("EvalResponseFn") or None,
            patch_address_fn=el.attrib.get("PatchAddressFn") or None,
            patch_content_fn=el.attrib.get("PatchContentFn") or None,
            patch_parameter_fn=el.attrib.get("PatchParameterFn") or None,
        )

    @property
    def is_request(self) -> bool:
        """True if this is a message the tool SENDS ('IN' == into the DUT)."""
        return self.cway == "IN"

    @property
    def is_response(self) -> bool:
        """True if this is a message the tool EXPECTS to RECEIVE."""
        return self.cway == "OUT"


@dataclass
class DnsMessage:
    id: str
    activate: bool
    cway: str
    wait: bool
    type: str            # "dnssd" | "mdns" | "dnsrecord"
    record_type: str
    service: str
    host: str
    port: str
    time_to_next: str
    comment_attr: str
    eval_fn: Optional[str] = None

    @classmethod
    def from_xml(cls, el: ET.Element) -> "DnsMessage":
        return cls(
            id=_attr(el, "ID"),
            activate=_attr(el, "Activate", "yes").lower() != "no",
            cway=_attr(el, "CWay"),
            wait=_attr(el, "Wait", "yes").lower() != "no",
            type=_attr(el, "Type"),
            record_type=_attr(el, "RecordType"),
            service=_attr(el, "Service"),
            host=_attr(el, "Host"),
            port=_attr(el, "Port"),
            time_to_next=_attr(el, "TimeToNext"),
            comment_attr=_attr(el, "Comment"),
            eval_fn=el.attrib.get("EvalFn") or None,
        )


SequenceItem = Union[Comment, CoAPMessage, DnsMessage]


def _parse_sequence(seq_el: ET.Element) -> list[SequenceItem]:
    items: list[SequenceItem] = []
    for child in seq_el:
        if child.tag == _tag("Comment"):
            items.append(Comment.from_xml(child))
        elif child.tag == _tag("CoAPMessage"):
            items.append(CoAPMessage.from_xml(child))
        elif child.tag == _tag("DnsMessage"):
            items.append(DnsMessage.from_xml(child))
    return items


# ---------------------------------------------------------------------------
# TestCase / TestSuite / TestCollection
# ---------------------------------------------------------------------------

@dataclass
class TestCase:
    id: str
    name: str
    version_suffix: str
    sequence: list[SequenceItem] = field(default_factory=list)

    # Filled in by the loader once the surrounding hierarchy is known.
    suite_name: str = ""
    collection_name: str = ""

    @property
    def qualified_name(self) -> str:
        """Stable dotted id used for pytest test-item ids / -k selection."""
        return f"{self.collection_name}::{self.suite_name}::{self.name}"

    @classmethod
    def from_xml(cls, el: ET.Element) -> "TestCase":
        seq_el = el.find(_tag("Sequence"))
        sequence = _parse_sequence(seq_el) if seq_el is not None else []
        return cls(
            id=_attr(el, "ID"),
            name=_attr(el, "Name"),
            version_suffix=_attr(el, "VersionSuffix"),
            sequence=sequence,
        )


@dataclass
class TestSuite:
    id: str
    name: str
    comment: str
    test_cases: list[TestCase] = field(default_factory=list)

    @classmethod
    def from_xml(cls, el: ET.Element) -> "TestSuite":
        cases = []
        cases_el = el.find(_tag("TestCases"))
        if cases_el is not None:
            for case_el in cases_el.findall(_tag("TestCase")):
                cases.append(TestCase.from_xml(case_el))
        return cls(
            id=_attr(el, "ID"),
            name=_attr(el, "Name"),
            comment=_attr(el, "Comment"),
            test_cases=cases,
        )


@dataclass
class TestCollection:
    id: str
    name: str
    comment: str
    fields: list[FieldDef] = field(default_factory=list)
    test_suites: list[TestSuite] = field(default_factory=list)

    @classmethod
    def from_xml(cls, el: ET.Element) -> "TestCollection":
        suites = []
        suites_el = el.find(_tag("TestSuites"))
        if suites_el is not None:
            for suite_el in suites_el.findall(_tag("TestSuite")):
                suites.append(TestSuite.from_xml(suite_el))
        return cls(
            id=_attr(el, "ID"),
            name=_attr(el, "Name"),
            comment=_attr(el, "Comment"),
            fields=_parse_fields(el),
            test_suites=suites,
        )


@dataclass
class EittProject:
    """Root of a parsed EITT conformance-test XML document."""
    id: str
    name: str
    version: str
    global_fields: list[FieldDef] = field(default_factory=list)
    collections: list[TestCollection] = field(default_factory=list)

    def iter_test_cases(self):
        """Yield every TestCase in document order, with suite_name and
        collection_name filled in."""
        for coll in self.collections:
            for suite in coll.test_suites:
                for tc in suite.test_cases:
                    tc.suite_name = suite.name
                    tc.collection_name = coll.name
                    yield coll, suite, tc

    def fields_for_collection(self, collection_name: str) -> list[FieldDef]:
        """Global Fields plus the named collection's own Fields — the
        full variable scope visible to TestCases inside that collection."""
        out = list(self.global_fields)
        for coll in self.collections:
            if coll.name == collection_name:
                out.extend(coll.fields)
                break
        return out


def parse_eitt_xml(path: str | Path) -> EittProject:
    """Parse an EITT conformance-test XML file into an EittProject."""
    tree = ET.parse(path)
    root = tree.getroot()

    collections = []
    collections_el = root.find(_tag("TestCollections"))
    if collections_el is not None:
        for coll_el in collections_el.findall(_tag("TestCollection")):
            collections.append(TestCollection.from_xml(coll_el))

    return EittProject(
        id=_attr(root, "ID"),
        name=_attr(root, "Name"),
        version=_attr(root, "Version"),
        global_fields=_parse_fields(root),
        collections=collections,
    )
