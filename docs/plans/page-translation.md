# Page translation

Status: scope agreed on 2026-11-06 in a grilling session. T363 to T366 on the [board](roadmap.md#board) build it. The user approved all four, and the designs, in advance. The designs are the frame "Page translation: every state (T363)" on the Doop canvas's "Page translation" page. T363, T364, and T365 were built on 2026-11-07; see the board for what each has been checked against.

Asked for on 2026-11-06: "i want the translate this page option back that is missing in chromium that is present in chrome."

Implementability, using the [roadmap convention](roadmap.md#implementability-estimates): **6/10** overall. Chromium keeps all of Chrome's translate menus, bubble, and settings, and its on-device engine translated English to Japanese in a dev build on 2026-11-06, keeping inline tags such as links in the right place. The cost is uw's own page script, which swaps a page's text and puts it back, the helper a signed Mac build needs, and three engines behind one route.

## Why Chromium has none

Chromium turns translation off when the build has no Google API key (`TranslateManager::CanManuallyTranslate` and `InitiateTranslation` in `components/translate/core/browser/translate_manager.cc`). uw has none on purpose, per [Google requests](google-requests.md). A key would not help: Chrome's translate script calls a Google service with Chrome's own key, and Google says those services are not for other browsers.

## Agreed scope

### Engines

- A profile's [translation engine](../CONTEXT.md#language) is None, This device, Google Cloud Translation, or AI provider. None is the default.
- **This device** is Chromium's on-device engine, TranslateKit, which Google's component updater serves to uw on Mac and Windows. The page's text never leaves the device. It knows about 39 languages, each paired with English; between two others it goes through English.
- **Google Cloud Translation** uses the user's own API key. It is fast, cheap, and knows over 130 languages.
- **AI provider** uses a language model from a provider saved on `uw://providers`, picked in its own Model menu. The menu starts on the model that names tasks, and translation asks at the lowest effort the model offers.
- Every engine runs in the browser, never in the page, so no page sees a key, and plain `http://` pages translate too.

### Ways in

- Right-click › "Translate to English", the app menu's "Translate…", and the address-bar icon. "English" is the "Translate into" language.
- With no engine, the right-click and app menu items are grey, and "Set up translation…" below them opens Settings › Languages › Translate. The address-bar icon does not show.
- With an engine, right-click also has "Translate with ›". It lists the three engines: the picked one ticked, the ones not set up grey, and "Set up translation…" at the bottom. Picking one translates this page this one time. The next page uses the picked engine again.
- uw never offers to translate by itself: the "Translate this page?" bubble never pops up. On a page in another language, the address-bar icon shows quietly.
- Languages on the "Automatically translate these languages" list still translate by themselves, with the picked engine.
- "Translate selection to English" comes back too, with the same rules, the same engines, and "Translate with ›". It shows the translation in Chromium's bubble and leaves the page alone.

### While a page is translated

- Text that shows up later, such as an infinite scroll or a new chat message, is translated as it appears, until Show original. Only the new text is sent.
- Show original puts the page back as it was.
- This device downloads a language the first time a page needs it, about 80 MB, with progress in the bubble. Picking the engine downloads nothing.
- When This device does not know the page's language, the bubble says "This device can't translate Swahili" and offers the other engines that are set up as buttons, for this one time. With neither set up, it offers "Set up another engine…".
- When an engine fails, such as a key Google rejects or a provider that is down, the bubble says so with the service's own message.

### Settings

- In Settings › Languages › Translate, the "Use Google Translate" switch becomes an Engine menu. Under it is what the picked engine needs:
  - This device: the downloaded languages with their size and a Remove button. When it does not know the "Translate into" language, a note says so.
  - Google Cloud Translation: a key field. Saving it translates one word, so a key without the Cloud Translation API turned on shows Google's error at once. The key sits in Keychain, entered once for every profile.
  - AI provider: the Model menu.
- A note says where the page's text goes: to Google for Google Cloud, to the provider for an AI provider, private windows included. This device sends nothing.
- uw never gives the Google Cloud key to Chromium's own Google key, so Safe Browsing and the other services that need one stay off.

### Private windows

Private windows translate as normal windows do, with the profile's engine and settings.

### Signed Mac builds

macOS will not load Google's engine into uw's signed helpers, since Google signed it and uw did not. The on-device translation service runs in its own helper, uw Helper (Translate), the only one allowed to load it. See [ADR 0034](../adr/0034-the-on-device-translation-engine-runs-in-its-own-helper.md).

## Tickets

Each is on the [board](roadmap.md#board), built in this order:

1. T363: translate a page on this device. Chromium's translate menus, icon, and bubble back; the Engine setting; uw's page script and the route to the engine; downloading and removing languages; the bubble for a language This device lacks; "Translate with ›" with the engines not yet built grey; uw Helper (Translate); the Help page; Mac and Windows.
2. T364: Google Cloud Translation. The key field, Keychain, the check on save, and the engine.
3. T365: translate with an AI provider. The Model menu and the engine, with long pages split into requests the model can take.
4. T366: translate a selection.

## Acceptance checks

1. A new profile has no address-bar icon on a Japanese page. Right-click shows "Translate to English" grey and "Set up translation…", which opens Settings › Languages › Translate.
2. With This device picked, a Japanese page shows the address-bar icon and no bubble. "Translate to English" translates it, and the network log holds no request carrying the page's text.
3. Show original gives back the same page, element for element, with its event handlers still working.
4. Text added to a translated page is translated. Text in a block the page changes is translated again.
5. A plain `http://` page translates.
6. A signed release build translates on this device on Mac, and the Windows build does in the Windows VM.
7. On This device, a Swahili page shows "This device can't translate Swahili" with the engines that are set up.
8. The first Japanese translation shows the download's progress, and the language then shows in settings with its size. Remove deletes it.
9. With This device picked, "Translate with › AI provider" translates once through a test model server, and the next Japanese page uses This device.
10. A Google Cloud key that Google rejects shows Google's message when saved. A good one translates a page through a test server.
11. "Translate selection to English" shows the translation of the selected text in each engine.
12. A private window translates with the profile's engine.
13. `pnpm run google-check` passes with This device picked and a page translated.
14. The Help page matches what was built.
