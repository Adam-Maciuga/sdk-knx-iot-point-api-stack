"""
Ported EvalFn / EvalResponseFn / PatchAddressFn / PatchContentFn /
PatchParameterFn callbacks from
tests/runtime/data/KnxConformanceTestTemplate-PointAPI.py.

Every function here corresponds 1:1 (by name) to a hook the real EITT
tool invokes via the matching XML attribute (see
tests/docs/eitt-xml-interpreter.md §5) — the runner looks up hook names
found on a <CoAPMessage> in HOOKS and calls them with the same signature
the original script used, only rebased onto a HookContext object instead
of bare module globals, so state is scoped per TestCase run rather than
leaking across the whole test session like the original script's globals
did.

Deliberately NOT ported (confirmed dead / out of scope for this XML by
the interpreter spec's Python-hook cross-reference, §5):
  - eval_pointapi_test: defined in the source script but never referenced
    by any EvalFn/EvalResponseFn attribute in the XML.
  - The entire kip_* family (KNXnet/IP-flavored helpers): no kip_* string
    appears anywhere in KnxConformanceTestTemplate-PointAPI.xml; they
    belong to a different/sibling conformance template that happens to
    share the .py file.

Each hook receives a HookContext carrying:
  - variables: the active VariableTable (mirrors EITT's
    SetVariableTableEntry/GetVariableTableEntry channel into the
    original script's `_PointApiVariables` dict).
  - state: a plain dict for cross-call Python-only state that never has
    a `#VAR` life in the XML (mirrors the original script's module
    globals: original_fingerprint*, mdns_ip/mdns_port, swu_file_bytes,
    last_sent_byte_index, last_sent_block_number).

Signatures mirror the original functions:
  EvalFn(ctx, planned, response) -> bool
  EvalFn(ctx, response) -> bool                      (DNS single-arg form)
  EvalResponseFn(ctx, planned, response) -> bool
  PatchAddressFn(ctx, device) -> str
  PatchContentFn(ctx, old_content) -> str | False
  PatchParameterFn(ctx, old_parameter) -> str | False

`response`/`planned` are strings, exactly as EITT passes them in the
original script (each hook that needs structure calls json.loads itself).
"""

from __future__ import annotations

import binascii
import json
import logging
import os
import re
from dataclasses import dataclass, field
from typing import Any, Optional

from .variables import VariableTable


@dataclass
class HookContext:
    """Per-TestCase-run state passed to every hook call."""
    variables: VariableTable
    state: dict = field(default_factory=dict)
    last_error: str = ""

    def error(self, msg: str) -> None:
        self.last_error = msg
        logging.debug(msg)


# ---------------------------------------------------------------------------
# 5.2.4.1 — fingerprint distinctness chain (steps 1/3/5/7/9)
# ---------------------------------------------------------------------------
# Ported from eval_5_2_4_1_step_1/3/5/7/9 (PointAPI.py lines 208-371).
# Each step reads the response's "value" field and asserts it differs
# from every fingerprint captured by an earlier step in the same chain.
# State keys: fp_0 (step 1), fp_3, fp_5, fp_7, fp_9.

def _fingerprint_step(ctx: HookContext, response: str,
                      this_key: str, prior_keys: list[str]) -> bool:
    try:
        value = json.loads(response)["value"]
    except (ValueError, KeyError, TypeError) as e:
        ctx.error(f"_fingerprint_step({this_key}): {type(e).__name__}: {e}")
        return False

    ctx.state[this_key] = value

    prior_values = {k: ctx.state.get(k) for k in prior_keys}
    if any(v is None for v in prior_values.values()):
        ctx.error(f"Some fingerprints have no value yet: {prior_values}")
        return False
    if value in prior_values.values():
        ctx.error(f"Fingerprint {this_key}={value!r} equals an earlier "
                  f"step's fingerprint: {prior_values}")
        return False
    return True


def eval_5_2_4_1_step_1(ctx: HookContext, planned: str, response: str) -> bool:
    try:
        ctx.state["fp_1"] = json.loads(response)["value"]
        for k in ("fp_3", "fp_5", "fp_7", "fp_9"):
            ctx.state.pop(k, None)
        return True
    except (ValueError, KeyError, TypeError) as e:
        ctx.error(f"eval_5_2_4_1_step_1: {type(e).__name__}: {e}")
        return False


def eval_5_2_4_1_step_3(ctx: HookContext, planned: str, response: str) -> bool:
    return _fingerprint_step(ctx, response, "fp_3", ["fp_1"])


def eval_5_2_4_1_step_5(ctx: HookContext, planned: str, response: str) -> bool:
    return _fingerprint_step(ctx, response, "fp_5", ["fp_1", "fp_3"])


def eval_5_2_4_1_step_7(ctx: HookContext, planned: str, response: str) -> bool:
    return _fingerprint_step(ctx, response, "fp_7", ["fp_1", "fp_3", "fp_5"])


def eval_5_2_4_1_step_9(ctx: HookContext, planned: str, response: str) -> bool:
    return _fingerprint_step(ctx, response, "fp_9",
                             ["fp_1", "fp_3", "fp_5", "fp_7"])


def set_original_fingerprint(ctx: HookContext, planned: str,
                             response: str) -> bool:
    try:
        ctx.state["original_fingerprint"] = json.loads(response)["value"]
        return True
    except (ValueError, KeyError, TypeError) as e:
        ctx.error(f"set_original_fingerprint: {type(e).__name__}: {e}")
        return False


def eval_compare_with_original_fingerprint(ctx: HookContext, planned: str,
                                           response: str) -> bool:
    try:
        value = json.loads(response)["value"]
    except (ValueError, KeyError, TypeError) as e:
        ctx.error(f"eval_compare_with_original_fingerprint: "
                  f"{type(e).__name__}: {e}")
        return False
    original = ctx.state.get("original_fingerprint")
    if value == original:
        return True
    ctx.error(f"Fingerprints are not equal. Original: {original!r}, "
             f"got: {value!r}")
    return False


# ---------------------------------------------------------------------------
# 5.1.2.1 — mDNS SRV/AAAA record capture + address patch
# ---------------------------------------------------------------------------
# Ported from pointapi_eval_5_1_2_1_get_port/get_address and
# patch_address_5_1_2_1 (PointAPI.py lines 409-487).

NO_SRV_RECORD_RECEIVED = "No SRV record was received."
NO_AAAA_RECORD_RECEIVED = "No AAAA record was received."
DNS_PLANNED_AND_RECEIVED_PORTS_DIFFER = "Planned and received ports differ."


def pointapi_eval_5_1_2_1_get_port(ctx: HookContext, response: str) -> bool:
    try:
        j_value = json.loads(response)
        if j_value["dns"]["received"]["type"] != "SRV":
            ctx.error(NO_SRV_RECORD_RECEIVED)
            return False
        if (j_value["dns"]["planned"]["service"]
                != j_value["dns"]["received"]["service"].rstrip(".")):
            ctx.error("Service error.")
            return False

        rcv_port = j_value["dns"]["received"]["port"]
        planned_port = j_value["dns"]["planned"]["port"]
        if planned_port != 0 and planned_port != rcv_port:
            ctx.error(DNS_PLANNED_AND_RECEIVED_PORTS_DIFFER)
            return False

        ctx.state["mdns_port"] = str(rcv_port)
        return True
    except (ValueError, KeyError, TypeError) as e:
        ctx.error(f"pointapi_eval_5_1_2_1_get_port: {type(e).__name__}: {e}")
        return False


def pointapi_eval_5_1_2_1_get_address(ctx: HookContext, response: str) -> bool:
    try:
        j_value = json.loads(response)
        if j_value["dns"]["received"]["type"] != "AAAA":
            ctx.error(NO_AAAA_RECORD_RECEIVED)
            return False
        ctx.state["mdns_ip"] = str(j_value["dns"]["received"]["host"])
        return True
    except (ValueError, KeyError, TypeError) as e:
        ctx.error(
            f"pointapi_eval_5_1_2_1_get_address: {type(e).__name__}: {e}")
        return False


def patch_address_5_1_2_1(ctx: HookContext, device: str) -> str:
    ip = ctx.state.pop("mdns_ip", "")
    port = ctx.state.pop("mdns_port", "")
    return f"[{ip}]:{port}"


# ---------------------------------------------------------------------------
# 5.10.1.1 — IPv6-address-shaped value regex check
# ---------------------------------------------------------------------------
# Ported from eval_5_10_1_1_IPv6_adresses (PointAPI.py lines 660-691).

_IPV6_HEX_RE = re.compile(r"h'[0-9a-fA-F]{32}'")


def eval_5_10_1_1_IPv6_adresses(ctx: HookContext, planned: str,
                                response: str) -> bool:
    try:
        j_response = json.loads(response)
    except ValueError as e:
        ctx.error(f"eval_5_10_1_1_IPv6_adresses: {type(e).__name__}: {e}")
        return False

    def _check(obj) -> bool:
        value = obj["value"]
        if not _IPV6_HEX_RE.match(value):
            ctx.error(f"The value '{value}' has an invalid format.")
            return False
        return True

    if isinstance(j_response, dict):
        return _check(j_response)
    if isinstance(j_response, list):
        return all(_check(o) for o in j_response)
    return True


# ---------------------------------------------------------------------------
# 5.2.9.1 — Software Update block-transfer patch functions
# ---------------------------------------------------------------------------
# Ported from swu_patch_content(_continue) / swu_patch_parameter(_continue)
# (PointAPI.py lines 499-657). These drive the "Upload Block - First/
# Second Half" loop TestCases — see runner.py for the loop-count/
# loop-limit protocol (tests/docs/eitt-xml-interpreter.md §7).
#
# ctx.state keys: swu_bytes, swu_cursor, swu_block_number.

def _swu_load_file(ctx: HookContext) -> bytes:
    file_name = ctx.variables.require("PREP_UPDATE_FILE_PATH")
    with open(_resolve_data_path(file_name), "rb") as f:
        data = f.read()
    ctx.state["swu_bytes"] = data
    return data


def _resolve_data_path(file_name: str) -> str:
    """Find a data file whatever the working directory is.

    `PREP_UPDATE_FILE_PATH` is `tests/runtime/data/FWUUploadDummySmall.bin`
    — relative to the REPO ROOT, which is where the Docker harness runs
    pytest from. The GUI runs from `tests/runtime/`, so the same path
    resolves to nothing and the SWU hooks died with a FileNotFoundError
    (and then a confusing `KeyError: 'swu_cursor'`, because the first hook
    aborted before setting the state the next one reads).

    Try the path as given, then the same name relative to this package's
    parent — which IS `tests/runtime`. Absolute paths are untouched.
    """
    if os.path.isabs(file_name) or os.path.exists(file_name):
        return file_name

    runtime_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    # Strip a leading `tests/runtime/` if the value carries one, so the
    # repo-root-relative form and a bare `data/...` both work.
    trimmed = file_name.replace("\\", "/")
    for prefix in ("tests/runtime/", "./"):
        if trimmed.startswith(prefix):
            trimmed = trimmed[len(prefix):]
            break
    candidate = os.path.join(runtime_dir, trimmed)
    return candidate if os.path.exists(candidate) else file_name


def _swu_loop_count(ctx: HookContext, loop_name: str) -> int:
    return int(ctx.variables.get(f"sequence_loop_count.{loop_name}", "1"))


def swu_patch_content(ctx: HookContext, old_content: str,
                      loop_name: str = "Copy of Upload Block - First Half"
                      ) -> Any:
    try:
        loop_count = _swu_loop_count(ctx, loop_name)
        block_size = int(ctx.variables.require("PREP_BLOCK_SIZE"))

        if loop_count == 1:
            ctx.state["swu_cursor"] = 0
            data = _swu_load_file(ctx)
            file_size = len(data)
            loop_limit = int(int(file_size / block_size) / 2)
            ctx.variables.set(f"sequence_loop_limit.{loop_name}",
                              str(loop_limit))
            start = 0
        else:
            start = ctx.state["swu_cursor"] + 1

        data = ctx.state["swu_bytes"]
        file_size = len(data)
        end = start + block_size
        if end > file_size:
            end = start + (file_size % block_size)

        ctx.state["swu_cursor"] = end - 1

        hex_data = binascii.hexlify(data[start:end]).decode("utf-8")
        return " ".join(hex_data[i:i + 2] for i in range(0, len(hex_data), 2))
    except Exception as e:  # noqa: BLE001 — mirror original catch-all
        ctx.error(f"swu_patch_content: {type(e).__name__}: {e}")
        return False


def swu_patch_parameter(ctx: HookContext, old_parameter: str,
                        loop_name: str = "Copy of Upload Block - First Half"
                        ) -> Any:
    try:
        loop_count = _swu_loop_count(ctx, loop_name)
        block_size = int(ctx.variables.require("PREP_BLOCK_SIZE"))

        if loop_count == 1:
            data = ctx.state.get("swu_bytes") or _swu_load_file(ctx)
            file_size = len(data)
            ctx.state["swu_block_number"] = 0
            return f"?ps={block_size}&pkgs={file_size}"

        ctx.state["swu_block_number"] = ctx.state.get("swu_block_number", 0) + 1
        block_number = ctx.state["swu_block_number"]
        return f"?po={block_number * block_size}&ps={block_size}"
    except Exception as e:  # noqa: BLE001
        ctx.error(f"swu_patch_parameter: {type(e).__name__}: {e}")
        return False


def swu_patch_content_continue(
        ctx: HookContext, old_content: str,
        loop_name: str = "Copy of Upload Block - Second Half") -> Any:
    try:
        loop_count = _swu_loop_count(ctx, loop_name)
        block_size = int(ctx.variables.require("PREP_BLOCK_SIZE"))
        data = ctx.state["swu_bytes"]
        file_size = len(data)
        cursor = ctx.state["swu_cursor"]
        remaining = file_size - (cursor + 1)

        if remaining > block_size:
            loop_limit = int(remaining / block_size) + loop_count
            if (remaining % block_size) == 0:
                loop_limit -= 1
            ctx.variables.set(f"sequence_loop_limit.{loop_name}",
                              str(loop_limit))

        start = cursor + 1
        end = start + block_size
        if end > file_size:
            end = start + (file_size % block_size)

        ctx.state["swu_cursor"] = end - 1

        hex_data = binascii.hexlify(data[start:end]).decode("utf-8")
        return " ".join(hex_data[i:i + 2] for i in range(0, len(hex_data), 2))
    except Exception as e:  # noqa: BLE001
        ctx.error(f"swu_patch_content_continue: {type(e).__name__}: {e}")
        return False


def swu_patch_parameter_continue(ctx: HookContext, old_parameter: str
                                 ) -> Any:
    block_size = int(ctx.variables.require("PREP_BLOCK_SIZE"))
    ctx.state["swu_block_number"] = ctx.state.get("swu_block_number", 0) + 1
    block_number = ctx.state["swu_block_number"]

    cursor = ctx.state["swu_cursor"]
    start = cursor + 1
    end = start + block_size
    data = ctx.state["swu_bytes"]
    file_size = len(data)
    if end > file_size:
        end = start + (file_size % block_size)

    return f"?po={block_number * block_size}&ps={end - start}"


# ---------------------------------------------------------------------------
# Hook registry — looked up by name from CoAPMessage/DnsMessage attributes.
# ---------------------------------------------------------------------------

HOOKS: dict[str, Any] = {
    "eval_5_2_4_1_step_1": eval_5_2_4_1_step_1,
    "eval_5_2_4_1_step_3": eval_5_2_4_1_step_3,
    "eval_5_2_4_1_step_5": eval_5_2_4_1_step_5,
    "eval_5_2_4_1_step_7": eval_5_2_4_1_step_7,
    "eval_5_2_4_1_step_9": eval_5_2_4_1_step_9,
    "set_original_fingerprint": set_original_fingerprint,
    "eval_compare_with_original_fingerprint":
        eval_compare_with_original_fingerprint,
    "pointapi_eval_5_1_2_1_get_port": pointapi_eval_5_1_2_1_get_port,
    "pointapi_eval_5_1_2_1_get_address": pointapi_eval_5_1_2_1_get_address,
    "patch_address_5_1_2_1": patch_address_5_1_2_1,
    "swu_patch_content": swu_patch_content,
    "swu_patch_parameter": swu_patch_parameter,
    "swu_patch_content_continue": swu_patch_content_continue,
    "swu_patch_parameter_continue": swu_patch_parameter_continue,
    "eval_5_10_1_1_IPv6_adresses": eval_5_10_1_1_IPv6_adresses,
}


def get_hook(name: Optional[str]):
    """Look up a ported hook by its XML attribute value.

    Raises KeyError (not a silent None) when the XML references a hook
    name this module hasn't ported — that must surface as a loud failure
    during test generation/collection, not a quietly-skipped step, so a
    future EITT XML revision that introduces a new hook can't pass
    silently degraded.
    """
    if name is None:
        return None
    try:
        return HOOKS[name]
    except KeyError:
        raise KeyError(
            f"No ported hook named '{name}' in eitt/hooks.py — this "
            "XML revision uses a Python callback that hasn't been "
            "ported yet.") from None
