#!/usr/bin/env bash
# packaging/install.sh [OPTIONS] DEVICE
#
# Installs the dock-receiver packages on a device from a PC, over SSH: copies the .deb files to the
# device, runs the dry run (dock-receiver-setup check, from the package's own preinst), asks, then runs
# `dpkg -i`. DEVICE is an ssh destination: root@<address>, or a Host alias of your ~/.ssh/config.
# Nothing about a device is built in: no address, no user's key, no password (ssh asks for it).
#
# Options
#   --debs DIR          where the two .deb files are (default: the directory of this script if they are
#                       there, else ../build/debs next to the repository, as tools/build-debs.sh writes them)
#   --check             only copy the packages and run the dry runs; change nothing
#   --yes               do not ask before installing
#   --no-dockremote     do not install the dock-remote package even on iPhone OS 3
#   --respring          let the install restart SpringBoard at the end (DOCKRECEIVER_RESPRING=1; it is still
#                       refused while something plays)
#
# Environment
#   SSH="ssh"  SCP="scp"   the commands to use (e.g. SSH="ssh -p 2222")
#   SSH_OPTS               options for both; default: the legacy algorithms the devices' OpenSSH 6.7 needs
#                          on a modern client (packaging/README.md):
#                          -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa
#   SCP_OPTS               extra options for scp only; default -O (the classic scp protocol) when the local
#                          scp knows it (OpenSSH 9 uses SFTP otherwise)
#
# Each ssh/scp call may ask for the password: four times for an install (one copy, the OS query, the dry
# run, the install). A key in ~/.ssh/authorized_keys on the device avoids that.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SSH="${SSH:-ssh}"; SCP="${SCP:-scp}"
SSH_OPTS="${SSH_OPTS:--o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa}"
if [ -z "${SCP_OPTS+x}" ]; then
  if $SCP -O 2>&1 | grep -q -i -e 'illegal option' -e 'unknown option'; then SCP_OPTS=""; else SCP_OPTS="-O"; fi
fi
DEBS=""; CHECK_ONLY=0; YES=0; DOCKREMOTE=auto; RESPRING=0; DEVICE=""
REMOTE_DIR=/var/tmp/dock-receiver-install

usage() { sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    --debs) DEBS="$2"; shift 2 ;;
    --check) CHECK_ONLY=1; shift ;;
    --yes) YES=1; shift ;;
    --no-dockremote) DOCKREMOTE=no; shift ;;
    --respring) RESPRING=1; shift ;;
    -h|--help) usage 0 ;;
    -*) echo "install.sh: unknown option $1" >&2; usage 2 ;;
    *) [ -z "$DEVICE" ] || usage 2; DEVICE="$1"; shift ;;
  esac
done
[ -n "$DEVICE" ] || usage 2

if [ -z "$DEBS" ]; then
  if ls "$HERE"/dock-receiver_*_iphoneos-arm.deb >/dev/null 2>&1; then DEBS="$HERE"; else DEBS="$HERE/../../build/debs"; fi
fi
pick() {  # PATTERN: exactly one file
  local f n=0 last=""
  for f in "$DEBS"/$1; do [ -f "$f" ] && { n=$((n + 1)); last="$f"; }; done
  [ "$n" = 1 ] || { echo "install.sh: need exactly one $1 in $DEBS (found $n)" >&2; exit 3; }
  printf '%s\n' "$last"
}
MAIN="$(pick 'dock-receiver_*_iphoneos-arm.deb')"
REMOTE="$(pick 'dock-receiver-dockremote_*_iphoneos-arm.deb')"
if [ -f "$DEBS/SHA256SUMS" ] && command -v sha256sum >/dev/null 2>&1; then
  ( cd "$DEBS" && sha256sum -c --quiet SHA256SUMS ) || { echo "install.sh: the packages do not match $DEBS/SHA256SUMS" >&2; exit 3; }
fi

rssh() { $SSH $SSH_OPTS "$DEVICE" "$@"; }

echo "== the device's OS"
OSV="$(rssh "sed -n -e '/<key>ProductVersion<\/key>/{n;s/.*<string>\([^<]*\)<\/string>.*/\1/p;}' /System/Library/CoreServices/SystemVersion.plist")"
echo "ProductVersion: ${OSV:-unknown}"
case "$OSV" in
  3.*) [ "$DOCKREMOTE" = no ] || DOCKREMOTE=yes ;;
  "") echo "install.sh: could not read the device's OS version" >&2; exit 3 ;;
  *) DOCKREMOTE=no ;;
esac
FILES="$MAIN"; [ "$DOCKREMOTE" = yes ] && FILES="$FILES $REMOTE"
NAMES=""; for f in $FILES; do NAMES="$NAMES $REMOTE_DIR/${f##*/}"; done

echo "== copying$( for f in $FILES; do printf ' %s' "${f##*/}"; done ) to $REMOTE_DIR"
rssh "mkdir -p $REMOTE_DIR && chmod 700 $REMOTE_DIR"
# shellcheck disable=SC2086
$SCP $SSH_OPTS $SCP_OPTS $FILES "$DEVICE:$REMOTE_DIR/"

echo "== dry run on the device"
DRY="rm -rf $REMOTE_DIR/ctl $REMOTE_DIR/ctl-remote && dpkg-deb -e $REMOTE_DIR/${MAIN##*/} $REMOTE_DIR/ctl && /bin/sh $REMOTE_DIR/ctl/preinst check; rc=\$?"
[ "$DOCKREMOTE" = yes ] && DRY="$DRY; dpkg-deb -e $REMOTE_DIR/${REMOTE##*/} $REMOTE_DIR/ctl-remote && /bin/sh $REMOTE_DIR/ctl-remote/preinst check || rc=1"
set +e
rssh "$DRY; exit \$rc"; drc=$?
set -e
if [ "$CHECK_ONLY" = 1 ]; then
  echo "== --check: nothing was installed (dry run exit $drc). The packages stay in $REMOTE_DIR."
  exit "$drc"
fi
[ "$drc" = 0 ] || { echo "install.sh: the dry run reports the device NOT ready (exit $drc): not installing" >&2; exit 1; }
if [ "$YES" != 1 ]; then
  printf 'Install%s on %s now? [y/N] ' "$( for f in $FILES; do printf ' %s' "${f##*/}"; done )" "$DEVICE"
  read -r answer
  case "$answer" in y|Y|yes) ;; *) echo "not installed. The packages stay in $REMOTE_DIR."; exit 0 ;; esac
fi

echo "== dpkg -i on the device"
ENVS=""; [ "$RESPRING" = 1 ] && ENVS="DOCKRECEIVER_RESPRING=1 "
set +e
rssh "${ENVS}dpkg -i$NAMES"; irc=$?
set -e
if [ "$irc" != 0 ]; then
  echo "install.sh: dpkg -i failed (exit $irc). The old setup was left running; see the messages above and" >&2
  echo "  /var/mobile/dockreceiver/install.log. To remove the packages and restore everything: dock-receiver-setup rollback" >&2
  exit 1
fi
echo "== done. Still to do on the device:"
[ "$RESPRING" = 1 ] || echo "  - restart SpringBoard when nothing plays: ssh $DEVICE dock-receiver-setup respring"
[ "$DOCKREMOTE" = yes ] && echo "  - the dock remote tweak: lift the phone off the dock, ssh $DEVICE killall iapd, wait 5 s, dock again"
echo "  - check: ssh $DEVICE dock-receiver-setup check"
