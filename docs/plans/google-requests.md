# Google requests

Agreed on 2026-10-29. uw sends Google nothing about what the user does, per the [privacy rule](../product.md#ad-blocking-and-privacy). This plan lists every request Chromium makes to Google in uw and what uw does with it. T260 and T261 on the [board](roadmap.md#board) build it.

Found on 2026-10-28 by running the built app headless with a new profile, in normal and private windows, and recording its network log. uw's own code made no Google requests. Every request below comes from Chromium.

## Stopped

| Request | Sent to | When | Private windows | What uw does |
| --- | --- | --- | --- | --- |
| Autofill form lookups and uploads | `content-autofill.googleapis.com` | A page with a form loads. Sends hashes of the form's fields and site, and the Chromium version. | Yes | Turned off. Google rejects them anyway, since uw has no Google API key. Autofill keeps its own field detection. |
| AI Mode check | `www.google.com/async/folae` | Each profile at launch, and each private session. Sends language and country, with the session's Google cookies. | Yes | AI Mode turned off, and the check is not sent while it is off. Google already answers "not eligible", so AI Mode shows nowhere today. |
| Safe Browsing list updates | `safebrowsing.googleapis.com` | About every 30 minutes. | Browser-wide | Skipped when uw has no Google API key. Google rejects them today, so the threat list stays empty either way. |
| Download scans | `sb-ssl.google.com` | A download of a file such as a `.dmg`, `.pkg`, or `.zip`. Sends the file's address, the pages that led to it, its name, and its hash. | Yes | Skipped when uw has no Google API key. The download gets the same "unknown" verdict it gets after Google's error today. |
| Phishing reports and password-reuse pings | `sb-ssl.google.com` | A page looks like phishing, or a saved password is typed on another site. Sends the page's address. | No | Skipped when uw has no Google API key. |
| Network time | `clients2.google.com/time` | At launch, then now and then. | Browser-wide | Turned off. macOS keeps the clock in sync. |
| Search engine preconnect | `www.google.com` | At launch, then about every 60 seconds while uw is in front. Opens a connection to the default search engine and sends no request, so the next search starts faster. Each connection tells Google that someone at the user's IP address is using the browser. | Browser-wide | Turned off on 2026-10-29. A search after a pause takes about a tenth of a second longer. |
| Address-bar stats on searches | `www.google.com/search` | A Google search from the address bar or the command bar. `gs_lcrp` lists the suggestions shown and the one picked; `oq` is the text typed before picking. | Yes | Both stripped. The search is unchanged. |
| "Make searches and browsing better" | Several Google services | Only when turned on. Sends the addresses of visited pages. | No | Removed. |
| "Enhanced protection" | `safebrowsing.google.com` | Only when turned on. Sends the address of every visited page. | No | Removed. |
| Chromium's new tab page | `www.google.com`, `www.gstatic.com`, `ogads-pa.clients6.google.com`, `play.google.com/log` | A new tab page opened in a normal window. Google's account bar logged to `play.google.com/log`, and doodles reported when shown. | No | Replaced by uw's own [new tab page](new-tab-page.md) (T49), which asks Google nothing. |

The two removed settings are gone from Settings, the Privacy Guide, and Chromium's prompts to turn on Enhanced protection, and they stay off whatever a profile or policy says. Neither works without a Google API key.

## Kept

| Request | Sent to | When | Why it stays |
| --- | --- | --- | --- |
| Google searches | `www.google.com` | The user searches with Google. | The user asked for it. Google is the default search engine. |
| Search suggestions | `www.google.com/complete/search` | Typing in the address bar of a normal window. Never in private windows; the command bar never sends typing. | Part of searching with Google. |
| Component updater | `update.googleapis.com`, `*.gvt1.com` | At launch, then every few hours. Sends the browser version and the components it has. | It carries certificate revocation lists, data for sites' trial features, and the on-device model behind search suggestions in private windows. |
| Push messaging | `android.clients.google.com`, `mtalk.google.com` | When a feature or a site registers for push. | Websites' notifications need it. |
| Sign-in check | `accounts.google.com/ListAccounts` | At launch, from the normal profile, with its Google cookies. | It carries no browsing. Turning off Chromium's Google sign-in could change how Google's own sign-in pages behave. |

## Already off

These reach Google only in Google Chrome builds, or only with Chromium's usage statistics consent, which uw never asks for:

- Usage statistics (UMA and UKM). The switch exists only in Google Chrome builds.
- Chromium's "Report an issue". uw's own [problem reports](distribution.md#problem-reports) replace it, and crash reports go to Sentry.
- Surveys, Domain Reliability reports, and AI model quality logs.
- The `X-Client-Data` header and downloaded field trials.
- Safe Browsing's real-time address lookups.

## The Google check

`pnpm run google-check` runs the built app headless with a new profile for about five minutes, since the background services start one to four minutes after launch. It opens a normal and a private window, loads a local page with a form, and records the network log. It does not download: a headless private window cancels downloads, and a test file that is not a real program gets no scan, so T261 shows the download scan is skipped with a browser test instead. It fails when a Google host outside the kept list appears, and names the host and the request.

It never opens Chromium's new tab page. `pnpm run release` runs it and stops when it fails, so a Chromium update that adds a Google service is caught before it ships.

## Acceptance checks

1. Against a build without T261, `pnpm run google-check` fails and names Autofill, the AI Mode check, Safe Browsing, and network time.
2. A browser test shows a download in a private window sends no download scan when uw has no Google API key.
3. With T261, `pnpm run google-check` passes.
4. A Google search from the address bar or the command bar, in a normal or a private window, carries neither `gs_lcrp` nor `oq`.
5. Settings, the Privacy Guide, and the download bubble offer neither "Make searches and browsing better" nor "Enhanced protection".
6. The Help page [Privacy: what uw sends](https://uwbrowser.com/help/privacy) matches this plan.

## Not in this plan

Safe Browsing protects nothing today, while Settings says "Standard protection". T262 decides between working Google protection through a paid key, public phishing and malware lists without Google, and labels that say there is no protection yet.
