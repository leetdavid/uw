# Each run of a scheduled chat is a subagent

A scheduled chat stays one conversation, but each run is a subagent that starts fresh with the request, the last few results, and the tracked values. It adds one message to the chat. Agreed on 2026-10-22.

## Considered options

- **Every run continues the conversation.** Each run would carry every earlier page reading, so its cost grows with every run. An hour apart, the provider's cache rarely helps.
- **A separate run each time, listed in the chat.** Cost stays flat, but the chat page fills with runs and reads as clutter.

## Consequences

Tracked values are stored apart from the conversation, so they can be charted and the main agent can answer questions over them.
