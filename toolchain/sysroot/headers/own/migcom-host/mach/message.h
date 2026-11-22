/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* Host build of migcom only; never installed into the sysroot.
 * migcom's sources include <mach/message.h> and use from it: the port and descriptor type codes of
 * Mach messages (to name them in the generated code and to classify arguments) and sizeof() of the
 * message header, the body count, a descriptor, a port name and natural_t (to lay out the request
 * and reply structures it writes). The host is not a Mach system, so this file states them. The
 * types are those of the 32-bit target (ARMv6, iPhone OS 3): natural_t and mach_port_t are 32 bits,
 * a message header is six 32-bit words, a descriptor is three. Only the sizes matter to migcom, so
 * the structures are given as plain words. */
#ifndef MIGCOM_HOST_MACH_MESSAGE_H
#define MIGCOM_HOST_MACH_MESSAGE_H
#include <stdint.h>
#include <mach/boolean.h>

typedef uint32_t natural_t;
typedef natural_t mach_port_t;
typedef natural_t mach_msg_type_number_t;
typedef natural_t mach_msg_type_name_t;
typedef natural_t mach_msg_descriptor_type_t;

typedef struct { uint32_t words[6]; } mach_msg_header_t;     /* bits, size, remote, local, reserved, id */
typedef struct { uint32_t count; } mach_msg_body_t;           /* number of descriptors */
typedef struct { uint32_t words[3]; } mach_msg_descriptor_t;  /* the largest descriptor: 12 bytes */

/* port rights as message type names */
#define MACH_MSG_TYPE_PORT_NAME       15
#define MACH_MSG_TYPE_MOVE_RECEIVE    16
#define MACH_MSG_TYPE_MOVE_SEND       17
#define MACH_MSG_TYPE_MOVE_SEND_ONCE  18
#define MACH_MSG_TYPE_COPY_SEND       19
#define MACH_MSG_TYPE_MAKE_SEND       20
#define MACH_MSG_TYPE_MAKE_SEND_ONCE  21
#define MACH_MSG_TYPE_COPY_RECEIVE    22
#define MACH_MSG_TYPE_PORT_RECEIVE    MACH_MSG_TYPE_MOVE_RECEIVE
#define MACH_MSG_TYPE_PORT_SEND       MACH_MSG_TYPE_MOVE_SEND
#define MACH_MSG_TYPE_PORT_SEND_ONCE  MACH_MSG_TYPE_MOVE_SEND_ONCE
#define MACH_MSG_TYPE_PORT_ANY(x) (((x) >= MACH_MSG_TYPE_MOVE_RECEIVE) && ((x) <= MACH_MSG_TYPE_MAKE_SEND_ONCE))
#define MACH_MSG_TYPE_POLYMORPHIC     ((mach_msg_type_name_t) -1)

/* descriptor kinds */
#define MACH_MSG_PORT_DESCRIPTOR          0
#define MACH_MSG_OOL_DESCRIPTOR           1
#define MACH_MSG_OOL_PORTS_DESCRIPTOR     2
#define MACH_MSG_OOL_VOLATILE_DESCRIPTOR  3
#endif
