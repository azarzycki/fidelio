# packaging/hosttest/stubs.sh: sourced by the packaging host tests. Writes the stand-ins for the device's
# launchctl, killall, uicache, su, dpkg and df into a directory, and a directory of the real tools that
# BOTH devices have (and nothing else), so a script that calls a missing tool fails the test.
#
# The stubs keep their state in $DOCKRECEIVER_ROOT/.fake (in the fake root):
#   procs/<name>    a "running process" of that name        jobs/<label>   a loaded launchd job
#   calls.log       every stub call                        dpkg/          the fake dpkg database
# Knobs (environment, read by the stubs):
#   FAKE_FAIL_LOAD=<label>      launchctl load of that label fails
#   FAKE_UNKILLABLE=<name>      killall does not stop that process
#   FAKE_NOSTART=<label>        load succeeds but the job's process never appears
#   FAKE_UICACHE_RC=<n>         uicache's exit status
#   FAKE_DF_ROOT_KB / FAKE_DF_VAR_KB   what df reports as available

# The tools both devices have (the device survey of 2026-11-22), minus the ones stubbed here.
DEVICE_TOOLS="grep egrep cp mv rm ln chmod chown mkdir rmdir cat ls sleep tar gzip gunzip date touch dirname mktemp readlink find xargs openssl dd which"

make_tooldirs() {  # DIR: creates DIR/tools (real device tools) and DIR/stubs; prints nothing
  local d="$1" t
  mkdir -p "$d/tools" "$d/stubs"
  for t in $DEVICE_TOOLS; do
    ln -sf "$(command -v "$t")" "$d/tools/$t"
  done
  # sed: the devices' sed 4.1.5 knows -r but not -E
  cat > "$d/tools/sed" <<EOF
#!$(command -v bash)
for a in "\$@"; do case "\$a" in -E|-*E*) case "\$a" in --*) ;; -[a-zA-Z]*E*) echo "sed: -E is not available on the devices (use -r)" >&2; exit 2;; esac;; esac; done
exec $(command -v sed) "\$@"
EOF
  chmod 755 "$d/tools/sed"

  # --- launchctl ---
  cat > "$d/stubs/launchctl" <<'EOF'
#!/bin/bash
F="$DOCKRECEIVER_ROOT/.fake"; mkdir -p "$F/jobs" "$F/procs"
echo "launchctl $*" >> "$F/calls.log"
label_of() { /bin/sed -n '/<key>Label<\/key>/{n;s/.*<string>\([^<]*\)<\/string>.*/\1/p;}' "$1"; }
proc_of() {
  case "$1" in
    com.fidelio.audiod) echo fidelio-audiod ;; com.fidelio.raop) echo fidelio-raop ;;
    com.fidelio.cspot|com.dockreceiver.cspot) echo cspot-ios3 ;;
    com.fidelio.keepalive|com.dockreceiver.keepalive) echo bash ;;
    com.dockreceiver.audiod) echo dock-audiod ;; com.dockreceiver.raop) echo dock-raop ;;
    *) echo "${1##*.}" ;;
  esac
}
stop() { /bin/rm -f "$F/jobs/$1"; local p; p="$(proc_of "$1")"; case " ${FAKE_UNKILLABLE:-} " in *" $p "*) ;; *) /bin/rm -f "$F/procs/$p" ;; esac; }
hidden() { case "${FAKE_LIST_HIDE:-}" in '') return 1 ;; esac; case "$1" in ${FAKE_LIST_HIDE}*) return 0 ;; esac; return 1; }
case "$1" in
  list)
    if [ -n "${2:-}" ]; then   # launchctl list LABEL: a dictionary, or an error
      [ -e "$F/jobs/$2" ] && ! hidden "$2" || { echo "launchctl list returned unknown response" >&2; exit 1; }
      printf '{\n\t"Label" = "%s";\n\t"PID" = 12;\n};\n' "$2"; exit 0
    fi
    # FAKE_LIST_FORMAT: tab (PID Status Label, default), spaces, labels (one label per line), crlf
    [ "${FAKE_LIST_FORMAT:-tab}" = labels ] || printf 'PID\tStatus\tLabel\n'
    # as on the phone: a first column that is not a usable pid, "-" as the status, anonymous jobs
    [ "${FAKE_LIST_FORMAT:-tab}" = tab ] && printf '9707\t-\t0x107810.anonymous.launchctl\n'
    for j in "$F"/jobs/*; do
      [ -e "$j" ] || continue; hidden "${j##*/}" && continue
      case "${FAKE_LIST_FORMAT:-tab}" in
        tab) printf '2%s\t-\t%s\n' "${#j}" "${j##*/}" ;;
        spaces) printf '  1%s     -     %s   \n' "${#j}" "${j##*/}" ;;
        labels) printf '%s\n' "${j##*/}" ;;
        crlf) printf '1%s\t0\t%s\r\n' "${#j}" "${j##*/}" ;;
      esac
    done; exit 0 ;;
  load)
    p="${@: -1}"; [ -f "$p" ] || { echo "launchctl: no such file $p" >&2; exit 1; }
    l="$(label_of "$p")"; [ -n "$l" ] || exit 1
    [ "${FAKE_FAIL_LOAD:-}" = "$l" ] && { echo "launchctl: load $l failed (FAKE_FAIL_LOAD)" >&2; exit 1; }
    [ "${FAKE_FAIL_LOAD_RC0:-}" = "$l" ] && { echo "launchctl: no plist was returned for: $p" >&2; exit 0; }
    [ -e "$F/jobs/$l" ] && { echo "$l: already loaded" >&2; exit 0; }
    : > "$F/jobs/$l"
    case " ${FAKE_NOSTART:-} " in *" $l "*) exit 0 ;; esac
    : > "$F/procs/$(proc_of "$l")"
    # FAKE_RESPAWN_OLD: an old job launchd still knows comes back (its audiod reappears)
    # FAKE_RESPAWN_OLD=N: N seconds after the new keepalive loads, an old audiod appears (as launchd
    # would restart a job it still knows after its throttle interval)
    if [ -n "${FAKE_RESPAWN_OLD:-}" ] && [ "$l" = com.dockreceiver.keepalive ]; then
      ( /bin/sleep "$FAKE_RESPAWN_OLD"; : > "$F/procs/fidelio-audiod" ) >/dev/null 2>&1 < /dev/null &
    fi
    if [ "$l" = com.dockreceiver.audiod ] && [ -d "$DOCKRECEIVER_ROOT/var/mobile/dockreceiver" ]; then
      /bin/sleep 1.1; echo none > "$DOCKRECEIVER_ROOT/var/mobile/dockreceiver/active-source"
    fi
    exit 0 ;;
  unload) p="${@: -1}"; [ -f "$p" ] || exit 1; l="$(label_of "$p")"; [ -e "$F/jobs/$l" ] || exit 1; stop "$l"; exit 0 ;;
  remove) [ -e "$F/jobs/$2" ] || exit 1; stop "$2"; exit 0 ;;
  *) exit 1 ;;
esac
EOF
  # --- killall ---
  cat > "$d/stubs/killall" <<'EOF'
#!/bin/bash
F="$DOCKRECEIVER_ROOT/.fake"; mkdir -p "$F/procs"
echo "killall $*" >> "$F/calls.log"
sig=TERM
case "$1" in -0) sig=0; shift ;; -9|-KILL) sig=9; shift ;; -TERM|-15|-HUP|-1) shift ;; esac
rc=1
for n in "$@"; do
  [ -e "$F/procs/$n" ] || continue
  rc=0
  [ "$sig" = 0 ] && continue
  case " ${FAKE_UNKILLABLE:-} " in *" $n "*) continue ;; esac
  /bin/rm -f "$F/procs/$n"
done
exit $rc
EOF
  # --- uicache, su ---
  cat > "$d/stubs/uicache" <<'EOF'
#!/bin/bash
echo "uicache $*" >> "$DOCKRECEIVER_ROOT/.fake/calls.log"; exit "${FAKE_UICACHE_RC:-0}"
EOF
  cat > "$d/stubs/su" <<'EOF'
#!/bin/bash
echo "su $*" >> "$DOCKRECEIVER_ROOT/.fake/calls.log"
[ "$1" = mobile ] && [ "$2" = -c ] || exit 1
eval "$3"
EOF
  # --- df ---
  cat > "$d/stubs/df" <<'EOF'
#!/bin/bash
case "${@: -1}" in */var|*/var/) kb="${FAKE_DF_VAR_KB:-3800000}" ;; *) kb="${FAKE_DF_ROOT_KB:-137000}" ;; esac
printf 'Filesystem   1K-blocks   Used Available Use%% Mounted on\n/dev/disk0s1 9999999 1000 %s 11%% /\n' "$kb"
EOF
  # --- dpkg: a fake package manager in the fake root ---
  cat > "$d/stubs/dpkg" <<'EOF'
#!/bin/bash
# dpkg -i DEB...  |  dpkg -r PKG  |  dpkg -s PKG   (the maintainer scripts run as DPKG_SH script args)
F="$DOCKRECEIVER_ROOT/.fake"; DB="$F/dpkg"
[ "$1" = -s ] || mkdir -p "$DB/info"
echo "dpkg $*" >> "$F/calls.log"
H=/usr/bin
run() { "$DPKG_SH" "$@"; }   # the device's /bin/sh
field() { $H/sed -n "s/^$2: //p" "$1"; }
case "$1" in
  -s) [ -f "$DB/$2.status" ] || { echo "dpkg-query: package '$2' is not installed" >&2; exit 1; }
      printf 'Package: %s\nStatus: %s\n' "$2" "$($H/cat "$DB/$2.status")"; exit 0 ;;
  -i) shift; pkgs=""; errs=0
      for deb in "$@"; do
        w="$($H/mktemp -d)"; $H/dpkg-deb -e "$deb" "$w/ctl" || exit 2
        p="$(field "$w/ctl/control" Package)"; v="$(field "$w/ctl/control" Version)"
        if [ -f "$DB/$p.status" ]; then mode=upgrade; old="$($H/cat "$DB/$p.version")"; else mode=install; old=""; fi
        $H/cp "$w/ctl/preinst" "$F/preinst-$p"
        if ! run "$w/ctl/preinst" $mode $old; then
          run "$w/ctl/postrm" abort-$mode $old; echo "dpkg: error processing archive $deb (preinst)" >&2
          errs=1; $H/rm -rf "$w"; continue   # dpkg goes on with the next archive
        fi
        # dpkg follows an existing link to a directory (on the devices /Applications, /usr/libexec and
        # /usr/share are links into /var/stash): unpack aside, then copy over the root, which does the same
        $H/mkdir -p "$w/data"
        $H/dpkg-deb --fsys-tarfile "$deb" | $H/tar -x -C "$w/data" --no-same-owner -f - || exit 2
        ( cd "$w/data" && $H/find . -mindepth 1 | LC_ALL=C $H/sort ) | while IFS= read -r m; do
          if [ -d "$w/data/$m" ]; then $H/mkdir -p "$DOCKRECEIVER_ROOT/$m"
          else $H/cp -p "$w/data/$m" "$DOCKRECEIVER_ROOT/$m.dpkg-new" && $H/mv -f "$DOCKRECEIVER_ROOT/$m.dpkg-new" "$DOCKRECEIVER_ROOT/$m"; fi
        done
        $H/dpkg-deb -c "$deb" | $H/sed -n 's|^[^ ]* [^ ]* *[0-9]* [^ ]* [^ ]* \./\(.*[^/]\)$|\1|p' > "$DB/$p.list"
        for s in postinst prerm postrm; do $H/cp "$w/ctl/$s" "$DB/info/$p.$s"; done
        echo "$v" > "$DB/$p.version"; echo "install ok unpacked" > "$DB/$p.status"
        pkgs="$pkgs $p"; $H/rm -rf "$w"
      done
      for p in $pkgs; do
        # the dock-remote package depends on dock-receiver: configured only once that one is
        if [ "$p" = dock-receiver-dockremote ] && [ "$($H/cat "$DB/dock-receiver.status" 2>/dev/null)" != "install ok installed" ]; then
          echo "dpkg: dependency problems prevent configuration of $p" >&2; errs=1; continue
        fi
        if ! run "$DB/info/$p.postinst" configure; then
          echo "install ok half-configured" > "$DB/$p.status"; echo "dpkg: error processing package $p (--configure)" >&2; errs=1; continue
        fi
        echo "install ok installed" > "$DB/$p.status"
      done
      exit $errs ;;
  -r|-P) p="$2"; [ -f "$DB/$p.status" ] || { echo "dpkg: $p is not installed" >&2; exit 0; }
      run "$DB/info/$p.prerm" remove || { echo "dpkg: prerm of $p failed" >&2; exit 1; }
      while IFS= read -r f; do [ -n "$f" ] && $H/rm -f "$DOCKRECEIVER_ROOT/$f"; done < "$DB/$p.list"
      # empty directories the package created
      $H/sed 's|/[^/]*$||' "$DB/$p.list" | $H/sort -ru | while IFS= read -r dir; do
        while [ -n "$dir" ] && $H/rmdir "$DOCKRECEIVER_ROOT/$dir" 2>/dev/null; do dir="${dir%/*}"; [ "$dir" = "${dir%/*}" ] && break; done
      done
      run "$DB/info/$p.postrm" remove || echo "dpkg: postrm of $p failed" >&2
      [ "$1" = -P ] && { run "$DB/info/$p.postrm" purge || echo "dpkg: postrm purge of $p failed" >&2; }
      $H/rm -f "$DB/$p.status" "$DB/$p.version" "$DB/$p.list" "$DB/info/$p".*
      exit 0 ;;
  --configure) # --configure -a: configure every package left unpacked or half-configured
      errs=0
      for s in "$DB"/dock-receiver.status "$DB"/*.status; do   # dock-receiver first (the other depends on it)
        [ -f "$s" ] || continue; p="${s##*/}"; p="${p%.status}"
        case "$($H/cat "$s")" in *unpacked|*half-configured) ;; *) continue ;; esac
        if [ "$p" = dock-receiver-dockremote ] && [ "$($H/cat "$DB/dock-receiver.status" 2>/dev/null)" != "install ok installed" ]; then
          echo "dpkg: dependency problems prevent configuration of $p" >&2; errs=1; continue
        fi
        if run "$DB/info/$p.postinst" configure; then echo "install ok installed" > "$s"; else echo "install ok half-configured" > "$s"; errs=1; fi
      done
      exit $errs ;;
esac
exit 2
EOF
  chmod 755 "$d"/stubs/*
}
