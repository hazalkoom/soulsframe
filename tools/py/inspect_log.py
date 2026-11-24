#!/usr/bin/env python3
"""Parse and summarize SoulsFrame runtime logs (`soulsframe.log`) for quick debugging.

Usage:
  python3 tools/py/inspect_log.py path/to/soulsframe.log [--json]
  python3 tools/py/inspect_log.py --self-test
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

# Supports both legacy `[INFO] msg` and timestamped `[+0.123s][T:01a4] [INFO] msg`.
LINE_RE = re.compile(
    r"^(?:\[\+(?P<elapsed>[0-9.]+)s\]\[T:(?P<tid>[0-9a-fA-F]+)\]\s+)?"
    r"\[(?P<level>DEBUG|INFO|WARN|ERROR)\]\s+(?P<msg>.*)$"
)
START_RE = re.compile(r"^SoulsFrame v(?P<version>\S+) starting \((?P<build_type>release|probe) build\)$")
MODULE_RE = re.compile(r"^module\s+(?P<name>\S+)\s+:\s+(?P<state>ACTIVE|INACTIVE)\s+\((?P<detail>.*)\)$")
DIAG_RE = re.compile(r"^config diagnostic \[(?P<code>[^\]]+)\]:\s+(?P<msg>.*)$")
PROFILE_RE = re.compile(r"^profile=(?P<profile>\S+)\s+engine_hooks_allowed=(?P<allowed>true|false)$")
TELEM_RE = re.compile(
    r"^avg frame time (?P<avg_ms>[0-9.]+) ms \((?P<fps>[0-9.]+) fps\) \| "
    r"p1_low (?P<p1_ms>[0-9.]+) ms \| spikes (?P<spikes>\d+)$"
)
HOTKEY_RE = re.compile(r"^Hotkey (?P<action>\S+) -> (?P<state>ON|OFF)$")


def parse_log_text(text: str) -> dict[str, Any]:
    """Parse raw `soulsframe.log` text into a structured diagnostic summary."""
    summary: dict[str, Any] = {
        "line_count": 0,
        "version": None,
        "build_type": None,
        "mod_enabled": True,
        "config_summary": None,
        "offline_guard": "unknown",
        "offline_reason": None,
        "exe_sha256": None,
        "profile": None,
        "engine_hooks_allowed": None,
        "overlays_detected": [],
        "modules": {},
        "diagnostics": [],
        "hooks": {
            "present": False,
            "wndproc": False,
            "flip": False,
            "particles": False,
            "probe_context": False,
        },
        "hotkey_events": [],
        "resizes": [],
        "latest_telemetry": None,
        "counts_by_level": {"DEBUG": 0, "INFO": 0, "WARN": 0, "ERROR": 0},
        "warnings": [],
        "errors": [],
        "hints": [],
    }

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        summary["line_count"] += 1

        m = LINE_RE.match(line)
        if m:
            elapsed = m.group("elapsed")
            tid = m.group("tid")
            level = m.group("level")
            msg = m.group("msg")
        else:
            elapsed = None
            tid = None
            level = "INFO"
            msg = line

        if level in summary["counts_by_level"]:
            summary["counts_by_level"][level] += 1

        if level == "WARN":
            summary["warnings"].append(msg)
        elif level == "ERROR":
            summary["errors"].append(msg)

        if m_start := START_RE.match(msg):
            summary["version"] = m_start.group("version")
            summary["build_type"] = m_start.group("build_type")
        elif msg.startswith("config summary: "):
            summary["config_summary"] = msg.removeprefix("config summary: ")
        elif msg == "Mod disabled by general.mod_enabled = false":
            summary["mod_enabled"] = False
        elif msg.startswith("OFFLINE GUARD BLOCKED MOD INIT: "):
            summary["offline_guard"] = "blocked"
            summary["offline_reason"] = msg.removeprefix("OFFLINE GUARD BLOCKED MOD INIT: ")
        elif msg.startswith("host executable SHA-256: "):
            summary["exe_sha256"] = msg.removeprefix("host executable SHA-256: ")
            if summary["offline_guard"] == "unknown":
                summary["offline_guard"] = "allowed"
        elif m_prof := PROFILE_RE.match(msg):
            summary["profile"] = m_prof.group("profile")
            summary["engine_hooks_allowed"] = m_prof.group("allowed") == "true"
        elif "detected overlay/injector module loaded: " in msg:
            part = msg.split("detected overlay/injector module loaded: ", 1)[1]
            dll = part.split(" (", 1)[0].strip()
            if dll not in summary["overlays_detected"]:
                summary["overlays_detected"].append(dll)
        elif m_diag := DIAG_RE.match(msg):
            summary["diagnostics"].append(
                {"severity": level, "code": m_diag.group("code"), "message": m_diag.group("msg")}
            )
        elif m_mod := MODULE_RE.match(msg):
            summary["modules"][m_mod.group("name")] = {
                "active": m_mod.group("state") == "ACTIVE",
                "detail": m_mod.group("detail"),
            }
        elif msg.startswith("Present hook installed"):
            summary["hooks"]["present"] = True
        elif msg.startswith("OverlayModule: hooked OutputWindow WndProc"):
            summary["hooks"]["wndproc"] = True
        elif msg.startswith("PresentationModule: IDXGIFactory::CreateSwapChain hooked"):
            summary["hooks"]["flip"] = True
        elif msg.startswith("ParticleModule: ") and "hooks installed" in msg:
            summary["hooks"]["particles"] = True
        elif "ID3D11DeviceContext hooks installed" in msg:
            summary["hooks"]["probe_context"] = True
        elif msg.startswith("IDXGISwapChain::ResizeBuffers("):
            summary["resizes"].append(msg)
        elif m_hk := HOTKEY_RE.match(msg):
            summary["hotkey_events"].append(
                {
                    "action": m_hk.group("action"),
                    "state": m_hk.group("state"),
                    "elapsed_s": float(elapsed) if elapsed is not None else None,
                    "tid": tid,
                }
            )
        elif m_tel := TELEM_RE.match(msg):
            summary["latest_telemetry"] = {
                "avg_ms": float(m_tel.group("avg_ms")),
                "fps": float(m_tel.group("fps")),
                "p1_low_ms": float(m_tel.group("p1_ms")),
                "spikes": int(m_tel.group("spikes")),
            }

    hints: list[str] = []
    if summary["offline_guard"] == "blocked":
        hints.append(
            "Hooks blocked by offline guard: set offline_acknowledged = true under [general] in "
            "soulsframe.toml (offline play only)."
        )
    if summary["overlays_detected"]:
        hints.append(
            "Third-party overlay(s) loaded ("
            + ", ".join(summary["overlays_detected"])
            + "): disable Steam Overlay / RTSS / ReShade if you see hook conflicts."
        )
    failed_mods = [
        name
        for name, info in summary["modules"].items()
        if not info["active"] and info["detail"] == "init failed"
    ]
    if failed_mods:
        hints.append("Module init failed for: " + ", ".join(failed_mods) + ".")
    if summary["errors"]:
        hints.append(f"{len(summary['errors'])} error(s) logged; inspect errors[] for root cause.")
    summary["hints"] = hints
    return summary


def format_human(summary: dict[str, Any], log_path: str) -> str:
    """Format a parsed log summary as concise terminal text."""
    lines = [
        f"=== SoulsFrame Log Summary: {log_path} ===",
        f"Build         : {summary['version'] or 'unknown'} ({summary['build_type'] or 'unknown'})",
        f"Offline Guard : {summary['offline_guard']}"
        + (f" ({summary['offline_reason']})" if summary["offline_reason"] else ""),
        f"Profile       : {summary['profile'] or 'unknown'} "
        f"(engine_hooks_allowed={summary['engine_hooks_allowed']})",
        f"Config        : {summary['config_summary'] or 'unknown'}",
        f"Hooks         : present={summary['hooks']['present']} wndproc={summary['hooks']['wndproc']} "
        f"flip={summary['hooks']['flip']} particles={summary['hooks']['particles']}",
        f"Counts        : DEBUG={summary['counts_by_level']['DEBUG']} "
        f"INFO={summary['counts_by_level']['INFO']} "
        f"WARN={summary['counts_by_level']['WARN']} "
        f"ERROR={summary['counts_by_level']['ERROR']}",
    ]
    if summary["modules"]:
        mod_parts = [
            f"{k}={'ON' if v['active'] else 'OFF'}" for k, v in summary["modules"].items()
        ]
        lines.append("Modules       : " + ", ".join(mod_parts))
    if summary["latest_telemetry"]:
        t = summary["latest_telemetry"]
        lines.append(
            f"Telemetry     : {t['fps']:.1f} FPS | avg {t['avg_ms']:.3f} ms | "
            f"1% low {t['p1_low_ms']:.3f} ms | spikes {t['spikes']}"
        )
    if summary["hotkey_events"]:
        ev_str = ", ".join(f"{e['action']}->{e['state']}" for e in summary["hotkey_events"])
        lines.append(f"Hotkeys ({len(summary['hotkey_events'])})   : {ev_str}")
    if summary["resizes"]:
        lines.append(f"Resizes ({len(summary['resizes'])})   : {summary['resizes'][-1]}")
    if summary["warnings"]:
        lines.append("Warnings      :")
        for w in summary["warnings"]:
            lines.append(f"  - {w}")
    if summary["errors"]:
        lines.append("Errors        :")
        for err in summary["errors"]:
            lines.append(f"  - {err}")
    if summary["hints"]:
        lines.append("Actionable Hints:")
        for h in summary["hints"]:
            lines.append(f"  * {h}")
    return "\n".join(lines)


class _SelfTest(unittest.TestCase):
    def test_timestamped_healthy_log(self) -> None:
        sample = "\n".join(
            [
                "[+0.001s][T:01a4] [INFO] SoulsFrame v1.0.0 starting (release build)",
                "[+0.002s][T:01a4] [INFO] config summary: log_level=info present.mode=game max_fps=60 modules=[frame_telemetry,overlay]",
                "[+0.010s][T:01a4] [INFO] host executable SHA-256: deadbeef",
                "[+0.011s][T:01a4] [INFO] profile=ds3 engine_hooks_allowed=false",
                "[+0.025s][T:01a4] [INFO] Present hook installed at 00007FF81234",
                "[+0.026s][T:01a4] [INFO] module frame_telemetry      : ACTIVE (active)",
                "[+0.027s][T:01a4] [INFO] module overlay              : ACTIVE (active)",
                "[+0.500s][T:02bc] [INFO] OverlayModule: hooked OutputWindow WndProc for hotkey capture",
                "[+1.200s][T:02bc] [INFO] Hotkey toggle_upscaler -> ON",
                "[+2.000s][T:02bc] [INFO] IDXGISwapChain::ResizeBuffers(1920x1080, buffers=1, fmt=28, flags=0x0)",
                "[+2.500s][T:02bc] [INFO] avg frame time 16.667 ms (60.0 fps) | p1_low 17.200 ms | spikes 0",
            ]
        )
        res = parse_log_text(sample)
        self.assertEqual(res["version"], "1.0.0")
        self.assertEqual(res["build_type"], "release")
        self.assertEqual(res["offline_guard"], "allowed")
        self.assertTrue(res["hooks"]["present"])
        self.assertTrue(res["hooks"]["wndproc"])
        self.assertEqual(len(res["hotkey_events"]), 1)
        self.assertEqual(res["hotkey_events"][0]["tid"], "02bc")
        self.assertAlmostEqual(res["latest_telemetry"]["fps"], 60.0)
        self.assertEqual(res["hints"], [])

    def test_offline_blocked_and_overlay_warning_legacy_format(self) -> None:
        sample = "\n".join(
            [
                "[INFO] SoulsFrame v1.0.0 starting (release build)",
                "[WARN] OFFLINE GUARD BLOCKED MOD INIT: offline_acknowledged is false",
                "[WARN] warning: detected overlay/injector module loaded: RTSSHooks64.dll (disable overlays)",
                "[ERROR] MH_CreateHook(Present) failed",
            ]
        )
        res = parse_log_text(sample)
        self.assertEqual(res["offline_guard"], "blocked")
        self.assertEqual(res["overlays_detected"], ["RTSSHooks64.dll"])
        self.assertEqual(res["counts_by_level"]["WARN"], 2)
        self.assertEqual(res["counts_by_level"]["ERROR"], 1)
        self.assertEqual(len(res["hints"]), 3)

    def test_cli_json_and_text_roundtrip(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "soulsframe.log"
            p.write_text("[+0.001s][T:0001] [INFO] SoulsFrame v1.0.0 starting (release build)\n", encoding="utf-8")
            parsed = parse_log_text(p.read_text(encoding="utf-8"))
            self.assertIn("SoulsFrame Log Summary", format_human(parsed, str(p)))
            self.assertIn('"version": "1.0.0"', json.dumps(parsed))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Inspect and summarize a SoulsFrame runtime log.")
    parser.add_argument("log_file", nargs="?", help="Path to soulsframe.log")
    parser.add_argument("--json", action="store_true", help="Emit structured JSON summary")
    parser.add_argument("--self-test", action="store_true", help="Run built-in unit tests")
    args = parser.parse_args(argv)

    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(_SelfTest)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        return 0 if result.wasSuccessful() else 1

    if not args.log_file:
        parser.error("log_file is required unless --self-test is passed")

    path = Path(args.log_file)
    if not path.is_file():
        print(f"ERROR: Log file not found: {path}", file=sys.stderr)
        return 2

    summary = parse_log_text(path.read_text(encoding="utf-8", errors="replace"))
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(format_human(summary, str(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
