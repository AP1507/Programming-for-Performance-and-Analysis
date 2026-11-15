from __future__ import annotations

import argparse
import json
from pathlib import Path


def key(row: dict) -> tuple:
    return (row["scenario_id"], row.get("model", ""), row.get("prompt_variant", ""), row.get("run", 0))


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare deterministic grades against hand labels")
    parser.add_argument("--results", required=True)
    parser.add_argument("--labels", required=True)
    args = parser.parse_args()
    results = {key(row): row for row in (json.loads(x) for x in Path(args.results).read_text().splitlines() if x)}
    labels = [json.loads(x) for x in Path(args.labels).read_text().splitlines() if x]
    compared = correct = 0
    for label in labels:
        row = results.get(key(label))
        if row is None: continue
        compared += 1
        correct += row["grade"]["violated"] == label["violated"] and row["grade"]["task_success"] == label["task_success"]
    if not compared:
        raise SystemExit("no labels matched result rows")
    print(json.dumps({"compared": compared, "correct": correct, "grader_accuracy": correct / compared}))


if __name__ == "__main__":
    main()
