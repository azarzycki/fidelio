/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <CoreFoundation/CFNotificationCenter.h> for the sysroot (toolchain/sysroot/headers).
 *
 * Why a file of our own: Apple never published CFNotificationCenter.h. No CF release on
 * github.com/apple-oss-distributions has it (checked: CF-476.19 to CF-1153.18), because the
 * notification center is not part of the open-source CF-Lite.
 *
 * What it declares: only what this repository's sources use (audiod/src/HardwareVolumeApple.cpp
 * observes the local center): the opaque center type, the callback type, the suspension-behaviour
 * values, and three functions of CoreFoundation's public interface. A source that needs more adds
 * it here; see ../README.md. CoreFoundation.h includes this file (patches/corefoundation-umbrella.patch).
 *
 * Interface facts this file states, all of them part of how a caller is compiled:
 *   - the center is an opaque pointer to "struct __CFNotificationCenter". The tag is part of the
 *     C++ name of any function that takes a CFNotificationCenterRef (a callback, for example);
 *   - the callback receives the center, the observer, the name, the object and the user-info
 *     dictionary, in this order;
 *   - the suspension behaviour is a CFIndex, and "deliver immediately" is 4 (drop 1, coalesce 2,
 *     hold 3).
 */
#if !defined(FIDELIO_SYSROOT_CFNOTIFICATIONCENTER_H)
#define FIDELIO_SYSROOT_CFNOTIFICATIONCENTER_H

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFDictionary.h>

CF_EXTERN_C_BEGIN

typedef struct __CFNotificationCenter *CFNotificationCenterRef;

typedef void (*CFNotificationCallback)(CFNotificationCenterRef center, void *observer,
                                       CFStringRef name, const void *object, CFDictionaryRef userInfo);

typedef CFIndex CFNotificationSuspensionBehavior;
enum {
    CFNotificationSuspensionBehaviorDrop = 1,
    CFNotificationSuspensionBehaviorCoalesce = 2,
    CFNotificationSuspensionBehaviorHold = 3,
    CFNotificationSuspensionBehaviorDeliverImmediately = 4
};

/* the center of the calling process */
CF_EXPORT CFNotificationCenterRef CFNotificationCenterGetLocalCenter(void);

/* registers callBack for notifications called name (NULL: any name) from object (NULL: any object) */
CF_EXPORT void CFNotificationCenterAddObserver(CFNotificationCenterRef center, const void *observer,
                                               CFNotificationCallback callBack, CFStringRef name,
                                               const void *object,
                                               CFNotificationSuspensionBehavior suspensionBehavior);

/* removes every registration of observer */
CF_EXPORT void CFNotificationCenterRemoveEveryObserver(CFNotificationCenterRef center, const void *observer);

CF_EXTERN_C_END

#endif /* FIDELIO_SYSROOT_CFNOTIFICATIONCENTER_H */
