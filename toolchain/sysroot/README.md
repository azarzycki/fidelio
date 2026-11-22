<!-- SPDX-License-Identifier: MIT
     Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. -->
# A sysroot without Apple's SDK, stage 1: the link libraries

The ARM build of this repository compiles and links against a sysroot: the directory that `IOS3_SDK`
names. Today that has to be a copy of Apple's `iPhoneOS3.1.2.sdk`, which is not part of this
repository and must not be published. The aim of this directory is a sysroot that the project makes
itself.

That is done in stages.

- **Stage 1, this directory: the link libraries.** `stubs/build-stubs.sh` generates stand-ins for
  the dylibs that the linker reads in the SDK. They are made from a list of names,
  `stubs/manifest.tsv`, and from nothing else.
- **The headers are not replaced yet.** Compiling still needs the header files of the SDK. Replacing
  them is a later stage, and nothing here does it.

So this directory does not give a complete sysroot yet. `tools/build-arm.sh`, the toolchain wrapper
and the build notes are unchanged and still ask for the SDK.

## What is here

| File | What it is |
|---|---|
| `stubs/manifest.tsv` | The libraries and, for each, the symbols a stub offers. The only input of the build. |
| `stubs/build-stubs.sh` | `build-stubs.sh OUTDIR` builds the stubs into a new directory tree. |
| `stubs/imports.py` | Writes the manifest from linked binaries, and checks the manifest and the stubs. |

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
  `-lSystem` (every link) and `-lobjc` (`fidelio-audiod`) search for.

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

## What is verified

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
  reference: `fidelio-audiod`, `fidelio-lockctl`, `fidelio-raop`, `cspot-ios3`, `NowPlaying`, both
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

## Later stages

The headers: `usr/include` and the `Headers` directory of each framework. Until they are replaced,
a build needs the SDK for compiling, and the stubs can be tried only next to the SDK's headers, as
in the checks above. When the headers exist, `tools/build-arm.sh`, the toolchain wrapper,
`armv6-ios3-check` and the build notes can be pointed at the project's own sysroot; none of them
is changed here.
