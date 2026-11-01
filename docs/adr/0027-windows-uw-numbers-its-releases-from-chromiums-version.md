# Windows uw numbers its releases from Chromium's version

Chromium's Windows installer updates only to a strictly higher four-part version, and its "Relaunch to update" appears only for one. The Mac numbers releases with uw's own version, 0.1.1, in the app bundle, while Chromium's version stays as it is; Windows keeps its version in the installer and the registry, and `chrome/VERSION` makes it. On 2026-11-01 we chose to number each Windows release from Chromium's version, with its last number times 100 plus a count: uw's releases on Chromium 153.0.8010.53 are 153.0.8010.5300, 5301, and so on, and the next Chromium patch, .60, starts at 6000. About shows uw's own version, as on the Mac.

Considered options: releasing Windows only on a new Chromium version (rejected: a fix to uw alone would wait weeks for Chromium), and putting uw's own version in `chrome/VERSION` (rejected: sites would see Chrome 0).

Consequences: sites that ask for the browser's full version see a version Chrome never had, such as 153.0.8010.5301. Within one Chromium build number the scheme cannot go back to Chromium's own last number, which would be lower; a new Chromium build or major version starts fresh. At most 100 uw releases fit on one Chromium version.
