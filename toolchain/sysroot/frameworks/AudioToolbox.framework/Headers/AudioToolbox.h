/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <AudioToolbox/AudioToolbox.h> for the sysroot (toolchain/sysroot/frameworks): the framework's
 * umbrella. Our own file (Apple publishes no source for AudioToolbox's headers); it includes the two
 * other AudioToolbox headers of this sysroot. Each says what it declares and why. See
 * ../../../README.md, "Stage 3".
 */
#if !defined(FIDELIO_SYSROOT_AUDIOTOOLBOX_H)
#define FIDELIO_SYSROOT_AUDIOTOOLBOX_H

#include <AudioToolbox/AudioQueue.h>
#include <AudioToolbox/AudioServices.h>

#endif /* FIDELIO_SYSROOT_AUDIOTOOLBOX_H */
