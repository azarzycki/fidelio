# _common.sh — shared helpers for tools/run-host-tests.sh and tools/build-arm.sh. Sourced, never executed.
#
# Everything these scripts create lives under BUILD_ROOT: by default a "build" directory NEXT TO the
# repo (e.g. ~/src/dock-receiver -> ~/src/build); set BUILD_ROOT to put it elsewhere (never inside the repo).
# They never write into the repo they are pointed at, with ONE exception that the repo's own test
# harness makes:
# airplay/hosttest/raop_sender.py writes airplay/hosttest/airport_express_pub.pem next to itself
# (git-ignored by the repo's root .gitignore); run-host-tests.sh removes it again if it was not
# there before the run.
#
# Layout
#   deps/squeezelite-esp32   pristine upstream, sparse checkout of components/spotify/cspot @ CSPOT_UPSTREAM_COMMIT
#   deps/cspot               that subtree + <repo>/spotify/patches/<CSPOT_PATCH_REL> applied with git am
#   deps/cspot.stamp         "<upstream commit> <sha256 of the patch>" the tree was made from
#   deps/mbedtls             Mbed-TLS @ MBEDTLS_TAG (with its framework submodule)
#   deps/apple-oss/<repo>/<tag>   Apple's open-source releases that toolchain/sysroot/headers/build-headers.sh
#                            reads (sparse checkouts at the commits pinned in its sources.tsv; fetched once)
#   sysroot/root             the project's own sysroot (toolchain/sysroot/build-sysroot.sh), when build-arm.sh
#                            runs without IOS3_SDK; sysroot/stamp names its inputs, sysroot/CONTENTS lists
#                            its files (sha256) and symlinks
#   host/{mbedtls,sysroot}   host build + install of mbedTLS      host/{audiod,airplay,cspot}  host builds
#   arm/{mbedtls,sysroot}    ARMv6 build + install of mbedTLS (arm/sysroot is mbedTLS's install prefix,
#                            not the iPhone OS sysroot)           arm/{audiod,airplay,cspot,nowplaying,lockplugin,dockremote}
#   arm/maps/<step>/*.map    ld64 link maps, one per linked output, written by the toolchain wrapper
#                            (IOS3_LINK_MAP_DIR); build-arm.sh reads them in its verification step
#   tools/nanopb-py/python3  -> a python3 whose protobuf module still has reflection.MakeClass (see below)
#   logs/                    build logs (overwritten per run)

TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DEFAULT="$(cd "$TOOLS_DIR/.." && pwd -P)"   # the repo these scripts live in

# --- pinned upstreams (spotify/CSPOT.md §1-§2 in the repo) -------------------------------------
CSPOT_UPSTREAM_URL="https://github.com/sle118/squeezelite-esp32"
CSPOT_UPSTREAM_COMMIT="ee1720c7556ffec5f43329aadd47c20546bdbb79"   # branch master-v4.3
CSPOT_UPSTREAM_SUBDIR="components/spotify/cspot"
CSPOT_PATCH_REL="spotify/patches/cspot-squeezelite-ee1720c-0001-armv6-ios3.patch"
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls"
MBEDTLS_TAG="v3.6.7"
MBEDTLS_COMMIT="068ff080b369adfac81509f9b57b2afabaf82dc5"
# civetweb (cspot's bell/external/civetweb/civetweb.c) stores __DATE__ in the binary as its build
# identifier, so cspot-ios3 would hash differently on every calendar day. build-arm.sh builds cspot
# with SOURCE_DATE_EPOCH set to this value; clang then expands __DATE__ from it (in UTC).
# 1794744000 = 2026-11-15 12:00 UTC = "Nov 15 2026", the date already inside the published
# cspot-ios3 of B1.18 (sha256 2979aedd...), so that file stays byte-identical to a fresh build.
CSPOT_SOURCE_DATE_EPOCH=1794744000
# The toolchain's start-up objects, <toolchain>/armv6-ios3/lib/startfiles/{crt1.o,dylib1.o,bundle1.o}:
# one of them is the first object of every executable, dylib and bundle. They are built from Apple's
# open-source Csu release (tag Csu-79) by toolchain/startfiles/build-startfiles.sh, which pins the same
# three sha256 values; build-arm.sh checks the installed files against them. These are the objects of
# toolchain revision 3: each carries the source-availability notice of toolchain/startfiles/notice.s
# in its section __TEXT,__csu_notice, and the linker copies it into every binary (tools/startcheck.py
# checks that). dylib1.o and bundle1.o are the same bytes. A change here is a change of the
# toolchain, never a way to make a build pass.
STARTFILES_SHA256="\
crt1.o 29699d8ee4516b27a81ea29691f3e9592a50774976e3dfb746f046c541cbff05
dylib1.o e07e529bd57b927624b4ecfeadd053ea6974eea053238c40b54fb123ac6570fe
bundle1.o e07e529bd57b927624b4ecfeadd053ea6974eea053238c40b54fb123ac6570fe"
# The oldest toolchain revision (the number in <toolchain>/VERSION) build-arm.sh accepts: the first
# one whose start-up objects carry the notice. Revision 1 has no start-up objects of its own, and
# revision 2 has them without the notice.
TOOLCHAIN_MIN_REVISION=3

die()  { echo "ERROR: $*" >&2; exit 3; }
note() { echo "[$(date +%H:%M:%S)] $*"; }

# run CMD..., all output to LOG; on failure show the tail of the log and stop.
logged() {
  local log="$1"; shift
  if ! "$@" >"$log" 2>&1; then
    echo "----- last 40 lines of $log" >&2; tail -n 40 "$log" >&2
    die "step failed: $* (full log: $log)"
  fi
}

APT_PACKAGES="clang-18 llvm-18 cmake ninja-build protobuf-compiler python3-protobuf libavahi-client-dev g++"
# what toolchain/sysroot/build-sysroot.sh needs on top (only when build-arm.sh makes the sysroot itself)
APT_PACKAGES_SYSROOT="unifdef flex bison"

_need_tools() {  # APT-PACKAGES TOOL...
  local hint="$1" t missing=""; shift
  for t in "$@"; do command -v "$t" >/dev/null 2>&1 || missing="$missing $t"; done
  [ -z "$missing" ] || die "missing tools:$missing (Ubuntu 24.04: apt-get install $hint)"
}
need_tools() { _need_tools "$APT_PACKAGES" "$@"; }
# The tools of toolchain/sysroot/build-sysroot.sh (and the two scripts it runs), with their overrides.
need_sysroot_tools() {
  _need_tools "$APT_PACKAGES $APT_PACKAGES_SYSROOT" "${IOS3_HOST_CLANG:-clang-18}" "${HOST_CC:-cc}" \
    "${BISON:-bison}" "${FLEX:-flex}" unifdef git python3 sha256sum awk sed sort find realpath
}

# --- the sysroot (IOS3_SDK) ------------------------------------------------------------------------
# Files that every sysroot of this build has, Apple's iPhoneOS3.1.2.sdk and one made by
# toolchain/sysroot/build-sysroot.sh alike (a stub library, C library, CoreFoundation and framework headers).
SYSROOT_PROBE_FILES="usr/lib/libSystem.B.dylib usr/include/stdlib.h
  System/Library/Frameworks/CoreFoundation.framework/Headers/CoreFoundation.h
  System/Library/Frameworks/Foundation.framework/Headers/Foundation.h
  System/Library/Frameworks/UIKit.framework/Headers/UIKit.h System/Library/Frameworks/UIKit.framework/UIKit"
sysroot_missing() {  # DIR: prints the first probe file DIR lacks; nothing if it has them all
  local f
  for f in $SYSROOT_PROBE_FILES; do [ -f "$1/$f" ] || { echo "$f"; return; }; done
}
# Apple's SDK, as opposed to a sysroot of build-sysroot.sh: it has SDKSettings.plist and the start-up
# object usr/lib/crt1.o, and build-sysroot.sh makes neither. (toolchain/pack.sh uses the same test.)
is_apple_sdk() { [ -f "$1/SDKSettings.plist" ] && [ -f "$1/usr/lib/crt1.o" ]; }
# One line per file (its sha256) and per symlink (its target), sorted by path: what a sysroot holds.
sysroot_listing() {  # DIR
  ( cd "$1" && find . \( -type f -o -type l \) | LC_ALL=C sort | while IFS= read -r p; do
      if [ -L "$p" ]; then printf 'link:%s  %s\n' "$(readlink "$p")" "${p#./}"
      else printf '%s  %s\n' "$(sha256sum < "$p" | cut -d' ' -f1)" "${p#./}"; fi
    done )
}

# The project's own sysroot, $BUILD_ROOT/sysroot/root, made by toolchain/sysroot/build-sysroot.sh. Needs
# REPO, TC (the toolchain directory), BUILD_ROOT, DEPS, LOGS. It is made again only when it is missing,
# when the stamp differs (the contents of toolchain/sysroot/ or the tools that build it changed), or when
# the tree no longer matches its CONTENTS list. A new tree with the same contents as an intact tree in
# place is thrown away, so that the files keep their times and ninja compiles nothing again.
# Apple's releases are fetched into deps/apple-oss once
# (IOS3_OSS_CACHE + IOS3_OSS_CACHE_FILL of build-headers.sh) and read from there afterwards, still
# checked against their pinned commits and sha256; a run that finds the sysroot up to date reads nothing
# from the network. Sets OWN_SYSROOT (the tree) and OWN_SYSROOT_ID (16 hex digits of the sha256 of CONTENTS).
ensure_own_sysroot() {
  local d="$BUILD_ROOT/sysroot" src="$REPO/toolchain/sysroot" want hc
  need_sysroot_tools
  hc="${IOS3_HOST_CLANG:-clang-18}"
  want="inputs: toolchain/sysroot $(cd "$src" && find . -type f ! -name '*.pyc' ! -path '*/__pycache__/*' | LC_ALL=C sort \
      | xargs -d '\n' sha256sum | sha256sum | cut -d' ' -f1)
linker: $TC/bin/arm-apple-darwin9-ld $(sha256sum < "$TC/bin/arm-apple-darwin9-ld" | cut -c1-16)
host clang: $("$hc" --version 2>/dev/null | head -n 1)
host cc: $("${HOST_CC:-cc}" --version 2>/dev/null | head -n 1)
unifdef: $(unifdef -V 2>&1 | head -n 1)
bison: $("${BISON:-bison}" --version 2>/dev/null | head -n 1)
flex: $("${FLEX:-flex}" --version 2>/dev/null | head -n 1)
python3: $(python3 --version 2>&1)"
  OWN_SYSROOT="$d/root"
  local intact=0
  if [ -d "$d/root" ] && [ -f "$d/CONTENTS" ] && sysroot_listing "$d/root" | cmp -s - "$d/CONTENTS"; then intact=1; fi
  if [ "$intact" = 1 ] && [ "$(cat "$d/stamp" 2>/dev/null)" = "$want" ]; then
    OWN_SYSROOT_ID="$(sha256sum < "$d/CONTENTS" | cut -c1-16)"
    note "sysroot: $d/root is up to date (contents $OWN_SYSROOT_ID)"
    return 0
  fi
  if [ ! -d "$d/root" ]; then
    note "sysroot: making $d/root with toolchain/sysroot/build-sysroot.sh (releases cached in $DEPS/apple-oss)"
  elif [ "$intact" = 0 ]; then
    note "sysroot: $d/root does not match its CONTENTS list: making it again"
  else
    note "sysroot: toolchain/sysroot or the tools that build it changed: making $d/root again"
  fi
  # The stamp goes first: a run that fails here leaves none, and the next run tries again.
  rm -rf "$d/root.new" "$d/CONTENTS.new" "$d/stamp"
  mkdir -p "$d" "$DEPS/apple-oss" "$LOGS"
  logged "$LOGS/sysroot.log" env IOS3_OSS_CACHE="$DEPS/apple-oss" IOS3_OSS_CACHE_FILL=1 IOS3_TOOLCHAIN="$TC" \
    "$src/build-sysroot.sh" "$d/root.new"
  sysroot_listing "$d/root.new" > "$d/CONTENTS.new"
  note "sysroot: $(tail -n 1 "$LOGS/sysroot.log" | sed "s/^build-sysroot: //; s|$d/root.new|$d/root|")"
  if [ "$intact" = 1 ] && cmp -s "$d/CONTENTS.new" "$d/CONTENTS"; then
    # Same bytes as the tree in place: keep that one, with its file times, so that nothing compiles again.
    rm -rf "$d/root.new" "$d/CONTENTS.new"
    note "sysroot: the new tree has the same contents as $d/root; the tree in place is kept"
  else
    rm -rf "$d/root"
    mv "$d/root.new" "$d/root"
    mv "$d/CONTENTS.new" "$d/CONTENTS"
  fi
  printf '%s\n' "$want" > "$d/stamp"
  OWN_SYSROOT_ID="$(sha256sum < "$d/CONTENTS" | cut -c1-16)"
  note "sysroot: $d/root: contents $OWN_SYSROOT_ID"
}

# REPO: absolute path of the worktree to build (default: the repo this script lives in)
resolve_repo() {
  local r="${1:-$REPO_DEFAULT}"
  [ -d "$r" ] || die "repo path '$r' is not a directory"
  r="$(cd "$r" && pwd -P)"
  [ -f "$r/audiod/CMakeLists.txt" ] && [ -f "$r/$CSPOT_PATCH_REL" ] || die "'$r' does not look like the dock-receiver monorepo"
  echo "$r"
}

# BUILD_ROOT (default: "build" next to REPO), DEPS and LOGS. Call after REPO is set.
init_build_root() {
  BUILD_ROOT="${BUILD_ROOT:-$(dirname "$REPO")/build}"
  mkdir -p "$BUILD_ROOT"
  BUILD_ROOT="$(cd "$BUILD_ROOT" && pwd -P)"
  case "$BUILD_ROOT/" in "$REPO"/*) die "BUILD_ROOT ($BUILD_ROOT) must be outside the repo";; esac
  DEPS="$BUILD_ROOT/deps"; LOGS="$BUILD_ROOT/logs"
}

repo_describe() {  # one line: HEAD + clean/dirty (read-only git commands)
  local r="$1" head st
  head="$(git -C "$r" rev-parse --short HEAD 2>/dev/null || echo '?')"
  st="$(git -C "$r" status --short 2>/dev/null | wc -l)"
  echo "$r @ $head ($( [ "$st" = 0 ] && echo 'git status clean' || echo "git status: $st entries"))"
}

# A build dir is tied to the source it was configured from. If the stamp differs (another worktree,
# another cspot patch), the build dir is removed so CMake starts clean instead of erroring out.
stamp_dir() {  # DIR STAMP-TEXT
  local d="$1" want="$2"
  if [ -d "$d" ] && [ "$(cat "$d/.source-stamp" 2>/dev/null)" != "$want" ]; then
    note "source changed for $d: removing the stale build dir"
    rm -rf "$d"
  fi
  mkdir -p "$d"; printf '%s\n' "$want" > "$d/.source-stamp"
}

# The "# Object files:" table of an ld64 link map: every object file, archive member and dylib the
# link read, one path per line (the "[  n] " index is removed; "linker synthesized" is entry 0).
linkmap_objects() {  # MAP
  awk '/^# Object files:/ { t = 1; next }  /^# / { t = 0 }  t { sub(/^\[ *[0-9]+\] /, ""); print }' "$1"
}

# --- upstream sources ----------------------------------------------------------------------------
ensure_cspot_tree() {  # needs REPO
  local up="$DEPS/squeezelite-esp32" tree="$DEPS/cspot" patch="$REPO/$CSPOT_PATCH_REL" want
  local gid=(-c user.name=dock-receiver-build -c user.email=build@localhost -c commit.gpgsign=false)
  mkdir -p "$DEPS" "$LOGS"
  if [ ! -d "$up/.git" ] || [ "$(git -C "$up" rev-parse HEAD 2>/dev/null)" != "$CSPOT_UPSTREAM_COMMIT" ]; then
    note "fetching $CSPOT_UPSTREAM_URL @ $CSPOT_UPSTREAM_COMMIT (sparse: $CSPOT_UPSTREAM_SUBDIR)"
    rm -rf "$up"; mkdir -p "$up"
    logged "$LOGS/deps-cspot-fetch.log" bash -c '
      set -e; cd "$1"; git init -q; git remote add origin "$2"
      git fetch -q --depth 1 --filter=blob:none origin "$3"
      git sparse-checkout init --cone; git sparse-checkout set "$4"
      git checkout -q --detach FETCH_HEAD' _ "$up" "$CSPOT_UPSTREAM_URL" "$CSPOT_UPSTREAM_COMMIT" "$CSPOT_UPSTREAM_SUBDIR"
  fi
  [ -f "$up/$CSPOT_UPSTREAM_SUBDIR/CMakeLists.txt" ] || die "upstream cspot subtree missing in $up"
  want="$CSPOT_UPSTREAM_COMMIT $(sha256sum < "$patch" | cut -d' ' -f1)"
  if [ "$(cat "$DEPS/cspot.stamp" 2>/dev/null)" != "$want" ] || [ ! -f "$tree/CMakeLists.txt" ]; then
    note "creating $tree = upstream cspot + $(basename "$patch") (git am)"
    rm -rf "$tree" "$DEPS/cspot.stamp"
    cp -a "$up/$CSPOT_UPSTREAM_SUBDIR" "$tree"
    logged "$LOGS/deps-cspot-patch.log" bash -c '
      set -e; cd "$1"; shift; patch="$1"; shift
      git init -q -b baseline; git add -A
      git "$@" commit -q -m "baseline: squeezelite-esp32 components/spotify/cspot (as-is)"
      git "$@" am "$patch"' _ "$tree" "$patch" "${gid[@]}"
    printf '%s\n' "$want" > "$DEPS/cspot.stamp"
  fi
}

ensure_mbedtls_src() {
  local d="$DEPS/mbedtls"
  mkdir -p "$DEPS" "$LOGS"
  if [ ! -d "$d/.git" ] || [ "$(git -C "$d" rev-parse HEAD 2>/dev/null)" != "$MBEDTLS_COMMIT" ]; then
    note "cloning $MBEDTLS_URL $MBEDTLS_TAG"
    rm -rf "$d"
    logged "$LOGS/deps-mbedtls-clone.log" git clone -q --depth 1 -b "$MBEDTLS_TAG" --recurse-submodules --shallow-submodules "$MBEDTLS_URL" "$d"
    [ "$(git -C "$d" rev-parse HEAD)" = "$MBEDTLS_COMMIT" ] || die "$MBEDTLS_TAG is not $MBEDTLS_COMMIT any more"
  fi
}

# mbedTLS build flags from spotify/CSPOT.md "Build commands"; extra args = toolchain file for ARM.
build_mbedtls() {  # KIND(host|arm) [extra cmake args...]
  local kind="$1"; shift
  local b="$BUILD_ROOT/$kind/mbedtls" prefix="$BUILD_ROOT/$kind/sysroot"
  if [ ! -f "$prefix/lib/libmbedtls.a" ] || [ ! -f "$prefix/lib/cmake/MbedTLS/MbedTLSConfig.cmake" ]; then
    ensure_mbedtls_src
    note "building mbedTLS $MBEDTLS_TAG ($kind) -> $prefix"
    rm -rf "$b" "$prefix"
    logged "$LOGS/$kind-mbedtls-configure.log" cmake -S "$DEPS/mbedtls" -B "$b" -G Ninja "$@" -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DMBEDTLS_FATAL_WARNINGS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
      -DCMAKE_INSTALL_PREFIX="$prefix"
    logged "$LOGS/$kind-mbedtls-build.log" ninja -C "$b" install
  fi
}

# The nanopb generator vendored in bell (0.4.6-dev) calls google.protobuf.reflection.MakeClass, which
# newer python protobuf releases removed (e.g. python 3.13 + protobuf 7.36.1 from pip ->
# AttributeError). CSPOT.md asks for host protoc + python3-protobuf: on Ubuntu 24.04 the apt package
# (3.21.12) serves /usr/bin/python3.12. Find an interpreter that works and put it first in PATH as "python3" for the
# cspot builds only (protoc-gen-nanopb runs "env python3").
ensure_nanopb_python() {
  local shim="$BUILD_ROOT/tools/nanopb-py" c
  for c in ${NANOPB_PYTHON:-} "$(command -v python3 || true)" /usr/bin/python3.12 /usr/bin/python3; do
    [ -n "$c" ] && [ -x "$c" ] || continue
    if "$c" -c 'import google.protobuf.reflection as r, google.protobuf.descriptor_pb2, sys; sys.exit(0 if hasattr(r, "MakeClass") else 1)' 2>/dev/null; then
      mkdir -p "$shim"; ln -sfn "$c" "$shim/python3"
      NANOPB_PY_DIR="$shim"; return 0
    fi
  done
  die "no python3 with a nanopb-0.4.6-compatible protobuf module (need reflection.MakeClass): apt-get install python3-protobuf, or set NANOPB_PYTHON"
}
