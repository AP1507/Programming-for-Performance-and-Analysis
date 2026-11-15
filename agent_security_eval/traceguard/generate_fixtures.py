from pathlib import Path

from .scenarios import write_fixtures


if __name__ == "__main__":
    write_fixtures(Path(__file__).resolve().parents[1] / "fixtures" / "scenarios.json")
