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
MIN_ROOT_KB=4096                           # /usr/bin, /usr/sbin, the plugins, the job plists: well under 1 MB
MIN_BIN_KB=12288                           # /usr/libexec (a link into /var/stash on the devices): 2 x 4 MB + margin
MIN_VAR_KB=20480
TIMEOUT_S="${DOCKRECEIVER_TIMEOUT_S:-20}"  # waits for a process to stop / a daemon to come up
ACTIVE_WAIT_S="${DOCKRECEIVER_ACTIVE_WAIT_S:-60}"  # for audiod's first active-source (its audio session may retry)
RECHECK_S="${DOCKRECEIVER_RECHECK_S:-15}"   # watch that no old audiod comes back (launchd throttles restarts by 10 s)

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
  # host tests only: the script dies here, as at a power loss or a reboot
  [ -z "${DOCKRECEIVER_TEST_CRASH:-}" ] || [ "$DOCKRECEIVER_TEST_CRASH" != "$1" ] || kill -9 $$
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

# Is the job loaded? Robust to the formats `launchctl list` may have: "PID Status Label" with tabs or
# spaces, or one label per line, with or without trailing blanks or a CR; and, if the list does not
# show it, `launchctl list LABEL`, which prints the job's dictionary (with its "Label") when it is loaded.
job_loaded() {  # LABEL
  local re
  re="$(printf '%s' "$1" | sed 's/\./\\./g')"
  launchctl list 2>/dev/null | sed 's/[[:space:]]*$//' | grep -q -E "(^|[[:space:]])$re\$" && return 0
  launchctl list "$1" 2>/dev/null | grep -q -F "\"$1\""
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

# Copy SRC into DIR, then remove SRC. A copy kept there earlier is never overwritten: the first one
# keeps the plain name (DIR/<name>), anything found later goes beside it as DIR/<name>.2, .3, ...
# A failure leaves SRC where it was. MOVED_TO is the copy's path.
MOVED_TO=""
move_to_backup() {  # SRC DIR
  local src="$1" dir="$2" name="${1##*/}" dest n=2
  mkdir -p "$dir" || return 1
  dest="$dir/$name"
  while [ -e "$dest" ]; do dest="$dir/$name.$n"; n=$((n + 1)); done
  rm -rf "$dest.copying"
  cp -Rp "$src" "$dest.copying" || { rm -rf "$dest.copying"; return 1; }
  mv "$dest.copying" "$dest" || { rm -rf "$dest.copying"; return 1; }
  MOVED_TO="$dest"
  rm -rf "$src"
}

# Put a copy of SRC at DEST, by a new name and a rename (a file that has run is never written over;
# the temporary name never ends in .dylib or .plist). A directory is put only where nothing is.
restore_copy() {  # SRC DEST
  mkdir -p "${2%/*}" || return 1
  rm -rf "$2.dr-restore"
  cp -Rp "$1" "$2.dr-restore" || { rm -rf "$2.dr-restore"; return 1; }
  if [ -d "$2.dr-restore" ]; then
    [ -e "$2" ] && { rm -rf "$2.dr-restore"; return 1; }
    mv "$2.dr-restore" "$2" || { rm -rf "$2.dr-restore"; return 1; }
  else
    mv -f "$2.dr-restore" "$2" || { rm -f "$2.dr-restore"; return 1; }
  fi
}

# The app at /Applications/NowPlaying.app, if it is not the old one (the new one, or what `dpkg -r`
# left of a restored old app: an empty id), goes to $B/new; then the kept old app is put there.
put_old_app_back() {
  [ -d "$B/Applications/NowPlaying.app" ] || return 0
  if [ -d "$APPS/NowPlaying.app" ] && [ "$(app_id)" != "$OLD_APP_ID" ]; then
    move_to_backup "$APPS/NowPlaying.app" "$B/new" && say "moved the app found at /Applications/NowPlaying.app (${1:-not the old one}) to ${MOVED_TO#$R}" \
      || { err "could not move /Applications/NowPlaying.app aside"; return 1; }
  fi
  [ -d "$APPS/NowPlaying.app" ] && return 0   # the old one is there
  restore_copy "$B/Applications/NowPlaying.app" "$APPS/NowPlaying.app" || { err "could not restore the old NowPlaying.app"; return 1; }
  state_set app_moved no
  say "the old NowPlaying.app is back"
}

install_file() {  # SRC DEST OWNER MODE: by a new name and a rename
  rm -f "$2.dr-new"
  cp "$1" "$2.dr-new" && chown "$3" "$2.dr-new" && chmod "$4" "$2.dr-new" && mv -f "$2.dr-new" "$2" \
    || { rm -f "$2.dr-new"; return 1; }
}

app_id() { plist_string "$APPS/NowPlaying.app/Info.plist" CFBundleIdentifier; }
app_version() { plist_string "$APPS/NowPlaying.app/Info.plist" CFBundleShortVersionString; }
# dpkg's status of a package: "installed", "unpacked", "half-configured", ... ("" if dpkg knows none)
pkg_status() { dpkg -s "$1" 2>/dev/null | sed 's/[[:space:]]*$//' | sed -n 's/^Status: [a-z]* [a-z]* //p' | sed -n 1p; }
pkg_installed() { [ "$(pkg_status "$1")" = installed ]; }           # fully configured
pkg_present() {                                                     # anything dpkg would remove
  case "$(pkg_status "$1")" in installed|unpacked|half-configured|half-installed|triggers-*) return 0 ;; esac
  return 1
}

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
# A `$NAME` / `${NAME}` reference resolved against FILES (the old job's chain, which the shell reads as
# one): every value assigned to NAME there, one per line; the value itself if it is no reference.
resolve_ref() {  # VALUE FILE...
  local name f
  case "$1" in
    \$\{*\}) name="${1#\$\{}"; name="${name%\}}" ;;
    \$*) name="${1#\$}" ;;
    *) printf '%s\n' "$1"; return 0 ;;
  esac
  shift
  case "$name" in ''|*[!A-Za-z0-9_]*) return 1 ;; esac
  for f in "$@"; do assign_values "$f" "$name"; done
  return 0
}

plist_sh_paths() {  # PLIST: the .sh files under the old directory that a job's ProgramArguments name
  [ -f "$1" ] && sed -n "s|.*<string>\(/var/mobile/fidelio/[^<]*\.sh\)</string>.*|\1|p" "$1"
}

# The text files of the old directory that FILE mentions: sourced, called, read or grepped (an
# absolute path under /var/mobile/fidelio ending in .sh/.env/.conf/.cfg/.txt), or by a known name
# (creds.env, cspotd.sh, test_cspot.sh, run_cspot.sh, test_raop.sh, run_raop.sh) relative to it.
file_refs() {  # FILE
  local p n
  prep_file "$1" | grep -o -E '/var/mobile/fidelio/[A-Za-z0-9._/-]+\.(sh|env|conf|cfg|txt)' | while IFS= read -r p; do
    printf '%s\n' "$R$p"
  done
  for n in creds.env cspotd.sh test_cspot.sh run_cspot.sh test_raop.sh run_raop.sh; do
    prep_file "$1" | grep -q -F "$n" || continue
    [ -f "$O/$n" ] && printf '%s\n' "$O/$n"
    [ -f "$O/b1/$n" ] && printf '%s\n' "$O/b1/$n"
  done
}

# The chain of the old job: the launcher its plist runs (the first .sh in ProgramArguments, in
# /Library/LaunchDaemons or, after the switch, in the recoverable place), then every file it mentions,
# recursively. One path per line, the launcher first, each once. Empty without a plist.
old_chain() {  # KIND(cspot|raop)
  local plist root queue f ref seen=" "
  plist="$LD/com.fidelio.$1.plist"
  [ -f "$plist" ] || plist="$B/LaunchDaemons/com.fidelio.$1.plist"
  root="$(plist_sh_paths "$plist" | sed -n 1p)"
  [ -n "$root" ] && [ -f "$R$root" ] || return 0
  queue="$R$root"
  while [ -n "$queue" ]; do
    f="${queue%% *}"; queue="${queue#"$f"}"; queue="${queue# }"
    case "$seen" in *" $f "*) continue ;; esac
    seen="$seen$f "
    printf '%s\n' "$f"
    for ref in $(file_refs "$f"); do [ -f "$ref" ] && queue="$queue $ref"; done
  done
}

# Files that may hold the old Spotify values without being the job's chain (named in the dry run).
OLD_CRED_CANDIDATES="creds.env cspotd.sh b1/cspotd.sh b1/test_cspot.sh b1/run_cspot.sh run_cspot.sh test_cspot.sh"

# Every value FILES give for the client id (or secret), references resolved within FILES. VALS holds
# the distinct values (newline-separated, never printed), VAL_N their number, VAL_FILES the files they
# came from. The shell uses the last assignment; which line runs last in a set of scripts is not
# something to guess, so one distinct value is required (VAL_N 1), and two or more is a conflict.
VALS=""; VAL_N=0; VAL_FILES=""
collect_values() {  # KIND(id|secret) FILE...
  local kind="$1" opt name f v r cands ifs="$IFS"
  shift
  VALS=""; VAL_N=0; VAL_FILES=""
  case "$kind" in
    id) opt="--client-id"; name="[A-Za-z_]*CLIENT_?ID[A-Za-z0-9_]*" ;;
    secret) opt="--client-secret"; name="[A-Za-z_]*SECRET[A-Za-z0-9_]*" ;;
  esac
  for f in "$@"; do
    [ -f "$f" ] || continue
    # The candidates stay in memory (a here-document would put them in a temporary file).
    cands="$(opt_values "$f" "$opt"; assign_values "$f" "$name")"
    set -f; IFS='
'
    for v in $cands; do
      for r in $(resolve_ref "$v" "$@"); do
        valid_token "$r" || continue
        case " $VAL_FILES " in *" $f "*) ;; *) VAL_FILES="${VAL_FILES:+$VAL_FILES }$f" ;; esac
        case "
$VALS
" in *"
$r
"*) ;; *) VALS="$VALS
$r"; VAL_N=$((VAL_N + 1)) ;; esac
      done
    done
    IFS="$ifs"; set +f
  done
  cands=""
}
valid_token() { case "$1" in ''|*[!A-Za-z0-9._-]*) return 1 ;; esac; return 0; }

# The old Spotify credentials, from the old job's chain only. Sets CRED_ID / CRED_SECRET (never
# printed) and CRED_FROM (the files). Returns 0 found, 1 none, 2 two different values for one of them
# (CRED_FROM names the files; nothing is taken). CRED_UNUSED: candidate files outside the chain that
# hold values too (named in the output, ignored).
CRED_ID=""; CRED_SECRET=""; CRED_FROM=""; CRED_UNUSED=""; CRED_CHAIN=""
find_old_credentials() {
  local chain f c ids
  CRED_ID=""; CRED_SECRET=""; CRED_FROM=""; CRED_UNUSED=""
  chain="$(old_chain cspot)"
  CRED_CHAIN="$(printf '%s ' $chain)"
  for c in $OLD_CRED_CANDIDATES; do
    f="$O/$c"
    [ -f "$f" ] || continue
    case " $CRED_CHAIN " in *" $f "*) continue ;; esac
    collect_values id "$f"; ids="$VAL_N"; collect_values secret "$f"
    [ "$ids" -gt 0 ] || [ "$VAL_N" -gt 0 ] && CRED_UNUSED="${CRED_UNUSED:+$CRED_UNUSED }$f"
  done
  VALS=""
  [ -n "$chain" ] || return 1
  collect_values id $chain
  [ "$VAL_N" -gt 1 ] && { CRED_FROM="$VAL_FILES"; VALS=""; return 2; }
  [ "$VAL_N" = 1 ] || { VALS=""; return 1; }
  CRED_ID="${VALS#?}"; CRED_FROM="$VAL_FILES"
  collect_values secret $chain
  [ "$VAL_N" -gt 1 ] && { CRED_ID=""; CRED_FROM="$VAL_FILES"; VALS=""; return 2; }
  [ "$VAL_N" = 1 ] || { CRED_ID=""; VALS=""; return 1; }
  CRED_SECRET="${VALS#?}"; VALS=""
  for f in $VAL_FILES; do case " $CRED_FROM " in *" $f "*) ;; *) CRED_FROM="$CRED_FROM $f" ;; esac; done
  return 0
}

# The values of an option across the old job's chain (device names, bitrate): OPT_VAL if all agree,
# empty with OPT_CONFLICT=1 if they differ. Unresolvable references are skipped.
OPT_VAL=""; OPT_CONFLICT=0
chain_option() {  # KIND(cspot|raop) OPT-ERE LINE-FILTER-ERE
  local chain f v r vals="" n=0 ifs="$IFS"
  OPT_VAL=""; OPT_CONFLICT=0
  chain="$(old_chain "$1")"
  [ -n "$chain" ] || return 1
  set -f; IFS='
'
  for f in $chain; do
    for v in $(opt_values "$f" "$2" "$3"); do
      for r in $(resolve_ref "$v" $chain); do
        case "$r" in ''|*[\$\`]*) continue ;; esac
        case "
$vals
" in *"
$r
"*) ;; *) vals="$vals
$r"; n=$((n + 1)) ;; esac
      done
    done
  done
  IFS="$ifs"; set +f
  [ "$n" = 1 ] && { OPT_VAL="${vals#?}"; return 0; }
  [ "$n" -gt 1 ] && OPT_CONFLICT=1
  return 1
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


# The keepalive target of an old keepalive script, only in its obvious forms: bash's /dev/udp/ADDR/PORT
# (or /dev/tcp), or the last argument of a ping. Anything else that looks like an address (a netmask,
# a broadcast) is not taken. Prints the address, or nothing.
keepalive_target() {  # FILE
  local v
  v="$(prep_file "$1" | sed -n -r 's#.*/dev/(udp|tcp)/([0-9]{1,3}(\.[0-9]{1,3}){3})/[0-9]+.*#\2#p' | sed -n '$p')"
  [ -n "$v" ] || v="$(prep_file "$1" | grep -E '(^|[;&|[:space:]])ping[[:space:]]' \
      | sed -n -r 's/.*[[:space:]]([0-9]{1,3}(\.[0-9]{1,3}){3})([[:space:]]*([;&|>].*)?)?$/\1/p' | sed -n '$p')"
  case "$v" in 255.*|0.*|*.255) v="" ;; esac
  printf '%s' "$v"
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

# Can we trust job_loaded for the old jobs? Not if a job has its plist, its daemon runs, and launchctl
# still says it is not loaded: then the format of `launchctl list` is not understood (or the daemon was
# started by hand), the old job would not be unloaded, and launchd would start its audiod again beside
# the new one. Sets JOBS_UNREADABLE to the reason.
JOBS_UNREADABLE=""
jobs_readable() {
  local l p
  JOBS_UNREADABLE=""
  for l in com.fidelio.audiod com.fidelio.raop; do
    p="fidelio-${l##*.}"
    if [ -f "$LD/$l.plist" ] && ! job_loaded "$l" && proc_running "$p"; then
      JOBS_UNREADABLE="$l has its plist and $p runs, but launchctl does not show the job as loaded (unknown 'launchctl list' format, or $p started by hand). Stop $p by hand ('launchctl unload $LD/$l.plist; killall $p'), then retry"
      return 1
    fi
  done
  return 0
}

# Where the keepalive will send to: printed for the dry run and the summary. The address itself is not
# printed (it would end up in logs that get copied around); only whether one was found, and where.
KA_TARGET=""; KA_FROM=""
find_keepalive_target() {
  local f
  KA_TARGET=""; KA_FROM=""
  if [ -f "$D/keepalive.conf" ] && grep -q '^KEEPALIVE_HOST=[0-9]' "$D/keepalive.conf"; then
    KA_TARGET=set; KA_FROM="$D/keepalive.conf"; return 0
  fi
  for f in $(old_launchers keepalive); do
    KA_TARGET="$(keepalive_target "$f")"
    [ -n "$KA_TARGET" ] && { KA_FROM="$f"; return 0; }
  done
  return 1
}
keepalive_report() {
  if find_keepalive_target; then
    say "keepalive target: found in ${KA_FROM#$R} (the address is not shown)"
  elif have ipconfig; then
    say "keepalive target: none in the old setup; the keepalive asks 'ipconfig getoption en0 router'"
  else
    warn "keepalive target: none in the old setup, and no ipconfig: the keepalive cannot send, so Wi-Fi may sleep. Put KEEPALIVE_HOST=<your router's address> into ${D#$R}/keepalive.conf"
  fi
  KA_TARGET=""
}

old_setup_present() {  # anything of the old setup still live
  local l
  for l in $OLD_LABELS; do [ -f "$LD/$l.plist" ] && return 0; job_loaded "$l" && return 0; done
  for l in $OLD_PROCS; do [ "$l" = cspot-ios3 ] && continue; proc_running "$l" && return 0; done
  return 1
}

# Free space. On the devices /Applications, /usr/libexec and /usr/share are links into /var/stash, so
# the daemons and the app take their room on /var; df follows the link (a path with a trailing /).
SPACE_INFO=""; SPACE_BAD=""
check_space() {
  local a b c
  a="$(free_kb "$R/")"; b="$(free_kb "$R/usr/libexec/")"; c="$(free_kb "$R/var/")"
  SPACE_INFO="free space: / ${a:-?} KB (needs $MIN_ROOT_KB), /usr/libexec's file system ${b:-?} KB (needs $MIN_BIN_KB), /var ${c:-?} KB (needs $MIN_VAR_KB)"
  SPACE_BAD=""
  [ -n "$a" ] && [ "$a" -lt "$MIN_ROOT_KB" ] && SPACE_BAD="$SPACE_BAD only $a KB free on / (needs $MIN_ROOT_KB);"
  [ -n "$b" ] && [ "$b" -lt "$MIN_BIN_KB" ] && SPACE_BAD="$SPACE_BAD only $b KB free for /usr/libexec (needs $MIN_BIN_KB);"
  [ -n "$c" ] && [ "$c" -lt "$MIN_VAR_KB" ] && SPACE_BAD="$SPACE_BAD only $c KB free on /var (needs $MIN_VAR_KB);"
  [ -z "$SPACE_BAD" ]
}

# What the dry run and the migration say about the old Spotify values (file names only, never values).
credentials_report() {  # MODE(check|migrate): sets CREDS_RC (0 found, 1 none, 2 conflict)
  local f
  if creds_present; then
    say "credentials: already in ${CREDS#$R}"; CREDS_RC=3
  else
    find_old_credentials; CREDS_RC=$?
    [ -n "$CRED_CHAIN" ] && say "  the old Spotify job runs:$(for f in $CRED_CHAIN; do printf ' %s' "${f#$R}"; done)"
    case "$CREDS_RC" in
      0) say "credentials: found in$(for f in $CRED_FROM; do printf ' %s' "${f#$R}"; done)" ;;
      2) say "credentials: two different values in$(for f in $CRED_FROM; do printf ' %s' "${f#$R}"; done): not taken; fill in ${CREDS#$R} by hand" ;;
      *) say "credentials: not found in the old Spotify job's files (the Spotify job waits until ${CREDS#$R} is filled)" ;;
    esac
  fi
  for f in $CRED_UNUSED; do
    case "$f" in
      */creds.env) say "  also present, not used: ${f#$R} (the old Spotify job does not read it)" ;;
      *) say "  also present, not used: ${f#$R} (not part of the old Spotify job)" ;;
    esac
  done
  [ "$1" = check ] && { CRED_ID=""; CRED_SECRET=""; }
  return 0
}

options_report() {
  local t=""
  chain_option cspot '-n|--name' 'cspot-ios3' && t="name"
  [ "$OPT_CONFLICT" = 1 ] && t="name (two different values: not copied)"
  chain_option cspot '-b|--bitrate' 'cspot-ios3' && t="${t:+$t, }bitrate"
  [ "$OPT_CONFLICT" = 1 ] && t="${t:+$t, }bitrate (two different values: not copied)"
  [ -n "$t" ] && say "  Spotify options in the old job's files: $t (copied into settings.conf where it does not set them)"
  t=""
  chain_option raop '-n|--name' 'raop' && t="name"
  [ "$OPT_CONFLICT" = 1 ] && t="name (two different values: not copied)"
  [ -n "$t" ] && say "  AirPlay options in the old job's files: $t (copied into settings.conf where it does not set them)"
  OPT_VAL=""
  return 0
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
  check_space; say "$SPACE_INFO"
  [ -n "$SPACE_BAD" ] && blockers="$blockers space"
  t="$(pkg_status dock-receiver)"; say "package dock-receiver: ${t:-not installed}"
  t="$(pkg_status dock-receiver-dockremote)"; [ -n "$t" ] && say "package dock-receiver-dockremote: $t"
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
  [ -f "$LD/com.fidelio.cspot.plist.rev6.bak" ] && say "  left alone: ${LD#$R}/com.fidelio.cspot.plist.rev6.bak (not loaded; 'cleanup' removes it later)"
  if [ -f "$IAPD_PREFS" ] && grep -q -e PrintIapPackets -e LogDeviceVolumeChanges -e LogLineOut -e LogSimpleRemoteEvents -e LogGSEvents "$IAPD_PREFS"; then
    say "  left alone: iapd logging preferences in com.apple.iapd.plist"
  fi
  if [ -d "$O" ]; then
    t=""; for f in $CARRY_FILES; do [ -f "$O/$f" ] && t="$t $f"; done
    say "  files to copy into $D (if not there yet):${t:- none}"
  fi
  if ! jobs_readable; then
    say "  CANNOT TELL whether the old jobs are loaded: $JOBS_UNREADABLE"
    blockers="$blockers launchctl-list"
  fi
  credentials_report check
  options_report
  keepalive_report
  if [ -d "$APPS/NowPlaying.app" ]; then
    say "app: /Applications/NowPlaying.app ($(app_id), $(app_version))$( [ "$(app_id)" = "$NEW_APP_ID" ] || printf '%s' ": would be closed if running and moved to ${B#$R}/Applications")"
  else
    say "app: none at /Applications/NowPlaying.app"
  fi
  for f in $OLD_PLUGINS; do [ -e "$SBP/$f" ] && say "lock plugin: $f: left in place (SpringBoard may have it loaded; cleanup removes it later)"; done
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
  say "  9 install the new job plists  10 load the new jobs (audiod, raop, cspot, keepalive), check each, wait"
  say "  for audiod's active-source, and watch for ${RECHECK_S} s that no old audiod comes back  11 refresh the icon cache"
  say "  (the preinst closes a running old NowPlaying and moves it to ${B#$R}/Applications before dpkg unpacks)"
  if [ -n "$blockers" ]; then say "result: NOT ready:$blockers"; return 1; fi
  say "result: ready"
  return 0
}

# --- preinst: what must happen before dpkg unpacks ----------------------------------------------------
preflight_device() {
  local t missing=""
  is_root || { err "run as root"; return 1; }
  for t in $REQUIRED_TOOLS; do have "$t" || missing="$missing $t"; done
  [ -z "$missing" ] || { err "missing tools:$missing"; return 1; }
  check_space || { err "not enough space:$SPACE_BAD"; return 1; }
  return 0
}

# The hand-installed app is in the way of the new one (same path). A running NowPlaying is closed
# first, so that its bundle never moves away under it.
move_old_app_aside() {
  local id
  [ -d "$APPS/NowPlaying.app" ] || return 0
  id="$(app_id)"
  [ "$id" = "$NEW_APP_ID" ] && return 0
  if proc_running NowPlaying; then
    killall NowPlaying >/dev/null 2>&1
    wait_gone NowPlaying || { err "the running NowPlaying did not quit; close it and retry"; return 1; }
    say "closed the running NowPlaying (the old app)"
  fi
  move_to_backup "$APPS/NowPlaying.app" "$B/Applications" || { err "could not move the old app aside"; return 1; }
  say "moved the old /Applications/NowPlaying.app (${id:-unknown id}) to ${MOVED_TO#$R}"
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

# --- the jobs ---------------------------------------------------------------------------------------------
wait_for_proc() {  # NAME SECONDS
  local n=0
  while ! proc_running "$1"; do
    n=$((n + 1)); [ "$n" -gt "$2" ] && return 1
    sleep 1
  done
  return 0
}

# Load the old jobs again (rollback, undo). Never while dock-audiod runs: two audio daemons must never
# run at once. Never when the old directory is gone: the jobs would only restart forever.
load_old_jobs() {  # LABEL... (in any order; loaded audiod, raop, cspot, keepalive)
  local l
  if proc_running dock-audiod; then
    err "dock-audiod still runs, so the old jobs are NOT loaded (two audio daemons must never run). Stop it with 'killall -9 dock-audiod', then load the old jobs: launchctl load $LD/com.fidelio.audiod.plist (then .raop, .cspot, .keepalive)"
    return 1
  fi
  [ -d "$O" ] || { err "the old directory ${O#$R} is gone: the old jobs are not loaded"; return 1; }
  for l in $OLD_LABELS; do
    case " $* " in *" $l "*) ;; *) continue ;; esac
    [ -f "$LD/$l.plist" ] || continue
    job_loaded "$l" && continue
    launchctl load "$LD/$l.plist" >/dev/null 2>&1
    if job_loaded "$l"; then say "loaded $l"; else err "could not load $l"; fi
  done
  return 0
}

# Load the new jobs that have a plist (undo of a re-run, abort-*). Never while an old audiod runs.
reload_new_jobs() {
  local l
  if proc_running fidelio-audiod; then
    err "an old audiod (fidelio-audiod) runs, so the new jobs are NOT loaded. Stop it, then run 'dock-receiver-setup migrate'"
    return 1
  fi
  for l in $NEW_LABELS; do
    [ -f "$LD/$l.plist" ] || continue
    job_loaded "$l" && continue
    launchctl load "$LD/$l.plist" >/dev/null 2>&1
    if job_loaded "$l"; then say "loaded $l"; else err "could not load $l"; fi
  done
}

stop_new_jobs() {
  local l procs="dock-audiod dock-raop"
  # cspot-ios3 is the old setup's process name too: it is stopped only when the new job was loaded
  job_loaded com.dockreceiver.cspot && procs="$procs cspot-ios3"
  for l in $NEW_LABELS; do
    if job_loaded "$l"; then launchctl unload "$LD/$l.plist" >/dev/null 2>&1 || launchctl remove "$l" >/dev/null 2>&1; fi
    rm -f "$LD/$l.plist"
  done
  wait_gone $procs || { warn "a new daemon is still running"; return 1; }
}

# --- postinst: the migration ----------------------------------------------------------------------------
# What this migration has changed so far, kept in $STATE as well (a reboot in the middle must not lose
# it: the next run reads it back and can still undo).
RUN_STOPPED_OLD=""     # old jobs that were unloaded
RUN_MOVED_PLISTS=""    # old plists that were moved to $B
RUN_NEW_PLISTS=0       # the new plists were installed while switching from the old setup
run_load() {
  RUN_STOPPED_OLD="$(state_get run_stopped_old)"
  RUN_MOVED_PLISTS="$(state_get run_moved_plists)"
  RUN_NEW_PLISTS="$(state_get run_new_plists)"; [ -n "$RUN_NEW_PLISTS" ] || RUN_NEW_PLISTS=0
}
run_save() {
  state_set run_stopped_old "$RUN_STOPPED_OLD" && state_set run_moved_plists "$RUN_MOVED_PLISTS" && state_set run_new_plists "$RUN_NEW_PLISTS"
}
run_clear() { RUN_STOPPED_OLD=""; RUN_MOVED_PLISTS=""; RUN_NEW_PLISTS=0; run_save; }

UNDO_MSG=""
undo() {
  local l p switching=0 old_live=0
  [ -n "$RUN_STOPPED_OLD$RUN_MOVED_PLISTS" ] && switching=1
  [ -f "$LD/com.fidelio.audiod.plist" ] && old_live=1
  if [ "$switching" = 1 ]; then
    say "undoing this run so that the old setup runs as before"
    stop_new_jobs || true
    if [ -d "$O" ]; then
      for p in $RUN_MOVED_PLISTS; do
        [ -f "$LD/$p" ] || restore_copy "$B/LaunchDaemons/$p" "$LD/$p" || err "could not restore $p"
      done
    else
      err "the old directory ${O#$R} is gone: the old job plists are not restored"
    fi
  fi
  if [ "$switching" = 1 ] || [ "$old_live" = 1 ]; then
    [ "$(state_get app_moved)" = pending ] && { put_old_app_back "the new one" && refresh_icons; }
    # dpkg may have unpacked the dock-remote package over a hand-installed tweak already (it is
    # configured only after this package): put the old tweak back, so that an iapd restart loads it.
    for p in DockRemote.dylib DockRemote.plist; do
      [ -f "$B/DynamicLibraries/$p" ] && [ -d "$MSDL" ] && restore_copy "$B/DynamicLibraries/$p" "$MSDL/$p" && say "put the old $p back"
    done
  fi
  if [ "$switching" = 1 ]; then
    if load_old_jobs $RUN_STOPPED_OLD; then UNDO_MSG="the old setup runs as before; ${O#$R} was never changed"
    else UNDO_MSG="the old setup could NOT be started again: see the ERROR above"; fi
  elif [ "$old_live" = 1 ]; then
    UNDO_MSG="nothing of the old setup was touched: it runs as before"
  else
    say "this run had not touched an old setup: loading the jobs that were installed before it"
    if reload_new_jobs; then UNDO_MSG="the jobs installed before this run are loaded again"
    else UNDO_MSG="the jobs could NOT all be loaded again: see the ERROR above"; fi
  fi
  run_clear
}

fail() {
  err "$*"
  undo
  say "dock-receiver $DR_VERSION: the install FAILED at step $step_name; log: ${LOG#$R}"
  say "$UNDO_MSG. Retry with dpkg -i, or remove the packages with: dock-receiver-setup rollback"
  exit 1
}

# The icon cache must be rebuilt as mobile: as root, uicache can leave it owned by root (iOS 5).
refresh_icons() {
  su mobile -c uicache >/dev/null 2>&1 && return 0
  warn "the icon cache was not refreshed ('su mobile -c uicache' failed): run 'su mobile -c uicache' as root"
  return 1
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

carry_option() {  # KEY KIND OPT-ERE FILTER [VALID-ERE]
  conf_has "$1" && return 0
  if chain_option "$2" "$3" "$4"; then
    if [ -n "${5:-}" ] && ! printf '%s\n' "$OPT_VAL" | grep -q -x -E "$5"; then OPT_VAL=""; return 0; fi
    conf_add "$1" "$OPT_VAL" || return 1
    say "settings.conf: $1=$OPT_VAL (from the old job's files)"
  elif [ "$OPT_CONFLICT" = 1 ]; then
    say "settings.conf: $1 not copied: the old job's files give two different values"
  fi
  OPT_VAL=""
  return 0
}
carry_options() {
  carry_option cspot.name cspot '-n|--name' 'cspot-ios3' && \
  carry_option cspot.bitrate cspot '-b|--bitrate' 'cspot-ios3' '96|160|320' && \
  carry_option raop.name raop '-n|--name' 'raop'
}

carry_credentials() {
  credentials_report migrate
  case "$CREDS_RC" in
    3) chown "$MOBILE" "$CREDS" && chmod 600 "$CREDS" ;;
    0) write_creds "$CRED_ID" "$CRED_SECRET" || { CRED_ID=""; CRED_SECRET=""; return 1; }
       say "credentials written to ${CREDS#$R} (mode 600, owner mobile)" ;;
    *) [ -f "$CREDS" ] || write_creds "" "" || return 1
       say "fill in ${CREDS#$R} by hand (README); the Spotify receiver starts within 30 s of that" ;;
  esac
  CRED_ID=""; CRED_SECRET=""
  return 0
}

carry_keepalive() {
  if [ -f "$D/keepalive.conf" ]; then say "keepalive.conf: already there"; return 0; fi
  if find_keepalive_target; then
    printf 'KEEPALIVE_HOST=%s\n' "$KA_TARGET" > "$D/keepalive.conf.dr-new" && chown "$MOBILE" "$D/keepalive.conf.dr-new" \
      && mv -f "$D/keepalive.conf.dr-new" "$D/keepalive.conf" || { KA_TARGET=""; return 1; }
    say "keepalive: target taken from ${KA_FROM#$R} into keepalive.conf (the address is not shown)"
  else
    keepalive_report
  fi
  KA_TARGET=""
  return 0
}

stop_jobs() {
  local l
  for l in $OLD_LABELS; do
    if job_loaded "$l"; then
      [ -f "$LD/$l.plist" ] && launchctl unload "$LD/$l.plist" >/dev/null 2>&1
      job_loaded "$l" && launchctl remove "$l" >/dev/null 2>&1
      job_loaded "$l" && { err "could not unload $l"; return 1; }
      case " $RUN_STOPPED_OLD " in *" $l "*) ;; *) RUN_STOPPED_OLD="${RUN_STOPPED_OLD:+$RUN_STOPPED_OLD }$l" ;; esac
      run_save
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
    # the first copy kept is the one restored; a later copy (after a rollback) stays beside it
    case " $RUN_MOVED_PLISTS " in *" $l.plist "*) ;; *) RUN_MOVED_PLISTS="${RUN_MOVED_PLISTS:+$RUN_MOVED_PLISTS }$l.plist" ;; esac
    run_save
    say "moved $l.plist to ${B#$R}/LaunchDaemons"
  done
  return 0
}

install_new_plists() {
  local l
  [ -n "$RUN_STOPPED_OLD$RUN_MOVED_PLISTS" ] && { RUN_NEW_PLISTS=1; run_save; }
  for l in $NEW_LABELS; do
    install_file "$SHARE/launchd/$l.plist" "$LD/$l.plist" "$ROOTOWN" 644 || { err "could not install $l.plist"; return 1; }
  done
  say "installed the job plists: $NEW_LABELS"
}

# Load the new jobs, each checked (launchctl load may exit 0 although it loaded nothing), wait for
# audiod's active-source, then watch for longer than launchd's throttle that no old audiod comes back.
load_new_jobs() {
  local l n
  proc_running fidelio-audiod && { err "an old audiod (fidelio-audiod) runs: the new jobs are not loaded"; return 1; }
  touch "$B/.load-ref"
  for l in $NEW_LABELS; do
    launchctl load "$LD/$l.plist" >/dev/null 2>&1 || { err "launchctl load $l failed"; return 1; }
    job_loaded "$l" || { err "launchctl load $l did not load it"; return 1; }
    case "$l" in
      com.dockreceiver.audiod) wait_for_proc dock-audiod "$TIMEOUT_S" || { err "dock-audiod did not start (see ${D#$R}/logs/audiod.log)"; return 1; } ;;
      com.dockreceiver.raop) wait_for_proc dock-raop "$TIMEOUT_S" || { err "dock-raop did not start (see ${D#$R}/logs/raop.log)"; return 1; } ;;
      com.dockreceiver.cspot)
        if creds_present; then
          wait_for_proc cspot-ios3 "$TIMEOUT_S" || { err "cspot-ios3 did not start (see ${D#$R}/logs/cspot.log)"; return 1; }
        else
          say "com.dockreceiver.cspot is loaded; it starts cspot-ios3 once the credentials file is filled"
        fi ;;
    esac
    say "loaded $l"
  done
  printf '%s' "waiting for dock-audiod's ${D#$R}/active-source "
  n=0
  while [ -z "$(find "$D/active-source" -newer "$B/.load-ref" 2>/dev/null)" ]; do
    n=$((n + 1)); [ "$n" -gt "$ACTIVE_WAIT_S" ] && { printf '\n'; err "dock-audiod runs but has not written ${D#$R}/active-source in $ACTIVE_WAIT_S s"; return 1; }
    printf '.'; sleep 1
  done
  printf ' ok\n'
  printf '%s' "checking for $RECHECK_S s that no old audiod comes back "
  n=0
  while [ "$n" -lt "$RECHECK_S" ]; do
    proc_running fidelio-audiod && { printf '\n'; err "an old audiod (fidelio-audiod) started again beside dock-audiod: launchd still runs an old job"; return 1; }
    proc_running dock-audiod || { printf '\n'; err "dock-audiod stopped (see ${D#$R}/logs/audiod.log)"; return 1; }
    printf '.'; sleep 1; n=$((n + 1))
  done
  printf ' ok\n'
  say "dock-audiod is running and writes ${D#$R}/active-source; no old audiod"
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
  local p
  say "dock-receiver $DR_VERSION is installed and running."
  if [ -d "$B/LaunchDaemons" ] || [ -d "$O" ]; then
    say "kept for a rollback: ${O#$R} (unchanged) and ${B#$R} (the old job plists, app and tweak)"
  fi
  creds_present || say "STILL NEEDED: the Spotify credentials in ${CREDS#$R} (README: /usr/share/doc/dock-receiver/README.md)"
  say "STILL NEEDED: restart SpringBoard to finish: the lock plugin and the app's new id."
  say "  Run 'dock-receiver-setup respring' when nothing is playing (it refuses while something plays)."
  for p in $OLD_PLUGINS; do [ -e "$SBP/$p" ] && say "left in place: the old lock plugin $p (SpringBoard may have it loaded); 'cleanup' removes it"; done
  [ -f "$LD/com.fidelio.syslogd.plist" ] && say "left alone: the job com.fidelio.syslogd (a debugging aid, not part of the product)"
  [ -f "$LD/com.fidelio.cspot.plist.rev6.bak" ] && say "left alone: ${LD#$R}/com.fidelio.cspot.plist.rev6.bak (not loaded; 'cleanup' removes it)"
  if [ -f "$IAPD_PREFS" ] && grep -q -e PrintIapPackets -e LogDeviceVolumeChanges -e LogLineOut "$IAPD_PREFS"; then
    say "left alone: iapd's logging preferences (com.apple.iapd.plist)"
  fi
  say "rollback: dock-receiver-setup rollback    the old files, once you are happy: dock-receiver-setup cleanup"
}

cmd_migrate() {
  local l
  run_load
  step preflight && preflight_device && check_package_files || fail "prerequisites"
  jobs_readable || fail "$JOBS_UNREADABLE"
  step datadir || fail "data directory"
  mkdir -p "$D/logs" && chown "$MOBILE" "$D" "$D/logs" && chmod 755 "$D" "$D/logs" || fail "cannot create $D"
  for l in audiod raop cspot keepalive; do
    [ -f "$D/logs/$l.log" ] || { : > "$D/logs/$l.log" && chown "$MOBILE" "$D/logs/$l.log"; }
  done
  mkdir -p "$B" && chown "$ROOTOWN" "$B" && chmod 700 "$B" || fail "cannot create $B"
  say "dock-receiver $DR_VERSION: migrate (log: ${LOG#$R})"
  [ -n "$RUN_STOPPED_OLD$RUN_MOVED_PLISTS" ] && say "an earlier run did not finish (it had changed:${RUN_STOPPED_OLD:+ unloaded $RUN_STOPPED_OLD;}${RUN_MOVED_PLISTS:+ moved $RUN_MOVED_PLISTS}): continuing it"
  step settings && carry_files || fail "copying the settings"
  step options && carry_options || fail "copying the device names"
  step credentials && carry_credentials || fail "the credentials"
  step keepalive-target && carry_keepalive || fail "the keepalive target"
  step stop-jobs && stop_jobs || fail "stopping the jobs"
  step old-plists && move_old_plists || fail "moving the old job plists"
  step new-plists && install_new_plists || fail "installing the job plists"
  step load-jobs && load_new_jobs || fail "starting the new jobs"
  # The point of no return: the new setup runs. Nothing below undoes anything.
  state_set switched yes; state_set version "$DR_VERSION"
  [ "$(state_get app_moved)" = pending ] && state_set app_moved yes
  run_clear
  if ! step icon-cache; then warn "the icon cache was not refreshed: run 'su mobile -c uicache' as root"
  elif refresh_icons; then say "icon cache refreshed"; fi
  summary
  return 0
}

# --- removal: back to the old setup ---------------------------------------------------------------------
restore_old_setup() {
  local p restored=0
  [ -d "$B" ] || { say "no old setup kept: nothing to restore"; return 0; }
  if [ ! -d "$O" ]; then
    err "the old directory ${O#$R} is gone (cleanup ran): the old setup is not restored"
    return 1
  fi
  for p in $OLD_LABELS; do
    [ -f "$B/LaunchDaemons/$p.plist" ] || continue
    [ -f "$LD/$p.plist" ] && continue
    if restore_copy "$B/LaunchDaemons/$p.plist" "$LD/$p.plist"; then restored=1; say "restored $p.plist"; else err "could not restore $p.plist"; fi
  done
  if [ -d "$B/Applications/NowPlaying.app" ] && [ "$(app_id)" != "$OLD_APP_ID" ]; then
    put_old_app_back "left by the package" && restored=1
  fi
  for p in DockRemote.dylib DockRemote.plist; do
    [ -f "$B/DynamicLibraries/$p" ] || continue
    [ -e "$MSDL/$p" ] && continue
    [ -d "$MSDL" ] && restore_copy "$B/DynamicLibraries/$p" "$MSDL/$p" && restored=1 && say "restored the old tweak file $p"
  done
  [ "$restored" = 1 ] && refresh_icons
  load_old_jobs $OLD_LABELS || return 1
  state_set switched no
  say "the old setup is back. Restart SpringBoard when nothing plays (killall SpringBoard) for the old app's id;"
  say "the old tweak needs an undocked 'killall iapd' or a reboot. ${D#$R} is kept."
}

do_prerm() {
  case "$1" in
    remove|deconfigure)
      say "dock-receiver $DR_VERSION: prerm $1: stopping the jobs"
      stop_new_jobs || true
      ;;
  esac
  exit 0
}

do_postrm() {
  local f kept=""
  case "$1" in
    remove)
      say "dock-receiver $DR_VERSION: postrm remove"
      restore_old_setup || true
      ;;
    purge)
      # The owner's credentials and settings are not thrown away: they go to $B/kept (root only; the
      # credentials keep mode 600), the rest of the data directory is removed.
      LOGGING=0
      if [ -d "$D" ]; then
        for f in spotify-credentials.conf settings.conf keepalive.conf; do
          [ -f "$D/$f" ] || continue
          mkdir -p "$B/kept" && chown "$ROOTOWN" "$B" "$B/kept" && chmod 700 "$B" "$B/kept" || continue
          mv -f "$D/$f" "$B/kept/$f" && kept="$kept $f"
        done
        rm -rf "$D"
      fi
      say "dock-receiver: purge: removed ${D#$R}${kept:+; kept${kept} in ${B#$R}/kept}"
      ;;
    abort-install|abort-upgrade)
      [ "$(state_get app_moved)" = pending ] && put_old_app_back "left by the aborted install"
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
      reload_new_jobs || true
      ;;
  esac
  exit 0
}

# --- the owner's commands ---------------------------------------------------------------------------------
cmd_rollback() {
  is_root || { err "run as root"; exit 1; }
  # Refuse before removing anything unless the old setup can run afterwards: its audiod job and the
  # old directory must still be there (cleanup removes both).
  if [ ! -d "$O" ] || { [ ! -f "$LD/com.fidelio.audiod.plist" ] && [ ! -f "$B/LaunchDaemons/com.fidelio.audiod.plist" ]; }; then
    err "no complete old setup is kept (${O#$R} and the old job plists are needed; cleanup removed them?): removing the packages would leave no receiver. Nothing changed."
    exit 1
  fi
  say "dock-receiver-setup rollback: removing the packages, then bringing the old setup back"
  if pkg_present dock-receiver-dockremote; then dpkg -r dock-receiver-dockremote || { err "dpkg -r dock-receiver-dockremote failed"; exit 1; }; fi
  if pkg_present dock-receiver; then dpkg -r dock-receiver || { err "dpkg -r dock-receiver failed"; exit 1; }; fi
  stop_new_jobs || true
  restore_old_setup || exit 1
  exit 0
}

cmd_cleanup() {
  local yes=0 l t e
  [ "${1:-}" = "--yes" ] && yes=1
  is_root || { err "run as root"; exit 1; }
  LOGGING=0
  for l in $OLD_LABELS; do
    if [ -f "$LD/$l.plist" ] || job_loaded "$l"; then err "the old setup is active ($l): nothing removed. Use rollback, or migrate first"; exit 1; fi
  done
  pkg_installed dock-receiver || { err "the package dock-receiver is not (fully) installed: the old files may be the only working setup; nothing removed"; exit 1; }
  [ "$(state_get switched)" = yes ] || { err "the migration has not completed (no switched=yes in $STATE): nothing removed"; exit 1; }
  say "cleanup removes these, which only a rollback needs (run it after SpringBoard has restarted since the install):"
  t=""
  [ -e "$O" ] && { say "  ${O#$R}   (the old data directory: binaries, launchers, credentials, logs, backups)"; t="$t $O"; }
  for e in "$B"/* "$B/.load-ref"; do
    [ -e "$e" ] || continue
    [ "$e" = "$B/kept" ] && continue
    say "  ${e#$R}"; t="$t $e"
  done
  for e in $OLD_PLUGINS; do [ -e "$SBP/$e" ] && { say "  ${SBP#$R}/$e   (the old lock plugin)"; t="$t $SBP/$e"; }; done
  [ -f "$LD/com.fidelio.cspot.plist.rev6.bak" ] && { say "  ${LD#$R}/com.fidelio.cspot.plist.rev6.bak"; t="$t $LD/com.fidelio.cspot.plist.rev6.bak"; }
  [ -z "$t" ] && { say "  nothing: already clean"; exit 0; }
  [ -d "$B/kept" ] && say "kept: ${B#$R}/kept (credentials and settings saved by a purge)"
  say "kept: ${D#$R}; older copies beside the app and the lock plugin; com.fidelio.syslogd."
  if [ "$yes" != 1 ]; then say "nothing removed. Run 'dock-receiver-setup cleanup --yes' to remove them."; exit 0; fi
  LOGGING=1
  for e in $t; do rm -rf "$e"; done
  rmdir "$B" 2>/dev/null
  say "removed the old setup's files (a rollback is no longer possible)"
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
