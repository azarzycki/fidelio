#!/usr/bin/env bash
# packaging/hosttest/test_setup.sh BASH40 [REPO]
#
# Host test of the install scripts (packaging/dock-receiver/setup.sh, the dock-remote package's
# scripts.sh, cspot-run, keepalive): the two packages, built by tools/build-debs.sh from a fake ARM
# output, are installed with a fake dpkg into fake device roots (DOCKRECEIVER_ROOT) that look like the
# phone and the iPad before B1.17, and like a fresh device.
#
# What the device code runs with, as on the devices:
#   * /bin/sh is BASH40 (bash 4.0, built by tools/run-host-tests.sh), started under the name `sh`;
#   * PATH holds ONLY the tools both devices have (stubs.sh), plus stubs of launchctl, killall, uicache,
#     su, dpkg and df: a call of any other tool fails, and so does `sed -E`.
# Output: one `ok <text>` or `FAIL <text>` line per check (tools/run-host-tests.sh counts them).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASH40="${1:?usage: test_setup.sh BASH40 [REPO]}"
REPO="${2:-$(cd "$HERE/../.." && pwd)}"
[ -x "$BASH40" ] || { echo "FAIL bash 4.0 not found at $BASH40"; exit 1; }
"$BASH40" -c 'case "$BASH_VERSION" in 4.0.*) exit 0;; esac; exit 1' || { echo "FAIL $BASH40 is not bash 4.0"; exit 1; }
. "$HERE/stubs.sh"

T="$(mktemp -d /tmp/dr-pkgtest.XXXXXX)"
[ -n "${KEEP_TMP:-}" ] || trap 'rm -rf "$T"' EXIT
make_tooldirs "$T"
mkdir -p "$T/bin"; ln -s "$BASH40" "$T/bin/sh"
SH="$T/bin/sh"
NOK=0; NFAIL=0
ok()   { NOK=$((NOK + 1)); echo "ok   $*"; }
bad()  { NFAIL=$((NFAIL + 1)); echo "FAIL $*"; }
check() { local d="$1" o; shift; if o="$("$@" 2>&1)"; then ok "$d"; else bad "$d"; [ -z "$o" ] || printf '%s
' "$o" | sed -n '1,5s/^/    /p'; fi; }
check_not() { local d="$1"; shift; if "$@" >/dev/null 2>&1; then bad "$d"; else ok "$d"; fi; }

# --- the packages, from a fake ARM output ------------------------------------------------------------
A="$T/arm-out"
mkdir -p "$A/verify" "$A/NowPlaying.app" "$A/NowPlaying.bundle" "$A/NowPlaying.lockbundle"
plist() {  # FILE ID VERSION
  cat > "$1" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
	<key>CFBundleIdentifier</key>
	<string>$2</string>
	<key>CFBundleShortVersionString</key>
	<string>$3</string>
</dict>
</plist>
EOF
}
for b in dock-audiod dock-raop cspot-ios3 dock-lockctl DockRemote.dylib NowPlaying.app/NowPlaying NowPlaying.bundle/NowPlaying NowPlaying.lockbundle/NowPlaying; do
  printf 'fake binary %s\n' "$b" > "$A/$b"; chmod 755 "$A/$b"
done
plist "$A/NowPlaying.app/Info.plist" com.dockreceiver.nowplaying 1.5.0
plist "$A/NowPlaying.bundle/Info.plist" com.dockreceiver.lockplugin 1.0
plist "$A/NowPlaying.lockbundle/Info.plist" com.dockreceiver.lockplugin 1.0
printf 'icon\n' > "$A/NowPlaying.app/Icon.png"
printf '{ Filter = { Executables = ( "iapd" ); }; }\n' > "$A/DockRemote.plist"
( cd "$A" && sha256sum dock-audiod dock-raop cspot-ios3 dock-lockctl DockRemote.dylib NowPlaying.app/NowPlaying NowPlaying.bundle/NowPlaying NowPlaying.lockbundle/NowPlaying ) > "$A/SHA256SUMS"
echo 0 > "$A/verify/.bad"
if BUILD_ROOT="$T/br" "$REPO/tools/build-debs.sh" --arm-out "$A" --out "$T/debs" --no-repro-check "$REPO" > "$T/build-debs.log" 2>&1; then
  ok "build-debs.sh builds both packages from a fake ARM output"
else
  bad "build-debs.sh failed: $(tail -n 3 "$T/build-debs.log")"; echo "TOTAL $NOK ok, $NFAIL FAIL"; exit 1
fi
MAIN="$(ls "$T"/debs/dock-receiver_*_iphoneos-arm.deb)"
REMOTE="$(ls "$T"/debs/dock-receiver-dockremote_*_iphoneos-arm.deb)"

# --- fake device roots ----------------------------------------------------------------------------------
R=""
OUT="$T/out.txt"
SECRETS=""   # every credential value used: none may appear in any output or log
dev() {  # run CMD... the way the device would (restricted PATH, bash 4.0 as sh), output to $OUT (appended)
  env -i PATH="$T/stubs:$T/tools" HOME=/var/root DOCKRECEIVER_ROOT="$R" DPKG_SH="$SH" \
    DOCKRECEIVER_TIMEOUT_S="${TIMEOUT_S:-3}" ${DEVENV:-} "$@" >> "$OUT" 2>&1
}
dpkg_i() { : > "$OUT"; dev "$T/stubs/dpkg" -i "$@"; }
setup_cmd() { : > "$OUT"; dev "$SH" "$R/usr/sbin/dock-receiver-setup" "$@"; }

oldplist() {  # LABEL PROGRAM [ARG...]
  local l="$1" a; shift
  {
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0">\n<dict>\n\t<key>Label</key>\n\t<string>%s</string>\n' "$l"
    printf '\t<key>ProgramArguments</key>\n\t<array>\n'
    for a in "$@"; do printf '\t\t<string>%s</string>\n' "$a"; done
    printf '\t</array>\n\t<key>UserName</key>\n\t<string>mobile</string>\n\t<key>KeepAlive</key>\n\t<true/>\n</dict>\n</plist>\n'
  } > "$R/Library/LaunchDaemons/$l.plist"
  chmod 644 "$R/Library/LaunchDaemons/$l.plist"
}
running() { : > "$R/.fake/procs/$1"; }
loaded() { : > "$R/.fake/jobs/$1"; }

new_root() {  # NAME OSVERSION
  R="$T/root.$1"; rm -rf "$R"
  mkdir -p "$R/Applications/Preferences.app" "$R/Library/LaunchDaemons" "$R/System/Library/SpringBoardPlugins" \
    "$R/System/Library/CoreServices" "$R/var/mobile/Library/Preferences" "$R/var/root" "$R/var/tmp" "$R/usr/lib" \
    "$R/usr/bin" "$R/usr/sbin" "$R/usr/libexec" "$R/usr/share/doc" "$R/.fake/procs" "$R/.fake/jobs"
  printf 'apple app\n' > "$R/Applications/Preferences.app/Info.plist"
  : > "$R/.fake/procs/SpringBoard"; : > "$R/.fake/procs/iapd"
  printf 'apple job\n' > "$R/Library/LaunchDaemons/com.apple.mDNSResponder.plist"
  cat > "$R/System/Library/CoreServices/SystemVersion.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
	<key>ProductName</key>
	<string>iPhone OS</string>
	<key>ProductVersion</key>
	<string>$2</string>
</dict>
</plist>
EOF
}

old_common() {  # the old data directory, binaries, jobs, app (both devices)
  local O="$R/var/mobile/fidelio"
  mkdir -p "$O/b1"
  printf 'audiod.dock_remote=on\naudiod.wake_mode=launch\naudiod.lock_plugin=on\n' > "$O/settings.conf"
  : > "$O/remote-tweak.observe"; : > "$O/lockplugin.debug"
  printf 'spotify\n' > "$O/active-source"; printf 'old status\n' > "$O/status-spotify.plist"
  for b in fidelio-audiod fidelio-raop cspot-ios3 fidelio-lockctl; do printf 'old %s\n' "$b" > "$O/b1/$b"; chmod 755 "$O/b1/$b"; done
  printf 'old\n' > "$O/b1/fidelio-audiod.pre-r3.bak"
  mkdir -p "$R/Applications/NowPlaying.app"
  plist "$R/Applications/NowPlaying.app/Info.plist" com.fidelio.nowplaying 1.4.3
  printf 'old app\n' > "$R/Applications/NowPlaying.app/NowPlaying"; chmod 755 "$R/Applications/NowPlaying.app/NowPlaying"
  printf 'old app 1.4.2\n' > "$R/Applications/NowPlaying.app.pre-b11616"
  oldplist com.fidelio.audiod /var/mobile/fidelio/b1/fidelio-audiod --event-log /var/mobile/fidelio/b1/events.log
  for l in audiod raop cspot keepalive; do loaded com.fidelio.$l; done
  running fidelio-audiod; running fidelio-raop; running cspot-ios3; running bash; running NowPlaying
}

phone_launcher() {  # FORM ID SECRET: the phone's Spotify launcher (and helpers) holding the credentials
  local O="$R/var/mobile/fidelio" id="$2" sec="$3" c=/var/mobile/fidelio/b1/cspot-ios3
  case "$1" in
    vars) printf '#!/bin/bash\n# Spotify launcher\nCLIENT_ID=%s\nCLIENT_SECRET=%s\ncd /var/mobile/fidelio\nexec %s --client-id "$CLIENT_ID" --client-secret "$CLIENT_SECRET" -b 320 -n "iPhone Fidelio"\n' "$id" "$sec" "$c" > "$O/b1/test_cspot.sh" ;;
    export) printf '#!/bin/bash\nexport SPOTIFY_CLIENT_ID="%s"\nexport SPOTIFY_CLIENT_SECRET='"'"'%s'"'"'\nwhile true; do %s --client-id ${SPOTIFY_CLIENT_ID} --client-secret "${SPOTIFY_CLIENT_SECRET}" --bitrate 320 --name "iPhone Fidelio"; sleep 2; done\n' "$id" "$sec" "$c" > "$O/b1/test_cspot.sh" ;;
    bare) printf '#!/bin/bash\n[ -n "$X" ] && echo -n hello\n%s -b 320 --client-id %s --client-secret %s -n "iPhone Fidelio" > /var/mobile/fidelio/b1/cspot.log 2>&1\n' "$c" "$id" "$sec" > "$O/b1/test_cspot.sh" ;;
    equals) printf '#!/bin/bash\nexec %s --client-id=%s --client-secret="%s" --name="iPhone Fidelio" --bitrate=320\n' "$c" "$id" "$sec" > "$O/b1/test_cspot.sh" ;;
    continued) printf '#!/bin/bash\n# old line: --client-id NOTTHIS\nexec %s \\\n  --client-id '"'"'%s'"'"' \\\n  --client-secret '"'"'%s'"'"' \\\n  -b 320 -n "iPhone Fidelio"\n' "$c" "$id" "$sec" > "$O/b1/test_cspot.sh" ;;
    helper) printf '#!/bin/bash\nexec /bin/bash /var/mobile/fidelio/cspotd.sh\n' > "$O/b1/test_cspot.sh"
            printf '#!/bin/bash\nAPP_CLIENT_ID=%s; APP_SECRET=%s\n%s -b 320 -n "iPhone Fidelio" --client-id $APP_CLIENT_ID --client-secret $APP_SECRET\n' "$id" "$sec" "$c" > "$O/cspotd.sh" ;;
    none) printf '#!/bin/bash\nexec %s -b 320 -n "iPhone Fidelio"\n' "$c" > "$O/b1/test_cspot.sh" ;;
  esac
}

phone_old() {  # FORM ID SECRET
  new_root "phone-$1" 3.1.3
  old_common
  local O="$R/var/mobile/fidelio"
  : > "$O/remote-tweak.swallow"
  phone_launcher "$@"
  printf '#!/bin/bash\nexec /var/mobile/fidelio/b1/fidelio-raop\n' > "$O/b1/test_raop.sh"
  printf '#!/bin/bash\nR=$(ipconfig getoption en0 router)\nwhile :; do ping -c1 $R >/dev/null; sleep 2; done\n' > "$O/keepalive.sh"
  oldplist com.fidelio.cspot /bin/bash /var/mobile/fidelio/b1/test_cspot.sh
  oldplist com.fidelio.raop /bin/bash /var/mobile/fidelio/b1/test_raop.sh
  oldplist com.fidelio.keepalive /bin/bash /var/mobile/fidelio/keepalive.sh
  oldplist com.fidelio.syslogd /usr/sbin/syslogd
  printf 'stale\n' > "$R/Library/LaunchDaemons/com.fidelio.cspot.plist.rev6.bak"
  mkdir -p "$R/System/Library/SpringBoardPlugins/Fidelio.bundle" "$R/System/Library/SpringBoardPlugins/Fidelio.bundle.pre-r3" "$R/System/Library/SpringBoardPlugins/IncomingCall.bundle"
  printf 'old plugin\n' > "$R/System/Library/SpringBoardPlugins/Fidelio.bundle/Fidelio"
  printf 'apple\n' > "$R/System/Library/SpringBoardPlugins/IncomingCall.bundle/IncomingCall"
  mkdir -p "$R/Library/MobileSubstrate/DynamicLibraries"
  printf 'substrate\n' > "$R/usr/lib/libsubstrate.dylib"
  printf 'old tweak\n' > "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib"
  printf 'old filter\n' > "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.plist"
  printf 'older tweak\n' > "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib.pre-r3.bak"
  printf '<plist><dict><key>PrintIapPackets</key><true/></dict></plist>\n' > "$R/var/mobile/Library/Preferences/com.apple.iapd.plist"
  chown -R 501:501 "$O"
  loaded com.fidelio.syslogd; running syslogd
}

ipad_old() {  # ID SECRET
  new_root ipad 5.1.1
  old_common
  local O="$R/var/mobile/fidelio"
  printf 'CLIENT_ID="%s"\nCLIENT_SECRET="%s"\n' "$1" "$2" > "$O/creds.env"; chmod 600 "$O/creds.env"
  printf 'audiod.unlock_on_play=off\n' >> "$O/settings.conf"
  printf '#!/bin/bash\n. /var/mobile/fidelio/creds.env\nexec /var/mobile/fidelio/b1/cspot-ios3 --client-id "$CLIENT_ID" --client-secret "$CLIENT_SECRET" -b 320 --name "iPad Fidelio"\n' > "$O/b1/run_cspot.sh"
  printf '#!/bin/bash\nexec /var/mobile/fidelio/b1/fidelio-raop --name "iPad Fidelio (AirPlay)"\n' > "$O/b1/run_raop.sh"
  printf '#!/bin/bash\nwhile :; do printf x > /dev/udp/192.0.2.10/9; sleep 2; done\n' > "$O/keepalive.sh"
  oldplist com.fidelio.cspot /bin/bash /var/mobile/fidelio/b1/run_cspot.sh
  oldplist com.fidelio.raop /bin/bash /var/mobile/fidelio/b1/run_raop.sh
  oldplist com.fidelio.keepalive /bin/bash /var/mobile/fidelio/keepalive.sh
  local S="$R/System/Library/SpringBoardPlugins"
  mkdir -p "$S/Fidelio.lockbundle" "$S/Fidelio.lockbundle.pre-r3" "$S/NowPlayingArtLockScreen.lockbundle"
  printf 'old plugin\n' > "$S/Fidelio.lockbundle/Fidelio"
  printf 'apple art\n' > "$S/NowPlayingArtLockScreen.lockbundle/NowPlayingArtLockScreen"
  printf 'apple plist\n' > "$S/NowPlayingArtLockScreen.lockbundle/Info.plist"
  chown -R 501:501 "$O"
}

snapshot() {  # everything in the fake root but the stubs' call log: paths, modes, owners, contents
  ( cd "$R" && find . -path ./.fake/calls.log -prune -o -printf '%p %m %U:%G %s %l\n' | LC_ALL=C sort
    find . -path ./.fake/calls.log -prune -o -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum )
}
tree_sum() { ( cd "$1" && find . -printf '%p %m %U:%G %s\n' | LC_ALL=C sort; find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum ) | sha256sum; }
mode_owner() { stat -c '%a %u:%g' "$1"; }
file_has() { grep -q -- "$2" "$1"; }
no_secret_anywhere() {  # nothing outside the credentials files holds a value
  local s f leak=""
  for s in $SECRETS; do
    grep -q -F -- "$s" "$OUT" && leak="$leak output"
    for f in "$R/var/mobile/dockreceiver/install.log" "$R/var/dockreceiver-migration/state" "$R/.fake/calls.log"; do
      [ -f "$f" ] && grep -q -F -- "$s" "$f" && leak="$leak ${f#$R}"
    done
    for f in $(grep -r -l -F -- "$s" "$R" 2>/dev/null); do
      case "$f" in
        "$R/var/mobile/dockreceiver/spotify-credentials.conf"|"$R/var/mobile/fidelio/"*) ;;
        *) leak="$leak ${f#$R}" ;;
      esac
    done
  done
  [ -z "$leak" ] || { echo "leak:$leak" >&2; return 1; }
}
no_tool_missing() { ! grep -q -e 'command not found' -e 'not available on the devices' "$OUT"; }
jobs_are() {  # LABEL...: exactly these jobs loaded (com.apple and syslogd ignored)
  local got want
  got="$(ls "$R/.fake/jobs" | grep -v -e com.fidelio.syslogd | LC_ALL=C sort | tr '\n' ' ')"
  want=""; [ $# = 0 ] || want="$(printf '%s\n' "$@" | LC_ALL=C sort | tr '\n' ' ')"
  [ "$got" = "$want" ] || { echo "jobs: '$got' want '$want'" >&2; return 1; }
}
proc() { [ -e "$R/.fake/procs/$1" ]; }
creds_are() {  # ID SECRET
  local f="$R/var/mobile/dockreceiver/spotify-credentials.conf"
  [ "$(mode_owner "$f")" = "600 501:501" ] && grep -qx "SPOTIFY_CLIENT_ID=$1" "$f" && grep -qx "SPOTIFY_CLIENT_SECRET=$2" "$f"
}

NEWJOBS="com.dockreceiver.audiod com.dockreceiver.raop com.dockreceiver.cspot com.dockreceiver.keepalive"
OLDJOBS="com.fidelio.audiod com.fidelio.raop com.fidelio.cspot com.fidelio.keepalive"
B_="var/dockreceiver-migration"
D_="var/mobile/dockreceiver"

# the old setup as it was: jobs, plists, processes, app, plugin (NAME of the plugin)
old_setup_live() {  # PLUGIN
  local l
  for l in $OLDJOBS; do [ -f "$R/Library/LaunchDaemons/$l.plist" ] || { echo "no $l.plist" >&2; return 1; }; done
  jobs_are $OLDJOBS || return 1
  proc fidelio-audiod && proc fidelio-raop && proc cspot-ios3 || { echo "old daemons not running" >&2; return 1; }
  ! proc dock-audiod || { echo "dock-audiod runs" >&2; return 1; }
  for l in $NEWJOBS; do [ ! -e "$R/Library/LaunchDaemons/$l.plist" ] || { echo "$l.plist left" >&2; return 1; }; done
  grep -q com.fidelio.nowplaying "$R/Applications/NowPlaying.app/Info.plist" || { echo "old app not in place" >&2; return 1; }
  [ -e "$R/System/Library/SpringBoardPlugins/$1" ] || { echo "no $1" >&2; return 1; }
}
new_setup_live() {
  local l
  jobs_are $NEWJOBS || return 1
  proc dock-audiod && proc dock-raop && proc cspot-ios3 || { echo "new daemons not running" >&2; return 1; }
  ! proc fidelio-audiod && ! proc fidelio-raop || { echo "an old daemon runs" >&2; return 1; }
  for l in $NEWJOBS; do [ "$(mode_owner "$R/Library/LaunchDaemons/$l.plist")" = "644 0:0" ] || return 1; done
  for l in $OLDJOBS; do [ ! -e "$R/Library/LaunchDaemons/$l.plist" ] || return 1; done
  [ -n "$(find "$R/$D_/active-source" -newer "$R/$B_/.load-ref")" ] || return 1
}

# =================================================================================================
echo "# phone (iPhone OS 3.1.3): the dry run, then the install of both packages"
ID=f1d0c0ffee01aa11bb22cc33dd44ee55; SEC=5ecre7a01bb22cc33dd44ee55ff66770; SECRETS="$SECRETS $ID $SEC"
phone_old vars "$ID" "$SEC"
O_SUM="$(tree_sum "$R/var/mobile/fidelio")"
before="$(snapshot)"; snapshot > "$T/snap.before"
w="$T/ctl-main"; rm -rf "$w"; dpkg-deb -e "$MAIN" "$w"
: > "$OUT"; dev "$SH" "$w/preinst" check; rc=$?
after="$(snapshot)"; snapshot > "$T/snap.after"
check "phone: the dry run exits 0 (ready)" [ "$rc" = 0 ]
check "phone: the dry run changes nothing in the fake root" [ "$before" = "$after" ]
check "phone: dry run: old setup found" file_has "$OUT" "old setup: yes"
check "phone: dry run: credentials found in the launcher, no value shown" file_has "$OUT" "credentials: found in /var/mobile/fidelio/b1/test_cspot.sh"
check "phone: dry run: lists the old jobs" file_has "$OUT" "old job com.fidelio.cspot: plist, loaded"
check "phone: dry run: free space and prerequisites" grep -q -e "free space on /: 137000 KB" "$OUT"
check "phone: dry run: names the syslogd job as left alone" file_has "$OUT" "com.fidelio.syslogd"
check "phone: dry run: Fidelio.bundle would be moved" file_has "$OUT" "lock plugin: Fidelio.bundle: would be moved"
check "phone: dry run used only the devices' tools" no_tool_missing
check "phone: dry run: no secret in the output" no_secret_anywhere
w="$T/ctl-remote"; rm -rf "$w"; dpkg-deb -e "$REMOTE" "$w"
: > "$OUT"; dev "$SH" "$w/preinst" check
check "phone: dock-remote dry run: ready" file_has "$OUT" "result: ready"

dpkg_i "$MAIN" "$REMOTE"; rc=$?
cp "$OUT" "$T/phone-install.txt"
check "phone: dpkg -i of both packages exits 0" [ "$rc" = 0 ]
check "phone: install used only the devices' tools" no_tool_missing
check "phone: the new setup runs (4 new jobs, new daemons, no old daemon, plists root 644)" new_setup_live
check "phone: no old daemon process left (fidelio-audiod, fidelio-raop)" eval '! proc fidelio-audiod && ! proc fidelio-raop'
check "phone: the old plists are in the recoverable place" eval 'for l in $OLDJOBS; do [ -f "$R/$B_/LaunchDaemons/$l.plist" ] || exit 1; done'
check "phone: the recoverable place is root-only (700 0:0)" [ "$(mode_owner "$R/$B_")" = "700 0:0" ]
check "phone: the syslogd job and its plist are untouched" eval '[ -f "$R/Library/LaunchDaemons/com.fidelio.syslogd.plist" ] && [ -e "$R/.fake/jobs/com.fidelio.syslogd" ]'
check "phone: the old data directory is unchanged" [ "$(tree_sum "$R/var/mobile/fidelio")" = "$O_SUM" ]
check "phone: settings.conf copied (old lines kept)" eval 'grep -qx audiod.dock_remote=on "$R/$D_/settings.conf" && grep -qx audiod.lock_plugin=on "$R/$D_/settings.conf"'
check "phone: the device name and bitrate came from the launcher into settings.conf" eval 'grep -qx "cspot.name=iPhone Fidelio" "$R/$D_/settings.conf" && grep -qx "cspot.bitrate=320" "$R/$D_/settings.conf"'
check "phone: the tweak's flag files were copied (owner mobile)" eval '[ "$(mode_owner "$R/$D_/remote-tweak.swallow")" = "644 501:501" ] && [ -f "$R/$D_/remote-tweak.observe" ]'
check "phone: status files are not copied" [ ! -e "$R/$D_/status-spotify.plist" ]
check "phone: the credentials came from the launcher (600, mobile)" creds_are "$ID" "$SEC"
check "phone: output says where the credentials were found" file_has "$OUT" "credentials: found in /var/mobile/fidelio/b1/test_cspot.sh"
check "phone: no keepalive.conf (the old one used ipconfig)" [ ! -e "$R/$D_/keepalive.conf" ]
check "phone: data dir and logs owned by mobile" eval '[ "$(mode_owner "$R/$D_")" = "755 501:501" ] && [ "$(mode_owner "$R/$D_/logs/cspot.log")" = "644 501:501" ]'
check "phone: the old app is in the recoverable place" grep -q com.fidelio.nowplaying "$R/$B_/Applications/NowPlaying.app/Info.plist"
check "phone: the new app is installed" grep -q com.dockreceiver.nowplaying "$R/Applications/NowPlaying.app/Info.plist"
check "phone: the old app copies beside it are untouched" [ -f "$R/Applications/NowPlaying.app.pre-b11616" ]
check "phone: Fidelio.bundle moved aside, its .pre-r3 copy and Apple's plugin untouched" eval '[ -f "$R/$B_/SpringBoardPlugins/Fidelio.bundle/Fidelio" ] && [ ! -e "$R/System/Library/SpringBoardPlugins/Fidelio.bundle" ] && [ -d "$R/System/Library/SpringBoardPlugins/Fidelio.bundle.pre-r3" ] && [ -f "$R/System/Library/SpringBoardPlugins/IncomingCall.bundle/IncomingCall" ]'
check "phone: both new lock plugin bundles are installed" eval '[ -f "$R/System/Library/SpringBoardPlugins/NowPlaying.bundle/NowPlaying" ] && [ -f "$R/System/Library/SpringBoardPlugins/NowPlaying.lockbundle/NowPlaying" ]'
check "phone: the old tweak was copied to the recoverable place" grep -qx "old tweak" "$R/$B_/DynamicLibraries/DockRemote.dylib"
check "phone: the new tweak is installed" grep -q "fake binary DockRemote.dylib" "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib"
check "phone: the icon cache was refreshed as mobile" grep -q "su mobile -c uicache" "$R/.fake/calls.log"
check_not "phone: SpringBoard was not restarted" grep -q -e "killall.*SpringBoard" -e sbreload "$R/.fake/calls.log"
check_not "phone: iapd was not restarted" grep -q "killall.*iapd" "$R/.fake/calls.log"
check "phone: the old NowPlaying process was closed (exact name)" grep -qx "killall NowPlaying" "$R/.fake/calls.log"
check "phone: says a SpringBoard restart is still needed" file_has "$OUT" "restart SpringBoard to finish: the lock plugin and the app's new id"
check "phone: says how the tweak takes effect (undocked killall iapd or reboot)" file_has "$OUT" "killall iapd"
check "phone: names what it left alone (syslogd, iapd logging)" eval 'grep -q "left alone: the job com.fidelio.syslogd" "$OUT" && grep -q "left alone: iapd" "$OUT"'
check "phone: the old jobs were loaded in order audiod, raop, cspot, keepalive" eval '[ "$(grep -o "launchctl load .*com.dockreceiver.[a-z]*" "$R/.fake/calls.log" | sed "s/.*com.dockreceiver.//" | tr "\n" " ")" = "audiod raop cspot keepalive " ]'
check "phone: a log of what was done exists" grep -q "moved com.fidelio.audiod.plist" "$R/$D_/install.log"
check "phone: no credential value in any output, log or other file" no_secret_anywhere
check "phone: the app's 'Daemons not running' cannot show: active-source is fresh and audiod runs" eval 'proc dock-audiod && [ -n "$(find "$R/$D_/active-source" -mmin -1)" ]'

echo "# phone: a second dpkg -i of the same packages (re-run)"
before_conf="$(cat "$R/$D_/settings.conf")"; before_creds="$(sha256sum < "$R/$D_/spotify-credentials.conf")"
before_b="$(tree_sum "$R/$B_/LaunchDaemons")"
dpkg_i "$MAIN" "$REMOTE"; rc=$?
check "re-run: exits 0" [ "$rc" = 0 ]
check "re-run: the new setup runs" new_setup_live
check "re-run: settings.conf unchanged (nothing added twice)" [ "$(cat "$R/$D_/settings.conf")" = "$before_conf" ]
check "re-run: credentials unchanged" [ "$(sha256sum < "$R/$D_/spotify-credentials.conf")" = "$before_creds" ]
check "re-run: the kept old plists unchanged" [ "$(tree_sum "$R/$B_/LaunchDaemons")" = "$before_b" ]
check "re-run: the kept old app is still the old one" grep -q com.fidelio.nowplaying "$R/$B_/Applications/NowPlaying.app/Info.plist"
check "re-run: no secret anywhere" no_secret_anywhere

echo "# phone: respring, cleanup, rollback"
printf 'spotify\n' > "$R/$D_/active-source"
setup_cmd respring; rc=$?
check "respring is refused while something plays" eval '[ "$rc" != 0 ] && ! grep -q "killall SpringBoard" "$R/.fake/calls.log"'
setup_cmd cleanup; rc=$?
check "cleanup without --yes lists, removes nothing" eval '[ "$rc" = 0 ] && grep -q "/var/mobile/fidelio" "$OUT" && [ -d "$R/var/mobile/fidelio" ] && [ -d "$R/$B_" ]'
setup_cmd rollback; rc=$?
cp "$OUT" "$T/phone-rollback.txt"
check "rollback exits 0" [ "$rc" = 0 ]
check "rollback: the old setup runs again" old_setup_live Fidelio.bundle
check "rollback: both packages are removed" eval '[ ! -e "$R/.fake/dpkg/dock-receiver.status" ] && [ ! -e "$R/.fake/dpkg/dock-receiver-dockremote.status" ] && [ ! -e "$R/usr/libexec/dock-receiver/dock-audiod" ]'
check "rollback: the old tweak is back" grep -qx "old tweak" "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib"
check "rollback: the old data directory is still unchanged" [ "$(tree_sum "$R/var/mobile/fidelio")" = "$O_SUM" ]
check "rollback: the new data directory is kept" [ -f "$R/$D_/spotify-credentials.conf" ]
check "rollback: used only the devices' tools" no_tool_missing
check "rollback: no secret anywhere" no_secret_anywhere
: > "$OUT"; dev "$SH" "$T/ctl-main/preinst" cleanup; rc=$?
check "cleanup is refused while the old setup is active" eval '[ "$rc" != 0 ] && [ -d "$R/var/mobile/fidelio" ]'

echo "# phone: install again after the rollback, then cleanup --yes"
dpkg_i "$MAIN" "$REMOTE"; rc=$?
check "install after rollback: exits 0, new setup runs" eval '[ "$rc" = 0 ] && new_setup_live'
setup_cmd cleanup --yes; rc=$?; cp "$OUT" "$T/cleanup-yes.txt"
check "cleanup --yes removes the old directory and the recoverable place" eval '[ "$rc" = 0 ] && [ ! -e "$R/var/mobile/fidelio" ] && [ ! -e "$R/$B_" ]'
check "cleanup --yes: the new setup still runs" eval 'jobs_are $NEWJOBS && proc dock-audiod'
printf 'none\n' > "$R/$D_/active-source"
setup_cmd respring; rc=$?
check "respring (nothing playing) restarts SpringBoard" eval '[ "$rc" = 0 ] && grep -q "killall SpringBoard" "$R/.fake/calls.log"'

# =================================================================================================
echo "# the phone's credential forms (each a fresh device)"
n=1
for form in export bare equals continued helper; do
  n=$((n + 1))
  ID="f1d0c0ffee0${n}aa11bb22cc33dd44ee55"; SEC="5ecre7a0${n}bb22cc33dd44ee55ff66770"; SECRETS="$SECRETS $ID $SEC"
  phone_old "$form" "$ID" "$SEC"
  dpkg_i "$MAIN" "$REMOTE"; rc=$?
  check "form $form: install exits 0 and the new setup runs" eval '[ "$rc" = 0 ] && new_setup_live'
  check "form $form: the credentials were carried over exactly" creds_are "$ID" "$SEC"
  check "form $form: name and bitrate carried" eval 'grep -qx "cspot.name=iPhone Fidelio" "$R/$D_/settings.conf" && grep -qx "cspot.bitrate=320" "$R/$D_/settings.conf"'
  check "form $form: no secret anywhere" no_secret_anywhere
done
phone_old none x y
dpkg_i "$MAIN" "$REMOTE"; rc=$?
check "no credentials: install exits 0 and the new setup runs" eval '[ "$rc" = 0 ] && new_setup_live'
check "no credentials: says not found and leaves an empty template (600, mobile)" eval 'grep -q "credentials: not found" "$OUT" && grep -qx "SPOTIFY_CLIENT_ID=" "$R/$D_/spotify-credentials.conf" && [ "$(mode_owner "$R/$D_/spotify-credentials.conf")" = "600 501:501" ]'
check "no credentials: says it is still needed" file_has "$OUT" "STILL NEEDED: the Spotify credentials"

# =================================================================================================
echo "# iPad (iOS 5.1.1): creds.env, Fidelio.lockbundle beside Apple's NowPlayingArtLockScreen.lockbundle"
ID=f1d0c0ffee09aa11bb22cc33dd44ee55; SEC=5ecre7a09bb22cc33dd44ee55ff66770; SECRETS="$SECRETS $ID $SEC"
ipad_old "$ID" "$SEC"
ART_SUM="$(tree_sum "$R/System/Library/SpringBoardPlugins/NowPlayingArtLockScreen.lockbundle")"
O_SUM="$(tree_sum "$R/var/mobile/fidelio")"
: > "$OUT"; dev "$SH" "$T/ctl-main/preinst" check; rc=$?
check "iPad: dry run ready, credentials found in creds.env" eval '[ "$rc" = 0 ] && grep -q "credentials: found in /var/mobile/fidelio/creds.env" "$OUT"'
check "iPad: dry run: keepalive target found" file_has "$OUT" "keepalive target: found in /var/mobile/fidelio/keepalive.sh"
: > "$OUT"; dev "$SH" "$T/ctl-remote/preinst" check; rc=$?
check "iPad: the dock-remote dry run says NOT ready (iOS 5)" eval '[ "$rc" != 0 ] && grep -q "iPhone OS 3.x only" "$OUT"'
dpkg_i "$MAIN"; rc=$?
check "iPad: dpkg -i of the main package exits 0" [ "$rc" = 0 ]
check "iPad: the new setup runs" new_setup_live
check "iPad: used only the devices' tools" no_tool_missing
check "iPad: credentials from creds.env" creds_are "$ID" "$SEC"
check "iPad: the iPad's names and bitrate in settings.conf" eval 'grep -qx "cspot.name=iPad Fidelio" "$R/$D_/settings.conf" && grep -qx "raop.name=iPad Fidelio (AirPlay)" "$R/$D_/settings.conf" && grep -qx "cspot.bitrate=320" "$R/$D_/settings.conf" && grep -qx audiod.unlock_on_play=off "$R/$D_/settings.conf"'
check "iPad: keepalive target carried into keepalive.conf (no value printed)" eval 'grep -qx KEEPALIVE_HOST=192.0.2.10 "$R/$D_/keepalive.conf" && ! grep -q 192.0.2.10 "$OUT"'
check "iPad: Fidelio.lockbundle moved aside" eval '[ -f "$R/$B_/SpringBoardPlugins/Fidelio.lockbundle/Fidelio" ] && [ ! -e "$R/System/Library/SpringBoardPlugins/Fidelio.lockbundle" ]'
check "iPad: Apple's NowPlayingArtLockScreen.lockbundle untouched" [ "$(tree_sum "$R/System/Library/SpringBoardPlugins/NowPlayingArtLockScreen.lockbundle")" = "$ART_SUM" ]
check "iPad: the old data directory is unchanged" [ "$(tree_sum "$R/var/mobile/fidelio")" = "$O_SUM" ]
check "iPad: no secret anywhere" no_secret_anywhere
dpkg_i "$REMOTE"; rc=$?
check "iPad: the dock-remote package is refused (preinst), nothing of it installed" eval '[ "$rc" != 0 ] && [ ! -e "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib" ] && new_setup_live'
setup_cmd rollback; rc=$?
check "iPad: rollback brings the old setup back" eval '[ "$rc" = 0 ] && old_setup_live Fidelio.lockbundle'
check "iPad: after the rollback Apple's lockbundle is still untouched" [ "$(tree_sum "$R/System/Library/SpringBoardPlugins/NowPlayingArtLockScreen.lockbundle")" = "$ART_SUM" ]

# =================================================================================================
echo "# a fresh device (no old setup)"
new_root fresh 3.1.3
mkdir -p "$R/Library/MobileSubstrate/DynamicLibraries"; printf 'substrate\n' > "$R/usr/lib/libsubstrate.dylib"
: > "$OUT"; dev "$SH" "$T/ctl-main/preinst" check; rc=$?
check "fresh: dry run ready, old setup: no" eval '[ "$rc" = 0 ] && grep -q "old setup: no" "$OUT"'
dpkg_i "$MAIN" "$REMOTE"; rc=$?
check "fresh: install exits 0, the new setup runs" eval '[ "$rc" = 0 ] && new_setup_live'
check "fresh: no recoverable old setup recorded" eval '[ ! -d "$R/$B_/LaunchDaemons" ] && [ ! -d "$R/$B_/Applications" ]'
check "fresh: used only the devices' tools" no_tool_missing
dpkg_i "$MAIN" "$REMOTE"; rc=$?
check "fresh: second run exits 0" eval '[ "$rc" = 0 ] && new_setup_live'
: > "$OUT"; dev "$T/stubs/dpkg" -r dock-receiver-dockremote; dev "$T/stubs/dpkg" -r dock-receiver
check "fresh: dpkg -r stops and removes everything, no old jobs appear" eval 'jobs_are && ! proc dock-audiod && [ ! -e "$R/Library/LaunchDaemons/com.dockreceiver.audiod.plist" ] && [ ! -e "$R/Applications/NowPlaying.app" ]'

# =================================================================================================
echo "# failures injected at each step (phone): the old setup stays restartable, rollback works"
for st in preflight app-aside settings options credentials keepalive-target stop-jobs old-plists new-plists old-plugin load-jobs; do
  ID=f1d0c0ffee77aa11bb22cc33dd44ee55; SEC=5ecre7a77bb22cc33dd44ee55ff66770; SECRETS="$SECRETS $ID $SEC"
  phone_old vars "$ID" "$SEC"
  O_SUM="$(tree_sum "$R/var/mobile/fidelio")"
  DEVENV="DOCKRECEIVER_TEST_FAIL=$st" dpkg_i "$MAIN" "$REMOTE"; rc=$?
  cp "$OUT" "$T/fail-$st.txt"
  check "fail at $st: dpkg -i exits non-zero" [ "$rc" != 0 ]
  check "fail at $st: the old setup runs as before (jobs, plists, daemons, app, plugin)" old_setup_live Fidelio.bundle
  check "fail at $st: the old data directory is unchanged" [ "$(tree_sum "$R/var/mobile/fidelio")" = "$O_SUM" ]
  check "fail at $st: a clear failure message" grep -q -e "ERROR: " "$OUT"
  check "fail at $st: the old tweak is in place right away" grep -qx "old tweak" "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib"
  if [ -x "$R/usr/sbin/dock-receiver-setup" ]; then setup_cmd rollback; else : > "$OUT"; dev "$SH" "$T/ctl-main/preinst" rollback; fi; rc=$?
  check "fail at $st: rollback exits 0 and the old setup runs" eval '[ "$rc" = 0 ] && old_setup_live Fidelio.bundle'
  check "fail at $st: after the rollback no package file is left" eval '[ ! -e "$R/usr/libexec/dock-receiver" ] && [ ! -e "$R/System/Library/SpringBoardPlugins/NowPlaying.bundle" ] && [ ! -e "$R/.fake/dpkg/dock-receiver.status" ]'
  check "fail at $st: the old tweak is in place" grep -qx "old tweak" "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib"
  check "fail at $st: no secret anywhere" no_secret_anywhere
done
for st in dockremote-target dockremote-backup; do
  phone_old vars f1d0c0ffee66aa11bb22cc33dd44ee55 5ecre7a66bb22cc33dd44ee55ff66770; SECRETS="$SECRETS f1d0c0ffee66aa11bb22cc33dd44ee55 5ecre7a66bb22cc33dd44ee55ff66770"
  DEVENV="DOCKRECEIVER_TEST_FAIL=$st" dpkg_i "$MAIN" "$REMOTE"; rc=$?
  cp "$OUT" "$T/fail-$st.txt"
  check "fail at $st: dpkg -i exits non-zero; dock-receiver itself is installed and runs" eval '[ "$rc" != 0 ] && new_setup_live'
  check "fail at $st: the old tweak is untouched" grep -qx "old tweak" "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib"
  setup_cmd rollback; rc=$?
  check "fail at $st: rollback brings the whole old setup back" eval '[ "$rc" = 0 ] && old_setup_live Fidelio.bundle && grep -qx "old tweak" "$R/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib"'
done
for st in icon-cache; do
  phone_old vars f1d0c0ffee88aa11bb22cc33dd44ee55 5ecre7a88bb22cc33dd44ee55ff66770; SECRETS="$SECRETS f1d0c0ffee88aa11bb22cc33dd44ee55 5ecre7a88bb22cc33dd44ee55ff66770"
  DEVENV="DOCKRECEIVER_TEST_FAIL=$st" dpkg_i "$MAIN" "$REMOTE"; rc=$?
  check "fail at $st (after the point of no return): install completes with a warning" eval '[ "$rc" = 0 ] && new_setup_live && grep -q "WARNING: uicache failed" "$OUT"'
done
echo "# realistic failures: a job that does not load, an old audiod that cannot be stopped, a new audiod that never starts"
for k in "FAKE_FAIL_LOAD=com.dockreceiver.cspot" "FAKE_UNKILLABLE=fidelio-audiod" "FAKE_NOSTART=com.dockreceiver.audiod"; do
  phone_old vars f1d0c0ffee99aa11bb22cc33dd44ee55 5ecre7a99bb22cc33dd44ee55ff66770; SECRETS="$SECRETS f1d0c0ffee99aa11bb22cc33dd44ee55 5ecre7a99bb22cc33dd44ee55ff66770"
  DEVENV="$k" dpkg_i "$MAIN" "$REMOTE"; rc=$?
  cp "$OUT" "$T/fail-$k.txt"
  check "$k: dpkg -i exits non-zero" [ "$rc" != 0 ]
  check "$k: the old setup runs as before" old_setup_live Fidelio.bundle
  check "$k: no new daemon left running" eval '! proc dock-audiod && ! proc dock-raop'
  DEVENV="" setup_cmd rollback; rc=$?
  check "$k: rollback leaves the old setup running and removes the package" eval '[ "$rc" = 0 ] && old_setup_live Fidelio.bundle && [ ! -e "$R/usr/libexec/dock-receiver" ]'
done
echo "# the preinst refuses a device short of space or tools"
phone_old vars a b
DEVENV="FAKE_DF_ROOT_KB=5000" dpkg_i "$MAIN"; rc=$?
check "too little space on /: refused before anything changed" eval '[ "$rc" != 0 ] && grep -q "free on /" "$OUT" && old_setup_live Fidelio.bundle && [ ! -e "$R/usr/libexec/dock-receiver" ]'
mv "$T/stubs/uicache" "$T/uicache.away"
dpkg_i "$MAIN"; rc=$?
check "a missing tool (uicache): refused, the old setup untouched" eval '[ "$rc" != 0 ] && grep -q "missing tools: uicache" "$OUT" && old_setup_live Fidelio.bundle'
mv "$T/uicache.away" "$T/stubs/uicache"
new_root nosubstrate 3.1.3
dpkg_i "$MAIN" "$REMOTE"; rc=$?
check "phone without MobileSubstrate: the dock-remote package is refused by its preinst" eval '[ "$rc" != 0 ] && grep -q "MobileSubstrate is missing" "$OUT"'

# =================================================================================================
echo "# cspot-run and keepalive"
new_root wrappers 3.1.3
mkdir -p "$R/usr/libexec/dock-receiver" "$R/var/mobile/dockreceiver/logs"
cp "$REPO/packaging/dock-receiver/cspot-run" "$REPO/packaging/dock-receiver/keepalive" "$R/usr/libexec/dock-receiver/"
cat > "$R/usr/libexec/dock-receiver/cspot-ios3" <<'EOF'
#!/bin/bash
echo "fake cspot: $# args: $1 ${2:0:4}... $3 ${4:0:4}..."
[ "$1" = --client-id ] && [ "$3" = --client-secret ] && [ "$2" = "$WANT_ID" ] && [ "$4" = "$WANT_SECRET" ] && echo "fake cspot: values OK"
EOF
chmod 755 "$R/usr/libexec/dock-receiver/cspot-ios3"
ID=f1d0c0ffee55aa11bb22cc33dd44ee55; SEC=5ecre7a55bb22cc33dd44ee55ff66770; SECRETS="$SECRETS $ID $SEC"
printf '# x\nSPOTIFY_CLIENT_ID=%s\nSPOTIFY_CLIENT_SECRET="%s"\n' "$ID" "$SEC" > "$R/var/mobile/dockreceiver/spotify-credentials.conf"
chmod 600 "$R/var/mobile/dockreceiver/spotify-credentials.conf"
: > "$OUT"; DEVENV="WANT_ID=$ID WANT_SECRET=$SEC" dev "$BASH40" "$R/usr/libexec/dock-receiver/cspot-run"
check "cspot-run: passes the two values to cspot-ios3 (quotes stripped), prints neither" eval 'grep -q "fake cspot: values OK" "$OUT" && grep -q "fake cspot: 4 args" "$OUT" && ! grep -q -F "$ID" "$OUT" && ! grep -q -F "$SEC" "$OUT"'
printf 'SPOTIFY_CLIENT_ID=\nSPOTIFY_CLIENT_SECRET=\n' > "$R/var/mobile/dockreceiver/spotify-credentials.conf"
: > "$OUT"; DEVENV="DOCKRECEIVER_CREDS_WAIT_S=1" dev "$(command -v timeout)" 3 "$BASH40" "$R/usr/libexec/dock-receiver/cspot-run"
check "cspot-run: without values it waits and says so once, cspot not started" eval '[ "$(grep -c "waiting" "$OUT")" = 1 ] && ! grep -q "fake cspot" "$OUT"'
printf 'KEEPALIVE_HOST=127.0.0.1\n' > "$R/var/mobile/dockreceiver/keepalive.conf"
dd if=/dev/zero of="$R/var/mobile/dockreceiver/logs/cspot.log" bs=1024 count=1100 2>/dev/null
printf 'small\n' > "$R/var/mobile/dockreceiver/logs/audiod.log"
: > "$OUT"; DEVENV="KEEPALIVE_LOOPS=2 KEEPALIVE_INTERVAL_S=0" dev "$BASH40" "$R/usr/libexec/dock-receiver/keepalive"; rc=$?
check "keepalive: runs with the devices' tools and sends to the configured host" eval '[ "$rc" = 0 ] && grep -q "sending to the discard port" "$OUT" && no_tool_missing'
check "keepalive: a log above 1 MB is kept as .1 and emptied; a small one is left" eval '[ ! -s "$R/var/mobile/dockreceiver/logs/cspot.log" ] && [ -s "$R/var/mobile/dockreceiver/logs/cspot.log.1" ] && grep -qx small "$R/var/mobile/dockreceiver/logs/audiod.log"'
rm -f "$R/var/mobile/dockreceiver/keepalive.conf"
: > "$OUT"; DEVENV="KEEPALIVE_LOOPS=1 KEEPALIVE_INTERVAL_S=0" dev "$BASH40" "$R/usr/libexec/dock-receiver/keepalive"
check "keepalive: without a host (no conf, no ipconfig) it says so and keeps running" file_has "$OUT" "no host yet"

# =================================================================================================
echo "# the scripts name no tool the devices lack"
for f in "$REPO"/packaging/dock-receiver/setup.sh "$REPO"/packaging/dock-receiver-dockremote/scripts.sh "$REPO"/packaging/dock-receiver/cspot-run "$REPO"/packaging/dock-receiver/keepalive; do
  # a forbidden tool in a command position: at the start, or after | ; & ( $( { then do else
  hits="$(sed -e 's/^[[:space:]]*#.*$//' "$f" | grep -n -E '(^|[|;&({]|\$\(|\b(then|do|else)\b)[[:space:]]*(awk|head|tail|tr|cut|wc|basename|stat|id|expr|sort|uniq|nohup|env|tee|du|pkill|plutil|defaults|respring|ps|sbreload|xz|zstd)([[:space:]]|$)' || true)"
  hits2="$(sed -e 's/^[[:space:]]*#.*$//' "$f" | grep -n -E 'sed[^|;]* -E|NowPlaying\*|\*NowPlaying' || true)"
  check "lint ${f#$REPO/}: no forbidden tool word outside strings, no sed -E, no NowPlaying glob" [ -z "$hits$hits2" ]
  [ -z "$hits$hits2" ] || printf '%s\n%s\n' "$hits" "$hits2" | sed 's/^/    /'
done

echo "TOTAL $NOK ok, $NFAIL FAIL"
[ "$NFAIL" = 0 ]
