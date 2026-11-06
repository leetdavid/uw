# The on-device translation engine runs in its own helper

uw translates pages on the device with TranslateKit, a library Google signs and serves through the component updater. On 2026-11-06 a signed uw release could not load it: macOS library validation lets a signed process load only Apple's libraries and its own team's, and uw's team is not Google's. Chrome never hits this because Google signs both. We decided to start the on-device translation service in a helper of its own, uw Helper (Translate), signed with the one entitlement that turns library validation off. Every other uw process keeps it, and the service stays in Chromium's translation sandbox.

Considered options:

- Library validation off on the main uw Helper. One entitlement and no new helper, but the network, storage, and audio services run in that helper too, and any of them could then load another team's library.
- Signing Google's library again with uw's certificate. The component updater checks the library against Google's hashes, so it would download it again, and uw would be changing Google's binary.

Consequences: one more helper app in the bundle, its entitlements file, and its line in `pnpm run release`. Chromium's old Helper (Plugin) did this job and is gone, so the helper is uw's own. Windows has no library validation and needs none of this.
