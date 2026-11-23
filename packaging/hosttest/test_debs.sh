#!/usr/bin/env bash
# packaging/hosttest/test_debs.sh ARM_OUT [REPO]
#
# Checks the two packages tools/build-debs.sh makes from the real ARM build output ARM_OUT
# (tools/build-arm.sh), and packaging/install.sh with stand-ins for ssh and scp:
#   * the format the devices' dpkg 1.18 reads: an ar archive of debian-binary 2.0, control.tar.gz and
#     data.tar.gz, both gzip; dpkg-deb -I / -c read them;
#   * owners (0:0), modes and paths as intended; the binaries are byte for byte the build's;
#   * no secret and no address in any file; the control fields;
#   * two builds give the same sha256 (build-debs.sh's own check, and a third build here);
#   * install.sh: the commands it runs, the legacy ssh options, nothing about a device built in.
# Output: one `ok <text>` or `FAIL <text>` line per check.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARM_OUT="${1:?usage: test_debs.sh ARM_OUT [REPO]}"
REPO="${2:-$(cd "$HERE/../.." && pwd)}"
T="$(mktemp -d /tmp/dr-debtest.XXXXXX)"
[ -n "${KEEP_TMP:-}" ] || trap 'rm -rf "$T"' EXIT
NOK=0; NFAIL=0
ok()   { NOK=$((NOK + 1)); echo "ok   $*"; }
bad()  { NFAIL=$((NFAIL + 1)); echo "FAIL $*"; }
check() { local d="$1" o; shift; if o="$("$@" 2>&1)"; then ok "$d"; else bad "$d"; [ -z "$o" ] || printf '%s\n' "$o" | sed -n '1,5s/^/    /p'; fi; }

[ -f "$ARM_OUT/SHA256SUMS" ] || { echo "FAIL no ARM build output in $ARM_OUT (run tools/build-arm.sh first)"; exit 1; }
VERSION="$(sed -n 1p "$REPO/packaging/VERSION")"
EPOCH="$(sed -n 's/^PACKAGE_SOURCE_DATE_EPOCH=//p' "$REPO/tools/_common.sh")"
WHEN="$(TZ=UTC date -d "@$EPOCH" '+%Y-%m-%d %H:%M:%S')"

if BUILD_ROOT="$T/br" "$REPO/tools/build-debs.sh" --arm-out "$ARM_OUT" --out "$T/debs" "$REPO" > "$T/build.log" 2>&1; then
  ok "build-debs.sh: built, and its second build gave the same sha256"
else
  bad "build-debs.sh failed: $(tail -n 3 "$T/build.log")"; echo "TOTAL $NOK ok, $NFAIL FAIL"; exit 1
fi
check "build-debs.sh reports the reproducibility check" grep -q "reproducible: a second build gave the same sha256" "$T/build.log"
BUILD_ROOT="$T/br" "$REPO/tools/build-debs.sh" --arm-out "$ARM_OUT" --out "$T/debs2" --no-repro-check "$REPO" > "$T/build2.log" 2>&1
check "a third, separate build gives identical files" cmp "$T/debs/SHA256SUMS" "$T/debs2/SHA256SUMS"
check "SHA256SUMS lists both packages and matches them" eval '( cd "$T/debs" && sha256sum -c --quiet SHA256SUMS ) && [ "$(grep -c . "$T/debs/SHA256SUMS")" = 2 ]'
MAIN="$T/debs/dock-receiver_${VERSION}_iphoneos-arm.deb"
REMOTE="$T/debs/dock-receiver-dockremote_${VERSION}_iphoneos-arm.deb"
check "the package files are named <name>_<version>_iphoneos-arm.deb" eval '[ -f "$MAIN" ] && [ -f "$REMOTE" ]'

for deb in "$MAIN" "$REMOTE"; do
  n="$(basename "$deb" | sed 's/_.*//')"
  x="$T/x-$n"; mkdir -p "$x"
  check "$n: ar t shows debian-binary, control.tar.gz, data.tar.gz in that order" [ "$(ar t "$deb" | tr '\n' ' ')" = "debian-binary control.tar.gz data.tar.gz " ]
  ( cd "$x" && ar x "$deb" )
  check "$n: debian-binary is 2.0" [ "$(cat "$x/debian-binary")" = "2.0" ]
  check "$n: both tar members are gzip (magic 1f8b, gzip -t)" eval 'for m in control data; do [ "$(od -An -tx1 -N2 "$x/$m.tar.gz" | tr -d " ")" = 1f8b ] && gzip -t "$x/$m.tar.gz" || exit 1; done'
  check "$n: the gzip headers carry no name and no time stamp" eval 'for m in control data; do [ "$(od -An -tx1 -j3 -N5 "$x/$m.tar.gz" | tr -d " ")" = 0000000000 ] || exit 1; done'
  check "$n: the ar headers: mtime SOURCE_DATE_EPOCH, owner 0/0, mode 100644" eval 'head -c 68 "$deb" | tail -c 60 | grep -q "^debian-binary   $EPOCH  0     0     100644  4         \`"'
  check "$n: dpkg-deb -I reads it" dpkg-deb -I "$deb"
  check "$n: dpkg-deb -c reads it" dpkg-deb -c "$deb"
  dpkg-deb -I "$deb" control > "$x/control"
  check "$n: Architecture iphoneos-arm, Version $VERSION" eval 'grep -qx "Architecture: iphoneos-arm" "$x/control" && grep -qx "Version: $VERSION" "$x/control"'
  check "$n: every member owned by 0/0, mtime fixed" eval '! dpkg-deb -c "$deb" | grep -v " 0/0 " && ! dpkg-deb --fsys-tarfile "$deb" | TZ=UTC tar -t -v --full-time -f - | grep -v " $WHEN "'
  check "$n: control members: control/md5sums 0644, maintainer scripts 0755" eval '[ "$(tar -tvzf "$x/control.tar.gz" | sed -n "s/^\([^ ]*\) .* \.\/\([a-z0-9][a-z0-9]*\)$/\1 \2/p" | LC_ALL=C sort | tr "\n" ",")" = "-rw-r--r-- control,-rw-r--r-- md5sums,-rwxr-xr-x postinst,-rwxr-xr-x postrm,-rwxr-xr-x preinst,-rwxr-xr-x prerm," ]'
  check "$n: md5sums match the data" eval 'mkdir -p "$x/root" && tar -xzf "$x/data.tar.gz" -C "$x/root" && tar -xzf "$x/control.tar.gz" -C "$x" ./md5sums && ( cd "$x/root" && md5sum -c --quiet ../md5sums )'
  check "$n: directories 0755, no setuid, no device files, no symlinks" eval '! dpkg-deb -c "$deb" | grep -v -e "^drwxr-xr-x " -e "^-rw-r--r-- " -e "^-rwxr-xr-x "'
  # nothing private: addresses, passwords, home paths, session ids, credential values
  check "$n: no private address, Windows home path or session id in any file (binaries included)" eval '! grep -r -a -l -i -E "10\.0\.0\.|192\.168\.|/Users/|session_0" "$x/root" "$x/control.tar.gz" "$x/control"'
  check "$n: no password or secret value in any text file" eval '! grep -r -I -l -i -E "passw(or)?d *[=:] *[A-Za-z0-9]|secret *= *[A-Za-z0-9]{8}" "$x/root"'
  check "$n: no filled-in credential (only the template's empty assignments)" eval '! grep -r -a -h -E "SPOTIFY_CLIENT_(ID|SECRET)=[A-Za-z0-9]" "$x/root"'
  check "$n: no IPv4 address at all outside the licence texts" eval '! grep -r -a -l -E "\b([0-9]{1,3}\.){3}[0-9]{1,3}\b" "$x/root" --exclude-dir=licenses --exclude="*.dylib" --exclude=dock-audiod --exclude=dock-raop --exclude=cspot-ios3 --exclude=dock-lockctl --exclude=NowPlaying | grep -v -e THIRD-PARTY-NOTICES.md'
done

M="$T/x-dock-receiver/root"; RM="$T/x-dock-receiver-dockremote/root"
want_main="Applications/NowPlaying.app/
System/Library/SpringBoardPlugins/NowPlaying.bundle/Info.plist
System/Library/SpringBoardPlugins/NowPlaying.bundle/NowPlaying
System/Library/SpringBoardPlugins/NowPlaying.lockbundle/Info.plist
System/Library/SpringBoardPlugins/NowPlaying.lockbundle/NowPlaying
usr/bin/dock-lockctl
usr/libexec/dock-receiver/cspot-ios3
usr/libexec/dock-receiver/cspot-run
usr/libexec/dock-receiver/dock-audiod
usr/libexec/dock-receiver/dock-raop
usr/libexec/dock-receiver/keepalive
usr/sbin/dock-receiver-setup
usr/share/doc/dock-receiver/
usr/share/dock-receiver/launchd/com.dockreceiver.audiod.plist
usr/share/dock-receiver/launchd/com.dockreceiver.cspot.plist
usr/share/dock-receiver/launchd/com.dockreceiver.keepalive.plist
usr/share/dock-receiver/launchd/com.dockreceiver.raop.plist"
got_main="$( cd "$M" && find . -type f | sed 's|^\./||' | sed -e 's|^\(Applications/NowPlaying.app/\).*|\1|' -e 's|^\(usr/share/doc/dock-receiver/\).*|\1|' | LC_ALL=C sort -u )"
check "main: exactly the intended paths (app, both plugin bundles, daemons, tool, setup, job templates, docs)" [ "$got_main" = "$want_main" ]
check "main: nothing in /Library/LaunchDaemons (the jobs are installed by the postinst, at the switch)" [ ! -e "$M/Library" ]
check "main: no voltest, no SDK file, no jailbreak file" eval '! find "$M" -name "voltest*" -o -name "*.sdk" -o -name "SDKSettings.plist" | grep -q .'
for b in dock-audiod dock-raop cspot-ios3; do
  check "main: usr/libexec/dock-receiver/$b is the build's (sha256), 0755" eval '[ "$(sha256sum < "$M/usr/libexec/dock-receiver/$b" | cut -d" " -f1)" = "$(sed -n "s/  $b\$//p" "$ARM_OUT/SHA256SUMS")" ] && [ "$(stat -c %a "$M/usr/libexec/dock-receiver/$b")" = 755 ]'
done
check "main: usr/bin/dock-lockctl is the build's" [ "$(sha256sum < "$M/usr/bin/dock-lockctl" | cut -d' ' -f1)" = "$(sed -n 's/  dock-lockctl$//p' "$ARM_OUT/SHA256SUMS")" ]
check "main: the app's binary is the build's" [ "$(sha256sum < "$M/Applications/NowPlaying.app/NowPlaying" | cut -d' ' -f1)" = "$(sed -n 's|  NowPlaying.app/NowPlaying$||p' "$ARM_OUT/SHA256SUMS")" ]
check "main: the app bundle is complete (every file of the build)" eval 'diff -r "$ARM_OUT/NowPlaying.app" "$M/Applications/NowPlaying.app"'
check "main: both lock plugin bundles are the build's" eval 'diff -r "$ARM_OUT/NowPlaying.bundle" "$M/System/Library/SpringBoardPlugins/NowPlaying.bundle" && diff -r "$ARM_OUT/NowPlaying.lockbundle" "$M/System/Library/SpringBoardPlugins/NowPlaying.lockbundle"'
check "main: the app's id is com.dockreceiver.nowplaying" grep -q "<string>com.dockreceiver.nowplaying</string>" "$M/Applications/NowPlaying.app/Info.plist"
check "main: the setup command and the maintainer scripts are the same script, version filled in" eval 'cmp "$M/usr/sbin/dock-receiver-setup" "$T/x-dock-receiver/preinst" 2>/dev/null || { tar -xzf "$T/x-dock-receiver/control.tar.gz" -C "$T/x-dock-receiver" ./preinst && cmp "$M/usr/sbin/dock-receiver-setup" "$T/x-dock-receiver/preinst"; } && grep -q "^DR_VERSION=\"$VERSION\"" "$M/usr/sbin/dock-receiver-setup"'
for l in audiod raop cspot keepalive; do
  p="$M/usr/share/dock-receiver/launchd/com.dockreceiver.$l.plist"
  check "main: job $l: label, user mobile, KeepAlive, RunAtLoad, log in the data dir" eval 'grep -q "<string>com.dockreceiver.$l</string>" "$p" && grep -A1 UserName "$p" | grep -q "<string>mobile</string>" && grep -A1 "<key>KeepAlive</key>" "$p" | grep -q "<true/>" && grep -A1 "<key>RunAtLoad</key>" "$p" | grep -q "<true/>" && grep -q "<string>/var/mobile/dockreceiver/logs/$l.log</string>" "$p"'
done
check "main: the jobs run the packaged programs, no option on a command line" eval '[ "$(grep -h -A2 "<key>ProgramArguments</key>" "$M"/usr/share/dock-receiver/launchd/*.plist | grep -c "<string>")" = 4 ]'
check "main: python's plistlib reads every job plist" python3 -I -c 'import plistlib,sys,glob; [plistlib.load(open(f,"rb")) for f in glob.glob(sys.argv[1]+"/*.plist")]' "$M/usr/share/dock-receiver/launchd"
D="$M/usr/share/doc/dock-receiver"
check "main: the MIT LICENSE, the notices, the README" eval 'cmp "$REPO/LICENSE" "$D/LICENSE" && cmp "$REPO/packaging/THIRD-PARTY-NOTICES.md" "$D/THIRD-PARTY-NOTICES.md" && cmp "$REPO/packaging/README.md" "$D/README.md"'
check "main: the GPL-3 text and the source pointer for cspot-ios3" eval 'grep -q "GNU GENERAL PUBLIC LICENSE" "$D/licenses/gcc.COPYING3.GPL-3.0.txt" && grep -q "Version 3, 29 June 2007" "$D/licenses/gcc.COPYING3.GPL-3.0.txt" && grep -q "ee1720c7556ffec5f43329aadd47c20546bdbb79" "$D/SOURCE-cspot-ios3.txt" && grep -q "spotify/patches/cspot-squeezelite-ee1720c-0001-armv6-ios3.patch" "$D/SOURCE-cspot-ios3.txt" && grep -q "$(git -C "$REPO" rev-parse HEAD)" "$D/SOURCE-cspot-ios3.txt"'
check "main: every licence text of MANIFEST.tsv is in the package, byte for byte" eval 'while IFS="	" read -r p c rest; do [ -n "$p" ] || continue; cmp "$REPO/$p" "$D/licenses/$(basename "$p")" || exit 1; done < "$REPO/packaging/licenses/MANIFEST.tsv"'
check "main: the start-up code notice is in the notices file" grep -q -i "start-up code" "$D/THIRD-PARTY-NOTICES.md"
check "main: Depends on the firmware only" grep -qx "Depends: firmware (>= 3.0)" "$T/x-dock-receiver/control"
check "dockremote: exactly the tweak, its filter and its docs" [ "$( cd "$RM" && find . -type f | sed 's|^\./||' | LC_ALL=C sort | tr '\n' ' ')" = "Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib Library/MobileSubstrate/DynamicLibraries/DockRemote.plist usr/share/doc/dock-receiver-dockremote/LICENSE usr/share/doc/dock-receiver-dockremote/THIRD-PARTY-NOTICES.md usr/share/doc/dock-receiver-dockremote/licenses/cctools-port.APSL-2.0.txt " ]
check "dockremote: DockRemote.dylib is the build's, mode 0644 as on the phone" eval '[ "$(sha256sum < "$RM/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib" | cut -d" " -f1)" = "$(sed -n "s/  DockRemote.dylib\$//p" "$ARM_OUT/SHA256SUMS")" ] && [ "$(stat -c %a "$RM/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib")" = 644 ]'
check "dockremote: Depends on dock-receiver (= $VERSION) and iPhone OS 3 by the firmware pseudo-package, not on mobilesubstrate" eval 'grep -qx "Depends: dock-receiver (= $VERSION), firmware (>= 3.0), firmware (<< 4.0)" "$T/x-dock-receiver-dockremote/control" && ! grep "^Depends:" "$T/x-dock-receiver-dockremote/control" | grep -qi substrate'
check "dockremote: its maintainer scripts never restart iapd" eval 'tar -xzf "$T/x-dock-receiver-dockremote/control.tar.gz" -C "$T/x-dock-receiver-dockremote" ./postinst && ! sed "s/^[[:space:]]*#.*//" "$T/x-dock-receiver-dockremote/postinst" | grep -E "^[^\"]*killall[^\"]*iapd"'
check "main: its maintainer scripts never restart SpringBoard on their own (only behind DOCKRECEIVER_RESPRING=1 or the respring command)" eval '[ "$(grep -c "^[[:space:]]*killall SpringBoard" "$M/usr/sbin/dock-receiver-setup")" = 1 ] && grep -q -F "if [ \"\${DOCKRECEIVER_RESPRING:-0}\" = 1 ]; then cmd_respring; fi" "$M/usr/sbin/dock-receiver-setup"'
check "installed size fits: under 5 MB for the main package" eval '[ "$(sed -n "s/^Installed-Size: //p" "$T/x-dock-receiver/control")" -lt 5120 ]'

# ===================================================================================================
echo "# install.sh with stand-ins for ssh and scp"
mkdir -p "$T/fakebin"
cat > "$T/fakebin/fssh" <<'EOF'
#!/bin/bash
printf 'ssh' >> "$CALLS"; printf ' [%s]' "$@" >> "$CALLS"; printf '\n' >> "$CALLS"
case "${@: -1}" in
  *SystemVersion.plist*) echo "${FAKE_OS:-3.1.3}" ;;
  *"preinst check"*) echo "result: ready"; exit "${FAKE_CHECK_RC:-0}" ;;
esac
exit 0
EOF
cat > "$T/fakebin/fscp" <<'EOF'
#!/bin/bash
printf 'scp' >> "$CALLS"; printf ' [%s]' "$@" >> "$CALLS"; printf '\n' >> "$CALLS"
exit 0
EOF
chmod 755 "$T/fakebin/fssh" "$T/fakebin/fscp"
export CALLS="$T/calls.log"
: > "$CALLS"; SSH="$T/fakebin/fssh" SCP="$T/fakebin/fscp" "$REPO/packaging/install.sh" --debs "$T/debs" --yes root@phone.invalid > "$T/inst.log" 2>&1; rc=$?
check "install.sh (phone): exits 0" [ "$rc" = 0 ]
check "install.sh: ssh gets the legacy options for OpenSSH 6.7" grep -q "\[-o\] \[HostKeyAlgorithms=+ssh-rsa\] \[-o\] \[PubkeyAcceptedAlgorithms=+ssh-rsa\] \[root@phone.invalid\]" "$CALLS"
check "install.sh (phone): copies both packages" grep -q "^scp .*dock-receiver_${VERSION}_iphoneos-arm.deb\] \[.*dock-receiver-dockremote_${VERSION}_iphoneos-arm.deb\] \[root@phone.invalid:/var/tmp/dock-receiver-install/\]" "$CALLS"
check "install.sh (phone): dry runs of both packages before dpkg -i" eval 'grep -n "preinst check" "$CALLS" | grep -q "ctl-remote/preinst check" && [ "$(grep -n "preinst check" "$CALLS" | cut -d: -f1)" -lt "$(grep -n "dpkg -i" "$CALLS" | cut -d: -f1)" ]'
check "install.sh (phone): dpkg -i of both, main first" grep -q "\[dpkg -i /var/tmp/dock-receiver-install/dock-receiver_${VERSION}_iphoneos-arm.deb /var/tmp/dock-receiver-install/dock-receiver-dockremote_${VERSION}_iphoneos-arm.deb\]" "$CALLS"
check "install.sh (phone): tells the owner about the respring and the undocked iapd restart" eval 'grep -q "restart SpringBoard" "$T/inst.log" && grep -q "lift the phone off the dock" "$T/inst.log"'
: > "$CALLS"; FAKE_OS=5.1.1 SSH="$T/fakebin/fssh" SCP="$T/fakebin/fscp" "$REPO/packaging/install.sh" --debs "$T/debs" --yes root@ipad.invalid > "$T/inst.log" 2>&1; rc=$?
check "install.sh (iOS 5): only the main package" eval '[ "$rc" = 0 ] && ! grep -q dockremote "$CALLS" && grep -q "\[dpkg -i /var/tmp/dock-receiver-install/dock-receiver_${VERSION}_iphoneos-arm.deb\]" "$CALLS"'
: > "$CALLS"; SSH="$T/fakebin/fssh" SCP="$T/fakebin/fscp" "$REPO/packaging/install.sh" --debs "$T/debs" --check root@phone.invalid > "$T/inst.log" 2>&1; rc=$?
check "install.sh --check: dry run only, no dpkg -i" eval '[ "$rc" = 0 ] && grep -q "preinst check" "$CALLS" && ! grep -q "dpkg -i" "$CALLS"'
: > "$CALLS"; FAKE_CHECK_RC=1 SSH="$T/fakebin/fssh" SCP="$T/fakebin/fscp" "$REPO/packaging/install.sh" --debs "$T/debs" --yes root@phone.invalid > "$T/inst.log" 2>&1; rc=$?
check "install.sh: a dry run that is not ready stops it before dpkg -i" eval '[ "$rc" != 0 ] && ! grep -q "dpkg -i" "$CALLS"'
: > "$CALLS"; SSH="$T/fakebin/fssh" SCP="$T/fakebin/fscp" "$REPO/packaging/install.sh" --debs "$T/debs" root@phone.invalid < /dev/null > "$T/inst.log" 2>&1; rc=$?
check "install.sh without --yes and no answer: does not install" eval '! grep -q "dpkg -i" "$CALLS"'
check "install.sh names no address, host, user key or password" eval '! grep -n -i -E "([0-9]{1,3}\.){3}[0-9]{1,3}|passw[^a-z]*=|id_rsa|id_ed25519|\.local\b" "$REPO/packaging/install.sh"'

echo "TOTAL $NOK ok, $NFAIL FAIL"
[ "$NFAIL" = 0 ]
