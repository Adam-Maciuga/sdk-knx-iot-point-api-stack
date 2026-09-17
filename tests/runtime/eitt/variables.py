"""
The EITT variable table: #VAR substitution, Fields-default seeding, and
the "shared" table that both EITT (attribute/content text) and Python
hooks (SetVariableTableEntry/GetVariableTableEntry) read and write.

Per the interpreter spec (tests/docs/eitt-xml-interpreter.md §6):
  - Global <Fields> + a TestCollection's own <Fields> seed the table with
    each field's DefaultValue when a TestCase from that collection runs.
  - "#VARNAME" tokens in attribute values and <Content> text are resolved
    against this table.
  - Hooks (hooks.py) read/write the *same* table via get()/set(), mirroring
    EITT's own SetVariableTableEntry/GetVariableTableEntry callback pair —
    this is the mechanism that lets e.g. swu_patch_content() see
    PREP_BLOCK_SIZE (an XML Field) and also write back
    "sequence_loop_limit.<TestCase>" for the runner to read.
"""

from __future__ import annotations

import re
from typing import Optional

from .model import EittProject, FieldDef

# Variable names may start with a digit (e.g. "5_1_1_3_EXPECTED_IF" —
# collection-scoped fields are conventionally named after their spec
# clause), so the leading character class must include digits too.
_VAR_RE = re.compile(r"#([A-Za-z0-9_]+)")


class VariableTable:
    """One shared variable table for a single TestCase run.

    Seed with Fields defaults (global + collection-scoped), then mutate
    freely as @@[pah...] commands and Python hooks run. Values are always
    stored/returned as strings, matching EITT's own GetVariableTableEntry/
    SetVariableTableEntry string contract.
    """

    def __init__(self, fields: list[FieldDef]):
        self._table: dict[str, str] = {}
        self._locked: set[str] = set()
        for f in fields:
            self._table[f.name] = f.default_value
            if f.locked:
                self._locked.add(f.name)

    @classmethod
    def for_collection(cls, project: EittProject,
                       collection_name: str,
                       overrides: Optional[dict] = None) -> "VariableTable":
        """Build the table for one TestCollection, EITT's own way.

        EITT resolves a variable in three layers (see the "How EITT
        generates a runnable test" section of the interpreter doc):
          1. the READ-ONLY conformance-test template's Fields defaults,
          2. the variable-values JSON — GLOBAL block, then the matching
             test-suite block, then that suite's per-collection block,
          3. any later operator edit pushed into the generated copy.
        `overrides` is layer 2/3: values that win over the template
        default, exactly as they do in a generated copy.
        """
        table = cls(project.fields_for_collection(collection_name))
        for name, value in (overrides or {}).items():
            table._table[name] = str(value)
        return table

    def get(self, name: str, default: Optional[str] = None) -> Optional[str]:
        return self._table.get(name, default)

    def require(self, name: str) -> str:
        """Like get(), but raises KeyError with a clear message when the
        variable was never declared/set — mirrors _get_variable_table_entry
        in the ported Python script, which raises LookupError."""
        if name not in self._table:
            raise KeyError(f"Variable not found: {name}")
        return self._table[name]

    def set(self, name: str, value: str) -> None:
        self._table[name] = str(value)

    def clear(self) -> None:
        self._table.clear()
        self._locked.clear()

    def is_locked(self, name: str) -> bool:
        return name in self._locked

    def __contains__(self, name: str) -> bool:
        return name in self._table

    def snapshot(self) -> dict:
        """Read-only copy, for debugging/assertions in tests."""
        return dict(self._table)

    # ------------------------------------------------------------------
    # #VAR substitution
    # ------------------------------------------------------------------

    def substitute(self, text: Optional[str]) -> Optional[str]:
        """Replace every '#VARNAME' token in `text` with its current
        value from the table. Unresolved names are left as-is (with a
        leading '#') rather than raising, so a missing/renamed variable
        surfaces as a visible mismatch in the resulting request/match
        instead of aborting the whole sequence."""
        if text is None:
            return None
        if "#" not in text:
            return text

        def _replace(m: re.Match) -> str:
            name = m.group(1)
            value = self._table.get(name)
            return value if value is not None else m.group(0)

        return _VAR_RE.sub(_replace, text)
