# B1.17: the project rename, `fidelio` → `dock-receiver` (the plan; on HOLD)

**Status, 2026-11-18: on HOLD until the owner says device testing is done.** It is the last queued round.
The project's current state is in [`docs/HANDOFF.md`](HANDOFF.md) (§3.5).

"Fidelio" is Philips' dock product name; the project is renamed so it is used only descriptively. **New
project name: `dock-receiver`.**

- **One round, no piecemeal work before then.** It must land as one consistent tree.
- The B1.15 naming rule (the app stays NowPlaying, new components get neutral names, the advertised device
  names are kept) stays in force until this round.
- §1–§3 are the plan as it was written before B1.16 shipped. §4 lists what later rounds added, and §5 what
  changed on 2026-11-18.
- Re-run the plan mechanically on the final tree. The first session's work in progress on it (645e34e) is
  gone and was stale anyway.

---

## 1. Name map (old → new)
Apply it in sources, build files, docs and tests.

- **Repo and title:** `fidelio` → `dock-receiver`, a **single monorepo** (today's `azarzycki/fidelio`).
  - The three old component repos (fidelio-ios3-spotify/-airplay/-nowplaying) are **retired, not renamed**:
    their history is already in the monorepo. Drop the `dock-receiver-ios3-*` names, and in docs replace
    every link or reference to those repos with the monorepo path (`spotify/`, `airplay/`, `nowplaying/`).
  - Component READMEs titled after the old repo names (e.g. `# fidelio-ios3-nowplaying`) get plain
    component titles.
  - The top-level README becomes the single-project entry point: what it is, the hardware, the parts under
    `airplay/ audiod/ nowplaying/ spotify/ common/`, how to build, where device setup lives. It is not a
    pointer to sibling repos.
- **Programs:** `fidelio-audiod` → `dock-audiod`; `fidelio-raop` → `dock-raop`; `fidelio-lockctl` →
  `dock-lockctl`; `cspot-ios3` stays. Any helper or test binary with "fidelio" follows the same pattern.
- **Lock plugin:** `Fidelio.lockbundle` → `NowPlaying.lockbundle`; class `FidelioLockPluginController` →
  `NowPlayingLockPluginController`; bundle id `com.fidelio.lockplugin` → `com.dockreceiver.lockplugin`.
- **App:** stays **NowPlaying**.
  - Bundle id `com.fidelio.nowplaying` → `com.dockreceiver.nowplaying` (update audiod's default wake target
    to match).
  - Classes `FidelioAppDelegate`/`FidelioStatus` and `FIDELIO_*` macros → `NP*`.
- **Identifiers:** every `com.fidelio.*` → `com.dockreceiver.*` (launchd labels
  `com.dockreceiver.audiod`/`.cspot`/`.raop`/`.keepalive`, notification names, etc.).
- **C++:** namespace, macros and filenames containing fidelio → `dockreceiver` or neutral. This **includes
  `fidelio::Registry`** → e.g. `dockreceiver::Registry`.
- **On-device dir:** `/var/mobile/fidelio` → `/var/mobile/dockreceiver` (socket, status files,
  settings.conf, manifests, creds, kill-switch).
  - **ONE compile-time default** plus the existing CLI/env overrides; no second hard-coded copy of the old
    path.
- **Unchanged:**
  - `AUDIOD_*` env vars stay.
  - **KEEP** the advertised device names ("iPhone Fidelio", "iPad Fidelio", "Fidelio (AirPlay)") and
    defaults, and every descriptive mention of the Philips Fidelio DS9000 dock.
  - Keep the README non-affiliation line (added in B1.15).

## 2. Compatibility constraints (the devices are live on the old names)
- The producer ↔ audiod wire protocol must NOT change this round (a half-updated device still works).
- Deliver a **portable migration sh**, with tools that exist on BOTH devices. The iPad lacks
  head/tail/tr/cut/sort/awk/nohup: use sed/grep/shell built-ins. It must:
  - stop and unload the old launchd jobs;
  - move `/var/mobile/fidelio` → `/var/mobile/dockreceiver`, preserving settings and creds;
  - install the new plists and binaries;
  - swap the lock bundle and the app, and refresh the icon cache where needed;
  - load the new jobs.

  **Re-runnable; it leaves the OLD setup intact and restartable if any step fails.** The owner runs it; do
  not assume it ran.
- The app must find files at the new path and show a clear "daemons not running" state (not an empty
  screen) when pointed at an empty dir.
- Do NOT rewrite git history. Old commit messages stay, and so do the old file names recorded in the
  history; only the new HEAD tree changes (§5).

## 3. Deliverable
One round on the then-current head:
- full regression green under the new names;
- three binaries + dock-lockctl + NowPlaying.lockbundle + app rebuilt and entitled;
- the migration script;
- a short `RENAME.md` (every old → new);
- a final `grep -ri fidelio` over the tree, listed with the reason each remaining hit is allowed
  (device-name default, or a descriptive dock mention).

## 4. New since the plan: the rename must also cover
- `LPLayout.c` and `NPRemoteControl.*`; the plugin's `FidelioSBAwayController` protocol; the `FIDELIO_DIR`
  and `FIDELIO_APP_VERSION` macros.
- `common/fidelio-springboard.entitlements.plist`. (The history file's Task 23 says the name
  `common/dock-springboard.entitlements.plist` "belongs to the B1.17 rename".)
- DockRemote's `/var/mobile/fidelio/remote-*` paths; `remote-control.*`.
- audiod's lock-plugin base name `"Fidelio"` (the `lock_plugin` write lambda) → `"NowPlaying"`. **Both**
  bundles: `NowPlaying.lockbundle` and `NowPlaying.bundle`.
- The default wake target `com.fidelio.nowplaying`.
- The names in the test, runner and build scripts, and `audiod/probe`.
- The `nowplaying:` URL scheme stays.
- Keep the advertised device names "iPhone Fidelio", "iPad Fidelio" and "Fidelio (AirPlay)".
- The migration script must work with the iPad's missing tools (no head/tail/tr/cut/sort/awk/nohup) and
  handle the owner's launchd jobs.

## 5. Changed on 2026-11-18
- **One session, and `main` is pushed directly.** The cloud build session is retired. The session on the
  owner's Windows PC builds, commits and pushes `main` (fast-forward only, a marker branch per device
  round). The plan's line about delivering a diff per round for the owner to apply is dropped.
- **The private store is retired.** The plan's name map also covered the file names published there and
  the STATUS file; neither exists any more. Where the plan said the final grep is "listed in STATUS", the
  list now goes in `docs/HANDOFF.md`.
- **The record is in the repo, so the rename covers it too:** `docs/HANDOFF.md` (names, paths and the
  hashes of §2.3), this file, and the README's pointers to the tools and docs.
- **`docs/history/STATUS-2026-11-18.md` is a frozen record.** By §2's rule that history is not rewritten,
  it keeps the old names, and the final grep lists it for that reason.
- **The reference build paths contain the old name:** the WSL clone `/home/user/fidelio` and the Windows
  clone `C:\code\fidelio\github\fidelio` (`docs/HANDOFF.md` §6.1). cspot-ios3's hash depends on the repo
  path (§6.3 there). Whether these directories are renamed is not decided.

## 6. Packaging (same round; decided 2026-11-18)

The owner decided that the suite is packaged in this round, because the rename already needs a migration
that stops the old launchd jobs, moves the data directory and swaps the bundles. The packages' install
scripts are that migration (§2), so the devices are migrated once. This lifts the B2 hold for packaging
only.

- **Format:** Debian packages, architecture `iphoneos-arm`, gzip-compressed (old `dpkg`). Installed with
  `dpkg -i` over SSH, or by one `install.sh <device>` run from a PC. They are attached to GitHub Releases.
  No Cydia source: Cydia on iPhone OS 3.1.3 and iOS 5.1.1 cannot speak modern TLS.
- **Two packages, split by OS version, not by device type:**
  - **The main package**, for every supported OS (the same ARMv6 binaries): the three daemons and the
    lock-control tool, their launchd jobs including the keepalive, NowPlaying.app, and the lock plugin
    under both bundle names (which one SpringBoard loads depends on the OS version).
  - **The dock-remote package**, for iPhone OS 3.x only: the tweak in iapd, where that OS delivers the
    remote's keys through `GSSendEvent`. It depends on MobileSubstrate. On iOS 5 the keys arrive through
    MediaRemote and are handled inside NowPlaying, so there is no tweak and no MobileSubstrate. iOS 4 is
    untested.
- **Install scripts:** stop and unload the jobs, install, refresh the icon cache, load the jobs. Only one
  audiod may run. The dock-remote package must never restart iapd while the device is docked; it takes
  effect after an undocked `killall iapd` or a reboot. The §2 constraints hold: re-runnable, the old setup
  left restartable if a step fails, and only the shell tools both devices have.
- **Spotify credentials:** a root-only file on the device that the Spotify job reads. Never in a package
  or the repo. A README says how to fill it.
- **Not packaged:** the volume probe, Apple's SDK, the jailbreak.
- **To check on the devices first:** the `dpkg` versions; whether Cydia's `firmware` pseudo-package is
  registered, so a package can declare its OS range as a dependency; which shell tools the install scripts
  can rely on.
