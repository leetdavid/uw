# Attached files are copies the agent reaches by path

On 2026-10-26 the user asked to attach files, not just images, to a chat message. uw keeps a copy of each [attached file](../CONTEXT.md#language) in the chat's own folder, `~/Library/Application Support/uw/Chat files/<chat>/`, and tells the agent its path. The model also gets the file's contents when it can read them: text, a PDF for a model that takes PDFs, or a PDF's text for one that doesn't.

Both, because each covers what the other can't. The agent's terminal can convert, unzip, or edit any file, but a stock Mac has no command that reads a PDF's text. The model reads text and PDFs well, but can't run anything on them.

A copy, because a web page can't learn where a file came from. Only a native file picker, or browser code that catches a drop, knows the original path, and a pasted screenshot has none. With a copy, picking, dragging, and pasting all work the same way, and the agent never changes the original.

The folder sits beside the profiles, not inside one, because a profile folder holds cookies and saved passwords, and a path into it invites the agent to look around. A chat's folder goes when the chat is deleted, and a [private chat](../CONTEXT.md#language)'s when the private windows close.

Considered options: the model reads the file only (rejected: the agent can't work on it). The agent gets the path only (rejected: PDFs read badly). The original path (rejected: needs a native picker and drop handling, and still needs copies for pastes).
