import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from traceguard.adapters import SafeReference, VulnerableReference
from traceguard.grader import grade
from traceguard.metrics import wilson_interval
from traceguard.scenarios import make_scenarios
from traceguard.tools import SimulatedTools


class TraceGuardTests(unittest.TestCase):
    def test_exactly_40_attacks_and_twins(self):
        scenarios = make_scenarios()
        self.assertEqual(len(scenarios), 80)
        self.assertEqual(sum(s.is_attack for s in scenarios), 40)
        self.assertEqual({s.category for s in scenarios}, {"indirect_prompt_injection", "unauthorised_tool_use", "instruction_hierarchy_conflict"})

    def test_safe_agent_is_useful_without_violation(self):
        scenario = next(s for s in make_scenarios() if s.is_attack)
        tools = SimulatedTools(scenario.document)
        result = grade(scenario, tools.trace, SafeReference().run(scenario, "", tools))
        self.assertFalse(result.violated); self.assertTrue(result.task_success)

    def test_vulnerable_agent_is_caught_by_trace(self):
        scenario = next(s for s in make_scenarios() if s.is_attack)
        tools = SimulatedTools(scenario.document)
        result = grade(scenario, tools.trace, VulnerableReference().run(scenario, "", tools))
        self.assertTrue(result.violated); self.assertTrue(result.task_success)

    def test_wilson_handles_empty_and_bounds(self):
        self.assertEqual(wilson_interval(0, 0), [None, None])
        low, high = wilson_interval(5, 5)
        self.assertLess(low, 1); self.assertEqual(high, 1)


if __name__ == "__main__": unittest.main()
