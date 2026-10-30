"""Stdio MCP bridge for the Luma DEVELOPMENT addon backend (Source/Core/includes/mcp_server.inl).

A Development build serves "\\\\.\\pipe\\luma-mcp-<pid>" from its load, and runs the requests at present.
This process advertises the tools up front, connects lazily (and reconnects after game restarts),
and post-processes readbacks: decodes DXGI formats, computes channel statistics and writes a PNG preview.

Needs Python 3 with numpy. Register it in .mcp.json, with the repository as the working directory:
  "luma": { "type": "stdio", "command": "python", "args": ["Scripts/luma_mcp.py"] }
"""

import collections
import difflib
import glob
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import threading
import time
import zlib

import numpy as np

PIPE_DIR = "\\\\.\\pipe\\"
PIPE_PREFIX = "luma-mcp-"
# Dual era: modern requests carry their version in "_meta" (stateless), legacy clients open with "initialize"
MODERN_VERSIONS = ("2026-07-28",)
LEGACY_VERSIONS = ("2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05")
META = "io.modelcontextprotocol/"
SERVER_INFO = {"name": "luma", "version": "1"}
LIST_TTL_MS = 3_600_000  # The tool list is fixed for the process lifetime

# Every tool also takes "pid" (pick a game when several run) and "timeout_ms" (the backend's default and clamp).
TIMEOUT_MS, TIMEOUT_MS_RANGE = 15000, (100, 600000)
COMMON = {
    "pid": {"type": "integer", "description": "Game process id, only needed when several Luma games run (see luma_list_games)"},
    "timeout_ms": {"type": "integer", "description": f"How long to wait for the game (default {TIMEOUT_MS})"},
}
TARGET = {
    "index": {"type": "integer", "description": "Entry index from luma_trace_list (preferred)"},
    "hash": {"type": "string", "description": "Shader hash (0x12345678), used when no index is given"},
    "instance": {"type": "integer", "description": "Nth draw of that shader in a frame, with hash (default 0)"},
}
# Computed by this bridge on the read back pixels, never sent to the game
ANALYSIS = {
    "preview": {"type": "boolean", "description": "Write a PNG preview (default true)"},
    "region": {"type": "array", "items": {"type": "integer"}, "description": "[x, y, w, h]: stats inside it and outside it (e.g. the render scale sub-rect)"},
    "row_bands": {"type": "integer", "description": "Split the height in N bands: exact-zero fraction and mean per band (flicker/black stripes)"},
    "laplacian": {"type": "boolean", "description": "Variance of the Laplacian of mean(rgb), a sharpness measure (inside/outside region too)"},
    "threshold": {"type": "number", "description": "Count values whose magnitude (first 2 channels, e.g. motion vectors) is above it"},
}
READ_TARGET = {
    "view": {"type": "string", "description": "rtv (default), srv, uav, depth, stencil, swapchain, ui, registered:<name>"},
    "slot": {"type": "integer", "description": "View slot (default 0)"},
    "mip": {"type": "integer", "description": "Mip level (default 0)"},
    "replaced": {"type": "boolean", "description": "Read the bindings of Luma's replaced pass (default true) or the original ones"},
}
COMPARE_OPTIONS = {
    "region": ANALYSIS["region"],
    "rect_a": {"type": "array", "items": {"type": "integer"}, "description": "[x, y, w, h] of a to compare"},
    "rect_b": {"type": "array", "items": {"type": "integer"}, "description": "[x, y, w, h] of b to compare"},
    "metrics": {"type": "array", "items": {"type": "string"},
                "description": "Per image on the plain mean of the first 3 channels (stored encoding, not luminance): mean, below:<t> (fraction "
                               "under t, e.g. deep AO), detail (mean |x - 2x2 box average|); correlation (Pearson, a vs b); and flip "
                               "(NVIDIA FLIP of b against reference a, x100: HDR-FLIP for float readbacks, LDR-FLIP otherwise, + error map png)"},
}
OUT_DIR = {"out_dir": {"type": "string", "description": "%TEMP%\\luma-mcp (default) or a subfolder of it"}}  # The backend refuses anything else
BRIDGE_ONLY_ARGS = set(ANALYSIS) | {"rows", "save_as"}


# Captures and readbacks leave only artifacts in %TEMP%\luma-mcp
READ_ONLY = {"readOnlyHint": True, "openWorldHint": False}
# Live state changes (not saved to the config) or a dump file
LIVE_WRITE = {"readOnlyHint": False, "destructiveHint": False, "idempotentHint": True, "openWorldHint": False}
ONE_SHOT = {**LIVE_WRITE, "idempotentHint": False}  # Some knobs fire once
# Writes into the baselines folder, replacing what is there
REPLACES_FILES = {**LIVE_WRITE, "destructiveHint": True}


def tool(name, description, properties=None, required=None, annotations=READ_ONLY, common=COMMON):
    schema = {"type": "object", "properties": {**(properties or {}), **common}}
    if required:
        schema["required"] = required
    return {"name": name, "description": description, "inputSchema": schema, "annotations": annotations}


TOOLS = [
    tool("luma_list_games", "List running games that expose the Luma dev MCP pipe (Development builds only).", common={}),
    tool("luma_status", "Game, resolutions, swapchain formats (real, after Luma upgrades), mod state, replaced shader count, compilation error flag, process address space use (x86 OOM watch). "
         "Answers even when the game stopped presenting (presenting=false, ms_since_present, pending_jobs, active_tool): the other tools would time out then."),
    tool("luma_log", "Tail of the game's ReShade.log (where Luma and the game mods log, e.g. the per 300 frames motion vector stats), optionally filtered by a regex.",
         {"pattern": {"type": "string", "description": "Python regex, lines matching it are kept"},
          "lines": {"type": "integer", "description": "Last N (matching) lines, default 200"},
          "log_path": {"type": "string", "description": "Explicit log file (works without a running game)"}}),
    tool(
        "luma_trace_capture",
        "Capture one frame with Luma's own frame trace. Unlike RenoDX DevKit it includes Luma's injected passes as type custom entries "
        "(Luma SMAA, RCAS, bloom, display composition, redirected copies/UI and the game mod's own passes, with the state they drew with like "
        "shader entries (stage, luma_shader); DLSS/FSR draws and phase markers by name only; a pass "
        "replacing a game draw, e.g. XeGTAO, is a note on that draw), "
        "merged deferred context work, clears/copies, and which shader variant ran. With 'trigger' it keeps capturing until a frame contains that shader. "
        "With 'frames' it captures several frames and returns draws per shader hash per frame (a capture spans two presents). "
        "'Did X draw at all' over a long window: trigger=<hash> max_frames=<N> (trigger_found).",
        {"trigger": {"type": "string", "description": "Shader hash that must appear in the captured frame"},
         "max_frames": {"type": "integer", "description": "Frames to try with trigger (default 120, max 3600)"},
         "frames": {"type": "integer", "description": "Consecutive captures to count draws per hash over (no trigger, max 600)"},
         "top": {"type": "integer", "description": "With frames: most drawn hashes returned (default 64)"},
         "summary": {"type": "boolean", "description": "With frames: frames_seen/min/max per hash instead of the per frame array (long windows)"},
         "save_as": {"type": "string", "description": "Store the capture's entry list under this name for luma_trace_diff"}},
    ),
    tool(
        "luma_trace_diff",
        "Diff two captures stored with luma_trace_capture save_as (e.g. SR off vs on): aligned inserted/removed/changed blocks, and shaders whose draw counts differ.",
        {"a": {"type": "string", "description": "save_as name of the first capture"}, "b": {"type": "string", "description": "save_as name of the second capture"},
         "max_blocks": {"type": "integer", "description": "Default 60"}}, ["a", "b"], common={},
    ),
    tool(
        "luma_trace_list",
        "List entries of the last capture (compact). Hides VS entries, bindings and buffer writes by default (like the dev UI). "
        "Fields: index, type, stage, hash, replaced (none/file/patch_*), variant (what ran for a cloned shader: original/file/patch, original when a "
        "recipe resource was missing or the game picked it), cancelled_or_replaced (the draw callbacks didn't let the draw through), updated (they changed its bindings, see luma_trace_get), upgrade_flags (^ input upgraded, v output upgraded, \\ / scaled), rt0, depth, "
        "copy_kind, note (the game's own per draw decision, e.g. mv / jitter / unpatched / mv_reject=N).",
        {"type": {"type": "string", "description": "shader, custom, copy, clear, present, cpu_read, cpu_write, ..."},
         "variant": {"type": "string", "description": "Only shader entries that ran this variant: original, file, patch"},
         "cancelled_only": {"type": "boolean", "description": "Only shader entries the draw callbacks cancelled or replaced"},
         "hash": {"type": "string", "description": "Only entries of this shader (0x12345678)"}, "stage": {"type": "string", "description": "PS, VS, CS, GS"},
         "resource": {"type": "string", "description": "Resource id (the 'resource' field of luma_trace_get) read or written by the entry"},
         "replaced_only": {"type": "boolean", "description": "Only entries whose shader Luma replaced or patched"},
         "include_vs": {"type": "boolean", "description": "Also list vertex shader entries"},
         "include_bindings": {"type": "boolean", "description": "Also list resource binding entries"},
         "include_buffer_writes": {"type": "boolean", "description": "Also list buffer map/update entries"},
         "offset": {"type": "integer", "description": "First filtered entry returned, for paging"}, "limit": {"type": "integer", "description": "Default 300"}},
    ),
    tool(
        "luma_trace_get",
        "Full state of one captured entry: RTV/SRV/UAV/DSV resource and view formats and sizes, blend per RT, depth/stencil (func, write, stencil ref/ops), "
        "rasterizer (cull, scissor enable, depth clip/bias), all viewports and scissor rects, samplers (filter, address, mip bias), bound cbuffers (with D3D11.1 ranges), "
        "input layout, draw/dispatch arguments, copy kind/regions (copy, region, resolve, buffer), the source/target resources of copies, clears, CPU writes and Luma passes, clear values, custom shader file and compilation errors, "
        "the recipes of the shader's current patch clone, a Luma pass's shader file (luma_shader) and compilation errors, and replaced_slots: "
        "the views, cbuffers and samplers the game mod's draw callback added, replaced or unbound (the rest of the entry is what the game bound). "
        "The state is recorded before the game mod's draw hook runs: a viewport it scales, a blend or shader it swaps in are not shown "
        "(read the result with luma_read_resource instead).",
        {"index": {"type": "integer", "description": "Entry index from luma_trace_list"}}, ["index"],
    ),
    tool(
        "luma_read_resource",
        "Read back a view of a pass right after it drew (the frame after the call, not a stale live read). Works for any format, including depth/stencil, "
        "3D LUTs, arrays, MSAA (resolved), BC, r16/rg16, r11g11b10. Writes the raw .bin + .json meta, returns per-channel stats (min/max/mean/percentiles, "
        "NaN/Inf/negative/above 1/exact zero) and a PNG preview path. PS and CS passes only (use the PS entry of a draw), including Luma passes "
        "(custom entries with a stage) by index. view=swapchain or ui reads the "
        "back buffer or Luma's UI texture at present (before display composition), no pass needed. view=registered:<name> reads one of Luma's own "
        "textures (SMAA input/predication mask/output, GTAO depth mips, motion vectors, core.bloom with mip=, core.smaa_edges...; see luma_dev_values kind "
        "texture) at present: its last content, usually this frame's.",
        {**TARGET,
         **READ_TARGET,
         **OUT_DIR,
         **ANALYSIS},
    ),
    tool(
        "luma_compare",
        "Per-pixel difference of two readbacks (.bin paths from luma_read_resource / luma_sr_capture, e.g. an A/B with a dev toggle): abs diff stats, the max's position, diff.png. "
        "rect_a/rect_b crop first; if the sizes still differ the smaller is stretched bilinearly to the bigger (e.g. a render scale sub-rect vs a 100% readback).",
        {"a": {"type": "string", "description": "Reference .bin path (its .json meta must sit next to it)"}, "b": {"type": "string", "description": "Compared .bin path"},
         **COMPARE_OPTIONS}, ["a", "b"], common={},
    ),
    tool(
        "luma_golden",
        "Golden image check of a readback (.bin from luma_read_resource view=swapchain etc.) against the accepted baseline of a case on one backend "
        "(native, dxvk...: each draws differently, so each has its own baseline). Passes when FLIP mean x100 <= the case's max_flip; "
        "error images are kept only on failure. accept=true makes the readback the baseline (only after looking at it: never to silence a failure). "
        "Baselines: %LUMA_BASELINES%, else %LOCALAPPDATA%\\luma-mcp\\baselines, as <case>/<backend>.bin/.json/.golden.json.",
        {"path": {"type": "string", "description": "The .bin to check"},
         "case": {"type": "string", "description": "<game>/<scene>, e.g. mele/me2_normandy_menu"},
         "backend": {"type": "string", "description": "What drew the image: native, dxvk..."},
         "accept": {"type": "boolean", "description": "Replace the case's baseline with this readback instead of checking it"},
         "max_flip": {"type": "number", "description": "On accept: the case's threshold, FLIP mean x100 (default: kept, else 1.0; calibrate on the scene's "
                                                       "luma_ab noise floor)"}},
        ["path", "case", "backend"], annotations=REPLACES_FILES, common={},
    ),
    tool(
        "luma_ab",
        "A/B a dev value on one readback in one call: per repeat sets 'a', waits 'settle' frames, reads back, same for 'b'; restores the original value "
        "even on failure. Returns a vs b diffs and the noise floor (a vs a, b vs b across repeats: the scene's own drift) with within_noise. "
        "Target arguments as luma_read_resource (index/hash/instance, view, slot, mip; view=swapchain needs no pass).",
        {"name": {"type": "string", "description": "Toggle, float or int from luma_dev_values"},
         "a": {"type": ["string", "number", "boolean"], "description": "First value"}, "b": {"type": ["string", "number", "boolean"], "description": "Second value"},
         "settle": {"type": "integer", "description": "Frames waited after each change (default 8; TAA/upscaler history needs more)"},
         "repeats": {"type": "integer", "description": "Reads per side (default 2, max 8), 2+ gives a noise floor"},
         **TARGET, **READ_TARGET, **COMPARE_OPTIONS},
        ["name", "a", "b"], annotations=ONE_SHOT,
    ),
    tool(
        "luma_read_cbuffer",
        "Read a constant buffer bound to a pass at draw time (VS, PS or CS). Returns float4 rows with float and uint views. "
        "With 'frames' it reads it on N consecutive frames (jitter sequences, camera latches, half rate updates) and reports which rows change.",
        {**TARGET, "slot": {"type": "integer", "description": "Constant buffer register b<slot>"},
         "frames": {"type": "integer", "description": "Consecutive frames to read (default 1)"},
         "rows": {"type": "string", "description": "With frames: float4 rows to print per frame, e.g. \"0-3,12\" (default: the rows that change)"}},
        ["slot"],
    ),
    tool("luma_dev_values", "The game's registered knobs: toggles, floats, ints (with their range), per frame counters (e.g. motion vector draws, matches, "
         "refusals by reason) and Luma textures (format/size, read with luma_read_resource view=registered:<name>). Core ones start with \"core.\" "
         "(hide_ui, frame_sleep_ms, force_disable_display_composition, ...).",
         {"filter": {"type": "string", "description": "Substring of the name"}}),
    tool("luma_set_dev_value", "Change a registered toggle, float or int live (A/B tests; counters and textures are read only). Ints are range checked. "
         "Some mirror persisted user settings: this bypasses the config, nothing is saved. One-shot knobs disarm themselves; "
         "read their result with luma_log.",
         {"name": {"type": "string", "description": "Exact name from luma_dev_values"}, "value": {"type": "string", "description": "true/false or 1/0 for toggles, a number otherwise"}}, ["name", "value"],
         annotations=ONE_SHOT),
    tool(
        "luma_sr_state",
        "What the game feeds DLSS/FSR: settings (render/output size, hdr, inverted depth, MV scale, jittered MVs, auto exposure, preset), the last draw "
        "(jitter, reset, pre-exposure, FOV, near/far, input formats/sizes), init/deinit/update counts, and per draw history (jitter sequence, resets, failures).",
        {"history": {"type": "integer", "description": "History rows (default 32, max 256)"}},
    ),
    tool(
        "luma_sr_capture",
        "Copy the next DLSS/FSR draw's inputs (color, motion vectors, depth, exposure, reactive/T&C masks) and output, and read them back with stats and previews.",
        {"max_frames": {"type": "integer", "description": "Frames to wait for an upscaler draw (default 30)"},
         **OUT_DIR, **ANALYSIS},
    ),
    tool("luma_shader_list", "Live shaders the game created: hash, stage, replaced, pipelines, shader model, size, and used slots (cbs/srvs/uavs/rtvs/samplers) from reflection.",
         {"stage": {"type": "string", "description": "VS, PS, CS, GS"},
          "replaced_only": {"type": "boolean", "description": "Only shaders Luma replaced or patched"},
          "include_unloaded": {"type": "boolean", "description": "Also shaders loaded earlier without a live pipeline now"},
          "offset": {"type": "integer", "description": "First matched shader returned, for paging"}, "limit": {"type": "integer", "description": "Default 200, 'matched' vs 'returned' says more remain"}}),
    tool("luma_dump_shader", "Write a loaded shader's original bytecode (.cso) to Luma's dump folder, for offline disassembly (Scripts/dxbc.py).",
         {"hash": {"type": "string", "description": "Shader hash (0x12345678)"}}, ["hash"], annotations=LIVE_WRITE),
    tool(
        "luma_shader_state",
        "Without hash: every replaced pipeline (file or patch) and the compilation error log. With hash: its pipelines, replacement kind, custom shader file and errors.",
        {"hash": {"type": "string", "description": "Shader hash (0x12345678)"}, "disasm": {"type": "boolean", "description": "Include the replacement's disassembly"}},
    ),
    tool("luma_reload_shaders", "Recompile and reload Luma's custom shaders (like the dev UI button) and return the compilation error log.",
         {"unload": {"type": "boolean", "description": "Unload them instead. Shader objects a game mod keeps itself (e.g. XeGTAO's compute shaders) keep running: "
                                                       "test a missing-shader fallback with the feature's dev toggle"}}, annotations=LIVE_WRITE),
    tool(
        "luma_patch_state",
        "Shader patching: the enabled providers, allow_patches (also the core.allow_patches dev value), patched clone count, the loaded DXP recipes "
        "(identity, stage folder, enabled, the game's registration, env variables with their default/game/user/effective values) and the patched shaders "
        "(method, size, recipes that modified them). With hash: that shader's per recipe reports (steps with rule matches, added resource bindings) and "
        "its pipelines (clone origin, whether the recipe resources resolve now, else draws fall back to the original).",
        {"hash": {"type": "string", "description": "Shader hash (0x12345678)"}},
    ),
    tool(
        "luma_set_recipe",
        "Enable/disable a DXP recipe or set/reset one of its env variables, like the Patch Settings tab (saved to the config). Waits for the recipes "
        "to reload; pipelines re-patch on their next bind. Env variables the game sets are read only.",
        {"identity": {"type": "string", "description": "Recipe identity from luma_patch_state, e.g. PS/dithering_fix.recipe.yml"},
         "enabled": {"type": "boolean", "description": "Enable or disable the recipe"},
         "env": {"type": "string", "description": "Env variable name"}, "value": {"type": "string", "description": "The env value (bool as 1/0, or a number)"},
         "reset": {"type": "boolean", "description": "Drop the user value of env, back to the recipe's default"}},
        ["identity"], annotations=ONE_SHOT,
    ),
    tool("luma_reload_patches", "Reload the recipes from disk and rebuild the patch clones (lazily, on each pipeline's next bind), like the dev UI's "
         "Reload Patches. unload=true unloads the patch clones instead (files are untouched).",
         {"unload": {"type": "boolean", "description": "Unload the patch clones instead"}}, annotations=LIVE_WRITE),
    tool("luma_get_settings", "Luma global settings (display mode, peak/paper white), the 10 dev settings with names/ranges, raw game settings slots, and shader defines."),
    tool(
        "luma_set_setting",
        "Change a setting live (not saved to the config). name: scene_peak_white, scene_paper_white, ui_paper_white, dev:<index>, game:<index>, define:<NAME> "
        "(defines need luma_reload_shaders).",
        {"name": {"type": "string", "description": "scene_peak_white, scene_paper_white, ui_paper_white, dev:<index>, game:<index> or define:<NAME>"},
         "value": {"type": "string", "description": "A number (or 0/1)"}, "as_uint": {"type": "boolean", "description": "For game:<index>, write the raw uint bits"}},
        ["name", "value"], annotations=LIVE_WRITE,
    ),
]
TOOL_PROPERTIES = {t["name"]: set(t["inputSchema"]["properties"]) for t in TOOLS}

# DXGI format id -> (dtype, channels, kind). kind: float, unorm, snorm, uint, bgra, depth24, stencil_hi, float_x8x24, stencil_x24,
# r10g10b10a2(_uint), r11g11b10, r9g9b9e5. sRGB formats keep their encoded values, see SRGB_IDS
FORMATS = {
    2: ("<f4", 4, "float"), 3: ("<u4", 4, "uint"), 6: ("<f4", 3, "float"), 10: ("<f2", 4, "float"), 11: ("<u2", 4, "unorm"),
    12: ("<u2", 4, "uint"), 13: ("<i2", 4, "snorm"), 16: ("<f4", 2, "float"), 17: ("<u4", 2, "uint"),
    21: ("<u4", 2, "float_x8x24"), 22: ("<u4", 2, "stencil_x24"), 24: ("<u4", 1, "r10g10b10a2"), 25: ("<u4", 1, "r10g10b10a2_uint"),
    26: ("<u4", 1, "r11g11b10"), 28: ("<u1", 4, "unorm"), 29: ("<u1", 4, "unorm"), 30: ("<u1", 4, "uint"), 31: ("<i1", 4, "snorm"),
    34: ("<f2", 2, "float"), 35: ("<u2", 2, "unorm"), 36: ("<u2", 2, "uint"), 37: ("<i2", 2, "snorm"), 40: ("<f4", 1, "float"),
    41: ("<f4", 1, "float"), 42: ("<u4", 1, "uint"), 43: ("<i4", 1, "uint"), 45: ("<u4", 1, "depth24"), 46: ("<u4", 1, "depth24"),
    47: ("<u4", 1, "stencil_hi"), 49: ("<u1", 2, "unorm"), 50: ("<u1", 2, "uint"), 51: ("<i1", 2, "snorm"), 54: ("<f2", 1, "float"),
    55: ("<u2", 1, "unorm"), 56: ("<u2", 1, "unorm"), 57: ("<u2", 1, "uint"), 58: ("<i2", 1, "snorm"), 61: ("<u1", 1, "unorm"),
    62: ("<u1", 1, "uint"), 63: ("<i1", 1, "snorm"), 65: ("<u1", 1, "unorm"), 67: ("<u4", 1, "r9g9b9e5"),
    87: ("<u1", 4, "bgra"), 88: ("<u1", 4, "bgra"), 91: ("<u1", 4, "bgra"), 93: ("<u1", 4, "bgra"),
}
# Typeless storage -> the typed format views of it usually use
TYPELESS = {1: 2, 5: 6, 9: 10, 15: 16, 19: 21, 23: 24, 27: 28, 33: 34, 39: 41, 44: 46, 48: 49, 53: 56, 60: 61, 90: 87, 92: 88}
SRGB_IDS = {29, 91, 93}
DEPTH_KINDS = {"depth24", "float_x8x24"}


def half_bits_to_float(bits):
    return bits.astype(np.uint16).view(np.float16).astype(np.float32)


def decode(raw, meta):
    """Returns (float32 array [slices*depth, height, width, channels], format id used, kind) or raises ValueError."""
    fmt = meta["view_format_id"]
    if fmt not in FORMATS:
        fmt = TYPELESS.get(meta["format_id"], meta["format_id"])
    if fmt not in FORMATS:  # Block compressed formats included
        raise ValueError(f"no decoder for {meta['view_format']} / {meta['format']}, the raw rows are in the .bin")
    dtype, channels, kind = FORMATS[fmt]
    item = np.dtype(dtype).itemsize * channels
    width, rows, pitch = meta["width"], meta["rows"], meta["row_pitch"]
    planes = meta["slices"] * meta["depth"]
    data = np.frombuffer(raw, dtype=np.uint8)[: planes * rows * pitch].reshape(planes, rows, pitch)[:, :, : width * item]
    values = np.ascontiguousarray(data).view(dtype).reshape(planes, rows, width, channels)

    if kind in ("float", "uint"):
        out = values.astype(np.float32, copy=False)
    elif kind == "unorm":
        out = values.astype(np.float32) / np.iinfo(values.dtype).max
    elif kind == "snorm":
        out = np.maximum(values.astype(np.float32) / np.iinfo(values.dtype).max, -1.0)
    elif kind == "bgra":
        out = values[..., [2, 1, 0, 3]].astype(np.float32) / 255.0
    elif kind == "depth24":
        out = (values & 0xFFFFFF).astype(np.float32) / 16777215.0
    elif kind == "stencil_hi":
        out = (values >> 24).astype(np.float32)
    elif kind == "float_x8x24":
        out = values[..., :1].copy().view(np.float32)
    elif kind == "stencil_x24":
        out = (values[..., 1:2] & 0xFF).astype(np.float32)
    elif kind.startswith("r10g10b10a2"):
        v = values[..., 0]
        out = np.stack([(v >> s) & 0x3FF for s in (0, 10, 20)] + [v >> 30], axis=-1).astype(np.float32)
        if kind == "r10g10b10a2":
            out /= np.array([1023, 1023, 1023, 3], np.float32)
    elif kind == "r11g11b10":
        v = values[..., 0]
        # Same exponent bias as fp16 and no sign bit: move the mantissa up to fp16's 10 bits
        r = half_bits_to_float(((v >> 6) & 0x1F) << 10 | (v & 0x3F) << 4)
        g = half_bits_to_float(((v >> 17) & 0x1F) << 10 | ((v >> 11) & 0x3F) << 4)
        b = half_bits_to_float(((v >> 27) & 0x1F) << 10 | ((v >> 22) & 0x1F) << 5)
        out = np.stack([r, g, b], axis=-1)
    elif kind == "r9g9b9e5":
        v = values[..., 0]
        scale = np.exp2(((v >> 27) & 0x1F).astype(np.float32) - 24.0)
        out = np.stack([((v >> s) & 0x1FF).astype(np.float32) * scale for s in (0, 9, 18)], axis=-1)
    else:
        raise ValueError(kind)
    return out, fmt, kind


def channel_stats(values):
    columns = values.reshape(-1, values.shape[-1])
    names = "rgba" if columns.shape[1] > 1 else "r"
    stats = []
    for c in range(columns.shape[1]):
        channel = columns[:, c]
        finite_mask = np.isfinite(channel)
        finite = channel if finite_mask.all() else channel[finite_mask]
        nan = int(np.isnan(channel).sum())
        entry = {"channel": names[c], "nan": nan, "inf": int(channel.size - finite.size) - nan,
                 "zero_fraction": float((channel == 0).mean()) if channel.size else 0.0}
        if finite.size:
            p = np.percentile(finite, [1, 50, 99])
            entry.update(min=float(finite.min()), max=float(finite.max()), mean=float(finite.mean()),
                         p1=float(p[0]), p50=float(p[1]), p99=float(p[2]),
                         negative=int((finite < 0).sum()), above_1=int((finite > 1).sum()))
        stats.append(entry)
    return stats


def write_png(path, rgb8):
    height = rgb8.shape[0]
    rows = np.concatenate([np.zeros((height, 1), np.uint8), rgb8.reshape(height, -1)], axis=1).tobytes()  # Filter byte 0 per row

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", rgb8.shape[1], height, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 1)) + chunk(b"IEND", b""))


def preview(values, meta, kind, fmt, path):
    """Tiles 3D slices side by side. Returns a description of the mapping (the preview is not a colorimetric rendering)."""
    planes = values[: min(meta["depth"], 64)]
    # Downscale before tiling (and before the per pixel mapping), so a large readback is never copied at full size
    step_y, step_x = max(1, -(-planes.shape[1] // 2048)), max(1, -(-planes.shape[2] * len(planes) // 2048))
    image = np.concatenate([plane[::step_y, ::step_x] for plane in planes], axis=1)
    channels = image.shape[-1]
    rgb = image[..., :3] if channels >= 3 else (np.repeat(image[..., :1], 3, -1) if channels == 1 else np.concatenate([image, np.zeros_like(image[..., :1])], -1))
    rgb = np.nan_to_num(rgb, nan=0.0, posinf=0.0, neginf=0.0)
    if kind in DEPTH_KINDS or kind.startswith("stencil"):
        lo, hi = float(rgb.min()), float(rgb.max())
        rgb = (rgb - lo) / (hi - lo) if hi > lo else rgb * 0
        mapping = f"normalized {lo:.6g}..{hi:.6g}"
    elif kind == "float" or kind in ("r11g11b10", "r9g9b9e5"):
        # Assumes linear light; the Reinhard knee keeps values above 1 visible
        rgb = np.maximum(rgb, 0)
        rgb = np.power(rgb / (1 + rgb), 1 / 2.2) if rgb.max() > 1 else np.power(rgb, 1 / 2.2)
        mapping = "linear assumed, x/(1+x) if above 1, then 1/2.2 power"
    elif fmt in SRGB_IDS:
        mapping = "raw encoded values"
    else:
        mapping = "raw values clipped to 0..1"
    write_png(path, (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8))
    return mapping


def region_mask(values, region):
    x, y, w, h = (int(v) for v in region)
    mask = np.zeros(values.shape[1:3], dtype=bool)
    mask[max(y, 0):y + h, max(x, 0):x + w] = True
    return mask


def finite_variance(values):
    finite = values[np.isfinite(values)]
    return float(finite.var()) if finite.size else None


def analyze(values, opts):
    """Measurements used to diagnose SR/render scale issues (black stripes, sub-rect coverage, softness, motion vector noise)."""
    out = {}
    mask = region_mask(values, opts["region"]) if opts.get("region") else None
    if mask is not None:
        out["region_stats"] = channel_stats(values[:, mask])
        out["outside_region_stats"] = channel_stats(values[:, ~mask])
    if opts.get("row_bands"):
        edges = np.linspace(0, values.shape[1], int(opts["row_bands"]) + 1).astype(int)
        bands = []
        for top, bottom in zip(edges[:-1], edges[1:]):
            band = values[:, top:bottom]
            bands.append({"rows": [int(top), int(bottom)], "zero_fraction": float((band == 0).all(-1).mean()),
                          "mean": float(np.nanmean(band[..., :3])) if band.size else None})
        out["row_bands"] = bands
    if opts.get("laplacian"):
        # Plain mean of the first (up to 3) channels in the stored encoding, not a luminance
        rgb_mean = np.nan_to_num(plain_mean(values))
        lap = rgb_mean[:-2, 1:-1] + rgb_mean[2:, 1:-1] + rgb_mean[1:-1, :-2] + rgb_mean[1:-1, 2:] - 4 * rgb_mean[1:-1, 1:-1]
        result = {"all": finite_variance(lap)}
        if mask is not None:
            inner = mask[1:-1, 1:-1]
            result["region"] = finite_variance(lap[inner]) if inner.any() else None
            result["outside_region"] = finite_variance(lap[~inner]) if (~inner).any() else None
        out["laplacian_variance"] = result
    if opts.get("threshold") is not None:
        magnitude = np.sqrt((values[..., :2] ** 2).sum(-1)) if values.shape[-1] >= 2 else np.abs(values[..., 0])
        above = int((magnitude > float(opts["threshold"])).sum())
        out["above_threshold"] = {"threshold": float(opts["threshold"]), "count": above, "fraction": above / max(magnitude.size, 1)}
    return out


def load_readback(path):
    """(values, kind) of a .bin readback, see decode()"""
    with open(os.path.splitext(path)[0] + ".json") as f:
        meta = json.load(f)
    with open(path, "rb") as f:
        values, _, kind = decode(f.read(), meta)
    return values, kind


def write_meta(result):
    """The .json next to a readback's .bin, what load_readback() decodes it with"""
    with open(os.path.splitext(result["path"])[0] + ".json", "w") as f:
        json.dump({k: v for k, v in result.items() if k != "ok"}, f, indent=1)


def postprocess_resource(result, opts):
    path = result["path"]
    write_meta(result)
    try:
        with open(path, "rb") as f:
            values, fmt, kind = decode(f.read(), result)
        result["decoded_as"] = kind
        result["stats"] = channel_stats(values)
        result.update(analyze(values, opts))
        if opts.get("preview", True):
            png = os.path.splitext(path)[0] + ".png"
            result["preview_mapping"] = preview(values, result, kind, fmt, png)
            result["preview"] = png
    except ValueError as e:
        result["decode_error"] = str(e)


def crop(values, rect):
    if not rect:
        return values
    x, y, w, h = (int(v) for v in rect)
    return values[:, max(y, 0):y + h, max(x, 0):x + w]


def resize_bilinear(values, height, width):
    """[planes, h, w, c] -> [planes, height, width, c], texel centers aligned and edges clamped, like a GPU bilinear stretch."""
    def taps(source, target):
        position = np.clip((np.arange(target) + 0.5) * source / target - 0.5, 0, source - 1)
        low = np.floor(position).astype(int)
        return low, np.minimum(low + 1, source - 1), (position - low).astype(np.float32)

    y0, y1, fy = taps(values.shape[1], height)
    x0, x1, fx = taps(values.shape[2], width)
    fy, fx = fy[None, :, None, None], fx[None, None, :, None]
    rows = values[:, y0] * (1 - fy) + values[:, y1] * fy
    return rows[:, :, x0] * (1 - fx) + rows[:, :, x1] * fx


def plain_mean(values):
    """First plane, mean of the first (up to 3) channels in the stored encoding: not a luminance."""
    return values[0, ..., : min(3, values.shape[-1])].mean(-1)


def image_metrics(values, metrics):
    mean = plain_mean(values)
    finite = mean[np.isfinite(mean)]
    out = {}
    for metric in metrics:
        if metric == "mean":
            out["mean"] = float(finite.mean()) if finite.size else None
        elif metric.startswith("below:"):
            out[metric] = float((finite < float(metric[len("below:"):])).mean()) if finite.size else None
        elif metric == "detail":
            # What a lower resolution loses (e.g. AO contact detail): |x - its 2x2 box average|
            h, w = mean.shape[0] // 2 * 2, mean.shape[1] // 2 * 2
            even = np.nan_to_num(mean[:h, :w])
            box = even.reshape(h // 2, 2, w // 2, 2).mean((1, 3))
            out["detail"] = float(np.abs(even - box.repeat(2, 0).repeat(2, 1)).mean()) if box.size else None
    return out


FLIP_PPD = 67.0  # FLIP's default pixels per degree (~0.7 m from a 24" 4K monitor); fixed so scores stay comparable


def flip_metric(va, vb, hdr, png=None):
    """FLIP of b against the reference a on the first plane's RGB, NVIDIA's perceptual difference: HDR-FLIP on linear values (negatives
    clipped) for float formats, LDR-FLIP on [0, 1] values taken as sRGB encoded otherwise. Writes the error map to png if given."""
    try:
        import flip_evaluator as F
    except ImportError:
        return {"error": f"flip-evaluator missing: {sys.executable} -m pip install flip-evaluator"}
    if va.shape[-1] < 3:
        return {"error": "FLIP needs 3 channels"}
    upper = np.inf if hdr else 1.0
    error_map, mean, _ = F.evaluate(np.ascontiguousarray(np.clip(np.nan_to_num(va[0, ..., :3]), 0.0, upper), np.float32),
                                    np.ascontiguousarray(np.clip(np.nan_to_num(vb[0, ..., :3]), 0.0, upper), np.float32), "HDR" if hdr else "LDR",
                                    applyMagma=False, parameters={"ppd": FLIP_PPD})
    error_map = np.asarray(error_map).reshape(va.shape[1], va.shape[2])
    result = {"mean_x100": 100.0 * float(mean), "p99_x100": 100.0 * float(np.percentile(error_map, 99)), "mode": "HDR" if hdr else "LDR", "ppd": FLIP_PPD}
    if png:
        write_png(png, np.repeat((np.clip(error_map, 0, 1) * 255 + 0.5).astype(np.uint8)[..., None], 3, -1))
        result.update(map_png=png, map_png_mapping="FLIP error 0..1 -> black..white")
    return result


def compare(a, b, region=None, rect_a=None, rect_b=None, metrics=None, png_dir=None, pngs=True):
    metrics = list(metrics or [])
    unknown = [m for m in metrics if m not in ("mean", "detail", "correlation", "flip") and not re.fullmatch(r"below:-?[0-9.eE+-]+", m)]
    if unknown:
        return {"ok": False, "error": f"Unknown metrics {unknown}, use mean, below:<t>, detail, correlation, flip"}
    (va, kind_a), (vb, kind_b) = load_readback(a), load_readback(b)
    va, vb = crop(va, rect_a), crop(vb, rect_b)
    resampled = None
    if va.shape[:3] != vb.shape[:3]:
        if not (rect_a or rect_b) or va.shape[0] != vb.shape[0] or 0 in va.shape[1:3] or 0 in vb.shape[1:3]:
            return {"ok": False, "error": f"Different sizes: {list(va.shape[:3])} vs {list(vb.shape[:3])} (planes, height, width); "
                                          "pass rect_a/rect_b to compare a sub-rect, the smaller is then stretched to the bigger"}
        if va.shape[1] * va.shape[2] < vb.shape[1] * vb.shape[2]:
            va, resampled = resize_bilinear(va, *vb.shape[1:3]), "a"
        else:
            vb, resampled = resize_bilinear(vb, *va.shape[1:3]), "b"
    channels = min(va.shape[-1], vb.shape[-1])
    va, vb = va[..., :channels], vb[..., :channels]
    nan_a, nan_b = np.isnan(va), np.isnan(vb)
    diff = np.abs(np.nan_to_num(va) - np.nan_to_num(vb))
    pixel_diff = diff.max(-1)
    result = {"ok": True, "stats": channel_stats(diff), "differing_pixels": int((pixel_diff > 1e-6).sum()), "nan_mismatch": int((nan_a != nan_b).sum())}
    plane, y, x, c = np.unravel_index(int(np.argmax(diff)), diff.shape)
    result["max"] = {"value": float(diff[plane, y, x, c]), "x": int(x), "y": int(y), "plane": int(plane), "channel": int(c),
                     "a": va[plane, y, x].tolist(), "b": vb[plane, y, x].tolist()}
    if resampled:
        result["resampled"] = resampled
        result["size"] = list(va.shape[1:3])
    if region:
        result.update(analyze(diff, {"region": region}))
    png_dir = png_dir or os.path.dirname(a)
    png_name = f"{os.path.splitext(os.path.basename(a))[0]}_vs_{os.path.splitext(os.path.basename(b))[0]}.png"
    if metrics:
        image_metric_names = [m for m in metrics if m not in ("correlation", "flip")]
        result["metrics"] = {"a": image_metrics(va, image_metric_names), "b": image_metrics(vb, image_metric_names)}
        if "correlation" in metrics:
            ma, mb = plain_mean(va), plain_mean(vb)
            finite = np.isfinite(ma) & np.isfinite(mb)
            ma, mb = ma[finite], mb[finite]
            result["metrics"]["correlation"] = float(np.corrcoef(ma, mb)[0, 1]) if ma.size > 1 and ma.std() > 0 and mb.std() > 0 else None
        if "flip" in metrics:
            result["metrics"]["flip"] = flip_metric(va, vb, kind_a == kind_b == "float", os.path.join(png_dir, "flip_" + png_name) if pngs else None)
    if pngs:
        peak = float(pixel_diff[0].max())
        gray = (np.clip(pixel_diff[0] / peak, 0, 1) * 255 + 0.5).astype(np.uint8) if peak > 0 else np.zeros(pixel_diff.shape[1:], np.uint8)
        result["diff_png"] = os.path.join(png_dir, "diff_" + png_name)
        write_png(result["diff_png"], np.repeat(gray[..., None], 3, -1))
        result["diff_png_mapping"] = f"max channel abs diff, 0..{peak:.6g} -> black..white"
    return result


# Outside the repository and %TEMP%: baselines are kept across sessions and cleanups
BASELINE_ROOT = os.environ.get("LUMA_BASELINES") or os.path.join(os.environ.get("LOCALAPPDATA") or tempfile.gettempdir(), "luma-mcp", "baselines")
NAME_RE = r"\w[\w.-]*"  # Starts with a letter or digit, so "." and ".." can't climb out of the roots below
CASE_RE = NAME_RE + r"(/" + NAME_RE + r")*"  # <game>/<scene>: a folder path under the roots above


def golden(path, case, backend, accept=False, max_flip=None):
    if not re.fullmatch(CASE_RE, case) or not re.fullmatch(NAME_RE, backend):
        return {"ok": False, "error": "case is <game>/<scene> and backend one name, letters, digits, . _ -"}
    base = os.path.join(BASELINE_ROOT, *case.split("/"), backend)
    golden_json = base + ".golden.json"
    settings = {}
    if os.path.exists(golden_json):
        with open(golden_json) as f:
            settings = json.load(f)
    if accept:
        os.makedirs(os.path.dirname(base), exist_ok=True)
        shutil.copyfile(path, base + ".bin")
        shutil.copyfile(os.path.splitext(path)[0] + ".json", base + ".json")
        settings = {"max_flip": float(max_flip if max_flip is not None else settings.get("max_flip", 1.0)), "source": os.path.abspath(path),
                    "accepted": time.strftime("%Y-%m-%d %H:%M:%S")}
        with open(golden_json, "w") as f:
            json.dump(settings, f, indent=1)
        return {"ok": True, "accepted": base + ".bin", **settings}
    if not os.path.exists(base + ".bin"):
        return {"ok": False, "error": f"No baseline {base}.bin: look at the readback, then accept=true"}
    out_dir = os.path.join(tempfile.gettempdir(), "luma-mcp", "golden")
    os.makedirs(out_dir, exist_ok=True)
    result = compare(base + ".bin", path, metrics=["flip"], png_dir=out_dir)
    if not result.get("ok"):
        return result
    flip = result["metrics"]["flip"]
    if "error" in flip:
        return {"ok": False, "error": flip["error"]}
    max_flip = settings.get("max_flip", 1.0)
    passed = flip["mean_x100"] <= max_flip
    if passed:  # Images only on a mismatch
        for png in (flip["map_png"], result["diff_png"]):
            os.remove(png)
    return {"ok": True, "pass": passed, "flip": {k: flip[k] for k in ("mean_x100", "p99_x100", "mode")}, "max_flip": max_flip,
            "baseline": base + ".bin", "max_diff": result["max"], **({} if passed else {"flip_png": flip["map_png"], "diff_png": result["diff_png"]})}


def parse_rows(spec):
    rows = set()
    for part in str(spec).split(","):
        if "-" in part:
            lo, hi = part.split("-", 1)
            rows.update(range(int(lo), int(hi) + 1))
        elif part.strip():
            rows.add(int(part))
    return rows


def dword_views(hex_string):
    # The backend prints each dword as "{:08X}", most significant byte first
    dwords = bytes.fromhex(hex_string)
    count = len(dwords) // 4
    return struct.unpack(f">{count}f", dwords[: count * 4]), struct.unpack(f">{count}I", dwords[: count * 4])


def format_row(floats, uints, row):
    i = row * 4
    return " ".join(f"{v:.6g}" for v in floats[i:i + 4]) + " | " + " ".join(f"0x{u:08X}" for u in uints[i:i + 4])


def postprocess_cbuffer(result, rows_spec):
    if "series" in result:
        frames = []
        for entry in result.pop("series"):
            frame, hex_string = entry.split(":", 1)
            frames.append((int(frame),) + dword_views(hex_string))
        row_count = min(len(f[2]) for f in frames) // 4
        changing = [r for r in range(row_count) if len({f[2][r * 4:r * 4 + 4] for f in frames}) > 1]
        shown = sorted(parse_rows(rows_spec)) if rows_spec else changing[:32]
        result["frames"] = len(frames)
        result["changing_rows"] = changing
        result["rows"] = [f"f{frame} c{r}: {format_row(floats, uints, r)}" for frame, floats, uints in frames for r in shown if r < row_count]
        return
    floats, uints = dword_views(result.pop("hex_dwords"))
    first = result.get("first_constant", 0)
    last = first + result["num_constants"] if result.get("num_constants") else len(floats) // 4
    result["rows"] = [f"c{r}: {format_row(floats, uints, r)}" + ("" if first <= r < last else " (outside bound range)") for r in range(len(floats) // 4)]


TRACE_DIR = os.path.join(tempfile.gettempdir(), "luma-mcp", "traces")


def trace_path(name):
    return os.path.join(TRACE_DIR, re.sub(r"[^\w.-]", "_", name) + ".json")


def save_trace(name, pid):
    entries, offset, page = [], 0, 5000
    while True:
        # Explicit filters (the dev UI's defaults), so a saved trace's meaning doesn't follow backend default changes
        result = backend.call("trace_list", {"pid": pid, "offset": offset, "limit": page, "include_vs": False, "include_bindings": False, "include_buffer_writes": False})
        if not result.get("ok"):
            raise RuntimeError(result.get("error", "trace_list failed"))
        entries += result["entries"]
        offset += result["returned"]
        if result["returned"] < page:
            break
    os.makedirs(TRACE_DIR, exist_ok=True)
    with open(trace_path(name), "w") as f:
        json.dump(entries, f)
    return trace_path(name)


def entry_key(entry):
    return tuple(entry.get(k) for k in ("type", "stage", "hash", "rt0", "name", "note", "copy_kind"))


def run_length(entries):
    """Consecutive entries with the same key as (key, count) runs, plus each run's first entry index (and the total at the end)."""
    runs, starts = [], []
    for i, entry in enumerate(entries):
        key = entry_key(entry)
        if runs and runs[-1][0] == key:
            runs[-1][1] += 1
        else:
            runs.append([key, 1])
            starts.append(i)
    starts.append(len(entries))
    return [tuple(run) for run in runs], starts


def trace_diff(a, b, max_blocks):
    traces = []
    for name in (a, b):
        with open(trace_path(name)) as f:
            traces.append(json.load(f))
    ta, tb = traces
    # Scenes repeat the same draw thousands of times in a row, which makes a per entry diff quadratic: diff the runs instead
    # (a run whose count changed shows up as a replaced block)
    (runs_a, starts_a), (runs_b, starts_b) = run_length(ta), run_length(tb)
    matcher = difflib.SequenceMatcher(None, runs_a, runs_b, autojunk=False)
    blocks = [{"tag": tag, "a": [starts_a[i1], starts_a[i2]], "b": [starts_b[j1], starts_b[j2]],
               "a_entries": ta[starts_a[i1]:min(starts_a[i2], starts_a[i1] + 4)], "b_entries": tb[starts_b[j1]:min(starts_b[j2], starts_b[j1] + 4)]}
              for tag, i1, i2, j1, j2 in matcher.get_opcodes() if tag != "equal"]
    counts = [collections.Counter(e["hash"] for e in t if "hash" in e) for t in traces]
    changes = sorted(((h, counts[0][h], counts[1][h]) for h in counts[0].keys() | counts[1].keys() if counts[0][h] != counts[1][h]),
                     key=lambda c: -abs(c[1] - c[2]))
    return {"ok": True, "a_entries": len(ta), "b_entries": len(tb), "run_similarity": matcher.ratio(), "blocks_total": len(blocks),
            "blocks": blocks[:max_blocks], "note": "Indices are positions in the stored (filtered) lists; each entry keeps its capture 'index'",
            "hash_count_changes": [{"hash": h, "a": ca, "b": cb} for h, ca, cb in changes[:100]]}


LOG_PATHS = {}  # Game pid -> its ReShade log
LOG_TAIL_BYTES = 4 << 20


def find_log(pid, timeout_ms):
    status = backend.call("status", {"pid": pid, "timeout_ms": timeout_ms})
    if not status.get("ok"):
        raise RuntimeError(status.get("error", "status failed"))
    folder = os.path.dirname(status["exe_path"])
    path = os.path.join(folder, "ReShade.log")
    if not os.path.isfile(path):
        path = None
        for candidate in sorted(glob.glob(os.path.join(folder, "*.log"))):
            with open(candidate, "rb") as f:
                if b"ReShade" in f.read(4096):
                    path = candidate
                    break
    if not path:
        raise RuntimeError(f"No ReShade log next to {status['exe_path']} (a custom [INSTALL] BasePath?), pass log_path")
    LOG_PATHS[status["pid"]] = path
    return path


def tail_log(args):
    path = args.get("log_path") or LOG_PATHS.get(args.get("pid") or backend.pid) or find_log(args.get("pid"), args.get("timeout_ms"))
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        f.seek(max(0, size - LOG_TAIL_BYTES))
        lines = f.read().decode("utf-8", errors="replace").splitlines()
    if size > LOG_TAIL_BYTES:
        lines = lines[1:]  # The first one is cut
    if args.get("pattern"):
        pattern = re.compile(args["pattern"])
        lines = [line for line in lines if pattern.search(line)]
    return {"ok": True, "path": path, "lines": lines[-int(args.get("lines") or 200):]}


class Backend:
    def __init__(self):
        self.pipe = None
        self.pid = None

    @staticmethod
    def list_pids():
        try:
            return sorted(int(n[len(PIPE_PREFIX):]) for n in os.listdir(PIPE_DIR) if n.startswith(PIPE_PREFIX) and n[len(PIPE_PREFIX):].isdigit())
        except OSError:
            return []

    def close(self):
        if self.pipe:
            try:
                self.pipe.close()
            except OSError:
                pass
        self.pipe, self.pid = None, None

    def connect(self, pid):
        if self.pipe and pid in (None, self.pid):
            return  # A dead pipe fails the next write or read, and "call" reconnects
        self.close()
        if pid is None:
            pids = self.list_pids()
            if not pids:
                raise RuntimeError("No Luma game with the dev MCP pipe is running (needs a Development build that has presented a frame)")
            if len(pids) > 1:
                raise RuntimeError(f"Several Luma games are running (pids {pids}), pass 'pid'")
            pid = pids[0]
        # The listening instance is briefly missing or busy while the backend hands the previous one to its client
        for _ in range(20):
            try:
                self.pipe = open(f"{PIPE_DIR}{PIPE_PREFIX}{pid}", "r+b", buffering=0)
                self.pid = pid
                return
            except FileNotFoundError:
                time.sleep(0.1)
            except OSError as e:
                if getattr(e, "winerror", None) != 231:  # ERROR_PIPE_BUSY
                    raise RuntimeError(f"Could not open the pipe of pid {pid}: {e}")
                time.sleep(0.1)
        raise RuntimeError(f"The pipe of pid {pid} did not come back (a build older than the multi client backend serves one client at a time)")

    @staticmethod
    def read_exact(pipe, size):
        data = bytearray()
        while len(data) < size:
            chunk = pipe.read(size - len(data))
            if not chunk:
                raise OSError("pipe closed")
            data += chunk
        return bytes(data)

    def exchange(self, payload, wait_s):
        # A game suspended in a debugger freezes its pipe thread too (no backend timeout): read on a worker, drop the connection after "wait_s"
        pipe, reply = self.pipe, {}

        def run():
            try:
                pipe.write(struct.pack("<I", len(payload)) + payload)
                reply["data"] = self.read_exact(pipe, struct.unpack("<I", self.read_exact(pipe, 4))[0])
            except OSError as e:
                reply["error"] = e
            finally:
                if pipe is not self.pipe:  # Abandoned
                    pipe.close()

        worker = threading.Thread(target=run, daemon=True)
        worker.start()
        worker.join(wait_s)
        if worker.is_alive():
            # ponytail: the 5 s margin also covers the backend writing the readback files (a huge sr_capture on a slow disk can exceed it:
            # raise it, or send progress notifications). The abandoned worker keeps its own pipe instance until the game answers or exits.
            self.pipe, self.pid = None, None
            raise RuntimeError(f"The game did not answer within {wait_s:.0f} s (suspended in a debugger, or its pipe thread is stuck), dropped the connection")
        if "error" in reply:
            raise reply["error"]
        return json.loads(reply["data"].decode("utf-8", errors="replace"))

    def call(self, name, args):
        pid = args.pop("pid", None)
        lines = [name] + [f"{k}={int(v) if isinstance(v, bool) else v}" for k, v in args.items() if v is not None]
        payload = "\n".join(lines).encode()
        # The backend's job timeout, plus a margin for it to answer
        timeout_ms = args.get("timeout_ms")
        wait_s = min(max(TIMEOUT_MS if timeout_ms is None else int(timeout_ms), TIMEOUT_MS_RANGE[0]), TIMEOUT_MS_RANGE[1]) / 1000 + 5
        for attempt in range(2):
            self.connect(pid)
            try:
                return self.exchange(payload, wait_s)
            except OSError:
                # The game restarted or closed, retry once on a fresh connection
                self.close()
                if attempt:
                    raise RuntimeError("Lost the connection to the game")


backend = Backend()


def game_call(tool_name, args):
    result = backend.call(tool_name, dict(args))
    if not result.get("ok"):
        raise RuntimeError(f"{tool_name}: {result.get('error')}")
    return result


def dev_value_text(common, name):
    """The knob's current value as set_dev_value parses it, to restore it after an A/B."""
    values = game_call("dev_values", {"filter": name, **common})["values"]
    match = next((v for v in values if v["name"] == name), None)
    if match is None or match["kind"] not in ("toggle", "float", "int"):
        raise RuntimeError(f"{name} is not a toggle, float or int dev value (see luma_dev_values)")
    return ("1" if match["value"] else "0") if match["kind"] == "toggle" else repr(match["value"])


def set_dev_value(common, name, value):
    game_call("set_dev_value", {"name": name, "value": value if isinstance(value, str) else json.dumps(value), **common})


def pair_mean_diff(result):
    return float(np.mean([s.get("mean", 0.0) for s in result["stats"]]))


def ab_test(args):
    common = {k: args[k] for k in ("pid", "timeout_ms") if args.get(k) is not None}
    name, settle, repeats = args["name"], int(args.get("settle") or 8), min(max(int(args.get("repeats") or 2), 1), 8)
    read_args = {k: args[k] for k in (*TARGET, *READ_TARGET) if args.get(k) is not None}
    compare_args = {k: args.get(k) for k in COMPARE_OPTIONS}
    original = dev_value_text(common, name)
    paths = {"a": [], "b": []}
    try:
        for _ in range(repeats):
            for side in ("a", "b"):
                set_dev_value(common, name, args[side])
                game_call("wait_frames", {"frames": settle, **common})
                read = game_call("read_resource", {**read_args, **common})
                write_meta(read)  # compare() decodes it, no stats or preview needed here
                paths[side].append(read["path"])
    finally:
        set_dev_value(common, name, original)

    a_vs_b = [compare(a, b, **compare_args) for a, b in zip(paths["a"], paths["b"])]
    noise = [compare(paths[side][0], other, region=compare_args["region"]) for side in ("a", "b") for other in paths[side][1:]]
    for result in a_vs_b + noise:
        if not result.get("ok"):
            return result
    a_vs_b_mean = float(np.median([pair_mean_diff(r) for r in a_vs_b]))
    result = {"ok": True, "name": name, "restored": original, "paths": paths, "a_vs_b": a_vs_b[0] if len(a_vs_b) == 1 else a_vs_b,
              "a_vs_b_mean_abs_diff": a_vs_b_mean}
    if noise:
        noise_mean = max(pair_mean_diff(r) for r in noise)
        result.update(noise_mean_abs_diff=noise_mean, within_noise=a_vs_b_mean <= noise_mean,
                      noise_differing_pixels=max(r["differing_pixels"] for r in noise))
    return result


def call_tool(name, args):
    # Tools that don't need the game (or only for a lookup)
    if name == "luma_list_games":
        return {"ok": True, "pids": backend.list_pids(), "connected": backend.pid}
    if name == "luma_log":
        return tail_log(args)
    if name == "luma_compare":
        return compare(args["a"], args["b"], **{k: args.get(k) for k in COMPARE_OPTIONS})
    if name == "luma_golden":
        return golden(args["path"], args["case"], args["backend"], bool(args.get("accept")), args.get("max_flip"))
    if name == "luma_ab":
        return ab_test(args)
    if name == "luma_trace_diff":
        return trace_diff(args["a"], args["b"], int(args.get("max_blocks") or 60))

    opts = {k: args.pop(k) for k in list(args) if k in BRIDGE_ONLY_ARGS}
    pid = args.get("pid")
    result = backend.call(name[len("luma_"):], args)
    if not result.get("ok"):
        return result
    if name == "luma_read_resource" and "path" in result:
        postprocess_resource(result, opts)
    elif name == "luma_sr_capture":
        for texture in result.get("textures", []):
            if "path" in texture:
                postprocess_resource(texture, opts)
    elif name == "luma_read_cbuffer":
        postprocess_cbuffer(result, opts.get("rows"))
    elif name == "luma_trace_capture" and opts.get("save_as"):
        result["saved_as"] = save_trace(opts["save_as"], pid)
    return result


# Loaded up front even when the tools are deferred (Claude Code truncates it at 2048 chars)
INSTRUCTIONS = (
    "Live inspection of a running Luma Development build (pipe \\\\.\\pipe\\luma-mcp-<pid>, one game at a time; luma_list_games when several run). "
    "Prefer it over the RenoDX DevKit for what only Luma sees: its injected passes (DLSS/FSR, SMAA, bloom, AO, display composition), merged deferred "
    "context work, which shader variant/replacement ran, the game mod's dev knobs and counters.\n"
    "Flow: luma_status -> luma_trace_capture (trigger=<hash> for a pass that doesn't draw every frame) -> luma_trace_list -> luma_trace_get / "
    "luma_read_resource / luma_read_cbuffer by entry index.\n"
    "Readbacks are files in %TEMP%\\luma-mcp (.bin + .json + a .png preview to open with Read) plus channel stats in the result; luma_compare diffs two .bin (metrics=[\"flip\"]: perceptual score), luma_golden checks one against a per-backend baseline.\n"
    "A/B: luma_ab name=<dev value> a=.. b=.. (one call, restores the value, gives the noise floor); manual: luma_dev_values -> luma_set_dev_value "
    "(live, not saved) -> read back; one-shot knobs report in luma_log.\n"
    "Shader patches: luma_patch_state -> luma_set_recipe / luma_reload_patches; luma_trace_list variant=original finds clones that fell back.\n"
    "'ignored_args' in a result means a misspelt or unknown argument. A timeout usually means the game isn't presenting (minimized, paused, "
    "in a loading screen or a debugger): luma_status still answers then (presenting=false)."
)


class RpcError(Exception):
    def __init__(self, code, message, data=None):
        super().__init__(message)
        self.code, self.data = code, data


def handle(message):
    method = message.get("method")
    params = message.get("params") or {}
    if not isinstance(params, dict) or not isinstance(params.get("_meta", {}), dict):
        raise RpcError(-32602, "params and its _meta must be objects")
    version = params.get("_meta", {}).get(META + "protocolVersion")
    if version is None:  # Legacy
        return dispatch(method, params)
    if version not in MODERN_VERSIONS:
        raise RpcError(-32022, "Unsupported protocol version", {"supported": list(MODERN_VERSIONS), "requested": version})
    if META + "clientCapabilities" not in params["_meta"]:
        raise RpcError(-32602, f"Missing {META}clientCapabilities in _meta")
    if method == "server/discover":  # Modern only, a legacy client gets "unknown method" as before
        result = {"supportedVersions": list(MODERN_VERSIONS), "capabilities": {"tools": {}}, "instructions": INSTRUCTIONS}
    else:
        result = dispatch(method, params)
    result.update(resultType="complete", _meta={META + "serverInfo": SERVER_INFO})
    if method in ("server/discover", "tools/list"):
        result.update(ttlMs=LIST_TTL_MS, cacheScope="public")
    return result


def dispatch(method, params):
    if method == "initialize":
        requested = params.get("protocolVersion")
        return {"protocolVersion": requested if requested in LEGACY_VERSIONS else LEGACY_VERSIONS[0],
                "capabilities": {"tools": {}}, "serverInfo": SERVER_INFO, "instructions": INSTRUCTIONS}
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "tools/call":
        name, args = params.get("name"), dict(params.get("arguments") or {})
        if name not in TOOL_PROPERTIES:
            raise RpcError(-32602, f"Unknown tool {name}")
        unknown = set(args) - TOOL_PROPERTIES[name]
        # Execution failures (a missing file, a bad regex, a lost game) go back as tool results the model can act on, not protocol errors
        try:
            result = call_tool(name, args)
        except Exception as e:
            result = {"ok": False, "error": str(e) if isinstance(e, RuntimeError) else f"{type(e).__name__}: {e}"}
        # Bridge-side tools drop unknown arguments, game-side ones get them reported by the backend too
        if unknown:
            result["ignored_args"] = sorted(unknown | set(result.get("ignored_args", [])))
        return {"content": [{"type": "text", "text": json.dumps(result, separators=(",", ":"))}], "isError": not result.get("ok", False)}
    if method == "ping":  # Legacy only, harmless to answer in the modern era too
        return {}
    raise RpcError(-32601, f"Unknown method {method}")


def self_test():
    """python Scripts/luma_mcp.py --self-test: the bridge-side math on synthetic R32_FLOAT readbacks, no game needed."""
    root = tempfile.mkdtemp(prefix="luma-mcp-selftest-")

    def readback(name, image, format_id=41):
        """R32_FLOAT from [h, w], or R32G32B32A32_FLOAT (format_id 2) from [h, w, 4]"""
        image = np.ascontiguousarray(image, np.float32)
        path = os.path.join(root, name + ".bin")
        image.tofile(path)
        with open(os.path.join(root, name + ".json"), "w") as f:
            json.dump({"format_id": format_id, "view_format_id": format_id, "width": image.shape[1], "rows": image.shape[0],
                       "row_pitch": image[0].nbytes, "slices": 1, "depth": 1}, f)
        return path

    ramp = np.tile((np.arange(16) + 0.5) / 16, (16, 1))
    full, same = readback("full", ramp), readback("same", ramp)
    result = compare(full, same, metrics=["correlation", "mean"])
    assert result["ok"] and result["differing_pixels"] == 0 and abs(result["metrics"]["correlation"] - 1) < 1e-6, result

    # The same ramp rendered at 50% into the top-left quarter of a full size target, stretched back over the whole frame
    sub_rect = np.zeros((16, 16))
    sub_rect[:8, :8] = (np.arange(8) + 0.5) / 8
    sub = readback("sub", sub_rect)
    result = compare(sub, full, rect_a=[0, 0, 8, 8], metrics=["correlation"])
    assert result["ok"] and result["resampled"] == "a" and result["metrics"]["correlation"] > 0.99, result
    assert compare(readback("small", sub_rect[:8, :8]), full)["ok"] is False  # Different sizes without rects

    halves = readback("halves", np.repeat([[0.0] * 8 + [1.0] * 8], 16, 0))
    checker = readback("checker", np.indices((16, 16)).sum(0) % 2)
    result = compare(halves, checker, metrics=["below:0.5", "detail"])
    assert result["metrics"]["a"]["below:0.5"] == 0.5 and result["metrics"]["a"]["detail"] == 0, result
    assert result["metrics"]["b"]["detail"] == 0.5, result
    assert compare(halves, checker, metrics=["bogus"])["ok"] is False

    # Golden: accept, the same image passes without leaving images, a changed one fails and keeps them
    global BASELINE_ROOT
    BASELINE_ROOT = os.path.join(root, "baselines")

    def rgba(name, image):
        return readback(name, image, 2)

    scene = np.ones((32, 32, 4)) * np.linspace(0.05, 4.0, 32)[None, :, None]
    shifted = scene.copy()
    shifted[8:24, 8:24, :3] *= [2.0, 0.5, 0.5]
    assert golden(rgba("scene", scene), "game/menu", "native")["ok"] is False  # No baseline yet
    assert golden(rgba("scene", scene), "game/menu", "native", accept=True)["max_flip"] == 1.0
    for case, backend_name in (("game/../x", "native"), ("..", "native"), ("game/menu", ".."), ("game/./x", "native")):
        assert "letters, digits" in golden(rgba("scene", scene), case, backend_name).get("error", ""), (case, backend_name)
    same_result = golden(rgba("scene2", scene), "game/menu", "native")
    try:
        import flip_evaluator  # noqa: F401
    except ImportError:
        assert same_result["ok"] is False and "flip-evaluator" in same_result["error"], same_result
        print("flip-evaluator missing: FLIP checks skipped")
    else:
        assert same_result["pass"] and same_result["flip"]["mode"] == "HDR" and "flip_png" not in same_result, same_result
        changed = golden(rgba("shifted", shifted), "game/menu", "native")
        assert not changed["pass"] and os.path.exists(changed["flip_png"]), changed
        assert golden(rgba("shifted", shifted), "game/menu", "native", accept=True, max_flip=100)["max_flip"] == 100
        assert golden(rgba("shifted2", shifted), "game/menu", "native")["pass"]
        os.remove(changed["flip_png"])
        os.remove(changed["diff_png"])
        os.remove(os.path.join(BASELINE_ROOT, "game", "menu", "native.golden.json"))  # A baseline without its settings: default 1.0
        assert golden(rgba("scene3", shifted), "game/menu", "native")["max_flip"] == 1.0

    shutil.rmtree(root)

    assert {"luma_ab", "luma_golden"} <= TOOL_PROPERTIES.keys() and len(INSTRUCTIONS) <= 2048, len(INSTRUCTIONS)
    undescribed = [(t["name"], k) for t in TOOLS for k, v in t["inputSchema"]["properties"].items() if "description" not in v]
    assert not undescribed, undescribed
    reply = json.loads(dispatch("tools/call", {"name": "luma_list_games", "arguments": {"bogus": 1}})["content"][0]["text"])
    assert reply["ignored_args"] == ["bogus"], reply
    print("self test passed")


def main():
    if "--self-test" in sys.argv:
        self_test()
        return
    for line in sys.stdin.buffer:
        if not line.strip():
            continue
        message = json.loads(line)
        if "id" not in message:
            continue  # Notifications need no answer
        try:
            response = {"jsonrpc": "2.0", "id": message["id"], "result": handle(message)}
        except Exception as e:  # Report every failure to the client instead of dying
            code, data = (e.code, e.data) if isinstance(e, RpcError) else (-32603, None)
            error = {"code": code, "message": str(e)}
            if data is not None:
                error["data"] = data
            response = {"jsonrpc": "2.0", "id": message["id"], "error": error}
        sys.stdout.buffer.write(json.dumps(response).encode() + b"\n")
        sys.stdout.buffer.flush()


if __name__ == "__main__":
    main()
