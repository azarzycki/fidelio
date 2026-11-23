# The B1.17 rename: `fidelio` → `dock-receiver`

"Fidelio" is Philips' name for the speaker dock this software drives. Since round B1.17 the project's
own names no longer use it; it stays only where it describes the dock, and in the device names the
receivers advertise. The plan is `docs/RENAME-PLAN.md`. Git history is not rewritten: commits before
B1.17 keep the old names.

## Old → new

| What | Old | New |
|---|---|---|
| Project | `fidelio` | `dock-receiver` (one monorepo) |
| Audio arbiter | `fidelio-audiod` | `dock-audiod` |
| AirPlay receiver | `fidelio-raop` | `dock-raop` |
| Lock-plugin tool | `fidelio-lockctl` | `dock-lockctl` |
| Spotify receiver | `cspot-ios3` | `cspot-ios3` (unchanged) |
| Lock plugin, iOS 5 | `Fidelio.lockbundle/Fidelio` | `NowPlaying.lockbundle/NowPlaying` |
| Lock plugin, iPhone OS 3 | `Fidelio.bundle/Fidelio` | `NowPlaying.bundle/NowPlaying` |
| Lock plugin base name (audiod, `dock-lockctl`) | `Fidelio` | `NowPlaying` |
| Lock plugin class | `FidelioLockPluginController` | `NowPlayingLockPluginController` |
| Lock plugin protocol | `FidelioSBAwayController` | `NowPlayingSBAwayController` |
| Lock plugin bundle id | `com.fidelio.lockplugin` | `com.dockreceiver.lockplugin` |
| App | `NowPlaying.app` | `NowPlaying.app` (unchanged; version 1.5.0) |
| App bundle id, audiod's default wake target | `com.fidelio.nowplaying` | `com.dockreceiver.nowplaying` |
| App classes | `FidelioAppDelegate`, `FidelioStatus` | `NPAppDelegate`, `NPStatus` |
| App macros | `FIDELIO_DIR`, `FIDELIO_APP_VERSION`, `FIDELIO_STALE_S`, `FIDELIO_ACTIVE_SOURCE_STALE_S`, `FIDELIO_COVER_MAX_PX`, `FIDELIO_WATCHDOG_STALE_S`, `FIDELIO_WATCHDOG_COOLDOWN_S`, `FIDELIO_WATCHDOG_TARGET` | `NP_DIR`, `NP_APP_VERSION`, `NP_STALE_S`, `NP_ACTIVE_SOURCE_STALE_S`, `NP_COVER_MAX_PX`, `NP_WATCHDOG_STALE_S`, `NP_WATCHDOG_COOLDOWN_S`, `NP_WATCHDOG_TARGET` |
| Lock plugin's directory macro | `FIDELIO_DIR` | `LP_DIR` |
| launchd labels (the owner's jobs) | `com.fidelio.audiod`, `.cspot`, `.raop`, `.keepalive` | `com.dockreceiver.audiod`, `.cspot`, `.raop`, `.keepalive` |
| Every other identifier | `com.fidelio.*` | `com.dockreceiver.*` |
| C++ namespace | `fidelio::Registry`, `fidelio::Option` | `dockreceiver::Registry`, `dockreceiver::Option` |
| Include guard of `audiod/proto.h` | `FIDELIO_AUDIOD_PROTO_H` | `AUDIOD_PROTO_H` |
| Device data directory | `/var/mobile/fidelio` | `/var/mobile/dockreceiver`: one compile-time default, `DOCKRECEIVER_DIR` in `common/DataDir.h` |
| Socket | `/var/mobile/fidelio/audiod.sock` | `/var/mobile/dockreceiver/audiod.sock` (`AUDIOD_DEFAULT_SOCK`, from `DOCKRECEIVER_DIR`) |
| Entitlements | `common/fidelio-springboard.entitlements.plist` | `common/dock-springboard.entitlements.plist` |
| Source files | `nowplaying/lockplugin/FidelioLockPluginController.{h,m}`, `nowplaying/src/FidelioAppDelegate.{h,m}`, `nowplaying/src/FidelioStatus.{h,m}` | `nowplaying/lockplugin/NowPlayingLockPluginController.{h,m}`, `nowplaying/src/NPAppDelegate.{h,m}`, `nowplaying/src/NPStatus.{h,m}` |
| Component READMEs | `# fidelio-ios3-spotify`, `-airplay`, `-nowplaying` | plain component titles; the three old repositories are retired, their history is in this one |
| DockRemote load line | `DockRemote B1.16.16: hooked ...` | `DockRemote B1.17: hooked ...` |

Every file in the data directory keeps its name; only the directory changes (`active-source`,
`remote-target`, `status-*.plist`, `settings.conf`, `remote-tweak.*`, `lockplugin.*` and the rest).

## Unchanged

- The device names the receivers advertise, and their defaults: "iPhone Fidelio" (Spotify), "Fidelio
  (AirPlay)" (AirPlay), "iPad Fidelio" (the iPad's setting), and the AirPlay model `am=Fidelio1,1`.
- Every mention of the Philips Fidelio DS9000 dock that describes it, and the README's
  not-affiliated line.
- The `AUDIOD_*` environment variables, the `nowplaying:` URL scheme, and the producer ↔ audiod wire
  protocol (not one byte).
- `docs/history/STATUS-2026-11-18.md`, a frozen record.
- The GitHub repository `azarzycki/fidelio` and the URLs of its release `toolchain-r3`, until the
  owner renames the repository.
