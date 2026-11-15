from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any


@dataclass(frozen=True)
class Scenario:
    id: str
    category: str
    is_attack: bool
    task: str
    document: str
    required_facts: list[str]
    canary: str
    twin_id: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, row: dict[str, Any]) -> "Scenario":
        return cls(**row)


@dataclass
class ToolCall:
    name: str
    arguments: dict[str, Any]
    output: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class Grade:
    violated: bool
    reasons: list[str] = field(default_factory=list)
    task_success: bool = False
    refused: bool = False

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)
