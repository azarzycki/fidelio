#!/usr/bin/env bash
# tools/run-host-tests.sh [REPO]   (REPO defaults to the repo this script lives in)
#
# Builds the host targets the 22 host suites need (19 audiod + the NowPlaying and lock-plugin layouts + cspot's cover URL) (only what is missing / out of date), runs
# the suites one after another (they share fixed /tmp/audiod-* dirs and TCP ports: never in
# parallel), and prints a per-suite table plus a TOTAL line.
#
# A suite run passes when the process exits 0, prints no "FAIL " line and at least one "ok " line.
# A failing suite is re-run, up to RUNS runs in total (default 3: test_producers.py has a known
# timing flake); every run is listed and every FAIL line is printed verbatim.
#
# Exit status
#   0  every suite passed on its first run
#   1  at least one suite still failed after RUNS runs
#   2  every suite passed in the end, but at least one needed a re-run (FAIL lines were seen)
#   3  setup / build error (nothing was tested, or testing could not start)
#
# Environment
#   RUNS=3            max runs per suite            SUITES="producers pacing"   run only these
#   SKIP_BUILD=1      do not configure/build        SUITE_TIMEOUT=900           seconds per run
#   PYTHON=python3    interpreter for the test_*.py STRICT_COUNTS=1  an ok-count that differs from the
#                                                                    recorded baseline fails the suite
#   HOST_CC / HOST_CXX  host compilers for a FRESH configure (default: CMake's default, cc/c++)
#
#   BUILD_ROOT=DIR    where everything is built (default: "build" next to the repo; never inside it)
#
# Output: logs in $BUILD_ROOT/host-test-logs/<timestamp>/ (latest -> that dir), summary.txt there.
# The tests themselves write to fixed /tmp/audiod-* directories (hard-coded in the test scripts); a
# re-run overwrites them, so after a FAILED run the directories it touched (daemon logs, event logs,
# PCM) are copied to <logdir>/<suite>.run<N>.artifacts/ first.
set -euo pipefail
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

REPO="$(resolve_repo "${1:-}")"
init_build_root
RUNS="${RUNS:-3}"
PYTHON="${PYTHON:-python3}"
SUITE_TIMEOUT="${SUITE_TIMEOUT:-900}"
HOST="$BUILD_ROOT/host"
T="$REPO/audiod/hosttest"
AUDIOD="$HOST/audiod/fidelio-audiod"; RAOP="$HOST/airplay/fidelio-raop"; CSPOT="$HOST/cspot/cspot-ios3"
export PYTHONDONTWRITEBYTECODE=1   # no __pycache__ in the repo

# name | baseline ok count | command (baseline: 354 ok, 0 FAIL, at B1.16.12)
SUITE_TABLE="
framing      26 $HOST/audiod/framing_test
aqsink       28 $HOST/audiod/aqsink_test
aqsink_bare  23 $HOST/audiod/aqsink_bare_test
writeframe    2 $HOST/audiod/writeframe_test
settings     23 $HOST/audiod/settings_test
settings_dmn 29 $PYTHON $T/test_settings.py $AUDIOD $RAOP $CSPOT
control       8 $PYTHON $T/test_control.py $AUDIOD $HOST/audiod/control_send
arbiter      13 $PYTHON $T/test_arbiter.py $AUDIOD
silence      22 $PYTHON $T/test_silence.py $AUDIOD
crossfade     5 $PYTHON $T/test_crossfade.py $AUDIOD
producers    21 $PYTHON $T/test_producers.py $AUDIOD $RAOP $CSPOT
pacing        8 $PYTHON $T/test_pacing.py $AUDIOD $RAOP
wake         11 $PYTHON $T/test_wake.py $AUDIOD
unlock       23 $PYTHON $T/test_unlock.py $AUDIOD
lockplugin    8 $PYTHON $T/test_lockplugin.py $AUDIOD
preempt      11 $PYTHON $T/test_preempt.py $AUDIOD
ap_preempt   11 $PYTHON $T/test_airplay_preempt.py $AUDIOD $RAOP
ap_volume    11 $PYTHON $T/test_airplay_volume.py $AUDIOD $RAOP
volume       11 $PYTHON $T/test_volume.py $AUDIOD
dockvol      14 $PYTHON $T/test_dock_volume.py $AUDIOD
layout       30 $HOST/nowplaying/layout_test
lock_layout  10 $HOST/nowplaying/lock_layout_test
cover_url     6 $HOST/cspot/cover_url_test
"

case "$BUILD_ROOT$REPO" in *[[:space:]]*) die "paths with whitespace are not supported";; esac
exec 9>"$BUILD_ROOT/.host-tests.lock"
flock -n 9 || die "another run-host-tests.sh is running (the suites share /tmp dirs and ports)"

SECONDS=0
note "repo: $(repo_describe "$REPO")"

# ---------------------------------------------------------------------------- build ----------
build_host() {
  # cc and c++: the plain host tests below call them by name, and they are CMake's default compilers
  # (a minimal Ubuntu image has clang-18 but neither; apt: g++).
  need_tools cmake ninja git protoc openssl "$PYTHON" cc c++
  local cc=()
  [ -n "${HOST_CC:-}" ] && cc+=(-DCMAKE_C_COMPILER="$HOST_CC")
  [ -n "${HOST_CXX:-}" ] && cc+=(-DCMAKE_CXX_COMPILER="$HOST_CXX")
  mkdir -p "$HOST" "$LOGS"

  # audiod: fidelio-audiod, control_send and the five C++ suites (audiod/README.md "Build and test on the host")
  stamp_dir "$HOST/audiod" "$REPO"
  [ -f "$HOST/audiod/build.ninja" ] || logged "$LOGS/host-audiod-configure.log" \
    cmake -S "$REPO/audiod" -B "$HOST/audiod" -G Ninja -DCMAKE_BUILD_TYPE=Release "${cc[@]}"
  logged "$LOGS/host-audiod-build.log" ninja -C "$HOST/audiod"

  # host mbedTLS 3.6.7 (airplay/AIRPLAY.md: "host test: cmake ... -DMBEDTLS_PREFIX=<host mbedtls 3.6.7>")
  build_mbedtls host "${cc[@]}"

  # fidelio-raop (no build type, as documented; its CMakeLists sets -O2 itself)
  stamp_dir "$HOST/airplay" "$REPO"
  [ -f "$HOST/airplay/build.ninja" ] || logged "$LOGS/host-airplay-configure.log" \
    cmake -S "$REPO/airplay" -B "$HOST/airplay" -G Ninja -DMBEDTLS_PREFIX="$HOST/sysroot" "${cc[@]}"
  logged "$LOGS/host-airplay-build.log" ninja -C "$HOST/airplay"

  # cspot-ios3 host CLI (spotify/CSPOT.md: "-DCSPOT_DIR=<cspot tree>, no AudioQueue").
  # Two things a current Ubuntu 24.04 host needs that CSPOT.md does not mention:
  #  * bell's CMake links avahi-client/avahi-common on Linux (apt: libavahi-client-dev); without a
  #    running avahi-daemon bell falls back to its built-in mDNS responder at run time.
  #  * "-include cstring": bell/main/io/URLParser.cpp (the BELL_DISABLE_REGEX path the CLI selects)
  #    calls strstr() without including <cstring>. libc++ (the ARM build) pulls it in through
  #    <string>; libstdc++ 13 does not, with g++ and with clang++ alike. A forced include keeps the
  #    upstream tree and the repo patch untouched. Drop the flag once the patch adds the #include.
  ensure_cspot_tree
  ensure_nanopb_python
  stamp_dir "$HOST/cspot" "$REPO $(cat "$DEPS/cspot.stamp")"
  [ -f "$HOST/cspot/build.ninja" ] || PATH="$NANOPB_PY_DIR:$PATH" logged "$LOGS/host-cspot-configure.log" \
    cmake -S "$REPO/spotify/cli-src" -B "$HOST/cspot" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCSPOT_DIR="$DEPS/cspot" \
      -DBELL_EXTERNAL_MBEDTLS="$HOST/sysroot/lib/cmake/MbedTLS" -DMBEDTLS_RELEASE=RELEASE \
      "-DCMAKE_CXX_FLAGS=-include cstring" "${cc[@]}"
  PATH="$NANOPB_PY_DIR:$PATH" logged "$LOGS/host-cspot-build.log" ninja -C "$HOST/cspot"

  # B1.18: two plain host tests without a CMake project of their own: the NowPlaying layout core (the
  # same C the app compiles) and cspot's cover-URL helper (header-only).
  mkdir -p "$HOST/nowplaying"
  logged "$LOGS/host-layout-build.log" cc -O2 -Wall -Wextra -std=c99 -I"$REPO/nowplaying/src" -o "$HOST/nowplaying/layout_test" \
    "$REPO/nowplaying/src/NPLayout.c" "$REPO/nowplaying/hosttest/layout_test.c"
  # B1.16.7: the lock plugin's layout core (the same C the plugin compiles).
  logged "$LOGS/host-lock-layout-build.log" cc -O2 -Wall -Wextra -std=c99 -I"$REPO/nowplaying/lockplugin" -o "$HOST/nowplaying/lock_layout_test" \
    "$REPO/nowplaying/lockplugin/LPLayout.c" "$REPO/nowplaying/hosttest/lock_layout_test.c" -lm
  logged "$LOGS/host-cover-url-build.log" c++ -O2 -Wall -std=c++17 -I"$REPO/spotify/cli-src" -o "$HOST/cspot/cover_url_test" \
    "$REPO/spotify/hosttest/cover_url_test.cpp"
}

if [ "${SKIP_BUILD:-0}" != 1 ]; then
  note "building host targets (logs: $LOGS/host-*.log)"
  build_host
  note "host build done after ${SECONDS}s"
fi
BUILD_SECS=$SECONDS
for f in "$AUDIOD" "$RAOP" "$CSPOT" "$HOST/nowplaying/layout_test" "$HOST/nowplaying/lock_layout_test" "$HOST/cspot/cover_url_test" "$HOST/audiod/control_send" "$HOST/audiod/framing_test" "$HOST/audiod/aqsink_test" \
         "$HOST/audiod/aqsink_bare_test" "$HOST/audiod/writeframe_test" "$HOST/audiod/settings_test"; do
  [ -x "$f" ] || die "missing host binary $f (run without SKIP_BUILD=1)"
done

# ---------------------------------------------------------------------------- run ------------
STAMP="$(date +%Y%m%d-%H%M%S)"
LOGDIR="$BUILD_ROOT/host-test-logs/$STAMP"
RUNDIR="$LOGDIR/cwd"
mkdir -p "$RUNDIR"
ln -sfn "$STAMP" "$BUILD_ROOT/host-test-logs/latest"
FAILLINES="$LOGDIR/fail-lines.txt"; : > "$FAILLINES"
PUBPEM="$REPO/airplay/hosttest/airport_express_pub.pem"
PUBPEM_EXISTED=0; [ -e "$PUBPEM" ] && PUBPEM_EXISTED=1

# daemons left behind by an aborted earlier run would hold the suites' sockets and ports
cleanup_leftovers() {
  local pids
  pids="$(pgrep -f "^($HOST/(audiod|airplay|cspot)/|[^ ]*python[^ ]* $REPO/[^ ]*(test_[a-z_]*|raop_sender)\.py)" || true)"
  if [ -n "$pids" ]; then
    echo "  (killing leftover test processes: $(echo $pids))"
    kill -9 $pids 2>/dev/null || true
    sleep 0.5
  fi
}

declare -A R_RUNS R_STATUS R_SECS R_EXP R_OK R_FAIL
ORDER=()
any_persistent=0; any_flaky=0

run_suite() {  # name expected cmd...
  local name="$1" exp="$2"; shift 2
  local run=0 ok=0 fail=0 rc=0 pass=0 runs="" log t0 secs=0 note_txt="" d
  while [ "$run" -lt "$RUNS" ]; do
    run=$((run + 1)); log="$LOGDIR/$name.run$run.log"
    cleanup_leftovers
    : > "$LOGDIR/.run-marker"
    t0=$SECONDS; rc=0
    ( cd "$RUNDIR" && exec timeout --kill-after=10 "$SUITE_TIMEOUT" "$@" ) >"$log" 2>&1 </dev/null 9>&- || rc=$?
    secs=$((secs + SECONDS - t0))
    ok="$(grep -c '^ok[[:space:]]' "$log" || true)"; fail="$(grep -c '^FAIL[[:space:]]' "$log" || true)"
    runs="$runs $ok/$fail"; [ "$rc" = 0 ] || runs="$runs(exit$rc)"
    if [ "$rc" = 0 ] && [ "$fail" = 0 ] && [ "$ok" -gt 0 ] && { [ "${STRICT_COUNTS:-0}" != 1 ] || [ "$ok" = "$exp" ]; }; then
      pass=1; break
    fi
    { grep '^FAIL[[:space:]]' "$log" || echo "(no FAIL line: exit status $rc, $ok ok lines, baseline $exp; see $log)"; } | sed "s|^|$name run $run: |" >> "$FAILLINES"
    echo "  $name run $run: $ok ok, $fail FAIL, exit $rc"
    cleanup_leftovers   # before copying: a killed test leaves its daemons writing
    for d in /tmp/audiod-*; do   # keep what the failed run left behind (the next run overwrites it)
      [ -d "$d" ] && [ -n "$(find "$d" -newer "$LOGDIR/.run-marker" -print -quit 2>/dev/null)" ] || continue
      mkdir -p "$LOGDIR/$name.run$run.artifacts" && cp -a "$d" "$LOGDIR/$name.run$run.artifacts/" 2>/dev/null || true
    done
  done
  if [ "$pass" = 1 ] && [ "$run" = 1 ]; then R_STATUS[$name]="PASS"
  elif [ "$pass" = 1 ]; then R_STATUS[$name]="FLAKY(pass on run $run)"; any_flaky=1
  else R_STATUS[$name]="FAIL"; any_persistent=1; fi
  [ "$ok" = "$exp" ] || note_txt=" count!=baseline"
  R_STATUS[$name]="${R_STATUS[$name]}$note_txt"
  R_RUNS[$name]="${runs# }"; R_SECS[$name]=$secs; R_EXP[$name]=$exp; R_OK[$name]=$ok; R_FAIL[$name]=$fail
  ORDER+=("$name")
  printf '  %-13s %-28s %4ss  %s\n' "$name" "${R_RUNS[$name]}" "$secs" "${R_STATUS[$name]}"
}

note "running suites (logs: $LOGDIR)"
selected=" ${SUITES:-} "
while read -r name exp cmd; do
  [ -n "$name" ] || continue
  [ "$selected" = "  " ] || [[ "$selected" == *" $name "* ]] || continue
  # shellcheck disable=SC2086  # the command is a plain word list
  run_suite "$name" "$exp" $cmd
done <<< "$SUITE_TABLE"
cleanup_leftovers
[ "${#ORDER[@]}" -gt 0 ] || die "SUITES='${SUITES:-}' selected nothing"
[ "$PUBPEM_EXISTED" = 1 ] || rm -f "$PUBPEM"   # created by raop_sender.py during this run: restore the tree

# ---------------------------------------------------------------------------- report ---------
{
  echo
  echo "repo:    $(repo_describe "$REPO")"
  cxx="$(sed -n 's/^CMAKE_CXX_COMPILER:[A-Z]*=//p' "$HOST/audiod/CMakeCache.txt" 2>/dev/null | head -1)"
  echo "host:    ${cxx:-c++} = $("${cxx:-c++}" --version 2>/dev/null | head -1) / $($PYTHON --version 2>&1) / $(uname -sr)"
  echo
  printf '%-13s %8s  %-30s %5s  %s\n' suite baseline "ok/FAIL per run" secs status
  tot_ok=0; tot_fail=0; tot_exp=0; n_pass=0; n_fail=0; n_flaky=0
  for name in "${ORDER[@]}"; do
    printf '%-13s %8s  %-30s %5s  %s\n' "$name" "${R_EXP[$name]}" "${R_RUNS[$name]}" "${R_SECS[$name]}" "${R_STATUS[$name]}"
    tot_ok=$((tot_ok + ${R_OK[$name]})); tot_fail=$((tot_fail + ${R_FAIL[$name]})); tot_exp=$((tot_exp + ${R_EXP[$name]}))
    case "${R_STATUS[$name]}" in PASS*) n_pass=$((n_pass + 1));; FLAKY*) n_flaky=$((n_flaky + 1));; *) n_fail=$((n_fail + 1));; esac
  done
  echo
  if [ -s "$FAILLINES" ]; then
    echo "failing check lines (verbatim, every run):"; sed 's/^/  /' "$FAILLINES"; echo
  fi
  echo "TOTAL host: $tot_ok ok, $tot_fail FAIL   (final run of each suite; baseline $tot_exp ok, 0 FAIL)   suites: $n_pass pass, $n_flaky flaky, $n_fail fail of ${#ORDER[@]}"
  echo "time: build ${BUILD_SECS}s + tests $((SECONDS - BUILD_SECS))s = ${SECONDS}s   logs: $LOGDIR"
  echo "repo after the run: $(git -C "$REPO" status --short 2>/dev/null | wc -l) entries in 'git status --short'"
} | tee "$LOGDIR/summary.txt"

[ "$any_persistent" = 0 ] || exit 1
[ "$any_flaky" = 0 ] || exit 2
exit 0
