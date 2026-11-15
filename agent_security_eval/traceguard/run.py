from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

from .adapters import make_adapter
from .grader import grade
from .scenarios import load_fixtures, write_fixtures
from .tools import SimulatedTools


def main() -> None:
    parser = argparse.ArgumentParser(description="Run trace-based agent security scenarios")
    parser.add_argument("--config", required=True)
    parser.add_argument("--fixtures", default="fixtures/scenarios.json")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_text())
    fixture_path = Path(args.fixtures)
    if not fixture_path.exists():
        write_fixtures(fixture_path)
    scenarios = load_fixtures(fixture_path)
    out_dir = Path(args.out); out_dir.mkdir(parents=True, exist_ok=True)
    result_path = out_dir / "runs.jsonl"
    rows = []
    for model_config in config["models"]:
        adapter = make_adapter(model_config)
        for variant in config.get("prompt_variants", [{"name": "baseline", "append": ""}]):
            for run_index in range(config.get("runs_per_scenario", 1)):
                for scenario in scenarios:
                    tools = SimulatedTools(scenario.document)
                    error = None
                    try:
                        final = adapter.run(scenario, variant.get("append", ""), tools)
                    except Exception as exc:  # retain failed runs as auditable records
                        final, error = "", f"{type(exc).__name__}: {exc}"
                    result = grade(scenario, tools.trace, final)
                    rows.append({"timestamp": datetime.now(timezone.utc).isoformat(), "scenario_id": scenario.id,
                                 "category": scenario.category, "is_attack": scenario.is_attack, "model": model_config["name"],
                                 "prompt_variant": variant["name"], "run": run_index, "final": final,
                                 "trace": [call.to_dict() for call in tools.trace], "grade": result.to_dict(), "error": error})
    result_path.write_text("".join(json.dumps(row, sort_keys=True) + "\n" for row in rows))
    print(f"wrote {len(rows)} auditable runs to {result_path}")


if __name__ == "__main__":
    main()
