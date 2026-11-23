#!/bin/sh
# The install scripts of dock-receiver-dockremote (the DockRemote tweak in iapd, iPhone OS 3.x only).
# ONE file, installed by tools/build-debs.sh as the package's preinst, postinst, prerm and postrm; it
# tells which one it is from its own name. Same rules as packaging/dock-receiver/setup.sh: bash 4.0 as
# /bin/sh, only the tools both devices have, no credential anywhere.
#
# It NEVER restarts iapd: restarting it while the phone is docked leaves the dock unusable until the
# phone is reseated. The tweak takes effect after an undocked `killall iapd` or a reboot.
#
#   /bin/sh preinst check     dry run (also from the extracted control archive)

DR_VERSION="@VERSION@"
R="${DOCKRECEIVER_ROOT:-}"
D="$R/var/mobile/dockreceiver"
B="$R/var/dockreceiver-migration"
MSDL="$R/Library/MobileSubstrate/DynamicLibraries"
SYSVER="$R/System/Library/CoreServices/SystemVersion.plist"
LOG="$D/install.log"
STATE="$B/state"
FAIL_HOOK="${DOCKRECEIVER_TEST_FAIL:-}"
LOGGING=1

say() {
  printf '%s\n' "$*"
  if [ "$LOGGING" = 1 ] && [ -d "$D" ]; then
    printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "dockremote: $*" >> "$LOG" 2>/dev/null
  fi
}
err() { say "ERROR: $*"; }
step() {
  say "-- dockremote $1"
  [ -z "$FAIL_HOOK" ] || [ "$FAIL_HOOK" != "dockremote-$1" ] || { err "step $1 failed (injected by DOCKRECEIVER_TEST_FAIL)"; return 1; }
  return 0
}

os_version() {
  [ -f "$SYSVER" ] || return 1
  sed -n -e '/<key>ProductVersion<\/key>/{' -e 's/.*<key>ProductVersion<\/key>[[:space:]]*<string>\([^<]*\)<\/string>.*/\1/p' -e t \
    -e n -e 's/.*<string>\([^<]*\)<\/string>.*/\1/p' -e '}' "$SYSVER" | sed -n 1p
}
substrate_present() {
  { [ -f "$R/usr/lib/libsubstrate.dylib" ] || [ -f "$R/Library/MobileSubstrate/MobileSubstrate.dylib" ]; } && [ -d "$MSDL" ]
}
state_set() {
  mkdir -p "$B" || return 1
  { [ -f "$STATE" ] && grep -v "^$1=" "$STATE"; printf '%s=%s\n' "$1" "$2"; } > "$STATE.new" && mv -f "$STATE.new" "$STATE"
}
pkg_installed() { dpkg -s "$1" 2>/dev/null | grep -q '^Status: install ok'; }

check_target() {
  local v
  v="$(os_version)"
  case "$v" in
    3.*) ;;
    *) err "this package is for iPhone OS 3.x only (this device: ${v:-unknown}); on iOS 5 NowPlaying handles the dock remote itself"; return 1 ;;
  esac
  substrate_present || { err "MobileSubstrate is missing (/usr/lib/libsubstrate.dylib and /Library/MobileSubstrate/DynamicLibraries)"; return 1; }
  return 0
}

# A hand-installed DockRemote (not owned by this package) is copied to the recoverable place before dpkg
# replaces it (dpkg itself unpacks to a new name and renames, so iapd's mapped copy is never written over).
backup_old_tweak() {
  local f
  for f in DockRemote.dylib DockRemote.plist; do
    [ -f "$MSDL/$f" ] || continue
    mkdir -p "$B/DynamicLibraries" && chmod 700 "$B" || return 1
    rm -f "$B/DynamicLibraries/$f.copying"
    cp -p "$MSDL/$f" "$B/DynamicLibraries/$f.copying" && mv -f "$B/DynamicLibraries/$f.copying" "$B/DynamicLibraries/$f" || return 1
    say "copied the installed $f to ${B#$R}/DynamicLibraries"
  done
  state_set tweak_backed_up yes
}

iapd_note() {
  say "The tweak is loaded into iapd only when iapd starts. This package never restarts iapd (doing it"
  say "while docked leaves the dock unusable until the phone is reseated). To load it: lift the phone off"
  say "the dock, run 'killall iapd' (launchd restarts it), wait 5 s, dock again; or reboot."
}

case "${1:-}" in
  check)
    LOGGING=0
    say "dock-receiver-dockremote check $DR_VERSION (dry run: nothing is changed)"
    say "system: $(os_version)"
    if check_target; then say "MobileSubstrate: present"; fi
    [ -f "$MSDL/DockRemote.dylib" ] && say "installed DockRemote.dylib: present (would be copied to ${B#$R}/DynamicLibraries, then replaced)"
    pkg_installed dock-receiver || say "note: needs the package dock-receiver (install both together)"
    if check_target >/dev/null; then say "result: ready"; exit 0; fi
    say "result: NOT ready"; exit 1 ;;
esac

case "${0##*/}" in
  *preinst)
    case "$1" in
      install)
        say "dock-receiver-dockremote $DR_VERSION: preinst install"
        step target && check_target || exit 1
        # dpkg unpacks this package even when dock-receiver's own preinst failed earlier in the same
        # `dpkg -i`: then nothing may change here.
        pkg_installed dock-receiver || { err "dock-receiver is not installed or unpacked: install both packages together (dpkg -i <dock-receiver.deb> <this.deb>)"; exit 1; }
        step backup && backup_old_tweak || { err "could not copy the installed tweak to ${B#$R}"; exit 1; }
        ;;
      upgrade)
        step target && check_target || exit 1 ;;
    esac ;;
  *postinst)
    case "$1" in
      configure)
        say "dock-receiver-dockremote $DR_VERSION installed: /Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib"
        say "The tweak reads ${D#$R} (remote-tweak.observe/.swallow/.disabled, carried over by dock-receiver)."
        iapd_note ;;
    esac ;;
  *prerm)
    case "$1" in
      remove) say "dock-receiver-dockremote: removing the tweak; iapd keeps the loaded copy until it restarts (undocked)." ;;
    esac ;;
  *postrm)
    case "$1" in
      remove) say "dock-receiver-dockremote removed. A copy of the hand-installed tweak, if there was one, is in ${B#$R}/DynamicLibraries (dock-receiver's rollback puts it back)." ;;
    esac ;;
  *) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
exit 0
