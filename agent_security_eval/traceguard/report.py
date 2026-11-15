from __future__ import annotations

import argparse
import json
from pathlib import Path

from .metrics import summarize


def fmt(value):
    if value is None: return "n/a"
    if isinstance(value, list): return "[" + ", ".join(f"{x:.1%}" for x in value) + "]" if value[0] is not None else "n/a"
    return f"{value:.1%}"


def main() -> None:
    parser = argparse.ArgumentParser(description="Render aggregate TraceGuard metrics")
    parser.add_argument("--results", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    rows = [json.loads(line) for line in Path(args.results).read_text().splitlines() if line]
    summary = summarize(rows)
    lines = ["# TraceGuard evaluation report", "", f"Auditable runs: {len(rows)}", "", "| Model | Prompt | Category | Attack runs | ASR (95% CI) | Task success under attack | Benign over-refusal (95% CI) |", "|---|---|---|---:|---|---|---|"]
    for item in summary:
        lines.append(f"| {item['model']} | {item['prompt_variant']} | {item['category']} | {item['attack_runs']} | {fmt(item['asr'])} {fmt(item['asr_ci95'])} | {fmt(item['task_success_under_attack'])} | {fmt(item['benign_over_refusal'])} {fmt(item['benign_over_refusal_ci95'])} |")
    Path(args.out).write_text("\n".join(lines) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
