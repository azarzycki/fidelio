#!/bin/sh
# dock-receiver-setup: the install scripts of the dock-receiver package and the migration from the
# hand-installed setup of before B1.17 (/var/mobile/fidelio, com.fidelio.* jobs, Fidelio.bundle /
# Fidelio.lockbundle, the hand-installed NowPlaying.app).
#
# ONE file, installed four times by tools/build-debs.sh: as the package's preinst, postinst, prerm and
# postrm (it tells which one it is from its own name), and as /usr/sbin/dock-receiver-setup, the command
# for the owner:
#
#   dock-receiver-setup check                     dry run: changes nothing, prints what it finds and would do
#   dock-receiver-setup migrate                   what the postinst does (re-runnable)
#   dock-receiver-setup rollback                  remove the packages and bring the old setup back
#   dock-receiver-setup cleanup [--yes]           list (and with --yes remove) the old setup's files
#   dock-receiver-setup respring [--even-if-playing]   restart SpringBoard (refused while something plays)
#
# Before the package is installed, the dry run is the preinst of the .deb:
#   dpkg-deb -e dock-receiver_<version>_iphoneos-arm.deb /var/tmp/dr-ctl && /bin/sh /var/tmp/dr-ctl/preinst check
#
# Written for the devices' /bin/sh (bash 4.0) and ONLY the tools both devices have: sed grep cp mv rm
# ln chmod chown mkdir rmdir cat ls sleep killall launchctl uicache dpkg su date touch find df (and the
# shell's builtins). No awk, head, tail, tr, cut, wc, basename, stat, id, expr; `sed -r`, never `sed -E`.
# packaging/README.md describes what it does; packaging/hosttest/ tests it in a fake root.
#
# Never prints or logs a credential: the Spotify client id and secret only pass through shell variables
# and the builtin printf into the credentials file.

DR_VERSION="@VERSION@"

# --- paths (DOCKRECEIVER_ROOT is a prefix for the host tests; empty on a device) --------------------
R="${DOCKRECEIVER_ROOT:-}"
D="$R/var/mobile/dockreceiver"            # the new data directory (common/DataDir.h)
O="$R/var/mobile/fidelio"                 # the old one: never changed, only read
B="$R/var/dockreceiver-migration"         # the recoverable place: root-only
LIBEXEC="$R/usr/libexec/dock-receiver"
SHARE="$R/usr/share/dock-receiver"
LD="$R/Library/LaunchDaemons"
APPS="$R/Applications"
SBP="$R/System/Library/SpringBoardPlugins"
MSDL="$R/Library/MobileSubstrate/DynamicLibraries"
SYSVER="$R/System/Library/CoreServices/SystemVersion.plist"
IAPD_PREFS="$R/var/mobile/Library/Preferences/com.apple.iapd.plist"
LOG="$D/install.log"
CREDS="$D/spotify-credentials.conf"
STATE="$B/state"

NEW_LABELS="com.dockreceiver.audiod com.dockreceiver.raop com.dockreceiver.cspot com.dockreceiver.keepalive"
OLD_LABELS="com.fidelio.audiod com.fidelio.raop com.fidelio.cspot com.fidelio.keepalive"
NEW_PROCS="dock-audiod dock-raop cspot-ios3"
OLD_PROCS="fidelio-audiod fidelio-raop cspot-ios3"
NEW_APP_ID="com.dockreceiver.nowplaying"
OLD_APP_ID="com.fidelio.nowplaying"
OLD_PLUGINS="Fidelio.bundle Fidelio.lockbundle"     # exact names only, never a pattern
# The files of the old data directory the new programs read (copied, never moved).
CARRY_FILES="settings.conf remote-tweak.observe remote-tweak.swallow remote-tweak.disabled lockplugin.debug remote-control.stage remote-probe.on remote-keys.log"
# Where the old setup's Spotify launcher may be (besides what the old job's plist names).
OLD_CSPOT_LAUNCHERS="b1/test_cspot.sh b1/run_cspot.sh cspotd.sh b1/cspotd.sh run_cspot.sh test_cspot.sh"
OLD_RAOP_LAUNCHERS="b1/test_raop.sh b1/run_raop.sh run_raop.sh test_raop.sh"
OLD_KEEPALIVE_SCRIPTS="keepalive.sh b1/keepalive.sh"
REQUIRED_TOOLS="sed grep cp mv rm ln chmod chown mkdir cat ls sleep killall launchctl uicache su date touch find df dpkg"
MOBILE="501:501"                           # the user mobile on both devices
ROOTOWN="0:0"
MIN_ROOT_KB=12288                          # 2 x the package (dpkg unpacks next to the old files) + margin
MIN_VAR_KB=20480
TIMEOUT_S="${DOCKRECEIVER_TIMEOUT_S:-20}"  # waits for a process to stop / audiod to come up

LOGGING=1
FAIL_HOOK="${DOCKRECEIVER_TEST_FAIL:-}"     # host tests only: make the named step fail

# --- output -----------------------------------------------------------------------------------------
say() {
  printf '%s\n' "$*"
  if [ "$LOGGING" = 1 ] && [ -d "$D" ]; then
    printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG" 2>/dev/null
  fi
}
warn() { say "WARNING: $*"; }
err()  { say "ERROR: $*"; }

step_name=""
step() {  # NAME: announce a step; the host tests can make it fail
  step_name="$1"
  say "-- $1"
  [ -z "$FAIL_HOOK" ] || [ "$FAIL_HOOK" != "$1" ] || { err "step $1 failed (injected by DOCKRECEIVER_TEST_FAIL)"; return 1; }
  return 0
}

# --- small helpers (no awk, head, tail, basename, stat, ...) ----------------------------------------
have() { command -v "$1" >/dev/null 2>&1; }
is_root() { [ "${EUID:-1}" = 0 ]; }

plist_string() {  # FILE KEY: the <string> after <key>KEY</key> (same line or the next), XML plists only
  [ -f "$1" ] || return 1
  sed -n -e "/<key>$2<\/key>/{" -e "s/.*<key>$2<\/key>[[:space:]]*<string>\([^<]*\)<\/string>.*/\1/p" -e t \
    -e n -e 's/.*<string>\([^<]*\)<\/string>.*/\1/p' -e '}' "$1" | sed -n 1p
}

os_version() { plist_string "$SYSVER" ProductVersion; }

free_kb() {  # DIR: available KB, from `df -k` (GNU or BSD layout: the field before the first NN%)
  local line prev f
  line="$(df -k "$1" 2>/dev/null | sed -n '2,$p')"
  prev=""
  for f in $line; do
    case "$f" in *%) printf '%s\n' "$prev"; return 0 ;; esac
    prev="$f"
  done
  return 1
}

state_get() {  # KEY
  [ -f "$STATE" ] || return 1
  sed -n "s/^$1=//p" "$STATE" | sed -n '$p'
}
state_set() {  # KEY VALUE
  mkdir -p "$B" || return 1
  { [ -f "$STATE" ] && grep -v "^$1=" "$STATE"; printf '%s=%s\n' "$1" "$2"; } > "$STATE.new" && mv -f "$STATE.new" "$STATE"
}

job_loaded() {  # LABEL
  launchctl list 2>/dev/null | grep -q "[[:space:]]$1\$"
}
proc_running() { killall -0 "$1" >/dev/null 2>&1; }

wait_gone() {  # PROC...: TERM, wait, then KILL; fails if one survives
  local p n
  for p in "$@"; do proc_running "$p" && killall "$p" >/dev/null 2>&1; done
  n=0
  while [ "$n" -lt "$TIMEOUT_S" ]; do
    local any=0
    for p in "$@"; do proc_running "$p" && any=1; done
    [ "$any" = 0 ] && return 0
    [ "$n" = 5 ] && for p in "$@"; do proc_running "$p" && killall -9 "$p" >/dev/null 2>&1; done
    sleep 1; n=$((n + 1))
  done
  for p in "$@"; do proc_running "$p" && { err "$p is still running"; return 1; }; done
  return 0
}

# Copy SRC into DIR (as DIR/<name>), replacing an older copy there, then remove SRC. A failure leaves
# SRC where it was.
move_to_backup() {  # SRC DIR
  local src="$1" dir="$2" name="${1##*/}"
  mkdir -p "$dir" || return 1
  rm -rf "$dir/$name.copying"
  cp -Rp "$src" "$dir/$name.copying" || { rm -rf "$dir/$name.copying"; return 1; }
  rm -rf "$dir/$name.previous"
  [ -e "$dir/$name" ] && { mv "$dir/$name" "$dir/$name.previous" || return 1; }
  mv "$dir/$name.copying" "$dir/$name" || return 1
  rm -rf "$dir/$name.previous"
  rm -rf "$src"
}

# Put a copy of SRC at DEST, by a new name and a rename (a file that has run is never written over;
# the temporary name never ends in .dylib or .plist).
restore_copy() {  # SRC DEST
  mkdir -p "${2%/*}" || return 1
  rm -rf "$2.dr-restore"
  cp -Rp "$1" "$2.dr-restore" || { rm -rf "$2.dr-restore"; return 1; }
  if [ -d "$2.dr-restore" ]; then
    [ -e "$2" ] && return 1
    mv "$2.dr-restore" "$2"
  else
    mv -f "$2.dr-restore" "$2"
  fi
}

install_file() {  # SRC DEST OWNER MODE: by a new name and a rename
  rm -f "$2.dr-new"
  cp "$1" "$2.dr-new" && chown "$3" "$2.dr-new" && chmod "$4" "$2.dr-new" && mv -f "$2.dr-new" "$2" \
    || { rm -f "$2.dr-new"; return 1; }
}

app_id() { plist_string "$APPS/NowPlaying.app/Info.plist" CFBundleIdentifier; }
app_version() { plist_string "$APPS/NowPlaying.app/Info.plist" CFBundleShortVersionString; }
pkg_installed() { dpkg -s "$1" 2>/dev/null | grep -q '^Status: install ok'; }

# --- reading the old setup (read-only) ----------------------------------------------------------------
# A launcher script, with continuation lines joined and comment lines dropped.
prep_file() {
  sed -e ':a' -e '/\\$/{' -e 'N' -e 's/\\\n/ /' -e 'ba' -e '}' -e '/^[[:space:]]*#/d' "$1" 2>/dev/null
}
# Every value given to an option in FILE: `OPT value`, `OPT=value`, quoted or not. OPT is an ERE
# alternative list (e.g. `-n|--name`); FILTER, if given, keeps only lines matching it (ERE).
opt_values() {  # FILE OPT [FILTER]
  local filter="${3:-.}"
  prep_file "$1" | grep -E -e "$filter" | sed -n -r \
    -e "s/^(.*[[:space:]])?($2)(=|[[:space:]]+)\"([^\"]*)\".*\$/\4/p" -e t \
    -e "s/^(.*[[:space:]])?($2)(=|[[:space:]]+)'([^']*)'.*\$/\4/p" -e t \
    -e "s/^(.*[[:space:]])?($2)(=|[[:space:]]+)([^[:space:]\"';|&)]+).*\$/\4/p"
}
# Every value assigned to a variable whose name matches NAME (ERE, case-insensitive): `NAME=value`,
# `export NAME=value`, quoted or not, also after a `;`.
assign_values() {  # FILE NAME
  prep_file "$1" | sed -n -r \
    -e "s/^(.*[;[:space:]])?(export[[:space:]]+)?($2)=\"([^\"]*)\".*\$/\4/Ip" -e t \
    -e "s/^(.*[;[:space:]])?(export[[:space:]]+)?($2)='([^']*)'.*\$/\4/Ip" -e t \
    -e "s/^(.*[;[:space:]])?(export[[:space:]]+)?($2)=([^[:space:]\"';|&)]+).*\$/\4/Ip"
}
# A `$NAME` / `${NAME}` reference resolved against FILE, then the old creds.env; empty if it is not one.
resolve_ref() {  # VALUE FILE
  local name
  case "$1" in
    \$\{*\}) name="${1#\$\{}"; name="${name%\}}" ;;
    \$*) name="${1#\$}" ;;
    *) printf '%s\n' "$1"; return 0 ;;
  esac
  case "$name" in ''|*[!A-Za-z0-9_]*) return 1 ;; esac
  { assign_values "$2" "$name"; [ -f "$O/creds.env" ] && assign_values "$O/creds.env" "$name"; } | sed -n 1p
}

FOUND=""     # the value found by find_value (never printed)
FOUND_IN=""  # the file it was found in
valid_token() { case "$1" in ''|*[!A-Za-z0-9._-]*) return 1 ;; esac; return 0; }

find_value() {  # KIND(id|secret) FILE: sets FOUND / FOUND_IN, or fails
  local opt name v r
  case "$1" in
    id) opt="--client-id"; name="[A-Za-z_]*CLIENT_?ID[A-Za-z0-9_]*" ;;
    secret) opt="--client-secret"; name="[A-Za-z_]*SECRET[A-Za-z0-9_]*" ;;
  esac
  [ -f "$2" ] || return 1
  # The candidates stay in memory (a here-document would put them in a temporary file).
  local cands ifs="$IFS" rc=1
  cands="$(opt_values "$2" "$opt"; assign_values "$2" "$name")"
  set -f; IFS='
'
  for v in $cands; do
    r="$(resolve_ref "$v" "$2")"
    if valid_token "$r"; then FOUND="$r"; FOUND_IN="$2"; rc=0; break; fi
  done
  IFS="$ifs"; set +f
  cands=""
  return $rc
}

plist_sh_paths() {  # PLIST: the .sh files under the old directory that a job's ProgramArguments name
  [ -f "$1" ] && sed -n "s|.*<string>\(/var/mobile/fidelio/[^<]*\.sh\)</string>.*|\1|p" "$1"
}

old_launchers() {  # KIND(cspot|raop|keepalive): candidate files, existing ones, each once
  local list rel p seen=" "
  case "$1" in
    cspot) list="$OLD_CSPOT_LAUNCHERS" ;; raop) list="$OLD_RAOP_LAUNCHERS" ;; keepalive) list="$OLD_KEEPALIVE_SCRIPTS" ;;
  esac
  {
    for p in $(plist_sh_paths "$LD/com.fidelio.$1.plist") $(plist_sh_paths "$B/LaunchDaemons/com.fidelio.$1.plist"); do
      printf '%s\n' "$R$p"
    done
    for rel in $list; do printf '%s\n' "$O/$rel"; done
  } | while IFS= read -r p; do
    case "$seen" in *" $p "*) continue ;; esac
    seen="$seen$p "
    [ -f "$p" ] && printf '%s\n' "$p"
  done
}

CRED_ID=""; CRED_SECRET=""; CRED_FROM=""
find_old_credentials() {  # sets CRED_ID / CRED_SECRET / CRED_FROM (never printed)
  local f
  CRED_ID=""; CRED_SECRET=""; CRED_FROM=""
  for f in "$O/creds.env" $(old_launchers cspot); do
    [ -f "$f" ] || continue
    find_value id "$f" || continue
    CRED_ID="$FOUND"
    if find_value secret "$f"; then CRED_SECRET="$FOUND"; CRED_FROM="$f"; return 0; fi
    CRED_ID=""
  done
  return 1
}

creds_present() {  # the new credentials file holds both values
  [ -f "$CREDS" ] || return 1
  grep -q '^SPOTIFY_CLIENT_ID=[^[:space:]]' "$CREDS" && grep -q '^SPOTIFY_CLIENT_SECRET=[^[:space:]]' "$CREDS"
}

write_creds() {  # ID SECRET (either may be empty: the template)
  local old_umask
  old_umask="$(umask)"; umask 077
  rm -f "$CREDS.new"
  {
    printf '%s\n' "# Spotify developer app credentials for cspot-ios3 (read by /usr/libexec/dock-receiver/cspot-run)."
    printf '%s\n' "# Owner mobile, mode 600. Fill in both values; the Spotify job picks them up within 30 s."
    printf '%s\n' "# See /usr/share/doc/dock-receiver/README.md. Never copy this file off the device."
    printf 'SPOTIFY_CLIENT_ID=%s\n' "$1"
    printf 'SPOTIFY_CLIENT_SECRET=%s\n' "$2"
  } > "$CREDS.new"
  local rc=$?
  umask "$old_umask"
  [ "$rc" = 0 ] && chown "$MOBILE" "$CREDS.new" && chmod 600 "$CREDS.new" && mv -f "$CREDS.new" "$CREDS" \
    || { rm -f "$CREDS.new"; return 1; }
}

old_setup_present() {  # anything of the old setup still live
  local l
  for l in $OLD_LABELS; do [ -f "$LD/$l.plist" ] && return 0; job_loaded "$l" && return 0; done
  for l in $OLD_PROCS; do [ "$l" = cspot-ios3 ] && continue; proc_running "$l" && return 0; done
  return 1
}

# --- the dry run ---------------------------------------------------------------------------------------
cmd_check() {
  LOGGING=0
  local blockers="" missing="" t v f l kb old=0
  say "dock-receiver-setup check $DR_VERSION (dry run: nothing is changed)"
  v="$(os_version)"; say "system: ${v:-unknown} (SystemVersion.plist)"
  is_root || { say "not running as root: the install needs root"; blockers="$blockers not-root"; }
  for t in $REQUIRED_TOOLS; do have "$t" || missing="$missing $t"; done
  if [ -n "$missing" ]; then say "prerequisites: MISSING:$missing"; blockers="$blockers tools"; else say "prerequisites: all present ($REQUIRED_TOOLS)"; fi
  kb="$(free_kb "$R/")"; say "free space on /: ${kb:-?} KB (needs $MIN_ROOT_KB)"
  [ -n "$kb" ] && [ "$kb" -lt "$MIN_ROOT_KB" ] && blockers="$blockers space-root"
  kb="$(free_kb "$R/var")"; say "free space on /var: ${kb:-?} KB (needs $MIN_VAR_KB)"
  [ -n "$kb" ] && [ "$kb" -lt "$MIN_VAR_KB" ] && blockers="$blockers space-var"
  if pkg_installed dock-receiver; then say "package dock-receiver: installed"; else say "package dock-receiver: not installed"; fi
  if pkg_installed dock-receiver-dockremote; then say "package dock-receiver-dockremote: installed"; fi
  [ -n "$(state_get switched)" ] && say "migration record ($STATE): switched=$(state_get switched)"

  if [ -d "$O" ] || old_setup_present; then old=1; fi
  if [ "$old" = 1 ]; then say "old setup: yes ($O)"; else say "old setup: no"; fi
  for l in $OLD_LABELS; do
    t=""
    [ -f "$LD/$l.plist" ] && t="plist"
    job_loaded "$l" && t="${t:+$t, }loaded"
    [ -n "$t" ] && say "  old job $l: $t"
  done
  for l in $NEW_LABELS; do
    t=""
    [ -f "$LD/$l.plist" ] && t="plist"
    job_loaded "$l" && t="${t:+$t, }loaded"
    [ -n "$t" ] && say "  new job $l: $t"
  done
  t=""; for l in $OLD_PROCS $NEW_PROCS; do case " $t " in *" $l "*) continue ;; esac; proc_running "$l" && t="$t $l"; done
  say "  processes running:${t:- none}"
  [ -f "$LD/com.fidelio.syslogd.plist" ] && say "  left alone: the job com.fidelio.syslogd (not part of the product)"
  if [ -f "$IAPD_PREFS" ] && grep -q -e PrintIapPackets -e LogDeviceVolumeChanges -e LogLineOut -e LogSimpleRemoteEvents -e LogGSEvents "$IAPD_PREFS"; then
    say "  left alone: iapd logging preferences in com.apple.iapd.plist"
  fi
  if [ -d "$O" ]; then
    t=""; for f in $CARRY_FILES; do [ -f "$O/$f" ] && t="$t $f"; done
    say "  files to copy into $D (if not there yet):${t:- none}"
  fi
  if creds_present; then
    say "credentials: already in $CREDS"
  elif find_old_credentials; then
    say "credentials: found in ${CRED_FROM#$R}"
  else
    say "credentials: not found (the Spotify job waits until $CREDS is filled)"
  fi
  CRED_ID=""; CRED_SECRET=""
  for f in $(old_launchers cspot); do
    t=""; [ -n "$(opt_values "$f" '-n|--name' 'cspot-ios3' | sed -n 1p)" ] && t="name"
    [ -n "$(opt_values "$f" '-b|--bitrate' 'cspot-ios3' | sed -n 1p)" ] && t="$t bitrate"
    [ -n "$t" ] && say "  Spotify options in ${f#$R}: $t (copied into settings.conf if not set there)"
  done
  for f in $(old_launchers raop); do
    [ -n "$(opt_values "$f" '-n|--name' 'raop' | sed -n 1p)" ] && say "  AirPlay name in ${f#$R} (copied into settings.conf if not set there)"
  done
  t="none: the router from ipconfig is used"
  for f in $(old_launchers keepalive); do
    prep_file "$f" | grep -q -E '([0-9]{1,3}\.){3}[0-9]{1,3}' && { t="found in ${f#$R}"; break; }
  done
  say "keepalive target: $t"
  if [ -d "$APPS/NowPlaying.app" ]; then
    say "app: /Applications/NowPlaying.app ($(app_id), $(app_version))$( [ "$(app_id)" = "$NEW_APP_ID" ] || printf '%s' ": would be moved to ${B#$R}/Applications")"
  else
    say "app: none at /Applications/NowPlaying.app"
  fi
  for f in $OLD_PLUGINS; do [ -e "$SBP/$f" ] && say "lock plugin: $f: would be moved to ${B#$R}/SpringBoardPlugins"; done
  [ -e "$SBP/NowPlaying.bundle" ] && say "lock plugin: NowPlaying.bundle present"
  [ -e "$SBP/NowPlaying.lockbundle" ] && say "lock plugin: NowPlaying.lockbundle present"
  if [ -e "$MSDL/DockRemote.dylib" ]; then say "tweak: DockRemote.dylib present (the dock-remote package backs it up and replaces it)"; fi
  if [ -f "$R/usr/lib/libsubstrate.dylib" ] || [ -f "$R/Library/MobileSubstrate/MobileSubstrate.dylib" ]; then
    say "MobileSubstrate: present"
  else
    say "MobileSubstrate: absent (the dock-remote package needs it; iPhone OS 3 only)"
  fi
  say "would do:"
  say "  1 check prerequisites and space  2 create $D  3 copy settings and flag files from the old directory"
  say "  4 copy device names/bitrate from the old launchers  5 carry the Spotify credentials  6 keepalive target"
  say "  7 stop old and new jobs, make sure no old daemon runs  8 move the old job plists to ${B#$R}"
  say "  9 install the new job plists  10 move the old lock plugin aside  11 refresh the icon cache"
  say "  12 load the new jobs (audiod, raop, cspot, keepalive) and check audiod runs"
  say "  (the preinst moves the old NowPlaying.app to ${B#$R}/Applications before dpkg unpacks)"
  if [ -n "$blockers" ]; then say "result: NOT ready:$blockers"; return 1; fi
  say "result: ready"
  return 0
}

# --- preinst: what must happen before dpkg unpacks ----------------------------------------------------
preflight_device() {
  local t missing="" kb
  is_root || { err "run as root"; return 1; }
  for t in $REQUIRED_TOOLS; do have "$t" || missing="$missing $t"; done
  [ -z "$missing" ] || { err "missing tools:$missing"; return 1; }
  kb="$(free_kb "$R/")"
  if [ -n "$kb" ] && [ "$kb" -lt "$MIN_ROOT_KB" ]; then err "only $kb KB free on / (needs $MIN_ROOT_KB)"; return 1; fi
  kb="$(free_kb "$R/var")"
  if [ -n "$kb" ] && [ "$kb" -lt "$MIN_VAR_KB" ]; then err "only $kb KB free on /var (needs $MIN_VAR_KB)"; return 1; fi
  return 0
}

move_old_app_aside() {
  local id
  [ -d "$APPS/NowPlaying.app" ] || return 0
  id="$(app_id)"
  [ "$id" = "$NEW_APP_ID" ] && return 0
  say "moving the old /Applications/NowPlaying.app (${id:-unknown id}) to ${B#$R}/Applications"
  move_to_backup "$APPS/NowPlaying.app" "$B/Applications" || { err "could not move the old app aside"; return 1; }
  state_set app_moved pending
}

do_preinst() {
  case "$1" in
    install|upgrade)
      say "dock-receiver $DR_VERSION: preinst $1"
      step preflight && preflight_device || exit 1
      mkdir -p "$B" && chown "$ROOTOWN" "$B" && chmod 700 "$B" || { err "cannot create $B"; exit 1; }
      step app-aside && move_old_app_aside || exit 1
      ;;
  esac
  exit 0
}

# --- postinst: the migration ----------------------------------------------------------------------------
RUN_STOPPED_OLD=""     # labels of old jobs this run unloaded
RUN_MOVED_PLISTS=""    # old plists this run moved to $B
RUN_NEW_PLISTS=0       # this run installed the new plists
RUN_MOVED_PLUGIN=""    # old lock plugin this run moved

undo() {
  local l p
  say "undoing this run's changes so that the old setup runs as before"
  if [ -n "$RUN_STOPPED_OLD" ] || [ -n "$RUN_MOVED_PLISTS" ]; then
    for l in $NEW_LABELS; do job_loaded "$l" && launchctl unload "$LD/$l.plist" >/dev/null 2>&1; done
    wait_gone $NEW_PROCS || true
    if [ "$RUN_NEW_PLISTS" = 1 ]; then for l in $NEW_LABELS; do rm -f "$LD/$l.plist"; done; fi
  fi
  for p in $RUN_MOVED_PLISTS; do
    [ -f "$LD/$p" ] || restore_copy "$B/LaunchDaemons/$p" "$LD/$p" || err "could not restore $p"
  done
  [ -n "$RUN_MOVED_PLUGIN" ] && { [ -e "$SBP/$RUN_MOVED_PLUGIN" ] || restore_copy "$B/SpringBoardPlugins/$RUN_MOVED_PLUGIN" "$SBP/$RUN_MOVED_PLUGIN" || err "could not restore $RUN_MOVED_PLUGIN"; }
  if [ "$(state_get app_moved)" = pending ] && [ -d "$B/Applications/NowPlaying.app" ]; then
    if [ -d "$APPS/NowPlaying.app" ]; then move_to_backup "$APPS/NowPlaying.app" "$B/new" || err "could not move the new app aside"; fi
    if [ ! -d "$APPS/NowPlaying.app" ] && restore_copy "$B/Applications/NowPlaying.app" "$APPS/NowPlaying.app"; then
      state_set app_moved no
      say "the old NowPlaying.app is back (the new one is in ${B#$R}/new; run dpkg -i again to retry)"
      refresh_icons || true
    fi
  fi
  # dpkg may have unpacked the dock-remote package over a hand-installed tweak already (it is configured
  # only after this package): while the old setup is the live one, put the old tweak back, so that an
  # iapd restart loads the old one.
  if [ -n "$RUN_STOPPED_OLD" ] || [ -n "$RUN_MOVED_PLISTS" ] || [ -f "$LD/com.fidelio.audiod.plist" ]; then
    for p in DockRemote.dylib DockRemote.plist; do
      [ -f "$B/DynamicLibraries/$p" ] && [ -d "$MSDL" ] && restore_copy "$B/DynamicLibraries/$p" "$MSDL/$p" && say "put the old $p back"
    done
  fi
  for l in $OLD_LABELS; do
    case " $RUN_STOPPED_OLD " in *" $l "*) launchctl load "$LD/$l.plist" >/dev/null 2>&1 || err "could not load $l" ;; esac
  done
  [ -n "$RUN_STOPPED_OLD" ] && say "the old jobs are loaded again:$RUN_STOPPED_OLD"
}

fail() {
  err "$*"
  undo
  say "dock-receiver $DR_VERSION: the install FAILED at step $step_name; log: ${LOG#$R}"
  say "the old setup is unchanged. Retry with dpkg -i, or remove the package with: dock-receiver-setup rollback"
  exit 1
}

refresh_icons() {
  su mobile -c uicache >/dev/null 2>&1 || uicache >/dev/null 2>&1
}

carry_files() {
  local f n=0
  [ -d "$O" ] || { say "no old data directory: nothing to copy"; return 0; }
  for f in $CARRY_FILES; do
    [ -f "$O/$f" ] || continue
    [ -e "$D/$f" ] && continue
    cp -p "$O/$f" "$D/$f.dr-new" && chown "$MOBILE" "$D/$f.dr-new" && mv -f "$D/$f.dr-new" "$D/$f" || { rm -f "$D/$f.dr-new"; return 1; }
    say "copied $f"; n=$((n + 1))
  done
  [ "$n" = 0 ] && say "nothing new to copy"
  return 0
}

conf_has() { [ -f "$D/settings.conf" ] && grep -q "^[[:space:]]*$1[[:space:]]*=" "$D/settings.conf"; }
conf_add() {  # KEY VALUE
  { [ -f "$D/settings.conf" ] && cat "$D/settings.conf"; printf '%s=%s\n' "$1" "$2"; } > "$D/settings.conf.dr-new" \
    && chown "$MOBILE" "$D/settings.conf.dr-new" && chmod 644 "$D/settings.conf.dr-new" && mv -f "$D/settings.conf.dr-new" "$D/settings.conf"
}

carry_options() {
  local f v
  for f in $(old_launchers cspot); do
    if ! conf_has cspot.name; then
      v="$(opt_values "$f" '-n|--name' 'cspot-ios3' | sed -n 1p)"; v="$(resolve_ref "$v" "$f")"
      case "$v" in ''|*[\$\`]*) ;; *) conf_add cspot.name "$v" || return 1; say "settings.conf: cspot.name=$v (from ${f#$R})" ;; esac
    fi
    if ! conf_has cspot.bitrate; then
      v="$(opt_values "$f" '-b|--bitrate' 'cspot-ios3' | sed -n 1p)"; v="$(resolve_ref "$v" "$f")"
      case "$v" in 96|160|320) conf_add cspot.bitrate "$v" || return 1; say "settings.conf: cspot.bitrate=$v (from ${f#$R})" ;; esac
    fi
  done
  for f in $(old_launchers raop); do
    conf_has raop.name && break
    v="$(opt_values "$f" '-n|--name' 'raop' | sed -n 1p)"; v="$(resolve_ref "$v" "$f")"
    case "$v" in ''|*[\$\`]*) ;; *) conf_add raop.name "$v" || return 1; say "settings.conf: raop.name=$v (from ${f#$R})" ;; esac
  done
  return 0
}

carry_credentials() {
  if creds_present; then
    say "credentials: already in ${CREDS#$R}"
    chown "$MOBILE" "$CREDS" && chmod 600 "$CREDS"
    return
  fi
  if find_old_credentials; then
    write_creds "$CRED_ID" "$CRED_SECRET" || { CRED_ID=""; CRED_SECRET=""; return 1; }
    say "credentials: found in ${CRED_FROM#$R}; written to ${CREDS#$R} (mode 600)"
  else
    [ -f "$CREDS" ] || write_creds "" "" || return 1
    say "credentials: not found; fill in ${CREDS#$R} (README) and the Spotify receiver starts within 30 s"
  fi
  CRED_ID=""; CRED_SECRET=""
  return 0
}

carry_keepalive() {
  local f v
  [ -f "$D/keepalive.conf" ] && { say "keepalive.conf: already there"; return 0; }
  for f in $(old_launchers keepalive); do
    v="$(prep_file "$f" | grep -o -E '([0-9]{1,3}\.){3}[0-9]{1,3}' | sed -n 1p)"
    if [ -n "$v" ]; then
      printf 'KEEPALIVE_HOST=%s\n' "$v" > "$D/keepalive.conf.dr-new" && chown "$MOBILE" "$D/keepalive.conf.dr-new" \
        && mv -f "$D/keepalive.conf.dr-new" "$D/keepalive.conf" || return 1
      say "keepalive: target taken from ${f#$R}"
      return 0
    fi
  done
  say "keepalive: no target in the old setup; the router from ipconfig is used"
  return 0
}

stop_jobs() {
  local l
  for l in $OLD_LABELS; do
    if job_loaded "$l"; then
      [ -f "$LD/$l.plist" ] && launchctl unload "$LD/$l.plist" >/dev/null 2>&1
      job_loaded "$l" && launchctl remove "$l" >/dev/null 2>&1
      job_loaded "$l" && { err "could not unload $l"; return 1; }
      RUN_STOPPED_OLD="$RUN_STOPPED_OLD $l"   # undo loads it again
      say "unloaded $l"
    elif [ -f "$LD/$l.plist" ]; then
      say "$l has a plist but is not loaded: it stays unloaded"
    fi
  done
  for l in $NEW_LABELS; do
    if job_loaded "$l"; then
      launchctl unload "$LD/$l.plist" >/dev/null 2>&1 || launchctl remove "$l" >/dev/null 2>&1
      say "unloaded $l (it is loaded again below)"
    fi
  done
  wait_gone fidelio-audiod fidelio-raop cspot-ios3 dock-audiod dock-raop || return 1
  proc_running fidelio-audiod && { err "an old audiod is still running"; return 1; }
  return 0
}

move_old_plists() {
  local l
  for l in $OLD_LABELS; do
    [ -f "$LD/$l.plist" ] || continue
    move_to_backup "$LD/$l.plist" "$B/LaunchDaemons" || { err "could not move $l.plist"; return 1; }
    RUN_MOVED_PLISTS="$RUN_MOVED_PLISTS $l.plist"
    say "moved $l.plist to ${B#$R}/LaunchDaemons"
  done
  [ -n "$RUN_MOVED_PLISTS" ] && state_set old_plists "${RUN_MOVED_PLISTS# }"
  return 0
}

install_new_plists() {
  local l
  for l in $NEW_LABELS; do
    install_file "$SHARE/launchd/$l.plist" "$LD/$l.plist" "$ROOTOWN" 644 || { err "could not install $l.plist"; return 1; }
    RUN_NEW_PLISTS=1
  done
  say "installed the job plists:$NEW_LABELS"
}

move_old_plugin() {
  local p
  for p in $OLD_PLUGINS; do
    [ -e "$SBP/$p" ] || continue
    move_to_backup "$SBP/$p" "$B/SpringBoardPlugins" || { err "could not move $p"; return 1; }
    RUN_MOVED_PLUGIN="$p"
    state_set old_plugin "$p"
    say "moved the old lock plugin $p to ${B#$R}/SpringBoardPlugins"
  done
  return 0
}

load_new_jobs() {
  local l n
  touch "$B/.load-ref"
  for l in $NEW_LABELS; do
    launchctl load "$LD/$l.plist" >/dev/null 2>&1 || { err "launchctl load $l failed"; return 1; }
    say "loaded $l"
  done
  n=0
  while ! proc_running dock-audiod; do
    n=$((n + 1)); [ "$n" -gt "$TIMEOUT_S" ] && { err "dock-audiod did not start (see ${D#$R}/logs/audiod.log)"; return 1; }
    sleep 1
  done
  proc_running fidelio-audiod && { err "an old audiod is running beside the new one"; return 1; }
  n=0
  while [ -z "$(find "$D/active-source" -newer "$B/.load-ref" 2>/dev/null)" ]; do
    n=$((n + 1)); [ "$n" -gt "$TIMEOUT_S" ] && { err "dock-audiod runs but has not written ${D#$R}/active-source"; return 1; }
    sleep 1
  done
  say "dock-audiod is running and writes ${D#$R}/active-source"
  return 0
}

check_package_files() {
  local f
  for f in dock-audiod dock-raop cspot-ios3 cspot-run keepalive; do
    [ -x "$LIBEXEC/$f" ] || { err "missing $LIBEXEC/$f (run dpkg -i again)"; return 1; }
  done
  for f in $NEW_LABELS; do [ -f "$SHARE/launchd/$f.plist" ] || { err "missing $SHARE/launchd/$f.plist"; return 1; }; done
  [ "$(app_id)" = "$NEW_APP_ID" ] || { err "the new NowPlaying.app is not in /Applications (run dpkg -i again)"; return 1; }
  return 0
}

summary() {
  local l
  say "dock-receiver $DR_VERSION is installed and running."
  if [ -n "$(state_get old_plists)" ] || [ -d "$O" ]; then
    say "kept for a rollback: ${O#$R} (unchanged) and ${B#$R} (the old job plists, app and lock plugin)"
  fi
  creds_present || say "STILL NEEDED: the Spotify credentials in ${CREDS#$R} (README: /usr/share/doc/dock-receiver/README.md)"
  say "STILL NEEDED: restart SpringBoard to finish: the lock plugin and the app's new id."
  say "  Run 'dock-receiver-setup respring' when nothing is playing (it refuses while something plays)."
  [ -f "$LD/com.fidelio.syslogd.plist" ] && say "left alone: the job com.fidelio.syslogd (a debugging aid, not part of the product)"
  if [ -f "$IAPD_PREFS" ] && grep -q -e PrintIapPackets -e LogDeviceVolumeChanges -e LogLineOut "$IAPD_PREFS"; then
    say "left alone: iapd's logging preferences (com.apple.iapd.plist)"
  fi
  say "rollback: dock-receiver-setup rollback    the old files, once you are happy: dock-receiver-setup cleanup"
}

cmd_migrate() {
  RUN_STOPPED_OLD=""; RUN_MOVED_PLISTS=""; RUN_NEW_PLISTS=0; RUN_MOVED_PLUGIN=""
  step preflight && preflight_device && check_package_files || fail "prerequisites"
  step datadir || fail "data directory"
  mkdir -p "$D/logs" && chown "$MOBILE" "$D" "$D/logs" && chmod 755 "$D" "$D/logs" || fail "cannot create $D"
  for l in audiod raop cspot keepalive; do
    [ -f "$D/logs/$l.log" ] || { : > "$D/logs/$l.log" && chown "$MOBILE" "$D/logs/$l.log"; }
  done
  mkdir -p "$B" && chown "$ROOTOWN" "$B" && chmod 700 "$B" || fail "cannot create $B"
  say "dock-receiver $DR_VERSION: migrate (log: ${LOG#$R})"
  step settings && carry_files || fail "copying the settings"
  step options && carry_options || fail "copying the device names"
  step credentials && carry_credentials || fail "the credentials"
  step keepalive-target && carry_keepalive || fail "the keepalive target"
  step stop-jobs && stop_jobs || fail "stopping the jobs"
  step old-plists && move_old_plists || fail "moving the old job plists"
  step new-plists && install_new_plists || fail "installing the job plists"
  step old-plugin && move_old_plugin || fail "moving the old lock plugin"
  if [ -n "$RUN_STOPPED_OLD" ]; then killall NowPlaying >/dev/null 2>&1 && say "closed the running NowPlaying (the old app)"; fi
  step load-jobs && load_new_jobs || fail "starting the new jobs"
  # The point of no return: the new setup runs. Nothing below undoes anything.
  state_set switched yes; state_set version "$DR_VERSION"
  [ "$(state_get app_moved)" = pending ] && state_set app_moved yes
  [ -n "$RUN_STOPPED_OLD" ] && state_set old_jobs "${RUN_STOPPED_OLD# }"
  if step icon-cache && refresh_icons; then say "icon cache refreshed"; else warn "uicache failed: run 'su mobile -c uicache'"; fi
  summary
  return 0
}

# --- removal: back to the old setup ---------------------------------------------------------------------
stop_new_jobs() {
  local l procs="dock-audiod dock-raop"
  # cspot-ios3 is the old setup's process name too: it is stopped only when the new job was loaded
  job_loaded com.dockreceiver.cspot && procs="$procs cspot-ios3"
  for l in $NEW_LABELS; do
    if job_loaded "$l"; then launchctl unload "$LD/$l.plist" >/dev/null 2>&1 || launchctl remove "$l" >/dev/null 2>&1; fi
    rm -f "$LD/$l.plist"
  done
  wait_gone $procs || warn "a new daemon is still running"
}

restore_old_setup() {
  local p l restored=0
  [ -d "$B" ] || { say "no old setup kept: nothing to restore"; return 0; }
  for p in $OLD_LABELS; do
    [ -f "$B/LaunchDaemons/$p.plist" ] || continue
    [ -f "$LD/$p.plist" ] && continue
    restore_copy "$B/LaunchDaemons/$p.plist" "$LD/$p.plist" && restored=1 && say "restored $p.plist" || err "could not restore $p.plist"
  done
  for p in $OLD_PLUGINS; do
    [ -e "$B/SpringBoardPlugins/$p" ] || continue
    [ -e "$SBP/$p" ] && continue
    restore_copy "$B/SpringBoardPlugins/$p" "$SBP/$p" && restored=1 && say "restored the lock plugin $p" || err "could not restore $p"
  done
  if [ -d "$B/Applications/NowPlaying.app" ]; then
    if [ -d "$APPS/NowPlaying.app" ] && [ "$(app_id)" = "$NEW_APP_ID" ]; then
      move_to_backup "$APPS/NowPlaying.app" "$B/new" || err "could not move the leftover new app aside"
    fi
    if [ ! -d "$APPS/NowPlaying.app" ]; then
      restore_copy "$B/Applications/NowPlaying.app" "$APPS/NowPlaying.app" && restored=1 && say "restored the old NowPlaying.app" || err "could not restore the old app"
      state_set app_moved no
    fi
  fi
  for p in DockRemote.dylib DockRemote.plist; do
    [ -f "$B/DynamicLibraries/$p" ] || continue
    [ -e "$MSDL/$p" ] && continue
    [ -d "$MSDL" ] && restore_copy "$B/DynamicLibraries/$p" "$MSDL/$p" && restored=1 && say "restored the old tweak file $p"
  done
  [ "$restored" = 1 ] && { refresh_icons || warn "uicache failed"; }
  for l in $OLD_LABELS; do
    [ -f "$LD/$l.plist" ] || continue
    job_loaded "$l" && continue
    launchctl load "$LD/$l.plist" >/dev/null 2>&1 && say "loaded $l" || err "could not load $l"
  done
  state_set switched no
  say "the old setup is back. Restart SpringBoard for the old lock plugin and app id (dock-receiver-setup is gone with the package: killall SpringBoard, when nothing plays);"
  say "the old tweak needs an undocked 'killall iapd' or a reboot. ${D#$R} is kept."
}

do_prerm() {
  case "$1" in
    remove|deconfigure)
      say "dock-receiver $DR_VERSION: prerm $1: stopping the jobs"
      stop_new_jobs
      ;;
  esac
  exit 0
}

do_postrm() {
  case "$1" in
    remove)
      say "dock-receiver $DR_VERSION: postrm remove"
      restore_old_setup
      ;;
    purge)
      say "dock-receiver: purge: removing ${D#$R} (settings, credentials, logs); ${B#$R} and ${O#$R} are kept"
      LOGGING=0
      rm -rf "$D"
      ;;
    abort-install|abort-upgrade)
      if [ "$(state_get app_moved)" = pending ] && [ ! -d "$APPS/NowPlaying.app" ] && [ -d "$B/Applications/NowPlaying.app" ]; then
        restore_copy "$B/Applications/NowPlaying.app" "$APPS/NowPlaying.app" && state_set app_moved no && say "restored the old NowPlaying.app"
      fi
      ;;
  esac
  exit 0
}

do_postinst() {
  case "$1" in
    configure)
      cmd_migrate
      if [ "${DOCKRECEIVER_RESPRING:-0}" = 1 ]; then cmd_respring; fi
      ;;
    abort-upgrade|abort-remove|abort-deconfigure)
      local l
      for l in $NEW_LABELS; do
        [ -f "$LD/$l.plist" ] || install_file "$SHARE/launchd/$l.plist" "$LD/$l.plist" "$ROOTOWN" 644
        job_loaded "$l" || launchctl load "$LD/$l.plist" >/dev/null 2>&1
      done
      ;;
  esac
  exit 0
}

# --- the owner's commands ---------------------------------------------------------------------------------
cmd_rollback() {
  is_root || { err "run as root"; exit 1; }
  say "dock-receiver-setup rollback: removing the packages, then bringing the old setup back"
  if pkg_installed dock-receiver-dockremote; then dpkg -r dock-receiver-dockremote || { err "dpkg -r dock-receiver-dockremote failed"; exit 1; }; fi
  if pkg_installed dock-receiver; then dpkg -r dock-receiver || { err "dpkg -r dock-receiver failed"; exit 1; }; fi
  stop_new_jobs
  restore_old_setup
  exit 0
}

cmd_cleanup() {
  local yes=0 l t
  [ "${1:-}" = "--yes" ] && yes=1
  is_root || { err "run as root"; exit 1; }
  LOGGING=0
  for l in $OLD_LABELS; do
    if [ -f "$LD/$l.plist" ] || job_loaded "$l"; then err "the old setup is active ($l): nothing removed. Use rollback, or migrate first"; exit 1; fi
  done
  pkg_installed dock-receiver || { err "the package dock-receiver is not installed: the old files are the only setup; nothing removed"; exit 1; }
  say "cleanup removes these, which only a rollback needs:"
  t=0
  [ -e "$O" ] && { say "  ${O#$R}   (the old data directory: binaries, launchers, credentials, logs, backups)"; t=1; }
  [ -e "$B" ] && { say "  ${B#$R}   (the old job plists, app, lock plugin, tweak; the migration record)"; t=1; }
  [ "$t" = 0 ] && { say "  nothing: already clean"; exit 0; }
  say "kept: ${D#$R}; anything else, e.g. older copies beside the app and the lock plugin, com.fidelio.syslogd."
  if [ "$yes" != 1 ]; then say "nothing removed. Run 'dock-receiver-setup cleanup --yes' to remove them."; exit 0; fi
  LOGGING=1
  rm -rf "$O" "$B" && say "removed ${O#$R} and ${B#$R} (a rollback is no longer possible)"
}

cmd_respring() {
  local src
  is_root || { err "run as root"; return 1; }
  if [ "${1:-}" != "--even-if-playing" ] && [ -n "$(find "$D/active-source" -mmin -1 2>/dev/null)" ]; then
    src="$(sed -n 1p "$D/active-source" 2>/dev/null)"
    case "$src" in
      ""|none) ;;
      *) err "something is playing ($src): not restarting SpringBoard (once a restart during playback was followed by a reboot). Pause first, or pass --even-if-playing"; return 1 ;;
    esac
  fi
  say "restarting SpringBoard"
  killall SpringBoard
}

usage() {
  sed -n '6,13p' "$0" 2>/dev/null | sed 's/^# \{0,1\}//'
}

# --- dispatch -------------------------------------------------------------------------------------------
case "${1:-}" in
  check) cmd_check; exit $? ;;
  migrate) is_root || { err "run as root"; exit 1; }; cmd_migrate; exit $? ;;
  rollback) cmd_rollback ;;
  cleanup) shift; cmd_cleanup "$@"; exit $? ;;
  respring) shift; cmd_respring "$@"; exit $? ;;
esac
case "${0##*/}" in
  *preinst) do_preinst "$@" ;;
  *postinst) do_postinst "$@" ;;
  *prerm) do_prerm "$@" ;;
  *postrm) do_postrm "$@" ;;
esac
usage
exit 2
