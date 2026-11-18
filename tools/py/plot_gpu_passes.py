#!/usr/bin/env python3
"""Plot GPU pass timings from a SoulsFrame JSONL capture.

Reads a `*.jsonl` telemetry capture, parses `gpu_pass` records, and calculates
the total and average GPU milliseconds per pass tag (particles_alpha, gbuffer,
shadow, postprocess, ...). Emits a Markdown/CSV summary table and, when
matplotlib is available, a bar chart PNG.

Runs on the Python standard library alone for `--self-test` (CI-friendly, zero
extra dependencies). With matplotlib installed, pass `--chart` to write a PNG.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import math
import sys
import tempfile
from pathlib import Path
from typing import Any


def load_gpu_passes(path: Path) -> list[dict[str, Any]]:
    """Return every `gpu_pass` event from a JSONL capture, in file order."""
    passes: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, raw_line in enumerate(handle, start=1):
            stripped = raw_line.strip()
            if not stripped:
                continue
            event = json.loads(stripped)
            if not isinstance(event, dict):
                raise ValueError(f"{path}:{line_no}: expected JSON object")
            if event.get("type") == "gpu_pass":
                passes.append(event)
    return passes


def summarize_by_tag(passes: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    """Aggregate total/average/count GPU ms per pass tag.

    Only events carrying a numeric `gpu_ms` are counted. Tags with no GPU
    timing appear with zero samples.
    """
    agg: dict[str, dict[str, Any]] = {}
    for ev in passes:
        tag = str(ev.get("tag", "unknown"))
        entry = agg.setdefault(
            tag,
            {"tag": tag, "events": 0, "gpu_samples": 0, "gpu_ms_total": 0.0},
        )
        entry["events"] += 1
        gpu_ms = ev.get("gpu_ms")
        if isinstance(gpu_ms, (int, float)) and math.isfinite(float(gpu_ms)):
            entry["gpu_samples"] += 1
            entry["gpu_ms_total"] += float(gpu_ms)
    for entry in agg.values():
        if entry["gpu_samples"] > 0:
            entry["gpu_ms_avg"] = round(entry["gpu_ms_total"] / entry["gpu_samples"], 4)
        else:
            entry["gpu_ms_avg"] = 0.0
        entry["gpu_ms_total"] = round(entry["gpu_ms_total"], 4)
    return agg


def summary_table(agg: dict[str, dict[str, Any]]) -> str:
    """Render the per-tag aggregation as a Markdown table."""
    lines: list[str] = []
    lines.append("| Pass Tag | Events | GPU Samples | GPU ms Total | GPU ms Avg |")
    lines.append("|----------|--------|-------------|--------------|------------|")
    for tag in sorted(agg):
        e = agg[tag]
        lines.append(
            f"| {tag} | {e['events']} | {e['gpu_samples']} | "
            f"{e['gpu_ms_total']} | {e['gpu_ms_avg']} |"
        )
    return "\n".join(lines)


def summary_csv(agg: dict[str, dict[str, Any]]) -> str:
    """Render the per-tag aggregation as CSV text."""
    buf = io.StringIO()
    writer = csv.DictWriter(
        buf,
        fieldnames=["tag", "events", "gpu_samples", "gpu_ms_total", "gpu_ms_avg"],
    )
    writer.writeheader()
    for tag in sorted(agg):
        e = agg[tag]
        writer.writerow(
            {
                "tag": tag,
                "events": e["events"],
                "gpu_samples": e["gpu_samples"],
                "gpu_ms_total": e["gpu_ms_total"],
                "gpu_ms_avg": e["gpu_ms_avg"],
            }
        )
    return buf.getvalue()


def render_bar_chart(agg: dict[str, dict[str, Any]], output: Path) -> None:
    """Write a PNG bar chart of average GPU ms per tag (matplotlib required)."""
    try:
        import matplotlib  # type: ignore

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt  # type: ignore
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "matplotlib is required for --chart; install tools/py/requirements.txt"
        ) from exc

    if not agg:
        return

    tags = sorted(agg)
    avgs = [agg[t]["gpu_ms_avg"] for t in tags]
    totals = [agg[t]["gpu_ms_total"] for t in tags]

    fig, ax = plt.subplots(figsize=(max(6, len(tags) * 1.2), 4))
    bars = ax.bar(tags, avgs, color="#4a90e2", edgecolor="#2c5aa0", linewidth=0.8)
    ax.set_ylabel("Average GPU ms per pass")
    ax.set_title("GPU Pass Timing by Tag (SoulsFrame JSONL capture)")
    ax.set_xlabel("Pass tag")
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    ax.set_axisbelow(True)

    for bar, total in zip(bars, totals):
        height = bar.get_height()
        ax.annotate(
            f"{total:.1f} ms total",
            xy=(bar.get_x() + bar.get_width() / 2, height),
            xytext=(0, 3),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=8,
        )

    plt.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)


def run_self_test() -> int:
    """Verify parsing/aggregation with a synthetic capture, zero dependencies."""
    import tempfile

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        jsonl = tmp / "sample.jsonl"
        events = [
            {"type": "session_header", "schema": 1, "game": "ds3"},
            {"type": "gpu_pass", "frame_idx": 1, "pass_id": 1, "tag": "particles_alpha", "draws": 20, "gpu_ms": 4.25},
            {"type": "gpu_pass", "frame_idx": 1, "pass_id": 2, "tag": "gbuffer", "draws": 40, "gpu_ms": 8.50},
            {"type": "gpu_pass", "frame_idx": 2, "pass_id": 1, "tag": "particles_alpha", "draws": 20, "gpu_ms": 3.75},
            {"type": "gpu_pass", "frame_idx": 2, "pass_id": 2, "tag": "gbuffer", "draws": 40, "gpu_ms": 9.00},
            {"type": "gpu_pass", "frame_idx": 3, "pass_id": 1, "tag": "shadow", "draws": 10},  # no gpu_ms
            {"type": "gpu_pass", "frame_idx": 3, "pass_id": 2, "tag": "particles_alpha", "draws": 20, "gpu_ms": 4.00},
            {"type": "frame", "idx": 3, "cpu_frame_ms": 16.6},
        ]
        jsonl.write_text(
            "\n".join(json.dumps(e) for e in events) + "\n", encoding="utf-8"
        )

        passes = load_gpu_passes(jsonl)
        assert len(passes) == 6, passes

        agg = summarize_by_tag(passes)
        assert set(agg) == {"particles_alpha", "gbuffer", "shadow"}, agg

        pa = agg["particles_alpha"]
        assert pa["events"] == 3, pa
        assert pa["gpu_samples"] == 3, pa
        assert pa["gpu_ms_total"] == 12.0, pa  # 4.25 + 3.75 + 4.00
        assert pa["gpu_ms_avg"] == 4.0, pa

        gb = agg["gbuffer"]
        assert gb["events"] == 2, gb
        assert gb["gpu_samples"] == 2, gb
        assert gb["gpu_ms_total"] == 17.5, gb  # 8.50 + 9.00
        assert gb["gpu_ms_avg"] == 8.75, gb

        sh = agg["shadow"]
        assert sh["events"] == 1, sh
        assert sh["gpu_samples"] == 0, sh
        assert sh["gpu_ms_total"] == 0.0, sh
        assert sh["gpu_ms_avg"] == 0.0, sh

        md = summary_table(agg)
        assert "| particles_alpha |" in md
        assert "| gbuffer |" in md
        assert "| shadow |" in md

        csv_out = summary_csv(agg)
        assert "tag,events,gpu_samples,gpu_ms_total,gpu_ms_avg" in csv_out
        assert "particles_alpha,3,3,12.0,4.0" in csv_out

        # Chart path requires matplotlib; only verify it raises cleanly when absent.
        try:
            render_bar_chart(agg, tmp / "chart.png")
        except RuntimeError:
            pass  # expected without matplotlib installed

    print("plot_gpu_passes.py self-test OK")
    return 0


def main(argv: list[str] | None = None) -> int:
    """CLI: summarize GPU pass timings from a JSONL capture."""
    parser = argparse.ArgumentParser(
        description="Summarize GPU pass timings from a SoulsFrame JSONL capture."
    )
    parser.add_argument("input", nargs="?", type=Path, help="Path to *.jsonl capture")
    parser.add_argument(
        "--self-test", action="store_true", help="Run built-in verification checks"
    )
    parser.add_argument(
        "--markdown", type=Path, help="Write Markdown summary table to this path"
    )
    parser.add_argument(
        "--csv", type=Path, help="Write CSV summary table to this path"
    )
    parser.add_argument(
        "--chart", type=Path, help="Write bar chart PNG to this path (matplotlib required)"
    )
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    if args.input is None:
        parser.error("Provide an input file path or pass --self-test")

    passes = load_gpu_passes(args.input)
    agg = summarize_by_tag(passes)

    if not agg:
        print("No gpu_pass events found in capture.", file=sys.stderr)
        return 1

    md = summary_table(agg)
    print(md)
    print()

    if args.markdown:
        args.markdown.write_text(md + "\n", encoding="utf-8")
        print(f"Markdown summary written to {args.markdown}")
    if args.csv:
        args.csv.write_text(summary_csv(agg), encoding="utf-8")
        print(f"CSV summary written to {args.csv}")
    if args.chart:
        render_bar_chart(agg, args.chart)
        print(f"Bar chart written to {args.chart}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
