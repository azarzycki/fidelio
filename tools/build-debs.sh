#!/usr/bin/env bash
# tools/build-debs.sh [--arm-out DIR] [--out DIR] [--no-repro-check] [REPO]
#
# Builds the two Debian packages of the suite from the output of tools/build-arm.sh
# (packaging/README.md):
#   dock-receiver_<version>_iphoneos-arm.deb             every supported OS: the daemons, dock-lockctl, the
#                                                         launchd jobs, NowPlaying.app, the lock plugin under
#                                                         both bundle names, licences and notices
#   dock-receiver-dockremote_<version>_iphoneos-arm.deb  iPhone OS 3.x: the DockRemote tweak in iapd
# into OUT (default $BUILD_ROOT/debs), with OUT/SHA256SUMS.
#
# The format the devices' dpkg 1.18 reads (they have gzip, but no xz and no zstd): an ar archive of
# debian-binary ("2.0"), control.tar.gz and data.tar.gz, both gzip. The archives are assembled here, not
# by dpkg-deb, so that every byte is fixed:
#   * every mtime is SOURCE_DATE_EPOCH (default PACKAGE_SOURCE_DATE_EPOCH in tools/_common.sh), in the
#     tar members and in the ar headers;
#   * members sorted by name, numeric owners root 0:0, explicit modes, GNU tar format, no xattrs/ACLs;
#   * gzip -9n (no name, no time stamp).
# Building twice gives the same sha256: the script builds a second time into a temporary directory and
# compares, unless --no-repro-check.
#
# The version is packaging/VERSION (<NowPlaying's version>-<packaging revision>); its first part must be
# the app's CFBundleShortVersionString. The binaries are copied as built (their sha256 is checked against
# ARM_OUT/SHA256SUMS) and the build must have passed its verification (ARM_OUT/verify/.bad = 0).
#
# Exit status: 0 built (and reproducible); 1 the second build differs; 3 setup error.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

ARM_OUT_ARG=""; OUT_ARG=""; REPRO=1; REPO_ARG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --arm-out) ARM_OUT_ARG="$2"; shift 2 ;;
    --out) OUT_ARG="$2"; shift 2 ;;
    --no-repro-check) REPRO=0; shift ;;
    -h|--help) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) die "unknown option $1" ;;
    *) REPO_ARG="$1"; shift ;;
  esac
done
REPO="$(resolve_repo "$REPO_ARG")"
init_build_root
ARM_OUT="${ARM_OUT_ARG:-${ARM_OUT:-$BUILD_ROOT/arm-out}}"
OUT="${OUT_ARG:-$BUILD_ROOT/debs}"
EPOCH="${SOURCE_DATE_EPOCH:-$PACKAGE_SOURCE_DATE_EPOCH}"
PKG="$REPO/packaging"
need_tools tar gzip sha256sum md5sum find sed git
case "$EPOCH" in ''|*[!0-9]*) die "SOURCE_DATE_EPOCH must be a number (got '$EPOCH')" ;; esac

VERSION="$(sed -n 1p "$PKG/VERSION")"
case "$VERSION" in [0-9]*.[0-9]*-[0-9]*) ;; *) die "packaging/VERSION is not <version>-<revision>: '$VERSION'" ;; esac

# --- the build output ---------------------------------------------------------------------------------
[ -d "$ARM_OUT" ] || die "no ARM build output at $ARM_OUT: run tools/build-arm.sh first"
[ "$(cat "$ARM_OUT/verify/.bad" 2>/dev/null || echo missing)" = 0 ] || die "$ARM_OUT did not pass tools/build-arm.sh's verification (verify/.bad): not packaging it"
MAIN_BINS="dock-audiod dock-raop cspot-ios3 dock-lockctl NowPlaying.app/NowPlaying NowPlaying.bundle/NowPlaying NowPlaying.lockbundle/NowPlaying"
for b in $MAIN_BINS DockRemote.dylib; do
  [ -f "$ARM_OUT/$b" ] || die "missing $ARM_OUT/$b"
  want="$(sed -n "s|^\([0-9a-f]\{64\}\)  $b\$|\1|p" "$ARM_OUT/SHA256SUMS" 2>/dev/null)"
  [ -n "$want" ] || die "$b is not listed in $ARM_OUT/SHA256SUMS"
  [ "$(sha256sum < "$ARM_OUT/$b" | cut -d' ' -f1)" = "$want" ] || die "$ARM_OUT/$b does not match its SHA256SUMS line"
done
[ -f "$ARM_OUT/DockRemote.plist" ] || die "missing $ARM_OUT/DockRemote.plist"
app_version="$(sed -n '/<key>CFBundleShortVersionString<\/key>/{n;s/.*<string>\([^<]*\)<\/string>.*/\1/p;}' "$ARM_OUT/NowPlaying.app/Info.plist")"
[ "${VERSION%-*}" = "$app_version" ] || die "packaging/VERSION ($VERSION) does not start with NowPlaying's version ($app_version)"

# The commit SOURCE-cspot-ios3.txt names. Outside a git checkout (a source tarball) there is none: the
# packages say so instead of naming one.
if git -C "$REPO" rev-parse --verify -q HEAD >/dev/null 2>&1; then
  COMMIT="$(git -C "$REPO" rev-parse HEAD)"
  DIRTY="$(git -C "$REPO" status --short | wc -l)"
else
  COMMIT="(unknown: packaged outside a git checkout of the repository)"
  DIRTY=0
  echo "WARNING: $REPO is not a git checkout: SOURCE-cspot-ios3.txt cannot name the commit" >&2
fi

# --- helpers -------------------------------------------------------------------------------------------
put() {  # SRC DEST MODE: copy one file into the staging tree
  mkdir -p "$(dirname "$2")"; cp "$1" "$2"; chmod "$3" "$2"
}
put_tree() {  # SRCDIR DESTDIR: a bundle; executables 0755, the rest 0644, directories 0755
  local f rel
  mkdir -p "$2"
  ( cd "$1" && find . -type f | LC_ALL=C sort ) | while IFS= read -r rel; do
    f="$1/${rel#./}"
    if [ -x "$f" ]; then put "$f" "$2/${rel#./}" 755; else put "$f" "$2/${rel#./}" 644; fi
  done
}
licences() {  # DESTDIR COMPONENT-FILTER(ERE on MANIFEST column 2, or "")
  local path comp rest
  mkdir -p "$1"
  while IFS=$'\t' read -r path comp rest; do
    case "$path" in ''|'#'*) continue ;; esac
    [ -z "$2" ] || printf '%s\n' "$comp" | grep -qE "$2" || continue
    [ -f "$REPO/$path" ] || die "packaging/licenses/MANIFEST.tsv names $path, which does not exist"
    put "$REPO/$path" "$1/$(basename "$path")" 644
  done < "$PKG/licenses/MANIFEST.tsv"
}
subst() {  # FILE: the build's values into a script or control file
  sed -i -e "s|@VERSION@|$VERSION|g" "$1"
}
installed_size() {  # STAGE: KB, each file rounded up, plus one per directory
  local total=0 s
  while read -r s; do total=$((total + (s + 1023) / 1024)); done < <(find "$1" -type f -printf '%s\n')
  echo $((total + $(find "$1" -mindepth 1 -type d | wc -l)))
}
tar_gz() {  # DIR OUTFILE: a reproducible gzip'ed tar of DIR's contents (./ first, sorted)
  chmod 755 "$1"
  tar --create --file=- --directory="$1" --sort=name --mtime="@$EPOCH" --owner=0 --group=0 --numeric-owner \
      --format=gnu --no-acls --no-xattrs --no-selinux . | gzip -9n > "$2"
}
ar_member() {  # ARCHIVE NAME FILE: one member, with a fixed header (mtime EPOCH, root, 0644)
  local size; size="$(stat -c %s "$3")"
  printf '%-16s%-12s%-6s%-6s%-8s%-10s`\n' "$2" "$EPOCH" 0 0 100644 "$size" >> "$1"
  cat "$3" >> "$1"
  [ $((size % 2)) = 0 ] || printf '\n' >> "$1"
}
assemble() {  # NAME STAGE CONTROLDIR OUTDIR: the .deb
  local name="$1" stage="$2" ctl="$3" out="$4" w
  w="$(mktemp -d)"
  ( cd "$stage" && find . -type f | LC_ALL=C sort | sed 's|^\./||' | while IFS= read -r f; do md5sum "$f"; done ) > "$ctl/md5sums"
  chmod 644 "$ctl/md5sums"
  sed -i "s|@INSTALLED_SIZE@|$(installed_size "$stage")|" "$ctl/control"
  tar_gz "$ctl" "$w/control.tar.gz"
  tar_gz "$stage" "$w/data.tar.gz"
  printf '2.0\n' > "$w/debian-binary"
  local deb="$out/${name}_${VERSION}_iphoneos-arm.deb"
  rm -f "$deb"; printf '!<arch>\n' > "$deb"
  ar_member "$deb" debian-binary "$w/debian-binary"
  ar_member "$deb" control.tar.gz "$w/control.tar.gz"
  ar_member "$deb" data.tar.gz "$w/data.tar.gz"
  rm -rf "$w"
}
maint_scripts() {  # SRC CONTROLDIR: the one script as preinst, postinst, prerm and postrm
  local s
  for s in preinst postinst prerm postrm; do put "$1" "$2/$s" 755; subst "$2/$s"; done
}

source_note() {  # FILE: where cspot-ios3's source is (RENAME-PLAN §6)
  cat > "$1" <<EOF
cspot-ios3: the source of this binary
=====================================

/usr/libexec/dock-receiver/cspot-ios3 is distributed under the GNU General Public License,
version 3 or later; the text is licenses/gcc.COPYING3.GPL-3.0.txt in this directory (the GPL v3
text, as shipped with the toolchain's notices). The project's own source files in it remain
available under the MIT licence (LICENSE).

Its complete source:
  * this project's repository, https://github.com/azarzycki/fidelio, at commit
    $COMMIT$( [ "$DIRTY" = 0 ] || printf '%s' " (packaged from a working tree with uncommitted changes)")
    (spotify/cli-src, common/, audiod/proto.h; tools/build-arm.sh builds it);
  * the Spotify library: $CSPOT_UPSTREAM_URL
    at commit $CSPOT_UPSTREAM_COMMIT, directory $CSPOT_UPSTREAM_SUBDIR,
    with the patch $CSPOT_PATCH_REL of the repository above applied (tools/_common.sh);
  * Mbed TLS $MBEDTLS_TAG ($MBEDTLS_URL, commit $MBEDTLS_COMMIT);
  * built with the armv6-ios3 toolchain, release toolchain-r3 of the repository above
    (toolchain/README.md; its own notices: toolchain/THIRD-PARTY-NOTICES.md).

The other components and their licences: THIRD-PARTY-NOTICES.md in this directory.
EOF
}

# --- one build ----------------------------------------------------------------------------------------
build_into() {  # OUTDIR
  local out="$1" w s c
  mkdir -p "$out"
  w="$(mktemp -d)"
  umask 022

  # dock-receiver
  s="$w/main/data"; c="$w/main/control"
  mkdir -p "$s" "$c"
  for b in dock-audiod dock-raop cspot-ios3; do put "$ARM_OUT/$b" "$s/usr/libexec/dock-receiver/$b" 755; done
  put "$PKG/dock-receiver/cspot-run" "$s/usr/libexec/dock-receiver/cspot-run" 755
  put "$PKG/dock-receiver/keepalive" "$s/usr/libexec/dock-receiver/keepalive" 755
  put "$ARM_OUT/dock-lockctl" "$s/usr/bin/dock-lockctl" 755
  put "$PKG/dock-receiver/setup.sh" "$s/usr/sbin/dock-receiver-setup" 755; subst "$s/usr/sbin/dock-receiver-setup"
  for l in audiod raop cspot keepalive; do
    put "$PKG/dock-receiver/launchd/com.dockreceiver.$l.plist" "$s/usr/share/dock-receiver/launchd/com.dockreceiver.$l.plist" 644
  done
  put_tree "$ARM_OUT/NowPlaying.app" "$s/Applications/NowPlaying.app"
  put_tree "$ARM_OUT/NowPlaying.bundle" "$s/System/Library/SpringBoardPlugins/NowPlaying.bundle"
  put_tree "$ARM_OUT/NowPlaying.lockbundle" "$s/System/Library/SpringBoardPlugins/NowPlaying.lockbundle"
  local doc="$s/usr/share/doc/dock-receiver"
  put "$PKG/README.md" "$doc/README.md" 644
  put "$REPO/LICENSE" "$doc/LICENSE" 644
  put "$PKG/THIRD-PARTY-NOTICES.md" "$doc/THIRD-PARTY-NOTICES.md" 644
  source_note "$doc/SOURCE-cspot-ios3.txt"; chmod 644 "$doc/SOURCE-cspot-ios3.txt"
  licences "$doc/licenses" ""
  put "$PKG/dock-receiver/control.in" "$c/control" 644; subst "$c/control"
  maint_scripts "$PKG/dock-receiver/setup.sh" "$c"
  find "$s" -type d -exec chmod 755 {} +
  assemble dock-receiver "$s" "$c" "$out"

  # dock-receiver-dockremote
  s="$w/remote/data"; c="$w/remote/control"
  mkdir -p "$s" "$c"
  put "$ARM_OUT/DockRemote.dylib" "$s/Library/MobileSubstrate/DynamicLibraries/DockRemote.dylib" 644
  put "$ARM_OUT/DockRemote.plist" "$s/Library/MobileSubstrate/DynamicLibraries/DockRemote.plist" 644
  doc="$s/usr/share/doc/dock-receiver-dockremote"
  put "$REPO/LICENSE" "$doc/LICENSE" 644
  put "$PKG/THIRD-PARTY-NOTICES.md" "$doc/THIRD-PARTY-NOTICES.md" 644
  licences "$doc/licenses" "$DOCKREMOTE_LICENCE_COMPONENTS"
  put "$PKG/dock-receiver-dockremote/control.in" "$c/control" 644; subst "$c/control"
  maint_scripts "$PKG/dock-receiver-dockremote/scripts.sh" "$c"
  find "$s" -type d -exec chmod 755 {} +
  assemble dock-receiver-dockremote "$s" "$c" "$out"

  rm -rf "$w"
  ( cd "$out" && sha256sum dock-receiver_"$VERSION"_iphoneos-arm.deb dock-receiver-dockremote_"$VERSION"_iphoneos-arm.deb > SHA256SUMS )
}

# The components whose licence texts go into the dock-remote package as well (MANIFEST.tsv column 2):
# DockRemote.dylib links only the start-up object dylib1.o, from Csu's dyld_glue.s (APSL 2.0;
# packaging/THIRD-PARTY-NOTICES.md).
DOCKREMOTE_LICENCE_COMPONENTS='dyld_glue'

note "packaging $VERSION from $ARM_OUT (repo $(repo_describe "$REPO"); SOURCE_DATE_EPOCH=$EPOCH)"
rm -f "$OUT"/dock-receiver_*_iphoneos-arm.deb "$OUT"/dock-receiver-dockremote_*_iphoneos-arm.deb "$OUT/SHA256SUMS"
build_into "$OUT"
for f in "$OUT"/*.deb; do note "$(basename "$f"): $(stat -c %s "$f") bytes, sha256 $(sha256sum < "$f" | cut -d' ' -f1)"; done
if [ "$REPRO" = 1 ]; then
  T2="$(mktemp -d)"
  build_into "$T2"
  if ( cd "$OUT" && sha256sum -c --quiet "$T2/SHA256SUMS" ) >/dev/null 2>&1 && cmp -s "$OUT/SHA256SUMS" "$T2/SHA256SUMS"; then
    note "reproducible: a second build gave the same sha256 for both packages"
    rm -rf "$T2"
  else
    echo "ERROR: a second build differs: compare $OUT and $T2" >&2
    exit 1
  fi
fi
note "done: $OUT/SHA256SUMS"
