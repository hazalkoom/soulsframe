#!/usr/bin/env python3
"""Host analyzer for SoulsFrame smoke-test summaries and JSONL frame captures.

Runs with the Python standard library alone (`json`, `statistics`) so CI and
developers can validate telemetry outputs without extra packages, and exposes an
optional `pandas` DataFrame helper for interactive notebook exploration.
"""

from __future__ import annotations

import argparse
import importlib
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any


def compute_percentile(sorted_values: list[float], fraction: float) -> float:
    """Compute a linear-interpolated percentile on an already-sorted non-empty list."""
    if not sorted_values:
        raise ValueError("sorted_values must not be empty")
    if len(sorted_values) == 1:
        return sorted_values[0]
    clamped = max(0.0, min(1.0, fraction))
    pos = clamped * (len(sorted_values) - 1)
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return sorted_values[lo]
    weight = pos - lo
    return sorted_values[lo] * (1.0 - weight) + sorted_values[hi] * weight


def summarize_frame_times(frame_times_ms: list[float], spike_threshold_ms: float = 25.0) -> dict[str, Any]:
    """Calculate average, p99 (1% low equivalent), p99.9 (0.1% low equivalent), and spike counts."""
    valid = [x for x in frame_times_ms if math.isfinite(x) and x > 0.0]
    if not valid:
        raise ValueError("No valid positive finite frame times supplied")
    ordered = sorted(valid)
    avg_ms = statistics.fmean(ordered)
    return {
        "count": len(ordered),
        "avg_ms": round(avg_ms, 4),
        "avg_fps": round(1000.0 / avg_ms, 2),
        "min_ms": round(ordered[0], 4),
        "max_ms": round(ordered[-1], 4),
        "p99_ms": round(compute_percentile(ordered, 0.99), 4),
        "p99_9_ms": round(compute_percentile(ordered, 0.999), 4),
        "spikes": sum(1 for x in ordered if x > spike_threshold_ms),
    }


def analyze_smoke_result(path: Path) -> dict[str, Any]:
    """Validate and summarize a `smoke-result.json` output emitted by `scripts/smoke-test.sh`."""
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"Expected JSON object in {path}")
    status = data.get("status")
    scenarios = data.get("scenarios", [])
    return {
        "kind": "smoke_result",
        "status": status,
        "scenario_count": len(scenarios) if isinstance(scenarios, list) else 0,
        "scenarios": scenarios,
    }


def analyze_jsonl_capture(path: Path, spike_threshold_ms: float = 25.0) -> dict[str, Any]:
    """Parse a SoulsFrame `*.jsonl` telemetry capture and summarize frame timings."""
    header: dict[str, Any] | None = None
    frame_times: list[float] = []
    total_draw_calls = 0
    pass_count = 0
    draw_sites = 0
    pass_breakdown: dict[str, dict[str, int]] = {}
    site_agg: dict[tuple[str, str, str, bool], dict[str, Any]] = {}
    resources: list[dict[str, Any]] = []

    with path.open("r", encoding="utf-8") as handle:
        for line_no, raw_line in enumerate(handle, start=1):
            stripped = raw_line.strip()
            if not stripped:
                continue
            event = json.loads(stripped)
            if not isinstance(event, dict):
                raise ValueError(f"{path}:{line_no}: expected JSON object")
            event_type = event.get("type")
            if event_type == "session_header" and header is None:
                header = event
            elif event_type == "frame":
                cpu_ms = event.get("cpu_frame_ms")
                if isinstance(cpu_ms, (int, float)):
                    frame_times.append(float(cpu_ms))
                draws = event.get("draw_calls")
                if isinstance(draws, int):
                    total_draw_calls += draws
            elif event_type == "gpu_pass":
                pass_count += 1
                tag = str(event.get("tag", "unknown"))
                entry = pass_breakdown.setdefault(tag, {"events": 0, "draws": 0})
                entry["events"] += 1
                entry["draws"] += int(event.get("draws", 0) or 0)
            elif event_type == "draw_site":
                draw_sites += 1
                rva = str(event.get("rva", "0x0"))
                api = str(event.get("api", "Unknown"))
                ps_hash = str(event.get("ps_hash", "0x0"))
                blend = bool(event.get("blend_enabled", False))
                key = (rva, api, ps_hash, blend)
                agg = site_agg.setdefault(
                    key,
                    {
                        "rva": rva,
                        "api": api,
                        "ps_hash": ps_hash,
                        "blend_enabled": blend,
                        "draws": 0,
                        "elements": 0,
                    },
                )
                agg["draws"] += int(event.get("count", 0) or 0)
                agg["elements"] += int(event.get("elements", 0) or 0)
            elif event_type == "resource":
                resources.append(event)

    top_draw_sites = sorted(site_agg.values(), key=lambda x: (-x["draws"], -x["elements"], x["rva"]))[:15]
    return {
        "kind": "jsonl_capture",
        "header": header,
        "frames": summarize_frame_times(frame_times, spike_threshold_ms) if frame_times else None,
        "total_draw_calls": total_draw_calls,
        "gpu_pass_events": pass_count,
        "pass_breakdown": pass_breakdown,
        "draw_site_events": draw_sites,
        "top_draw_sites": top_draw_sites,
        "resource_count": len(resources),
        "resources": resources,
    }


def load_frames_dataframe(path: Path) -> Any:
    """Optional pandas loader for interactive notebooks; raises RuntimeError if pandas is absent."""
    try:
        pandas = importlib.import_module("pandas")
    except ModuleNotFoundError as exc:
        raise RuntimeError("Install tools/py/requirements.txt to enable pandas DataFrame analysis") from exc

    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for raw_line in handle:
            stripped = raw_line.strip()
            if not stripped:
                continue
            event = json.loads(stripped)
            if isinstance(event, dict) and event.get("type") == "frame":
                rows.append(event)
    return pandas.DataFrame(rows)


def run_self_test() -> int:
    """Exercise stdlib analysis helpers without requiring external files or packages."""
    sample = [16.5, 16.6, 16.7, 16.6, 33.4, 16.5]
    summary = summarize_frame_times(sample, spike_threshold_ms=25.0)
    assert summary["count"] == 6, summary
    assert summary["spikes"] == 1, summary
    assert summary["p99_ms"] > 30.0, summary

    import tempfile

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        jsonl = tmp / "sample.jsonl"
        jsonl.write_text(
            "\n".join(
                [
                    json.dumps({"type": "session_header", "schema": 1, "game": "ds3"}),
                    json.dumps({"type": "resource", "frame_idx": 1, "id": "0x1234", "kind": "Texture2D", "rt": True}),
                    json.dumps({"type": "frame", "idx": 1, "cpu_frame_ms": 16.6, "draw_calls": 10}),
                    json.dumps({"type": "frame", "idx": 2, "cpu_frame_ms": 28.0, "draw_calls": 25}),
                    json.dumps({"type": "gpu_pass", "frame_idx": 2, "pass_id": 1, "tag": "particles_alpha", "draws": 20}),
                    json.dumps({"type": "draw_site", "frame_idx": 2, "rva": "0x1a00", "api": "DrawIndexed", "ps_hash": "0xaa", "blend_enabled": True, "count": 20, "elements": 600}),
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        parsed = analyze_jsonl_capture(jsonl)
        assert parsed["header"] is not None and parsed["header"]["game"] == "ds3"
        assert parsed["frames"] is not None and parsed["frames"]["spikes"] == 1
        assert parsed["total_draw_calls"] == 35
        assert parsed["gpu_pass_events"] == 1
        assert parsed["pass_breakdown"]["particles_alpha"]["draws"] == 20
        assert parsed["top_draw_sites"][0]["rva"] == "0x1a00"
        assert parsed["resource_count"] == 1

        smoke = tmp / "smoke-result.json"
        smoke.write_text(json.dumps({"status": "ok", "scenarios": ["offline_block", "resize"]}), encoding="utf-8")
        smoke_summary = analyze_smoke_result(smoke)
        assert smoke_summary["status"] == "ok"
        assert smoke_summary["scenario_count"] == 2

    print("analyze_capture.py self-test OK")
    return 0


def main(argv: list[str] | None = None) -> int:
    """CLI entry point for analyzing smoke results or JSONL frame telemetry."""
    parser = argparse.ArgumentParser(description="Analyze SoulsFrame telemetry or smoke-test outputs.")
    parser.add_argument("input", nargs="?", type=Path, help="Path to *.jsonl capture or smoke-result.json")
    parser.add_argument("--spike-ms", type=float, default=25.0, help="Frame-time spike threshold in milliseconds")
    parser.add_argument("--self-test", action="store_true", help="Run built-in verification checks")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()
    if args.input is None:
        parser.error("Provide an input file path or pass --self-test")

    if args.input.suffix == ".json":
        result = analyze_smoke_result(args.input)
    else:
        result = analyze_jsonl_capture(args.input, spike_threshold_ms=args.spike_ms)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
