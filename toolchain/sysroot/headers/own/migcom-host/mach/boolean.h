/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* Host build of migcom only (build-headers.sh); never installed into the sysroot.
 * migcom's sources include <mach/boolean.h> for boolean_t, TRUE and FALSE. The host is not a Mach
 * system, so this file gives the three names: boolean_t is an int, as on every Mach target. */
#ifndef MIGCOM_HOST_MACH_BOOLEAN_H
#define MIGCOM_HOST_MACH_BOOLEAN_H
typedef int boolean_t;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#endif
