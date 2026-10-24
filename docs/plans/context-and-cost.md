# Context and cost

Status: agreed on 2026-10-24, and built the same day: T170 to T172. See [Built](#built). The stages are T169 to T172 on the [board](roadmap.md#board).

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). The hard parts:

- Each of the three [wire formats](../adr/0007-uw-speaks-three-wire-formats-to-language-models.md) reports token usage its own way.
- Saved chats must keep opening after usage is added to their files.

## Agreed scope

A chat shows how full its model's context is and what the chat has cost, and a card says why.

### The indicator

- It sits at the right end of the chat's header, as `◔ 124k · $10.73`. It is hidden until the chat's first answer.
- **The ring and the tokens:** the latest request's input tokens, cached ones included, as the provider reports them. The ring fills toward whichever comes first: the point where uw makes room, a request of 400,000 characters (`kMaxRequestLength`, about 100k tokens), or the model's own limit. It turns amber at 80%.
- **The cost:** the chat's total so far.
- A provider that sends no usage shows `—`. Tokens show as `124k`; cost to the cent, and `<$0.01` below a cent.

### The card

Clicking the indicator opens a card under it, which closes on a click outside.

1. **Context.** A bar with the tokens against the limit, cut into what fills it: instructions and tools, the conversation, page readings and tool results, and images. uw splits the provider's count by each part's share of the request's characters. A line names the limit: "uw makes room at about 100k tokens", or the model's.
2. **Cost.** Fresh input, cache reads, cache writes, output with thinking, and command checks, each with its tokens and dollars, and "The cache saved $8.40" when it did.
3. **Model.** The provider and model, and its price per million input and output tokens.
4. **Charts.** Context per request, with the limit dashed and a mark where uw made room; and cost per message the user sent, whose bar shows the message's start and its cost on hover. Both run along the chat's requests. They reuse the charts of T151 once those land.

Left out: the session's id and times, message counts, the instructions' text, and the raw messages with an export.

### What counts

- The agent's requests in the chat, the [command checks](../CONTEXT.md#language) its terminal commands needed, and, for a scheduled chat, its runs. Each run's line on the chat page shows what that run cost.
- Not the tab organizer's requests, which belong to no chat.
- The context is always the chat's own latest request.

### Prices and limits

- A provider's own cost for a request counts when it sends one, as OpenRouter does. Otherwise uw prices it with [models.dev](https://models.dev)'s per-million-token rates for input, output, cache reads, and cache writes, which uw now keeps from the catalog it already reads ([ADR 0008](../adr/0008-uw-reads-what-a-model-can-do-from-models-dev.md)). Each request is priced with its own model, so a chat that changed model adds up right.
- A model with no price, such as one on Ollama or one models.dev doesn't list, shows tokens only, and the card says "No price for this model".
- The model's context limit also comes from models.dev.

### Keeping it

- Each request's tokens by kind, its cost, and its model are saved in the chat's file, so the card and its charts come back after a restart. A chat from before this shows "uw counts from Oct 24".
- A private chat shows the indicator and card, and saves nothing.
- uw reads a model's answer whole, never streamed, and a whole answer carries its usage in every format, so no request has to ask for it.

### Not in this plan

- Spending across chats or by day.
- A budget, or a warning beyond the amber ring.
- An inspector with the raw messages and an export.

## Design

T169 draws the indicator in its states, the card, and its charts on the [Doop canvas](../../AGENTS.md#designs), on the "Agent chats" page. The user signs it off before T171 starts.

## Built

T170 and T172, built on 2026-10-24:

- `components/ai_providers/usage.h` reads an answer's usage and prices it. The Anthropic and Responses answers carry their usage in the chat-completions shape, so one reader serves all three formats. The models.dev copy keeps each model's price and input limit: the catalog's input limit, or its context less up to 16,000 tokens for the answer.
- `components/agent/chat_usage.h` is one request's record. `AgentContext` keeps the chat's records with the chat, splits each request's characters into its parts, and notes when it made room. `AgentLoop` records each answer and each command check; a run's total goes on its line.
- A request stays under the model's input limit at 3 characters a token, or under 400,000 characters, whichever is less.

T171, built on 2026-10-24: `Summarize` in `components/agent/chat_usage.h` turns the records and the runs into what the card shows, and the chat page gets it as `OnUsage` after each request. Each record keeps the price it was charged at, for the split by kind and what the cache saved. The page is `src/usage.tsx`; its charts use the parts of T151's charts. Each run's line ends with its cost.

Where the build differs:

- Prices tiered above a context size, as some models have, are left out: uw's requests stay well below them.
- A command check through the decision model counts its tokens only when its answer carries chat-completions usage, and has no price.
- uw's limit in tokens is its 400,000 characters at the latest request's own tokens per character.
- The model line shows no provider icon, and the card says "Earlier requests aren't in these numbers" without the date the chat began, which uw does not keep.

Tests: `UsageTest.*`, `ChatUsageTest.*`, `CatalogTest.PricesAndInputLimitsComeFromTheCatalog`, and `AgentContextTest.*` in unit_tests; `AgentLoopBrowserTest.EachRequestCountsWhatItUsedAndCost`, `AgentLoopBrowserTest.ASmallModelsRequestsStayUnderItsLimit`, `AgentCheckBrowserTest.ACheckCountsOnItsOwn`, `ChatRunBrowserTest.ARunSaysWhatItsRequestsCost`, and `ScheduledChatsBrowserTest.ASavedScheduleRunsAtOnceAndTellsWhatItFound`, and `UwChatBrowserTest.TheIndicatorShowsContextAndCost` in browser_tests.

## Stages

Each stage is a ticket on the [board](roadmap.md#board) and ends with tests that pass on their own.

1. T169: the design.
2. T170: usage and prices: usage from the three formats, prices and limits from models.dev, each request's usage in the chat file, command checks, and runs.
3. T171: the indicator and the card with its charts. Needs T169 and T170.
4. T172: a request stays under the model's own limit as well as uw's 400,000 characters. It does not need the others.

## Acceptance checks

1. Against a test server for each format, a chat's indicator shows the latest request's input tokens and the chat's cost, priced from a models.dev entry, and an OpenRouter response's own cost counts instead.
2. A model with a 1M-token limit fills the ring toward uw's 400,000 characters; one with a 32k limit fills it toward 32k.
3. The card's context bar splits the tokens across its parts, and its charts show each request and each message's cost.
4. A command check's cost shows on its own row, and a scheduled chat's total includes its runs.
5. A restarted chat shows the same numbers. A private chat saves none. A chat saved before this opens.
6. With a 32k-token model, a request never goes past 32k tokens.
7. `pnpm run check` and the focused test suites pass. Browser tests stay headless.
