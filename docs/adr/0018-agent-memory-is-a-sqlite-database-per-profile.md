# Agent memory is a SQLite database per profile

On 2026-10-26 the user chose SQLite for [agent memory](../CONTEXT.md#language), one database in each profile folder, though uw keeps chats as JSON files. A typical user's memory grows over time, so the agent cannot read every memory on every request. It reads the [core memories](../CONTEXT.md#language) and searches the rest, and a database fits a growing, searched set better than a file read whole.

Search is word matching: the agent passes several words, and uw returns the sentences that contain any of them, the most matches first, then the newest. Chromium's SQLite has no full-text search compiled in.

Considered options: one JSON file per profile (rejected: it suits a small set read whole, not one that grows); turning on SQLite's FTS5 (rejected: one more Chromium patch to carry on every update, for better ranking only); embeddings (rejected: not every provider offers an embedding model, and Anthropic does not).
