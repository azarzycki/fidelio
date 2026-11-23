<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. -->
# A sysroot without Apple's SDK: link libraries (stage 1), published headers (stage 2), framework headers of our own (stage 3)

The ARM build of this repository compiles and links against a sysroot: the directory that the
toolchain wrapper gets as `IOS3_SDK`. This directory makes that sysroot without Apple's SDK, and
`tools/build-arm.sh` uses it unless the developer names another one ("How `tools/build-arm.sh` uses
the sysroot" below). Apple's `iPhoneOS3.1.2.sdk` is not part of this repository and must not be
published; a developer who has a copy can still build against it.

The sysroot has three parts, made in three stages.

- **Stage 1: the link libraries.** `stubs/build-stubs.sh` generates stand-ins for the dylibs that
  the linker reads in the SDK. They are made from a list of names, `stubs/manifest.tsv`, and from
  nothing else.
- **Stage 2: the C library, kernel and CoreFoundation headers.** `headers/build-headers.sh` builds
  `usr/include` and `CoreFoundation.framework/Headers` from Apple's published open-source releases,
  fetched by commit and checked by sha256, with the releases' own install processing, a few small
  patches of this project and one header of its own. See "Stage 2: the headers" below.
- **Stage 3: the headers of the other frameworks** (Foundation, UIKit, CoreGraphics, QuartzCore,
  AudioToolbox, CoreAudio, AVFoundation). Apple publishes no source for them, so they are files of
  this project, under `frameworks/`, that declare only what this repository's sources use. See
  "Stage 3: framework headers of our own" below.

`build-sysroot.sh OUTDIR` puts the three together into a complete sysroot, from nothing and without
an SDK. With it, `tools/build-arm.sh` gives the same nine binaries, byte for byte, as with Apple's
SDK ("What is verified" in the stage 3 section and at the end). `tools/build-arm.sh` makes it itself
when `IOS3_SDK` is not set.

## What is here

| File | What it is |
|---|---|
| `build-sysroot.sh` | `build-sysroot.sh OUTDIR` builds the complete sysroot: the stubs, the stage 2 headers and the framework headers. |
| `frameworks/<X>.framework/Headers/` | This project's own headers of the frameworks Apple publishes no source for (stage 3). |
| `frameworks/HEADERS.sha256` | The sha256 of every framework header; `build-sysroot.sh` checks the files against it. |
| `stubs/manifest.tsv` | The libraries and, for each, the symbols a stub offers. The only input of the build. |
| `stubs/build-stubs.sh` | `build-stubs.sh OUTDIR` builds the stubs into a new directory tree. |
| `stubs/imports.py` | Writes the manifest from linked binaries, and checks the manifest and the stubs. |
| `headers/sources.tsv` | Each header of stage 2 and where it comes from: release, commit, path, sha256, processing. |
| `headers/build-headers.sh` | `build-headers.sh OUTDIR` builds the headers into a new directory tree. |
| `headers/HEADERS.sha256` | The sha256 of every header that `build-headers.sh` writes; the build is checked against it. |
| `headers/patches/` | This project's changes to published files, one file per topic. |
| `headers/own/` | Files of this project where nothing was published: `CFNotificationCenter.h`, and the Mach types that mig's own sources need on a host that is not a Mach system (`own/migcom-host/`). |

## What a stub is

When the linker links a program against a dylib, it reads little from the dylib: its install name,
its two version numbers, the libraries it re-exports, and the names of the symbols it exports. It
writes the install name and the versions into the program (`LC_LOAD_DYLIB`), and for every
imported symbol it records which of the program's libraries provides it. No code of the library
reaches the program. On the device, dyld loads the real library by its install name.

A stub is a dylib that has those things and no more: an ARMv6 `MH_DYLIB` with the right
`LC_ID_DYLIB`, an `LC_REEXPORT_DYLIB` command where the manifest says so, and one exported symbol
per manifest entry. Behind each symbol is one word, a trap instruction. A stub contains no code of
Apple's and is never installed on a device.

A program can only import what a stub offers. A symbol that is not in the manifest is an undefined
symbol at link time, even if the real library has it. That makes the manifest an allow-list: every
system symbol the binaries use is written down, with the reason it is there.

## Building the stubs

```sh
toolchain/sysroot/stubs/build-stubs.sh OUTDIR
```

`OUTDIR` must not exist yet, or be an empty directory. The script needs the host clang 18 (as the
assembler), the toolchain's `arm-apple-darwin9-ld` (from `PATH`, or `IOS3_TOOLCHAIN` names the
toolchain directory), `python3`, `awk` and `sha256sum`. It needs no SDK and no network.

It writes

```
OUTDIR/usr/lib/libSystem.B.dylib, libgcc_s.1.dylib, libobjc.A.dylib, libstdc++.6.dylib
OUTDIR/usr/lib/libSystem.dylib -> libSystem.B.dylib        the name -lSystem looks for
OUTDIR/usr/lib/libobjc.dylib -> libobjc.A.dylib            the name -lobjc looks for
OUTDIR/System/Library/Frameworks/<X>.framework/<X>         AudioToolbox, AVFoundation, CoreFoundation,
                                                           CoreGraphics, Foundation, QuartzCore, UIKit
OUTDIR/SHA256SUMS                                          the sha256 of each stub
```

How a stub is made (the header of the script has the reasons):

1. The host clang assembles a file of labels, one per symbol, each followed by the trap word.
2. The linker makes the dylib, with the install name and versions of the manifest, without
   `LC_UUID` and without a version command:
   `ld -dylib -arch armv6 -iphoneos_version_min 3.0 -no_version_load_command -no_function_starts
   -no_data_in_code_info -no_uuid -Z -install_name ... -current_version ... -compatibility_version
   ... labels.o [-reexport_library child]... seed.dylib -o stub`
3. This linker refuses to write a dylib that links no dylib at all. Every stub is therefore linked
   against a *seed*: an empty dylib that is made first, has the install name
   `/usr/lib/system/libsystem_kernel.dylib` (the one name for which the linker waives that rule) and
   is marked `MH_DEAD_STRIPPABLE_DYLIB`. Because of the mark, the linker writes no load command for
   the seed into a stub that takes nothing from it. The seed stays in the work directory.
4. Foundation re-exports libobjc and CoreFoundation, so its stub is linked after theirs, with
   `-reexport_library` for each. (`-dead_strip_dylibs` would also keep the seed out of a stub, but
   it removes the re-exports with it. That is why the seed carries the mark instead.)

The output is deterministic: two runs give the same bytes. Before the script writes to `OUTDIR` it
checks what it built: the tree holds exactly the files of the manifest, and `imports.py stubs`
reads each stub back and compares file type, install name, versions, load commands and export list
with the manifest. The linker must not print a warning.

All symbols are made the same way, as labels in `__TEXT,__text`. The manifest does not say which
names are functions and which are variables, and the linker does not ask: from a dylib of this
kind it reads each export's name and whether the definition is weak. No symbol is defined weakly,
because none of our binaries is bound to a weak definition; `imports.py` stops with an error if
one ever is.

## The manifest

`stubs/manifest.tsv` is plain text: tab-separated fields, one record per line, sorted in byte
order, with a comment header that repeats what follows.

| Record | Fields |
|---|---|
| `lib` | path in the sysroot; install name; current version; compatibility version; the install names of the libraries it re-exports, in the order of the stub's load commands, or `-` |
| `link` | path in the sysroot; target of the symlink |
| `sym` | install name of the library; symbol; *imported by*; *note* |

- *imported by* lists the binaries that import the symbol from that library, by label: the path
  of a product below `arm-out`, or `smoke/` plus the name of a smoke-test file. `imports.py`
  rewrites this field.
- *note* is `-`, or says where a symbol that no binary imports comes from and how it was confirmed.
  It is written by hand and kept by `imports.py`.
- A library's path is its install name without the first slash. A `lib` row without `sym` rows is
  a library that binaries name without taking a symbol from it (QuartzCore).

What it holds on 2026-11-22: 11 libraries, 2 links, 426 symbols.

| Library | current / compatibility version | Symbols |
|---|---|---|
| `/usr/lib/libSystem.B.dylib` | 111.2.1 / 1.0.0 | 270 |
| `/usr/lib/libgcc_s.1.dylib` | 1.0.0 / 1.0.0 | 23 |
| `/usr/lib/libobjc.A.dylib` | 227.0.0 / 1.0.0 | 15 |
| `/usr/lib/libstdc++.6.dylib` (only the smoke-test file `cxx` links it) | 7.9.0 / 7.0.0 | 17 |
| AudioToolbox | 246.0.0 / 1.0.0 | 14 |
| AVFoundation | 1.0.0 / 1.0.0 | 1 |
| CoreFoundation | 478.52.0 / 150.0.0 | 34 |
| CoreGraphics | 359.16.0 / 64.0.0 | 9 |
| Foundation (re-exports libobjc and CoreFoundation) | 678.51.0 / 300.0.0 | 12 |
| QuartzCore | 1.6.0 / 1.2.0 | 0 |
| UIKit | 1000.0.0 / 1.0.0 | 31 |

Where the entries come from:

- **402 symbols, and every library with its versions, from the import tables of our binaries**: the
  nine outputs of `tools/build-arm.sh` and the seven files of the start-up smoke test of toolchain
  revision 3 (`docs/HANDOFF.md`; a C, a C++ and an Objective-C program, a loader, a dylib and a
  bundle, whose sources are not in this repository). All sixteen were linked against the SDK with
  toolchain revision 3. Each binary records the libraries it links (`LC_LOAD_DYLIB`) and, for
  every undefined symbol, the library the linker found it in (the two-level namespace ordinal).
  `imports.py update` reads exactly that.
- **21 symbols from the toolchain's static archives.** `libc++.a` and `libios3compat.a` have
  members that no binary pulls in today and that import further symbols (file-system calls of
  `std::filesystem`, for example). `imports.py archives` lists them. Their notes begin with
  `toolchain archive`. An archive does not say which library provides a symbol, so each name was
  looked up, yes or no, in the SDK's library on 2026-11-22: 20 are in libSystem, one in libgcc_s.
  (A 22nd undefined name, `___dso_handle`, is made by the linker and needs no entry.)
- **3 symbols from a configure check.** CMake's `FindThreads` links a small test program
  (`CMAKE_HAVE_LIBC_PTHREAD`) while mbedTLS and cspot are configured. It calls `pthread_atfork`,
  `pthread_cancel` and `pthread_exit`, which no binary of ours uses. The program is thrown away
  after the check, so it is in no import table; the link maps of the build show it
  (`arm/maps/mbedtls/cmTC_*.map`). Without the three entries the test program does not link
  against the stubs, and the check gives another answer than it gives with the SDK. Looked up in
  the SDK's libSystem on 2026-11-22.
- **The re-exports of Foundation and the two links, by hand.** A binary does not record why it names
  a library. That Foundation re-exports libobjc and CoreFoundation shows in the binaries all the
  same: `voltest` is linked with `-framework Foundation` alone and names all three. The two are
  listed in the order in which the SDK's Foundation names them. A linked binary does not depend on
  that order; the order of the files in a link map does. The two links are the names that
  `-lSystem` (every link) and `-lobjc` (`dock-audiod`) search for.

Two symbols of the lock-screen bundles are in no library and not in the manifest:
`_OBJC_CLASS_$_SBAwayViewPluginController` and its metaclass are left to a flat lookup at load time
(`-U`), and SpringBoard provides them.

## Adding a symbol

A link against the stubs that needs a new system symbol fails with `Undefined symbols`. A symbol may
be added to the manifest only after it was shown to exist in that library on iPhone OS 3.1.x, in
one of two ways:

1. **A link against the SDK that the developer supplies.** Build the binary with `IOS3_SDK` set to
   the real `iPhoneOS3.1.2.sdk`. The linker finds the symbol in the SDK's library, and the binary
   records which library that was. Then run `imports.py update` (below) with that build.
2. **A `dlsym()` probe on a device that runs iPhone OS 3.1.x.** Write the `sym` row by hand, with a
   note that names the device, the OS version and the date, and name the library the symbol was
   found in.

No name is copied from the export list of an SDK library. After a change, `imports.py validate`
checks the file and `build-stubs.sh` builds the new stubs.

A new library is added the same way: `imports.py update` writes its `lib` row from the binary that
links it. Its re-exports, and a `link` row for a name such as `libm.dylib`, are added by hand.

## imports.py

```sh
cd toolchain/sysroot/stubs
# rewrite the manifest from all linked binaries (the build's outputs and the smoke-test files)
./imports.py update manifest.tsv BUILD/arm-out smoke/=SMOKE-DIR
./imports.py update --check manifest.tsv BUILD/arm-out smoke/=SMOKE-DIR   # change nothing, exit 1 if it would
./imports.py validate manifest.tsv                 # is the file well-formed
./imports.py stubs manifest.tsv SYSROOT            # do the libraries in SYSROOT match the manifest
./imports.py archives manifest.tsv TOOLCHAIN/armv6-ios3/lib/*.a   # what the static archives can import
```

`update` rebuilds the *imported by* field from the files it is given, so it must be given all of
them. It keeps the notes, the re-exports and the links, never drops a library, and drops a symbol
only when it is left with neither an importer nor a note; it says so when it does. It needs
python3's standard library and nothing else.

## What is verified for the stubs

On 2026-11-22, with toolchain revision 3 and the host's clang 18.1.3, against the reference build
of the same day (the nine outputs built with the SDK) and the seven smoke-test files:

- **The stubs.** Two runs of `build-stubs.sh`, with no SDK in the environment, gave identical
  trees. For each of the eleven stubs, `otool -l` showed the install name and versions of the
  manifest, the two re-exports for Foundation and no other load command that names a library, and
  the export list from `nm` equalled the manifest.
- **The manifest.** `imports.py update --check` on the reference outputs and the smoke-test files
  reports the committed manifest as up to date. `imports.py archives` finds every symbol the
  toolchain's four archives can import.
- **Re-linking.** The checks below used a test sysroot: the generated stubs, plus symlinks into the
  SDK for the headers only (`usr/include` and each framework's `Headers`). Each of the sixteen
  files was linked again from the object files of the reference build, with its original link
  command, then stripped and signed as the build does. All sixteen came out byte-identical to the
  reference: `dock-audiod`, `dock-lockctl`, `dock-raop`, `cspot-ios3`, `NowPlaying`, both
  lock-screen bundles, `DockRemote.dylib`, `voltest`, and the smoke-test files `hello`, `cxx`,
  `cxx-libcxx`, `objc`, `loader`, `libt.dylib`, `t.bundle`. (`DockRemote.dylib` is compiled and
  linked in one command, so it was compiled again as well.)
- **A full build.** `tools/build-arm.sh`, from nothing, with `IOS3_SDK` set to that test sysroot,
  ended with `result: OK`, and the sha256 of all nine outputs equalled those of the build against
  the SDK. So did all 405 object files and the `CMakeCache.txt` of the four CMake build
  directories, and no configure check failed for an undefined symbol. `armv6-ios3-check` gave the
  same verdicts as against the SDK.
- **The allow-list.** A program that calls `getppid()`, which is not in the manifest, links against
  the SDK and fails against the stubs with `Undefined symbols for architecture armv6: "_getppid"`.

Nothing was run on a device for this, and nothing needed to be: the binaries are the same bytes as
the ones built with the SDK.

## What the stubs cannot tell you

- **`armv6-ios3-check` proves less.** It compares a binary's imports with the libraries in
  `IOS3_SDK`. Against the SDK that shows that each symbol exists in the real library. Against the
  stubs it shows that each symbol is in the manifest, which the link has shown already.
- **Configure checks ask the manifest, not iPhone OS.** A check that links a test program
  (`check_function_exists` and the like) fails for a function that is not in the manifest, although
  the system may have it. The build then goes on with another configuration and says nothing.
  Today the only check that asks for a function is the one named above. When a dependency or its
  version changes, look for failed checks:
  `grep -l 'Undefined symbols' BUILD/arm/*/CMakeFiles/CMakeConfigureLog.yaml`.
- **Weak definitions.** A stub does not say that a library defines a symbol weakly, and it lists
  only what our binaries import. If a binary were bound to a weak definition, or itself defined a
  symbol that one of its libraries exports as a weak definition, a link against the SDK and a link
  against the stubs could give different binaries. Neither is the case for any of the sixteen
  files (looked at on 2026-11-22; of the libraries here only libstdc++ exports weak definitions).
  `imports.py update` refuses a binary that is bound to a weak definition; the second case it
  cannot see.
- **Only what is used.** Libraries and names that nothing here links are missing on purpose:
  `-lm`, `-lpthread` and `-lc`, for example, are not there, because no link of the ARM build
  asks for them.

## Stage 2: the headers

`headers/build-headers.sh OUTDIR` writes

```
OUTDIR/usr/include/...                                               229 headers
OUTDIR/System/Library/Frameworks/CoreFoundation.framework/Headers/... 35 headers
```

from nothing: from Apple's open-source releases on https://github.com/apple-oss-distributions,
fetched by commit, and from the files in `headers/`. It reads no file of the SDK and needs no SDK.
The script's header describes the recipe and its options; this section says what comes from where
and what was compared.

```sh
toolchain/sysroot/headers/build-headers.sh OUTDIR                        # fetches what it needs
IOS3_OSS_CACHE=DIR toolchain/sysroot/headers/build-headers.sh OUTDIR     # DIR/<repo>/<tag>/ checkouts
IOS3_OSS_CACHE=DIR IOS3_OSS_CACHE_FILL=1 toolchain/sysroot/headers/build-headers.sh OUTDIR
                                         # what DIR lacks is fetched into it and kept for the next run
```

Besides git, sha256sum, awk and sed it needs `unifdef`, the host clang (as the preprocessor for mig),
a host C compiler, `bison` and `flex` (to build mig, see below; on Ubuntu 24.04
`apt-get install unifdef flex bison`). `OUTDIR` must not exist yet, or be
empty. Every published file is checked against the sha256 of its row in `sources.tsv` before it is
used; the result is checked against `HEADERS.sha256`. On any mismatch the script stops and writes
nothing to `OUTDIR`. Two runs give the same bytes.

### The licence of these files

The headers are Apple's files, under the licences their releases state: the Apple Public Source
License 2.0 (the `APPLE_LICENSE` file of each release; xnu, Libc, CF, objc4, dyld, Libinfo, Libm,
CarbonHeaders, and bootstrap_cmds for mig), with the Berkeley and Carnegie Mellon notices
that many BSD and Mach headers carry as well, and a BSD-style licence for `dns_sd.h`
(mDNSResponder). A file without a licence comment is under the licence of its release. They are not
part of this repository: the script fetches them. What the repository holds is the list
(`sources.tsv`, MIT), the script (MIT), this project's own files under `headers/own/` (MIT) and the
patches under `headers/patches/`. **A patch changes a file of Apple's and is under the licence of the
file it changes** (each patch says which); it is not under the MIT licence. The headers that mig
generates carry no licence text; they are made from the release's APSL `.defs` files.
`toolchain/THIRD-PARTY-NOTICES.md` does not describe these headers yet: the sysroot is not part of
the toolchain tarball, and the notice belongs with the stage that ships it.

### The allow-list

`sources.tsv` has one `hdr` row per installed file, and the tree holds exactly those files. That
matters for more than tidiness: libc++ asks with `__has_include` for `sys/_types/_mbstate_t.h` and
`bits/types/mbstate_t.h`, clang's `stdint.h`, `limits.h`, `inttypes.h` and `float.h` include the
sysroot's file of the same name only when one exists, and a header that exists changes what such
code compiles. So a header is added only when a source of the build needs it. 218 headers under
`usr/include` and 38 CoreFoundation headers were read by the build against the SDK; the stage 2 tree
differs from that set as follows:

- left out on purpose: `usr/include/stdint.h` (in the SDK a link to a gcc file). clang's own
  `<stdint.h>` is used instead and gives the same objects (see "What differs" below). Also left out: `CFAttributedString.h`,
  `CFFileDescriptor.h` and `CFStringTokenizer.h`, which Apple never published and which nothing in
  the build uses (CoreFoundation.h does not name them here).
- added: the twelve small headers that the xnu-4570.1.46 versions of the ARM headers include
  (`sys/_types/_int8_t.h` ... `_uintptr_t.h`, `sys/_types/_os_inline.h`, `mach/machine/_structs.h`).
  `sys/_types/_mbstate_t.h`, which libc++ probes for, is not among them and stays absent.

### Where the headers come from

| Category | Files | How |
|---|---:|---|
| published file as is | 144 | copied |
| published file + the release's install processing | 94 | `libc` 14, `xnu` 19, `xnu-bsd-sys` 26, `xnu-4570` 33, `carbon` 2 |
| published file (+ processing) + a patch of this project | 12 | 8 patches, below (the ninth patch is for mig) |
| generated by mig from published `.defs` | 13 | `mig` |
| file of this project | 1 | `own/CFNotificationCenter.h` |

The processing rules are those of the releases' own makefiles, with the settings of an iPhone OS
build (the script's header quotes them): `libc` is Libc's `installhdrs` step (the `//Begin-Libc`
blocks removed, `unifdef` with the iPhone feature set), `xnu` and `xnu-4570` are xnu's
`SINCFRAME_UNIFDEF` (for 4570 with `PLATFORM=iPhoneOS`), `xnu-bsd-sys` adds `bsd/sys/Makefile`'s
`-DPRODUCT_iPhone` (it selects the iPhone settings of `sys/cdefs.h`), and `carbon` is
CarbonHeaders' `sed` of `@CONFIG_EMBEDDED@`, `@CONFIG_IPHONE@`, `@CONFIG_IPHONE_SIMULATOR@` to 1, 1, 0
(it makes `TargetConditionals.h` describe the phone).

The releases used, all from https://github.com/apple-oss-distributions:

| Repository | Tag | Commit | Headers |
|---|---|---|---:|
| xnu | xnu-1228.15.4 | 083332250d12f1d698a5c3282a18fee46847ec92 | 68 |
| xnu | xnu-1456.1.26 | 3ca3bd5593ac4736fb236ffd68a7ccb91ee6c0f0 | 49 (13 of them by mig) |
| xnu | xnu-4570.1.46 | 76e12aa3ea3036173a61fa1081c3be890e626e79 | 50 |
| Libc | Libc-498.1.7 | 7198b93dbba0617e5927ff5a5e783f0e54ff957b | 45 |
| CF | CF-476.19 | e92003b7fb6f7be08ee3723e6004ec155c88527a | 33 |
| CF | CF-550 | 83e0f91d8f5285e9e0e914cda406754ed72251eb | 1 |
| CarbonHeaders | CarbonHeaders-18.1 | 214a9ae7ab3c0c78ddb50328ca226e6313c4c782 | 6 |
| objc4 | objc4-437.1 | 2b9fa8c3a278f9089126d1df603713b93878c22d | 4 |
| Libinfo | Libinfo-278.0.3, Libinfo-324.1 | 0e3671f7da4f29cbcf20a3f9b49ff3a1b07b8e6f, 696adf4cfed7ac64ae7032bafee0312ea9adfe48 | 2 + 1 |
| Libm | Libm-315 | 845f432413a66ea992f4797c66157c9824421719 | 2 |
| dyld | dyld-132.13 | aa1d7f7a6fb257f393648940bfc32c06fa886458 | 1 |
| mDNSResponder | mDNSResponder-212.1 | 601a08a9012e11cd93ab21902edef64cdbf3a2ae | 1 |
| bootstrap_cmds | bootstrap_cmds-72 | 46504af5bfee6086d69d889c07bd5ca865b29fd1 | (mig itself) |

The iPhone OS 3 releases of xnu and Libc were never published. The headers are therefore taken
from the Mac OS X releases of the same time (10.5.x and 10.6: xnu-1228, xnu-1456, Libc-498), which
match most of them byte for byte, and the ARM-specific headers from xnu-4570.1.46 (2017), the only
published release with `bsd/arm`, `osfmk/mach/arm` and `libkern/arm`. Where several releases have a
file, the one whose processed text is closest to what the build needs was chosen.

### The patches and the file of our own

Each patch begins with a comment that says what it changes, why, and which objects depend on it.

| Patch | Files | Changed lines | Why |
|---|---|---:|---|
| `arm-dispatch.patch` | `machine/_structs.h`, `machine/setjmp.h`, `mach/task.h`, `mach/thread_act.h` | +8 | the 2009 Mac releases choose the header of the architecture among ppc and x86 only and stop with `#error` on ARM; no published version of these four has an ARM branch |
| `arm-setjmp.patch` | `arm/setjmp.h` (from xnu-1228 `bsd/i386/setjmp.h`) | +8 | the user-space ARM `setjmp.h` was never published; the i386 file declares the same functions, the patch adds the ARM size of `jmp_buf` (28 ints) |
| `sys-cdefs-arm.patch` | `sys/cdefs.h` | +4 | ARM branch of the long-double section (long double is double, no `$LDBL128` variants) |
| `mach-arm-vm_param.patch` | `mach/arm/vm_param.h` (xnu-4570) | +5 −7 | the page size is the constant 4096 on 32-bit ARM, not the run-time variable of the 64-bit era |
| `libkern-arm-osbyteorder.patch` | `libkern/arm/OSByteOrder.h` (xnu-4570) | +10 −8 | byte swaps as `rev`/`rev16` asm statements, as the headers of the time had them; clang's builtins give a different instruction order in 9 units |
| `cfbase-mactypes.patch` | `CFBase.h` (CF-476.19) | +1 −1 | iPhone OS's CoreFoundation takes its base types from `MacTypes.h`; the open-source CF-Lite takes them from the kernel's `OSTypes.h` |
| `corefoundation-umbrella.patch` | `CoreFoundation.h` (CF-550) | +3 | the umbrella names `CFNotificationCenter.h` |
| `availability-iphone-3_1.patch` | `Availability.h`, `AvailabilityInternal.h` (CarbonHeaders-18.1) | +2 −1 | the sysroot states iPhone OS 3.1: `__IPHONE_3_1` (30100) and `__IPHONE_OS_VERSION_MAX_ALLOWED` 30100, as the 3.1.2 SDK does; the release stops at 3.0 (stage 3) |
| `migcom-64bit-host.patch` | mig's `mig.c`, `utils.c`, `mig_machine.h` | +3 −10 | lets the 2009 mig run on a 64-bit Linux host (it is not a header of the sysroot) |

`own/CFNotificationCenter.h` (57 lines, MIT): Apple never published `CFNotificationCenter.h`; no CF
release on apple-oss-distributions has it, because the notification center is not part of the
open-source CF-Lite. It declares what `audiod/src/HardwareVolumeApple.cpp` uses: the opaque type
`CFNotificationCenterRef` (a pointer to `struct __CFNotificationCenter`, a name that is part of the
C++ symbol names of functions that take one), the callback type, the four suspension behaviours and
three functions. A source that needs more adds it there.

### mig

Fifteen Mach headers (`mach/mach_port.h`, `mach/vm_map.h`, `mach/arm/task.h`, ...) are not source
files in Apple's releases: the build of libsyscall generates them from `.defs` interface definitions
with Apple's interface generator, mig. `build-headers.sh` does the same:

1. It builds `migcom` from bootstrap_cmds-72 (Mac OS X 10.6) with the host C compiler, bison (as
   yacc) and flex, with `migcom-64bit-host.patch` and the small host headers in
   `own/migcom-host/mach/` (the message types and constants that migcom's sources include; the
   host is not a Mach system). migcom still uses the 4-byte alignment rules of the time, which are
   those of 32-bit ARM.
2. It preprocesses xnu-1456.1.26's `libsyscall/mach/*.defs` for ARMv6 with `-D__MACH30__`, as
   libsyscall's makefile hands them to mig, with the published `osfmk/mach/*.defs` and the headers
   built here on the include path, and runs migcom to write the user header. The ARM
   `machine_types.defs` of the time was not published; its place is taken by the same release's
   `osfmk/mach/i386/machine_types.defs`, which states the same 32-bit types.

Two of the generated headers (`mach/arm/task.h`, `mach/arm/thread_act.h`) came out byte-identical to
the SDK's copies. Most others differ from them by one line (a second `#include <mach/mig.h>`);
`mach/host_priv.h` and `mach/mach_port.h` differ by routines and arguments that xnu-1456 has and the
iPhone OS 3.1 kernel did not, which no source of the build calls. The two `.y`/`.l` generators were
tested with Ubuntu 24.04's bison 3.8.2 and flex 2.6.4, and with bison 1.28 and flex 2.5.4 (Apple's
bison-6.1.1 and flex-9 releases): with either pair the headers match `HEADERS.sha256`, with no change
to how migcom is built. They only build the tool, so the distribution's versions are the ones to use.

### CoreFoundation

The CF-476.19 files differ from the SDK's copies mostly by Apple's 22-line licence comment, which the
SDK's copies do not carry, and further by Windows conditionals, the order of operands in version
comparisons and the version-number constants, none of which reaches an object. Two differences do
matter and are the two CF patches: where the base types come from (`cfbase-mactypes.patch`) and the
notification center, which CF-Lite leaves out (`corefoundation-umbrella.patch` and
`own/CFNotificationCenter.h`).

### Adding a header

A compile that fails with "file not found" for a header of `usr/include` or CoreFoundation, or a
declaration that is missing, is fixed in this order:

1. Find the file in a published release (the closest in time first) and add a `hdr` row: path,
   repository, tag, commit, path in the release, its sha256, and the release's own processing rule.
2. Only if no published version gives the same objects as the SDK did (check with a build against
   the SDK, which a developer can supply): a patch in `patches/`, as small as possible, starting
   with a comment that says what fact it encodes and why it is needed.
3. Only for a header Apple never published: a file in `own/` that declares, in this project's own
   words, what the sources use.

Then run `build-headers.sh --update-pins OUTDIR`, which rewrites `HEADERS.sha256`, and review the
change.

### What is verified

On 2026-11-22, with toolchain revision 3 and the host's clang 18.1.3, against the reference build of
the same day (built with the SDK). The test sysroot was the stage 1 stubs, the stage 2 headers, and
symlinks to the SDK's `Headers` directories for the other frameworks (stage 3 is not done):

- **Reproducible.** Two runs of `build-headers.sh` from nothing, one from a local cache of the
  releases and one fetching all 14 releases from GitHub, gave identical trees (264 files, no
  symlink) that match `HEADERS.sha256` and hold exactly the `hdr` rows of `sources.tsv`.
- **No copy of the SDK.** 147 of the 264 files have the same bytes as a file of the SDK. Each of them
  is a published file, as is or after its release's processing rule (145), or one of the two mig
  headers named above, generated from published `.defs` files. No patched file and no file of our
  own equals a file of the SDK.
- **Every compile of the build.** All 409 compiler invocations of the reference build were replayed
  against the test sysroot: all 409 objects are byte-identical to those compiled against the SDK
  (mbedtls 113, cspot 247, airplay 19, audiod 12, nowplaying 11, the lock-screen plugin 2 + 2,
  DockRemote 2, voltest 1). Without `mach/mach_port.h` and `CFNotificationCenter.h` in the tree,
  34 of them fail to compile; `-H` shows the headers coming from the test sysroot.
- **ABI probes.** A C file with the size, alignment and field offsets of about 100 types and
  structures, about 150 constants (among them every macro that third-party code tests with
  `#ifdef` to choose an implementation: `CLOCK_MONOTONIC`, `SO_NOSIGPIPE`, `MSG_NOSIGNAL`,
  `TCP_KEEPALIVE`, `AI_*`, `IPV6_*`, `O_CLOEXEC`, ...), the assembler names of about 120 libSystem
  functions (with their `$UNIX2003`, `$INODE64`, `$DARWIN_EXTSN` suffixes) and the byte-swap
  inlines; a C++ file with the mangled names of functions over about 90 system and CoreFoundation
  types; an Objective-C file with the `@encode` strings of 24 types; and a program that calls the
  setjmp family: all compile to the same object against the SDK and against the test sysroot.
- **A full build.** `tools/build-arm.sh`, from nothing, with `IOS3_SDK` set to the test sysroot,
  ended with `result: OK`. The sha256 of all nine outputs equal those of the reference build, and so
  do all 405 object files, the members of all 15 static archives (the archive files differ only by
  the time stamps that `ar` writes), the `CMakeCache.txt` of the four CMake build directories, and
  every other file of `arm-out` but the report and DockRemote's link map (which name temporary
  files). The compiles read their headers from the test sysroot only (ninja's dependency logs).

### What differs and does not matter today

The headers are not the SDK's headers, and the checks above say only that nothing this build
compiles comes out differently. Differences that a future source could notice:

- `Availability.h` and `AvailabilityInternal.h` are CarbonHeaders-18.1's with
  `availability-iphone-3_1.patch` (stage 3; until then they came from xnu-4570.1.46, because the
  SDK's UIKit headers named iPhone OS 3.1, and set `__IPHONE_OS_VERSION_MAX_ALLOWED` to 10.3).
  `__IPHONE_OS_VERSION_MAX_ALLOWED` is 30100 now, as in the SDK. The availability macros for 3.1
  itself (`__AVAILABILITY_INTERNAL__IPHONE_3_1...`) are not there, because nothing uses them: a
  declaration annotated with `__IPHONE_3_1` does not compile until they are added.
- There is no `usr/include/stdint.h`, deliberately, so clang's own is used: same types and limits,
  but `__WORDSIZE` is not defined, and the limit macros are spelled with clang's builtins. The only
  published file that defines `__WORDSIZE` is gcc_42-5577's `more-hdrs/stdint.h` (the file the SDK's
  `stdint.h` link leads to; all 409 compiles give the same objects with it). It carries no licence of
  its own and its release is GCC's, under the GPL, so it is not used. No APSL release has one:
  Libc-498.1.7 (and Libc-583, Libc-594.9.5) have no `stdint.h`, and xnu's
  `EXTERNAL_HEADERS/stdint.h` does not define `__WORDSIZE`.
- The ARM headers come from 2017 (xnu-4570.1.46): `arm/_structs.h` is that release's
  `bsd/arm/_mcontext.h`, whose machine context has the same layout but is called
  `struct __darwin_mcontext32` (the SDK: `__darwin_mcontext`), and `mcontext_t` is declared by
  `<signal.h>`; `VM_MAX_ADDRESS` is 0x80000000 (the SDK: 0x40000000); `MACHINE_THREAD_STATE_COUNT`
  counts the unified thread state; `EXC_TYPES_COUNT` and the 64-bit ARM types are those of that
  release; `ARM_PGBYTES` and `ARM_PGSHIFT` do not exist (`PAGE_SIZE` is 4096 all the same).
- The Mac OS X 10.5/10.6 headers lack a few iPhone OS additions and have a few later ones:
  `AF_IEEE80211` is missing (`AF_MAX` is 37, the SDK: 38), `EVFILT_SESSION` exists
  (`EVFILT_SYSCOUNT` is 11, the SDK: 10), `kDNSServiceInterfaceIndexP2P` is missing from `dns_sd.h`,
  `VM_BEHAVIOR_FREE` is missing, and mig's `mach/host_priv.h` and `mach/mach_port.h` declare the
  xnu-1456 routines (`mach_port_kobject`, for example).
- CoreFoundation has no `CFAttributedString.h`, `CFFileDescriptor.h` or `CFStringTokenizer.h`, and
  `CFNotificationCenter.h` declares only what this repository uses.

## Stage 3: framework headers of our own

Apple publishes no source for the headers of Foundation, UIKit, CoreGraphics, QuartzCore,
AudioToolbox, CoreAudio and AVFoundation. The headers of these frameworks are therefore files of this
project, under `frameworks/<Name>.framework/Headers/`, MIT-licensed, written for this repository:

| Framework | Header files | Declares | Used by |
|---|---|---|---|
| Foundation | `Foundation.h` | base types, the NSObject protocol and root class, strings, numbers, dates, data, collections, files, bundle, timer, process info, exception | NowPlaying, the lock-screen plugin, DockRemote, voltest; the start-up smoke test (`objc.m`) |
| UIKit | `UIKit.h` | the application, events, device and screen, views, controls, tables, view controllers and bars, image drawing, the adopted protocols | NowPlaying, the lock-screen plugin |
| CoreGraphics | `CoreGraphics.h` | `CGFloat`, `CGPoint`, `CGSize`, `CGRect`, `CGRectMake`, `CGSizeMake`, `CGRectZero`, `CGRectGetMaxY`, seven drawing calls | through UIKit |
| QuartzCore | `QuartzCore.h` | `CALayer` with two properties | NowPlaying (rounded cover) |
| AVFoundation | `AVFoundation.h` | `AVAudioPlayer` (init from data, play, loops) | NowPlaying (`NPRemoteControl.m`) |
| AudioToolbox | `AudioToolbox.h`, `AudioQueue.h`, `AudioServices.h` | an output AudioQueue and its buffers; the audio session (initialise, category, route, listeners) | the AudioQueue sinks of airplay, cspot and audiod; NowPlaying |
| CoreAudio | `CoreAudioTypes.h` | `AudioStreamBasicDescription`, the linear PCM format constants | through AudioQueue.h |

Nine files, 1176 lines. Security, AudioUnit and OpenGLES have no headers here: the SDK's Foundation and
UIKit umbrellas named them, nothing in this repository uses them.

### The rules they were written under

- **An allow-list.** A header declares what this repository's sources use and nothing else, like the
  stub manifest (stage 1). The file names are those the sources include (`Foundation/Foundation.h`,
  `UIKit/UIKit.h`, `QuartzCore/QuartzCore.h`, `AVFoundation/AVFoundation.h`,
  `AudioToolbox/AudioToolbox.h`, `AudioToolbox/AudioQueue.h`, `AudioToolbox/AudioServices.h`), plus
  `CoreGraphics/CoreGraphics.h` and `CoreAudio/CoreAudioTypes.h`, which hold what two of them share.
  The arrangement inside each file is this project's own.
- **Where the facts come from**, in this order: (a) this repository's sources, which show every class,
  method, function, type and constant that is used (and `audiod/hosttest/mock/mock_audiotoolbox.h`,
  the host tests' own stand-in for AudioToolbox, which has the four-character codes); (b) this
  repository's own reference objects and binaries, built with the SDK: selectors, method type
  encodings, the protocol descriptions the compiler wrote, the instance sizes of our subclasses,
  string constants, imported symbol names; (c) general knowledge of the documented API; (d) as a last
  resort a look-up of one named identifier in the SDK's header, recorded with the reason. No look-up
  of kind (d) was needed. No file, excerpt or comment of the SDK is in these headers.
- **Each header begins with** the project's licence lines and a comment that says why the file is
  ours, what it declares and which interface facts it states (the model is
  `headers/own/CFNotificationCenter.h`).

### Where the text has to follow Apple's interface

The compiler writes some interface facts into the objects of our own classes. There the headers say
exactly what the SDK said, taken from this repository's reference objects (protocol descriptions read
from a test link of each object, instance sizes from the `instanceStart` of our subclasses):

- **Protocols that classes of ours adopt** are given in full: every method, required or optional, in
  the recorded order, with its types: `NSObject` (19 methods), `UIApplicationDelegate` (15),
  `UIScrollViewDelegate` (10), `UITableViewDataSource` (11; it inherits `NSObject`),
  `UITableViewDelegate` (19; it inherits `NSObject` and `UIScrollViewDelegate`),
  `UIAlertViewDelegate` (6), `UITextFieldDelegate` (7).
- **Instance sizes of the classes ours inherit from**, as opaque reserved storage: `UIApplication` 57
  bytes, `UIWindow` 88, `UIViewController` 92 (`NSObject`: its one instance variable, 4).
- **`NSException`** carries the `objc_exception` attribute, so that a `@catch` refers to Foundation's
  exception type instead of defining one (the reference object of `DockRemoteHook.mm` imports
  `_OBJC_EHTYPE_$_NSException`).
- **Names that reach objects**: structure tags that appear in type encodings and C++ names
  (`CGRect`, `CGPoint`, `CGSize`, `_NSRange`, `_NSZone`, `OpaqueAudioQueue`, `AudioQueueBuffer`),
  selectors, the types of results and arguments, enumeration values and four-character codes, the
  names of the two `NSString` constants and the route-change key strings.

### Adding a declaration

1. Start from the source that needs it. A missing function, type, class or constant is a compile
   error. **A missing method is only a warning** ("instance method '-x' not found",
   `-Wobjc-method-access`), and the compiler then sends the message as if it returned `id` and took
   promoted arguments (a `float` passed as a `double`): code that compiles and is wrong. The build
   scripts of the Objective-C components (NowPlaying, the lock-screen plugin, DockRemote, voltest)
   therefore compile with `-Werror=objc-method-access`, which makes it an error for instance and class
   messages alike. One related case stays a warning: when the receiver's class lacks the method but
   another class declares one of that name, clang says "'X' may not respond to 'y'" and uses that
   other declaration. This warning has no flag of its own, so it cannot be made an error by itself;
   look for it in the build logs.
2. Declare it in the header of its framework, in the documented shape (selector, result and argument
   types, getter name of a property, enumeration value), and nothing more.
3. Confirm it, in one of two ways: compile the unit also against an SDK that a developer supplies and
   compare the objects (they must be byte-identical; `IOS3_SDK` names the SDK), or test the binary on
   a device that runs iPhone OS 3.1.x.
4. If a class of ours adopts a protocol that is not here yet, or inherits from a framework class whose
   size is not given here, the protocol's full method list or the class's instance size must come
   from an object compiled against the SDK (rules above).
5. Run `build-sysroot.sh --update-pins OUTDIR`, which rewrites `frameworks/HEADERS.sha256`, and
   review the change.

### Building the complete sysroot

```sh
toolchain/sysroot/build-sysroot.sh OUTDIR                       # fetches the published releases
IOS3_OSS_CACHE=DIR toolchain/sysroot/build-sysroot.sh OUTDIR    # DIR/<repo>/<tag>/ checkouts
IOS3_SDK=OUTDIR tools/build-arm.sh                              # the build against that sysroot
```

`tools/build-arm.sh` without `IOS3_SDK` runs the script itself (next section).

`OUTDIR` must not exist yet, or be empty. The script checks the framework headers against
`frameworks/HEADERS.sha256` first, runs `stubs/build-stubs.sh` and `headers/build-headers.sh` (each
checks its own part), puts the three parts together (no file may come from two parts), and checks
that no symlink leaves the tree. It needs what the two scripts need and no SDK. Two runs give the same
bytes. It prints the layout and a one-line summary:

```
usr/lib (stubs)                                           4 files, 2 symlinks
usr/include                                             229 header files
System/Library/Frameworks/AVFoundation.framework          1 header files + stub library
System/Library/Frameworks/AudioToolbox.framework          3 header files + stub library
System/Library/Frameworks/CoreAudio.framework             1 header files
System/Library/Frameworks/CoreFoundation.framework       35 header files + stub library
System/Library/Frameworks/CoreGraphics.framework          1 header files + stub library
System/Library/Frameworks/Foundation.framework            1 header files + stub library
System/Library/Frameworks/QuartzCore.framework            1 header files + stub library
System/Library/Frameworks/UIKit.framework                 1 header files + stub library
SHA256SUMS (the stubs)
build-sysroot: OK: 285 files and 2 relative symlinks in OUTDIR: the stubs, 264 published headers, 9 framework headers of this project; nothing read from an SDK
```

### What is verified

On 2026-11-22, with toolchain revision 3 and the host's clang 18.1.3, against the reference build of
the same day (built with the SDK):

- **Reproducible, and no SDK.** `build-sysroot.sh` ran twice from nothing, with `IOS3_SDK` unset and
  inside a mount namespace in which `/home/user/sdk` (where the SDK lies on the test machine) was an
  empty file system: once from a local cache of the releases, once fetching all 14 releases from
  GitHub. The two trees are identical: 285 files and 2 relative symlinks (`libSystem.dylib`,
  `libobjc.dylib`), none leaving the tree. 147 of the files have the same bytes as a file of the SDK;
  they are the same 147 published files that stage 2 lists (above). No framework header of this
  project, no patched file and no stub equals a file of the SDK.
- **Every compile of the build.** All 409 compiler invocations of the reference build were replayed
  against that sysroot: all 409 objects are byte-identical to those compiled against the SDK (mbedtls
  113, cspot 247, airplay 19, audiod 12, nowplaying 11, the lock-screen plugin 2 + 2, DockRemote 2,
  voltest 1). Without the framework headers 21 of them fail to compile. The compiles print no warning
  that a method or class was not found. `-H` on a NowPlaying and an audiod unit shows every header
  coming from the sysroot, the toolchain, clang's own directory or the repository.
- **The values matter.** With two enumeration values changed on purpose
  (`kAudioSessionProperty_AudioCategory`, `UITextAlignmentCenter`), 7 objects differ.
- **A full build without the SDK.** `tools/build-arm.sh` with `IOS3_SDK` set to the sysroot, inside a
  mount namespace in which `/home/user/sdk` was empty, ended with `result: OK`. The sha256 of all nine
  outputs equal those of the reference build, and so do all 405 object files, the members of all 15
  static archives, the `CMakeCache.txt` of the four CMake build directories, and every other file of
  `arm-out` but the report and DockRemote's link map (which name temporary files). The link maps and
  ninja's dependency logs name no path of the SDK. (The dependency checkouts under `deps/`, mbedTLS
  and cspot's upstream, were copied from the reference tree, not fetched again; the build checks them
  against their pinned commits.)
- **The start-up smoke test** (seven programs, docs/HANDOFF.md) against the sysroot: `hello`,
  `cxx-libcxx`, `objc`, `loader`, `libt.dylib` and `t.bundle`, and their objects, are byte-identical to
  those built with the SDK. `cxx` does not compile: it uses the SDK's libstdc++ 4.2.1 headers, which
  this sysroot does not have (see below).
- **Availability.** `__IPHONE_OS_VERSION_MAX_ALLOWED` is 30100, as with the SDK.

### What these headers cannot tell you

They declare what today's sources use and were checked by what today's build compiles. A future
source could notice:

- **Anything else is missing.** A class, function, constant or header file that is not here does not
  compile. A method that is not here does not compile in the build scripts
  (`-Werror=objc-method-access`); elsewhere it compiles with a warning and wrong code (above).
- **Partial structures.** `AudioQueueBuffer` declares only its first three members; its size must not
  be taken. Enumerations list only the values in use.
- **Subclassing.** Only `NSObject`, `UIApplication`, `UIWindow` and `UIViewController` have their
  instance sizes. `UIResponder` and `UIView` have no storage of their own here (their sizes are counted
  in their subclasses'), nor does any other class. A new subclass of any other framework class compiles
  but gets wrong instance-variable offsets until the size is added (step 4 above).
- **Protocols of framework classes.** Which protocols a framework class adopts is declared only where it
  is needed: `NSArray` adopts `NSFastEnumeration`; `UITextField`'s text input traits are declared as
  its own properties, without the `UITextInputTraits` protocol. Only the seven protocols above exist.
- **Fewer diagnostics.** No availability, deprecation or format attributes on the framework
  declarations: misuse that the SDK's headers would warn about compiles silently.
- **No C++ standard library headers.** The SDK's libstdc++ 4.2.1 headers (`usr/include/c++/4.2.1`) are
  not part of the sysroot, so C++ code must use the toolchain's libc++ (`IOS3_CXX_RUNTIME=libc++`).
  Every C++ compile of the build does; the smoke-test program `cxx` is the only user of the old
  library.
- **Missing frameworks.** Security, AudioUnit and OpenGLES have no headers.

## How `tools/build-arm.sh` uses the sysroot

```sh
tools/build-arm.sh                  # the project's own sysroot, made in $BUILD_ROOT/sysroot/root
IOS3_SDK=DIR tools/build-arm.sh     # DIR as it is: Apple's iPhoneOS3.1.2.sdk, or a sysroot of build-sysroot.sh
```

- **`IOS3_SDK` unset or empty: the project's own sysroot.** The script runs `build-sysroot.sh` into
  `$BUILD_ROOT/sysroot/root` and gives that directory to the toolchain wrapper as `IOS3_SDK`. It makes
  the sysroot only when it is missing or when the stamp `$BUILD_ROOT/sysroot/stamp` differs. The stamp
  holds a sha256 over every file under `toolchain/sysroot/` and what identifies the tools that build
  it: the toolchain's linker (by sha256) and the versions of the host clang, the host C compiler,
  `unifdef`, `bison`, `flex` and `python3`. A tree that no longer matches its list
  `$BUILD_ROOT/sysroot/CONTENTS` (the sha256 of every file, the target of every symlink) is made again
  as well. When a new tree has the same contents as the intact tree in place (after a change to this
  README, for example), the tree in place is kept, with its file times, so nothing compiles again. In
  this case only, the script also needs `unifdef`, `flex` and `bison` (Ubuntu 24.04:
  `apt-get install unifdef flex bison`).
- **The cache.** The first run fetches Apple's releases into `$BUILD_ROOT/deps/apple-oss/<repo>/<tag>/`
  (`build-headers.sh` with `IOS3_OSS_CACHE` and `IOS3_OSS_CACHE_FILL=1`); later runs read them from
  there and check them against their pinned commits and sha256 values all the same. A cached release
  at another commit, or without a file that `sources.tsv` now names, is fetched again; a cached file
  with another sha256 stops the build. A run that finds the sysroot up to date uses no network, and
  neither does a rebuild of the sysroot from a complete cache.
- **`IOS3_SDK` set: a supplied directory.** It is used as it is. It must hold what every sysroot of
  this build holds (`usr/lib/libSystem.B.dylib`, `usr/include/stdlib.h`, the CoreFoundation, Foundation
  and UIKit headers and UIKit's library), or the script stops before it builds anything. This is how a
  developer builds against Apple's SDK, which is still needed for one thing: confirming a new symbol
  ("Adding a symbol") or a new declaration ("Adding a header", "Adding a declaration") against the
  real libraries and headers. A sysroot made earlier by `build-sysroot.sh` can be named the same way.
- **The report.** The second line of `report.txt` names the sysroot. For the project's own: its path
  and `contents` with the first 16 hex digits of the sha256 of `CONTENTS`. For a supplied directory:
  its path, "supplied in IOS3_SDK", and whether it is Apple's SDK (it has `SDKSettings.plist` and
  `usr/lib/crt1.o`, which `build-sysroot.sh` never makes).
- **Rebuilds.** The sysroot (its path, and for the project's own its contents) is part of the stamps of
  the CMake build directories and of the ARMv6 mbedTLS, so a build against another sysroot compiles
  everything again.
- **The checks are the same for both kinds**: no object file under the sysroot linked (the link maps),
  the start-up objects, the notice, the instruction scan, `armv6-ios3-check`. Against the project's own
  sysroot `armv6-ios3-check` confirms that each import is in the stub manifest, which the link has
  shown already; against Apple's SDK it confirms the import against the real library.
- **Objective-C.** The four Objective-C build scripts compile with `-Werror=objc-method-access`
  ("Adding a declaration").

Not changed, because they belong to the toolchain and a change would be a new toolchain revision:
the wrapper `armv6-ios3-clang` and `share/armv6-ios3/toolchain.cmake` (their messages still ask for
"your iPhoneOS3.1.2.sdk directory"; any sysroot of this build passes their test; the wrapper's
`-cxx-isystem` path for `IOS3_CXX_RUNTIME=libstdc++` leads into the SDK's libstdc++ headers, which this
sysroot does not have), `armv6-ios3-check` (it says "SDK" for whatever `IOS3_SDK` names), and
`toolchain/THIRD-PARTY-NOTICES.md` (the sysroot is not part of the toolchain tarball, so the published
headers' licences are not listed there). `toolchain/pack.sh` compares the staged files with the
files under `IOS3_SDK` only when that is Apple's SDK, by the same test as the report, and says when it
does not.

### What is verified for the integration

On 2026-11-22, with toolchain revision 3 and the host's clang 18.1.3, against the reference build of
the same day (built with the SDK):

- **The default build from nothing.** `tools/build-arm.sh` with `IOS3_SDK` unset, from an empty build
  root at the reference path, inside a mount namespace in which `/home/user/sdk` (where the SDK lies on
  the test machine) was an empty file system: it fetched mbedTLS, cspot's upstream and the 14 releases,
  made the sysroot and ended with `result: OK`. The sha256 of all nine outputs equal those of the
  reference build, and so do all 405 object files, the members of all 15 static archives and the
  `CMakeCache.txt` of the four CMake build directories. A second run in the same tree, inside a
  network namespace without network, found the sysroot up to date and gave the same nine sha256.
- **With a supplied directory.** With `IOS3_SDK` set to Apple's SDK, and with it set to a sysroot made
  beforehand by `build-sysroot.sh`, each in a build root of its own: `result: OK`, and the outputs that
  embed no build path have the sha256 of the reference build.
- **Every compile.** All 409 compiler invocations of the reference build, replayed against the sysroot
  that the default build made: all 409 objects byte-identical to those compiled against the SDK.
- **bison and flex.** `build-headers.sh` with Ubuntu 24.04's bison 3.8.2 and flex 2.6.4 gives headers
  that match `HEADERS.sha256`.
- **The script's own paths.** A cached release file with another content stops the build; an
  `IOS3_SDK` that names an empty directory is refused before anything is built; a change of a file
  under `toolchain/sysroot/` makes the sysroot again on the next run (and, when its contents come out
  the same, keeps the tree in place and compiles nothing), and a run without a change does not.
- **The Objective-C flag.** A copy of a NowPlaying source with a message to an undeclared instance
  method and one to an undeclared class method does not compile; the unchanged sources give the same
  objects as before (above).
