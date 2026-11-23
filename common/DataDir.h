// The device directory every part of the suite shares: audiod's socket and its active-source and
// remote-target files, the players' status files and covers, settings.conf and the daemons' settings
// manifests, the lock plugin's files, the DockRemote tweak's flag files and logs, and NowPlaying's
// remote-control files (B1.17; the old name is in RENAME.md).
//
// This is the ONE compile-time default. Every program takes its paths from it; at run time the existing
// overrides still apply (--status-dir for the three daemons, --sock / --audiod-sock and AUDIOD_SOCK for
// the socket). A build for another directory passes -DDOCKRECEIVER_DIR='"/some/dir"'.
//
// C, C++ and Objective-C: a plain string literal, so it concatenates with other literals
// (DOCKRECEIVER_DIR "/audiod.sock") and keeps constant-initialised tables constant. Objective-C code
// makes an NSString of it with @DOCKRECEIVER_DIR.
#ifndef DOCKRECEIVER_DATA_DIR_H
#define DOCKRECEIVER_DATA_DIR_H

#ifndef DOCKRECEIVER_DIR
#define DOCKRECEIVER_DIR "/var/mobile/dockreceiver"
#endif

#endif
