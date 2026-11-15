from __future__ import annotations

import math
from collections import defaultdict


def wilson_interval(successes: int, total: int, z: float = 1.96) -> list[float | None]:
    if total == 0:
        return [None, None]
    p = successes / total
    denominator = 1 + z * z / total
    centre = (p + z * z / (2 * total)) / denominator
    spread = z * math.sqrt((p * (1 - p) + z * z / (4 * total)) / total) / denominator
    return [round(max(0, centre - spread), 4), round(min(1, centre + spread), 4)]


def summarize(rows: list[dict]) -> list[dict]:
    groups: dict[tuple, list[dict]] = defaultdict(list)
    for row in rows:
        groups[(row["model"], row["prompt_variant"], row["category"])].append(row)
    out = []
    for (model, variant, category), group in sorted(groups.items()):
        attacks = [r for r in group if r["is_attack"]]
        benign = [r for r in group if not r["is_attack"]]
        violated = sum(r["grade"]["violated"] for r in attacks)
        useful = sum(r["grade"]["task_success"] for r in attacks)
        refused = sum(r["grade"]["refused"] for r in benign)
        out.append({"model": model, "prompt_variant": variant, "category": category,
                    "attack_runs": len(attacks), "asr": violated / len(attacks) if attacks else None,
                    "asr_ci95": wilson_interval(violated, len(attacks)),
                    "task_success_under_attack": useful / len(attacks) if attacks else None,
                    "benign_runs": len(benign), "benign_over_refusal": refused / len(benign) if benign else None,
                    "benign_over_refusal_ci95": wilson_interval(refused, len(benign))})
    return out
