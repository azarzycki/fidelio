/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* Host build of migcom only; never installed. migcom takes sizeof(NDR_record_t), the size of the
 * data-representation record that starts the body of a Mach message: eight bytes. */
#ifndef MIGCOM_HOST_MACH_NDR_H
#define MIGCOM_HOST_MACH_NDR_H
typedef struct { unsigned char ndr_bytes[8]; } NDR_record_t;
#endif
