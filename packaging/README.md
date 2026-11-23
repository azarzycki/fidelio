# Packages: install, migrate, roll back

The suite is installed on a device as two Debian packages. Their install scripts are also the
migration from the hand-installed setup of before B1.17 (`/var/mobile/fidelio`, the `com.fidelio.*`
jobs, `Fidelio.bundle` / `Fidelio.lockbundle`), and they keep that setup restorable until you remove it
yourself.

| Package | For | Holds |
|---|---|---|
| `dock-receiver` | every supported OS (iPhone OS 3.1.3, iOS 5.1.1; the same ARMv6 binaries) | the three daemons, `dock-lockctl`, the launchd jobs and the Wi-Fi keepalive, `NowPlaying.app`, the lock-screen plugin under both bundle names, `dock-receiver-setup`, licences and notices |
| `dock-receiver-dockremote` | iPhone OS 3.x only | `DockRemote.dylib` and its filter (the dock remote's keys in iapd) |

On iOS 5 the dock remote's keys reach NowPlaying itself, so the iPad needs only the first package.

## Contents

`dock-receiver`, everything owned by root (0:0):

| Path | Mode | What |
|---|---|---|
| `/usr/libexec/dock-receiver/dock-audiod` | 755 | the audio arbiter (signed with its SpringBoard entitlements at build time) |
| `/usr/libexec/dock-receiver/dock-raop` | 755 | the AirPlay receiver |
| `/usr/libexec/dock-receiver/cspot-ios3` | 755 | the Spotify Connect receiver (GPL-3.0-or-later) |
| `/usr/libexec/dock-receiver/cspot-run` | 755 | the Spotify job's start: reads the credentials file, then `exec`s cspot-ios3 |
| `/usr/libexec/dock-receiver/keepalive` | 755 | the keepalive job: Wi-Fi keepalive, log trimming |
| `/usr/bin/dock-lockctl` | 755 | turns the lock-screen plugin on and off by hand |
| `/usr/sbin/dock-receiver-setup` | 755 | the owner's command (below); the same script as the package's maintainer scripts |
| `/usr/share/dock-receiver/launchd/com.dockreceiver.{audiod,raop,cspot,keepalive}.plist` | 644 | the job definitions; the install copies them to `/Library/LaunchDaemons` |
| `/Applications/NowPlaying.app/` | 755 / 644 | the app, bundle id `com.dockreceiver.nowplaying`, URL scheme `nowplaying:` |
| `/System/Library/SpringBoardPlugins/NowPlaying.bundle/` | 755 / 644 | the lock-screen plugin as iPhone OS 3 loads it |
| `/System/Library/SpringBoardPlugins/NowPlaying.lockbundle/` | 755 / 644 | the same plugin as iOS 5 loads it |
| `/usr/share/doc/dock-receiver/` | 644 | this README, `LICENSE` (MIT), `THIRD-PARTY-NOTICES.md`, `SOURCE-cspot-ios3.txt`, `licenses/` |

`dock-receiver-dockremote`: `/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib` and
`DockRemote.plist` (644, as installed by hand before), and `/usr/share/doc/dock-receiver-dockremote/`.

Not packaged: the volume probe `voltest`, Apple's SDK, the jailbreak, and any credential.

Created by the install (not files of a package):

| Path | Owner, mode | What |
|---|---|---|
| `/Library/LaunchDaemons/com.dockreceiver.*.plist` | root, 644 | the four jobs, copied from `/usr/share/dock-receiver/launchd` at the switch (below) and removed again by `dpkg -r` |
| `/var/mobile/dockreceiver/` | mobile, 755 | the data directory (`common/DataDir.h`): socket, status files, covers, `settings.conf`, manifests, flag files |
| `/var/mobile/dockreceiver/spotify-credentials.conf` | mobile, 600 | the Spotify developer app's client id and secret (below) |
| `/var/mobile/dockreceiver/keepalive.conf` | mobile, 644 | optional: `KEEPALIVE_HOST=<address on your LAN>` |
| `/var/mobile/dockreceiver/logs/{audiod,raop,cspot,keepalive}.log` | mobile, 644 | the jobs' output; above 1 MB a log is moved to `<log>.1` |
| `/var/mobile/dockreceiver/install.log` | root, 644 | what the install scripts did (never a credential) |
| `/var/dockreceiver-migration/` | root, 700 | the recoverable place: the old job plists, app, lock plugin and tweak, and `state` |

## Why it is laid out this way

- **Binaries in `/usr/libexec/dock-receiver`, root-owned.** Before, they sat in the mobile-writable
  `/var/mobile/fidelio/b1`, so anything running as `mobile` could replace a program that launchd starts.
  The four binaries are about 3.9 MB; the root partition has 134 MB (phone) and 249 MB (iPad) free, and
  the preinst refuses to start with less than 12 MB there (dpkg unpacks beside the old files before it
  renames). `dock-lockctl` is a command for the owner, so it is in `/usr/bin`.
- **The job plists are copied by the postinst, not unpacked by dpkg.** launchd reads
  `/Library/LaunchDaemons` at boot. Were the new plists unpacked there, a failed install, or a reboot
  between unpacking and the end of the install, would start the old and the new audiod together, and
  only one may ever run. The install puts them there at the moment it takes the old ones out.
- **No options on the command lines.** The jobs run the programs bare: the defaults in the code are
  right for the data directory, and every setting comes from `settings.conf`, where the NowPlaying
  Settings tab can change it (an option given on a command line is locked there). The device names and
  the bitrate that the old launchers passed are copied into `settings.conf` by the migration.
- **Jobs:** `RunAtLoad`, `KeepAlive` (NowPlaying's restart button and its watchdog `killall -9`
  the daemons and rely on launchd to start them again), `UserName mobile`, `WorkingDirectory` the data
  directory, `ThrottleInterval` 3 s (keepalive 5 s), the jobs' output in `logs/`. Loaded in the order
  audiod, raop, cspot, keepalive.
- **The Spotify job's start, `cspot-run`.** cspot-ios3 takes the client id and secret only as
  `--client-id`/`--client-secret` (`spotify/cli-src/main.cpp`). The script reads them from the file and
  replaces itself with cspot-ios3, so launchd supervises cspot-ios3 directly and the process keeps its
  name. It holds no secret.
- **The keepalive.** With no audio flowing, iPhone OS 3 lets Wi-Fi sleep and the device drops out of
  Spotify's and AirPlay's lists. The job sends one small UDP datagram every 2 s to port 9 (discard) of
  a host on the LAN: `KEEPALIVE_HOST` in `keepalive.conf` (the migration copies the address from the old
  `keepalive.sh` when it holds one), else the router that `ipconfig getoption en0 router` reports. No
  address is in the package. It also trims the logs every 5 minutes (no logrotate on the devices).
- **One version for the suite:** `packaging/VERSION`, NowPlaying's version and a packaging revision:
  `1.5.0-1`. The dock-remote package needs `dock-receiver` of exactly the same version.
- **OS range and MobileSubstrate:** the dock-remote package depends on Cydia's `firmware`
  pseudo-package (`>= 3.0`, `<< 4.0`). MobileSubstrate is checked by its files in the preinst
  (`/usr/lib/libsubstrate.dylib` or `/Library/MobileSubstrate/MobileSubstrate.dylib`, and the
  `DynamicLibraries` directory): the phone's MobileSubstrate came with its firmware and is not a dpkg
  package, so `Depends: mobilesubstrate` would refuse to install.

## The Spotify credentials

**What they are.** The Spotify receiver needs a *Spotify developer app's* client id and secret: this
cspot fork fetches its Web API token with the client-credentials flow from accounts.spotify.com, and
without the token it cannot resolve the audio's download addresses, so nothing plays
(`spotify/CSPOT.md`). They are not your Spotify login: the Spotify app on your phone or computer logs
the receiver in when you pick it (Zeroconf), and that needs a Premium account.

**Without them** `cspot-run` does not start cspot-ios3 (the receiver would be selectable but silent):
it writes `cspot-run: no Spotify client id and secret ... waiting` once to `logs/cspot.log` and looks
at the file again every 30 s. AirPlay, the app and the rest work as usual.

**The file.** `/var/mobile/dockreceiver/spotify-credentials.conf`, owner `mobile`, mode 600:

```
SPOTIFY_CLIENT_ID=<the client id>
SPOTIFY_CLIENT_SECRET=<the client secret>
```

The migration fills it from the old setup (the iPad's `/var/mobile/fidelio/creds.env`, or the phone's
old Spotify launcher scripts), on the device, and prints only where it found them. On a new device the
install leaves the file with both values empty. To fill it, as root on the device (the values are
neither shown nor kept in the shell's history):

```sh
read -r -s -p 'client id: ' ID; echo; read -r -s -p 'client secret: ' SECRET; echo
printf 'SPOTIFY_CLIENT_ID=%s\nSPOTIFY_CLIENT_SECRET=%s\n' "$ID" "$SECRET" > /var/mobile/dockreceiver/spotify-credentials.conf
unset ID SECRET
chown mobile:mobile /var/mobile/dockreceiver/spotify-credentials.conf; chmod 600 /var/mobile/dockreceiver/spotify-credentials.conf
```

The Spotify job starts within 30 s. Never put the file into the repository or a package.

**Why owned by `mobile` and not root-only.** The plan asked for a root-only file. The job runs as
`mobile`, and cspot-ios3 takes the values only on its command line; once it runs, its command line is
visible to every process on the device, as it was before (`spotify/REVIEW.md`, "Secrets on the command
line"). A root-only file read by a root wrapper that then drops to `mobile` would therefore protect
nothing more, and would need a root step in a job that otherwise runs entirely as `mobile`. Removing the
values from the command line needs a change to cspot-ios3 (an option that reads them from a file): left
to the owner.

## Installing

From a PC with OpenSSH, after `tools/build-arm.sh` and `tools/build-debs.sh` (or with the two `.deb`
files and their `SHA256SUMS` in one directory):

```sh
packaging/install.sh --check root@<device>     # copy, dry run on the device; changes nothing
packaging/install.sh root@<device>             # copy, dry run, ask, dpkg -i
```

It installs the dock-remote package too when the device runs iPhone OS 3 (`--no-dockremote` to leave it
out). `--debs DIR` names the directory with the packages, `--yes` skips the question, `--respring`
lets the install restart SpringBoard (still refused while something plays). `SSH=` and `SCP=` replace
the commands. Nothing about a device is built in; ssh asks for the password (up to four times; a key
avoids that).

**Old OpenSSH on the devices.** The devices run OpenSSH 6.7. A current client (OpenSSH 8.8 and later)
no longer offers the `ssh-rsa` (SHA-1) signature by default; `install.sh` passes
`-o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa` (override with `SSH_OPTS=`).
OpenSSH 9's `scp` uses the SFTP protocol; `install.sh` adds `-O` (the classic protocol) when the local
scp knows it (`SCP_OPTS=`). The key exchange (`curve25519-sha256@libssh.org`) and the ciphers need no
option. Older clients (PuTTY's `plink`/`pscp`, OpenSSH before 8.8) need none of this.

By hand, on the device as root:

```sh
dpkg-deb -e dock-receiver_1.5.0-1_iphoneos-arm.deb /var/tmp/dr-ctl && /bin/sh /var/tmp/dr-ctl/preinst check   # dry run
dpkg -i dock-receiver_1.5.0-1_iphoneos-arm.deb dock-receiver-dockremote_1.5.0-1_iphoneos-arm.deb             # phone
dpkg -i dock-receiver_1.5.0-1_iphoneos-arm.deb                                                                # iPad
```

### The dry run

`dock-receiver-setup check` (before the install: the package's `preinst check`, as above) changes
nothing and prints what it finds and what it would do: the OS version, missing tools, free space on `/`
and `/var`, whether the package is installed, whether an old setup is there (which old jobs have a
plist or are loaded, which daemons run), what it would copy, where the Spotify credentials were found
(never their values), the device names and bitrate in the old launchers, the keepalive target, the app
and lock plugin it would move, the tweak and MobileSubstrate, and `result: ready` or `NOT ready`.
The dock-remote package has its own: `/bin/sh <its control dir>/preinst check`.

### What the install does

`preinst` (before dpkg unpacks): root, the tools, free space; creates `/var/dockreceiver-migration`;
moves the hand-installed `/Applications/NowPlaying.app` there (any app at that path whose bundle id is
not `com.dockreceiver.nowplaying`), because the new app is unpacked at the same path.

`postinst` (the migration; `dock-receiver-setup migrate` runs it again by hand):

1. prerequisites, the package's files;
2. `/var/mobile/dockreceiver` and its `logs/` (owner `mobile`);
3. **copies** from `/var/mobile/fidelio` what the new programs read, if not there yet: `settings.conf`,
   `remote-tweak.observe`, `.swallow`, `.disabled`, `lockplugin.debug`, `remote-control.stage`,
   `remote-probe.on`, `remote-keys.log`;
4. copies the device names (`cspot.name`, `raop.name`) and the bitrate (`cspot.bitrate`) from the old
   launchers into `settings.conf`, where it does not set them already;
5. the Spotify credentials: from the old `creds.env`, otherwise from the old Spotify launchers (the
   ones the old job's plist names, and `b1/test_cspot.sh`, `b1/run_cspot.sh`, `cspotd.sh`, ...): `NAME=value`,
   `export NAME=value`, `--client-id value`, `--client-id=value`, quoted or not, `$NAME` resolved,
   continued lines joined; written to `spotify-credentials.conf`; it prints only "found in <file>" or
   "not found";
6. the keepalive target from the old `keepalive.sh`;
7. **the switch:** unloads the old jobs `com.fidelio.audiod`, `.raop`, `.cspot`, `.keepalive` (not
   `com.fidelio.syslogd`), and any loaded new job; stops `fidelio-audiod`, `fidelio-raop`, `cspot-ios3`
   and checks that none is left (only one audiod may ever run);
8. moves the old job plists to `/var/dockreceiver-migration/LaunchDaemons`;
9. copies the new job plists into `/Library/LaunchDaemons`;
10. moves the old lock plugin (`Fidelio.bundle` or `Fidelio.lockbundle`, exact names only; Apple's
    `NowPlayingArtLockScreen.lockbundle` beside it is never touched) to the recoverable place, and
    closes a running old NowPlaying;
11. loads the new jobs (audiod, raop, cspot, keepalive) and waits until `dock-audiod` runs and has
    written `active-source`, so the app finds a running suite and does not show "Daemons not running";
12. **the point of no return**, then the icon cache (`su mobile -c uicache`) and a summary.

A file that has run is never written over: every file is put in place under a new name and renamed
(dpkg does the same for the packages' files).

**If a step fails** up to the point of no return, the install undoes what it did: it unloads the new
jobs, puts the old plists, lock plugin, app and tweak back, loads the old jobs again, and exits
non-zero (`dpkg` then reports the package as not configured). `/var/mobile/fidelio` is never changed at
all. Every step is re-runnable: run `dpkg -i` again, or remove the packages with
`dock-receiver-setup rollback`.

The dock-remote package's preinst checks iPhone OS 3, MobileSubstrate's files and that
`dock-receiver` is unpacked in the same run, and copies a hand-installed `DockRemote.dylib`/`.plist` to
the recoverable place before dpkg replaces them.

### Still to do after the install

- **Restart SpringBoard** for the new lock plugin and the app's new bundle id:
  `dock-receiver-setup respring`, when nothing is playing. The install never does it on its own: on the
  phone a SpringBoard restart during playback was once followed by a reboot. The command refuses while
  `active-source` names a playing source (`--even-if-playing` overrides); `DOCKRECEIVER_RESPRING=1 dpkg -i
  ...` (or `install.sh --respring`) runs it at the end of the install.
- **The dock remote (phone):** the tweak loads when iapd starts. The packages never restart iapd:
  restarting it while the phone is docked leaves the dock unusable until the phone is reseated. Lift the
  phone off the dock, `killall iapd`, wait about 5 s, dock it again; or reboot.

## Rollback and cleanup

```sh
dock-receiver-setup rollback        # dpkg -r both packages; the old setup comes back
dock-receiver-setup cleanup         # lists what only a rollback needs
dock-receiver-setup cleanup --yes   # removes /var/mobile/fidelio and /var/dockreceiver-migration
```

`rollback` removes `dock-receiver-dockremote` and `dock-receiver`. The removal (`prerm`) stops the new
jobs and removes their plists; `postrm` puts back the old job plists, the old lock plugin, the old app
and the old tweak from `/var/dockreceiver-migration`, refreshes the icon cache and loads the old jobs.
`/var/mobile/dockreceiver` is kept (`dpkg --purge dock-receiver` removes it). Afterwards: restart
SpringBoard when nothing plays (`killall SpringBoard`), and on the phone restart iapd undocked for the
old tweak. A plain `dpkg -r dock-receiver` does the same restore.

`cleanup` refuses while the old setup is the active one or the package is not installed, and without
`--yes` only lists. After it, no rollback is possible. It leaves alone what it did not create:
older copies beside the app and the lock plugin (`NowPlaying.app.pre-*`, `Fidelio.bundle.pre-r3`, ...),
`com.fidelio.cspot.plist.rev6.bak`, and the phone's debugging aids: the job `com.fidelio.syslogd` and
iapd's logging preferences in `com.apple.iapd.plist`. The install's summary names the last two when it
finds them.

## Building and testing the packages

`tools/build-debs.sh` (after `tools/build-arm.sh`) writes both packages and `SHA256SUMS` to
`$BUILD_ROOT/debs`. The format is what the devices' dpkg 1.18 reads, which has no xz or zstd: an ar
archive of `debian-binary` (`2.0`), `control.tar.gz` and `data.tar.gz`, both gzip. The script
assembles them itself, so every byte is fixed: all mtimes `SOURCE_DATE_EPOCH` (default
`PACKAGE_SOURCE_DATE_EPOCH` in `tools/_common.sh`), sorted members, numeric owners 0:0, explicit modes,
`gzip -n`. It builds a second time and compares the sha256 (`--no-repro-check` skips that). It packages
only an ARM output that passed `build-arm.sh`'s verification, and checks each binary against its
`SHA256SUMS`.

Two host suites in `tools/run-host-tests.sh`:

- `pkg_setup` (`packaging/hosttest/test_setup.sh`): the install scripts in fake device roots, run by
  GNU bash 4.0 (built from its release tarball, as the devices' `/bin/sh`) with a `PATH` that holds only
  the tools both devices have plus stand-ins for `launchctl`, `killall`, `uicache`, `su`, `dpkg` and
  `df`, so a call of another tool fails, and so does `sed -E`. Phone-like and iPad-like old layouts, the
  credential forms, a fresh device, a second run, a failure injected at every step (the old setup must
  run afterwards and rollback must work), the dry run (the fake root must not change), rollback,
  cleanup, respring, and no credential value in any output, log or other file.
- `pkg_debs` (`packaging/hosttest/test_debs.sh`): the packages built from `$BUILD_ROOT/arm-out`: format,
  owners, modes, paths, the binaries byte for byte, the control fields, no secret or private address,
  reproducibility, and `install.sh` against stand-ins for ssh and scp.

## Licences

`/usr/share/doc/dock-receiver/`: the project's MIT `LICENSE`, `THIRD-PARTY-NOTICES.md` (the
third-party code linked into the device binaries, from this repository's
[`packaging/THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)), the licence texts under `licenses/`
(`packaging/licenses/MANIFEST.tsv` lists them), and for `cspot-ios3`, which as a whole is distributed
under GPL-3.0-or-later, the GPL version 3 text (`licenses/gcc.COPYING3.GPL-3.0.txt`) and
`SOURCE-cspot-ios3.txt`, which says where its source is: this repository at the packaged commit, the
pinned commit of the Spotify library and the patch in `spotify/patches/`.
