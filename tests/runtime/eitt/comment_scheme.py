"""Reader for EITT's own comment-command grammar.

`data/EITT_KnxCommentCommandsScheme.xml` is EITT's authoritative description
of the `@`-command language: 27 commands with Command/Format/Category and
short+long descriptions. It ships with the tool, so it — not our reading of
the template — is what settles what a command means.

Two consumers:
  - the GUI's Telegram Properties pane, which shows Command/Description/
    Format for whatever comment row is selected (GUI plan phase 1);
  - anyone diagnosing an unrecognised `@`-command, which the runner now
    fails loudly on rather than passing silently (findings F7/F8).

Deliberately Qt-free and dependency-free so it can be used from a test or a
one-off script, not just the GUI.

Note the matching subtlety this module exists to get right: commands share
prefixes (`@`, `@@`, `@@!`, `@@[`, `@[w`), so a naive startswith() scan
returns `@` for everything. `lookup()` resolves LONGEST-FIRST.
"""

from __future__ import annotations

import os
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from functools import lru_cache
from typing import Optional

_NS = "{http://knx.org/xml/eitt/4.2}"

# Spellings the TEMPLATE uses that the SCHEME file does not list.
#
# The conformance template predates the scheme's naming and still writes
# `@@[MDNS"rqcheck=..."` (16 uses) where the scheme documents `@@[pahmdns`.
# Without this the prefix search falls back to plain `@@` — the generic
# "suspend evaluation" operator prompt — and the properties pane would
# describe an mDNS request-check toggle as a modal dialog.
#
# ProvisioningEngine already treats the two as one command
# (`_apply_pahmdns = _apply_mdns`, provisioning.py); this keeps the
# DISPLAY in step with the EXECUTION rather than letting them disagree.
_ALIASES = {
    "@@[MDNS": "@@[pahmdns",
}

_DEFAULT_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data", "EITT_KnxCommentCommandsScheme.xml")


@dataclass(frozen=True)
class CommentCommand:
    """One row of EITT's comment-command table."""

    command: str
    name: str = ""
    category: str = ""
    format: str = ""
    description: str = ""       # DescrShort
    description_long: str = ""  # DescrLong, often empty
    takes_body: bool = False    # Body="y" — the command carries free text
    takes_param: bool = False   # Param="y" — the command takes "..." params

    @property
    def summary(self) -> str:
        """Short label for a properties pane header."""
        return f"{self.command} — {self.description}" if self.description \
            else self.command


class CommentCommandScheme:
    """The parsed command table, with prefix lookup."""

    def __init__(self, commands: list[CommentCommand],
                 categories: Optional[dict[str, str]] = None) -> None:
        self.commands = commands
        self.categories = categories or {}
        # Longest command string first. `@@!` must win over `@@` and `@`,
        # and `@@[` over `@@` — sorting by length once here is what makes
        # lookup() correct rather than order-of-declaration dependent.
        self._by_length = sorted(commands, key=lambda c: len(c.command),
                                 reverse=True)
        self._by_command = {c.command: c for c in commands}

    def lookup(self, text: str) -> Optional[CommentCommand]:
        """The command a comment's raw text invokes, or None if the text is
        not an `@`-command at all (plain prose in a Comment element)."""
        if not text:
            return None
        stripped = text.strip()

        # Template-only spellings first: an alias is by definition longer
        # and more specific than the scheme entry it would otherwise fall
        # back to (`@@[MDNS` vs bare `@@`), so it has to win.
        for alias, canonical in _ALIASES.items():
            if stripped.startswith(alias):
                found = self._by_command.get(canonical)
                if found is not None:
                    return found

        for cmd in self._by_length:
            if cmd.command and stripped.startswith(cmd.command):
                return cmd
        return None

    def category_name(self, index: str) -> str:
        return self.categories.get(str(index), "")

    def __len__(self) -> int:
        return len(self.commands)

    def __iter__(self):
        return iter(self.commands)


def parse_comment_scheme(path: Optional[str] = None) -> CommentCommandScheme:
    root = ET.parse(path or _DEFAULT_PATH).getroot()

    categories = {
        el.attrib.get("Index", ""): el.attrib.get("Name", "")
        for el in root.findall(f".//{_NS}Category")
    }

    commands = []
    for el in root.findall(f".//{_NS}CommentCommand"):
        get = el.attrib.get
        commands.append(CommentCommand(
            command=get("Command", ""),
            name=get("Name", ""),
            category=categories.get(get("Category", ""), ""),
            format=get("Format", ""),
            description=get("DescrShort", ""),
            description_long=get("DescrLong", ""),
            takes_body=get("Body", "n").lower() == "y",
            takes_param=get("Param", "n").lower() == "y",
        ))
    return CommentCommandScheme(commands, categories)


@lru_cache(maxsize=4)
def get_comment_scheme(path: Optional[str] = None) -> CommentCommandScheme:
    """Cached accessor — the file is small but a GUI would otherwise
    reparse it on every selection change."""
    return parse_comment_scheme(path)
