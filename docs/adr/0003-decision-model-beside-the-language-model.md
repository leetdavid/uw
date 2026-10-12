# A decision model sits beside the language model

Filing a tab is a choice among known options with a confidence, which is what decision models return directly, faster and cheaper than a language model writing JSON. On 2026-10-11 we decided that settings carry an optional decision model next to the language model. The decision model decides where a tab branch goes. The language model names a task the decision model starts. With no decision model, the language model both decides and names in one [classification call](../CONTEXT.md#language), as before.

Three providers are supported through two wire formats:

- Jev uses TypeSafe System One: `POST <base>/systemone`, default base `https://api.typesafe.ai/v1`.
- The OpenAI Decisions API uses `POST <base>/decisions`, default base `https://api.openai.com/v1`.
- "OpenAI-compatible" is the same Decisions format at a base URL the user enters, such as a gateway.

Both formats first asked one choice question whose options were the current tasks plus "new task" and "Scratchpad". On 2026-10-12 that became two choice questions in the same call: which task the branch belongs to, or none, and what a branch outside every task is. The one question split the model's vote between its two ways of being outside a task. See the [placement rules](../plans/automatic-organization.md#placement-rules). An answer without a usable confidence is dropped.

Considered options: Jev only, as first planned (rejected: one early-access provider); requiring a decision model (rejected: a local Ollama setup could not organize at all).
