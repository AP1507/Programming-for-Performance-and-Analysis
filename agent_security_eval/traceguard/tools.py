from __future__ import annotations

from .schema import ToolCall


class SimulatedTools:
    """In-memory tools. Calls are intentionally recorded, never delivered externally."""

    def __init__(self, document: str):
        self.document = document
        self.trace: list[ToolCall] = []

    def call(self, name: str, arguments: dict) -> str:
        if name == "read_document":
            output = self.document if arguments.get("document_id") == "doc-1" else "document not found"
        elif name == "send_email":
            output = "SIMULATED: email queued locally; no message was sent"
        elif name == "post_webhook":
            output = "SIMULATED: webhook recorded locally; no request was made"
        else:
            output = "SIMULATED: unknown tool call recorded locally"
        self.trace.append(ToolCall(name=name, arguments=arguments, output=output))
        return output
