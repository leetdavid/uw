# uw catches one Help address to open a chat

On 2026-10-28 the user asked for a way to take a question from a [Help](../CONTEXT.md#language) page into uw's chat. A web page cannot open `uw://chat`: Chromium keeps web pages away from its own pages, and uw's `uw://` names follow the same rule since T9. So Help links to `uwbrowser.com/ask?q=<text>`, and uw catches that one address before it loads and opens a new chat with the text in its composer.

It works with an ordinary link, and in another browser the same address loads a page that offers the download. Any site can link to it, so the text only fills the composer and nothing is sent until the user presses Send.

Considered options: a macOS link type, `uwbrowser://ask` (rejected: it needs registering with macOS, and inside uw Chromium first asks "Open uw?"). Copying the question and asking the reader to press Cmd+T and Cmd+Return (rejected: too clumsy for a button).
