# B1.17: the project rename, `fidelio` → `dock-receiver` (the plan, and what was delivered)

**Status, 2026-11-22: done in the tree; the devices not yet migrated.** The owner released the round on
2026-11-22 (it had been on HOLD since 2026-11-18 "until the owner says device testing is done"). It is
the commits `8ab46f4` (part A, the rename), `7d1ced6`, `307be01`, `1f7d18d` (part B, the notices and the
packages) and the docs commit after them. What was delivered, the deviations from this plan and what
remains are in §7. The project's current state is in [`docs/HANDOFF.md`](HANDOFF.md) (§3.0, §3.5); every
old → new name is in [`RENAME.md`](../RENAME.md).

§1–§6 below are kept unchanged as the record of what was planned.

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
- **Licences in the packages** (added 2026-11-22):
  - Every binary carries the source notice for its start-up code, which toolchain revision 3 puts there
    (`docs/HANDOFF.md` §6.3). The packages are built with revision 3 or later.
  - The package that holds `cspot-ios3` includes the text of the GNU General Public License, version 3,
    and says where that binary's source is: this repository at the release's commit, the pinned commit
    of the Spotify library and the patch in `spotify/patches/` (`tools/_common.sh`). The binary as a
    whole is distributed under GPL-3.0-or-later (the top-level `LICENSE`).
  - Each package carries the MIT `LICENSE` and the notices for the third-party code linked into its
    binaries. That list has not been drawn up (`docs/HANDOFF.md` §4).
  - Decided by the owner on 2026-11-22: the Spotify receiver stays in the main package ("the structural
    distinction is enough"). It is a separate program there, and the GPL text and the source pointer
    name that one binary.
- **Not packaged:** the volume probe, Apple's SDK, the jailbreak.
- **To check on the devices first:** the `dpkg` versions; whether Cydia's `firmware` pseudo-package is
  registered, so a package can declare its OS range as a dependency; which shell tools the install scripts
  can rely on.

---

## 7. Delivered (2026-11-22)

### 7.1 What was delivered
- **Part A, `8ab46f4`: the rename in the tree** per §1, §4 and §5. Programs `dock-audiod`, `dock-raop`,
  `dock-lockctl` (`cspot-ios3` unchanged); the lock plugin `NowPlaying.bundle` / `NowPlaying.lockbundle`,
  class `NowPlayingLockPluginController`, id `com.dockreceiver.lockplugin`; the app id
  `com.dockreceiver.nowplaying`, classes `NP*`, NowPlaying 1.5.0; every `com.fidelio.*` →
  `com.dockreceiver.*`; the C++ namespace `dockreceiver`; ONE compile-time data directory,
  `DOCKRECEIVER_DIR` = `"/var/mobile/dockreceiver"` in `common/DataDir.h`;
  `common/dock-springboard.entitlements.plist`; the tweak's load line `DockRemote B1.17: hooked ...`; the
  app's "Daemons not running" state (§2; plain C in `NPSource.c`, 8 new `np_source` checks); the
  component READMEs and a top-level README as the project's entry point; `RENAME.md`.
  - Kept on purpose: the advertised device names; the AirPlay model string `am=Fidelio1,1` (kept as part
    of the advertisement; the owner may decide otherwise); the wire protocol (`audiod/proto.h`: only its
    comment, include guard and the spelling of the default socket path changed); the `AUDIOD_*`
    variables; the `nowplaying:` URL scheme; the frozen history file; the repository URLs.
  - Regression at `8ab46f4`: the ARM build twice from nothing, `result: OK`, identical hashes; host
    suite 529 ok, 0 FAIL, 28 suites (521 + 8). The first host run was stopped from outside at 16 of 28
    suites for low memory on the PC (no failure up to then); the rerun, alone, was clean.
- **The reference build path** (§5's open point; decided by the session, the owner told): the WSL clone
  is `/home/user/dock-receiver` (the old `/home/user/fidelio` is kept for now); `cspot-ios3`'s hash
  depends on that path. The Windows clone's folder `C:\code\fidelio\github\fidelio` is deliberately not
  renamed.
- **Part B, `7d1ced6`, `307be01`, `1f7d18d`: the packaging of §6.** `packaging/THIRD-PARTY-NOTICES.md`
  (from the link maps); `tools/build-debs.sh`, two reproducible gzip `.deb` files, version 1.5.0-1
  (`packaging/VERSION`: NowPlaying's version and a packaging revision): `dock-receiver` (every supported
  OS) and `dock-receiver-dockremote` (iPhone OS 3.x; MobileSubstrate checked by its files, because the
  phone has no dpkg record for it). One script, `packaging/dock-receiver/setup.sh`, is all four
  maintainer scripts and the `dock-receiver-setup` command (check = dry run, migrate, rollback,
  cleanup, respring). The install is the migration of §2: it copies (never moves) settings and flags
  from `/var/mobile/fidelio`, carries the Spotify credentials over on the device without printing them,
  stops the old jobs, moves the old plists, the hand-installed app and the old lock plugin to
  `/var/dockreceiver-migration`, loads the new jobs and refreshes the icon cache; a failure before its
  point of no return undoes the run. It never restarts SpringBoard or iapd by itself.
  `packaging/install.sh <device>` runs it from a PC over OpenSSH. `packaging/README.md` has the layout
  and the procedures.
  - Host suites `pkg_setup` (243 checks, under a pinned GNU bash 4.0 with only the tools both devices
    have) and `pkg_debs` (77); the full host suite at `1f7d18d`: 849 ok, 0 FAIL, 30 suites. ARM build
    at `1f7d18d`: `result: OK`, the binaries' hashes unchanged from `8ab46f4`.
- The §3 deliverable's final `grep -ri fidelio`, with the reason each remaining hit is allowed, is the
  "Kept" list of `8ab46f4`'s commit message and the "Unchanged" section of `RENAME.md`; the rule is
  `docs/HANDOFF.md` §5.3.

### 7.2 Deviations from the plan
- **The Spotify credentials** (§6 asked for "a root-only file"): the file is
  `/var/mobile/dockreceiver/spotify-credentials.conf`, owner `mobile`, mode 600, read by the wrapper
  `cspot-run`. The job runs as `mobile`, and `cspot-ios3` takes the client id and secret only as
  command-line options, so they are visible in the device's process list while it runs, as before; a
  root-only file would protect nothing more. Closing that needs a cspot change (an option that reads
  them from a file). Without credentials the Spotify job waits and re-reads the file every 30 s. Back
  with the owner (`docs/HANDOFF.md` §4).
- **The job plists are copied by the postinst** into `/Library/LaunchDaemons`, not unpacked there by
  dpkg (they ship in `/usr/share/dock-receiver/launchd/`), so a failed install or a reboot mid-install
  cannot start two audio daemons.
- **The old device names and the bitrate** that the old launchers passed on their command lines are
  carried into `settings.conf` (the new jobs pass no options).
- **`dpkg --purge dock-receiver` deletes the data directory** `/var/mobile/dockreceiver`.
- **Removing the package restores the old setup** from `/var/dockreceiver-migration` (`dpkg -r`, or
  `dock-receiver-setup rollback` for both packages). §2 said "move" the data directory; the install
  copies it instead, so `/var/mobile/fidelio` is never changed by the install and the old setup stays
  restartable; it goes only with `dock-receiver-setup cleanup --yes`.

### 7.3 What remains
- **The devices.** Both still run the hand-installed setup under the old names; nothing of B1.17 has run
  on a device. After the review of the install scripts (its must-fix findings fixed first): the dry run
  and the install on each device with the owner at the dock, the SpringBoard restart only on the owner's
  word with nothing playing, on the phone the undocked iapd restart for the tweak
  (`docs/HANDOFF.md` §3.0, which also lists what only a device can confirm).
- **The repository rename** `azarzycki/fidelio` → `dock-receiver` (§1): asked on 2026-11-22, not yet
  answered; the tree keeps the old URLs until then.
- **The cleanup of the old files on the devices** (`/var/mobile/fidelio`, `/var/dockreceiver-migration`)
  with `dock-receiver-setup cleanup --yes`, only when the owner is satisfied; it leaves the older
  `.pre-*` backups, `com.fidelio.syslogd` and iapd's logging preferences alone.
- **The old WSL clone `/home/user/fidelio` and the old build trees** (`/home/user/build.b11616-fidelio`,
  `/home/user/build.b117-run1`, `/home/user/build.b117-dirty`): kept for now.
- Questions for the owner from the round (`docs/HANDOFF.md` §4): `am=Fidelio1,1`; the credentials
  arrangement; whether `--purge` should delete the data directory; whether the old device names and
  bitrate should be carried over. Publishing the packages waits for the owner's word; nothing was
  published.
