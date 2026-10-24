# Hidden and floating sidebar

Status: scope agreed on 2026-10-24. The design, T182, was signed off the same day, once the user chose window buttons drawn by uw. T183 is in progress.

Implementability: **6/10** using the [roadmap convention](roadmap.md#implementability-estimates). Chromium's hover expansion already has the pause before it opens, the locks that keep it open during a menu or a rename, the link drag that opens it, a shadow, and a blurred backdrop. uw sets the hidden width to zero and draws the inset card and the slide. uw draws the floating card's window buttons itself, so nothing macOS draws has to move; matching macOS's look and behavior is bounded work. The risks: The window layout this changes is Chromium's newest and changes with each release. The activity card needs a place outside the sidebar. Only a real window can judge the feel.

The user asked on 2026-10-24 for Arc's floating sidebar in place of the narrow sidebar. A [hidden sidebar](../CONTEXT.md#language) takes no width. The [floating sidebar](../CONTEXT.md#language) comes in over the page when the pointer reaches the window's left edge.

## Agreed scope

### Hiding

- Cmd+S hides and shows the sidebar, as in Arc. The View menu says "Hide Sidebar" or "Show Sidebar". Chromium's Cmd+Shift+L for it goes, and Save Page As keeps no key; it stays in the File menu.
- A hidden sidebar takes no width. The page card fills the window with its 8px inset on every side.
- The window's close, minimize, and zoom buttons hide with the sidebar. The toolbar's empty space still drags the window, as it does today.
- Each window remembers its own state, and a restart brings it back. A new window starts the way the last window was used. These are Chromium's rules for its collapsed sidebar.
- Full screen keeps the window's state. A hidden sidebar comes in floating at the screen's left edge.
- The narrow sidebar is gone. Nothing shows a strip of favicons.

### The floating sidebar

- It comes in when the pointer rests at the window's left edge: the 8px of tint left of the card, or the screen's edge when the window touches it. It uses Chromium's pause: about 350 ms at rest, and 50 to 200 ms when the pointer moves fast toward the edge. A pointer passing over the edge does not open it.
- A link, a tab from another window, or a side-by-side tab dragged to the edge brings it in the same way, and it stays until the drop.
- Keys that need a row bring it in while they are in use, such as Focus the sidebar (Ctrl+Cmd+L) and Rename task (Ctrl+Cmd+R).
- It is a card inset 8px from the window's top, left, and bottom, as wide as the shown sidebar, with the page card's 12px corners and a soft shadow. It shows everything the shown sidebar does, foot included, with the window buttons at its top left.
- uw draws the window buttons on the floating card, so they slide with it. They look like macOS's: its colors, with ×, −, and the full-screen arrows under the pointer, and the tint's text color at 24% in a window not in front. They close, minimize, and go full screen as macOS's do, and Option-click on the green one zooms. macOS's tiling menu on the green button is not offered there. The shown sidebar keeps macOS's own buttons.
- It is painted in the theme's tint. With [Transparency](../CONTEXT.md#language) above solid, the page under it shows through, blurred, as the desktop does through the shown sidebar. [Glass](../CONTEXT.md#language) applies as in the shown sidebar.
- It slides in from past the window's left edge at full width and slides back out. Its rows never reflow on the way. The page underneath does not move.
- It slides out when the pointer leaves it for the page. Picking a tab does not send it away. It stays while one of its menus, a rename, a drag from it, or keyboard focus is in it. Esc sends it away.
- Settings → Appearance keeps Chromium's hover switch, renamed "Show the sidebar at the window's edge" and on by default. Off, the edge and drags do nothing, and Cmd+S and the row keys still work.

### The activity card while hidden

- The [activity card](automatic-organization.md#agreed-scope) shows alone over the page's bottom-left corner, where it sits in the sidebar, for as long as it shows today. Undo and its other buttons work there.
- With the floating sidebar in, the card shows in its foot as usual.

## Design

Drawn on 2026-10-24 on the Doop canvas named in [AGENTS.md](../../AGENTS.md#designs), page "Keyboard and mouse", with the rules in the canvas guide `uw-hidden-sidebar`:

- "Floating sidebar: hidden": the page card filling the window.
- "Floating sidebar: over the page": the card with its window buttons, the pointer on a row.
- "Floating sidebar: every state": hidden in light and dark, the pointer at the edge, sliding in, in place in light and dark, with Transparency, the activity card alone in light and dark, the View menu, the setting, and uw's window buttons in each state.

uw's proposals in the frames: it slides in over 200 ms, ease out, and out over 150 ms, ease in. The user chose window buttons drawn by uw over a fade-in of macOS's own, which cannot ride the slide. In black dark mode the card has a 1px #262626 line and a deep shadow. The lone activity card sits 16px from the window's left and bottom edges, on the floating sidebar's surface.

## Stages

Each stage is a ticket on the [board](roadmap.md#board).

1. T182: the design, signed off by the user on 2026-10-24.
2. T183: hiding, the floating sidebar, the keys, and the setting. The narrow sidebar's code, tests, and docs go.
3. T184: the activity card while hidden.

## Acceptance checks

1. Cmd+S hides the sidebar: the page card fills the window with an 8px inset, and the window buttons are gone. Cmd+S shows it again. The View menu names the command by state. Cmd+Shift+L does nothing, and Save Page As has no key.
2. With the pointer at rest on the left edge, the floating sidebar comes in after about 350 ms. A pointer passing over the edge does not open it. The page's size and place do not change.
3. The floating card is inset 8px from the window's top, left, and bottom, is the shown sidebar's width, has 12px corners and a shadow, and shows uw's window buttons at its top left, which slide with it. It slides in and out without its rows reflowing. The buttons close, minimize, and go full screen, Option-click on the green one zooms, and they show their glyphs under the pointer and the faint look in a window not in front.
4. It slides out when the pointer moves onto the page, and not when a tab is picked. It stays while a menu, a rename, a drag, or keyboard focus is in it. Esc sends it away.
5. Ctrl+Cmd+L and Ctrl+Cmd+R bring it in with focus in the right row.
6. A link dragged to the left edge brings it in, and a drop on a row opens the link there.
7. With Transparency above solid, the page shows blurred through the card.
8. With the sidebar hidden, an automatic move shows the activity card alone at the page's bottom left, and its Undo works.
9. With the setting off, the edge and drags do nothing, and Cmd+S and the row keys still work.
10. A hidden window comes back hidden after a restart. A new window starts as the last window was used. Full screen keeps the state.
11. No code path draws the narrow sidebar. Its screenshot test and doc lines are gone.
12. Headless screenshots match the frames in light and dark mode. A [real-window test](../../README.md#real-window-tests) checks the hover, the slide, and the window buttons.
13. `pnpm run check` and the focused test suites pass. Browser tests stay headless.

## Open questions

None.
