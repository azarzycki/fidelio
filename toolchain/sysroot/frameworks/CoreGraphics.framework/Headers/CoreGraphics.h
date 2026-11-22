/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <CoreGraphics/CoreGraphics.h> for the sysroot (toolchain/sysroot/frameworks).
 *
 * Why a file of our own: Apple publishes no source for CoreGraphics' headers. This file is written for
 * this repository and declares only what its sources use (NowPlaying and the lock-screen plugin, through
 * UIKit): the geometry types, the two functions that make sizes and rectangles, the zero rectangle, one
 * rectangle function, and the drawing calls with which NowPlaying draws its transport glyphs. See
 * ../../../README.md, "Stage 3".
 *
 * Interface facts stated here, all part of how a caller is compiled:
 *   - CGFloat is float on this 32-bit platform;
 *   - CGPoint (x, y), CGSize (width, height) and CGRect (origin, size) are structures with exactly those
 *     tags; the tags are part of the type encodings of methods that take or return them;
 *   - CGRectMake and CGSizeMake are inline functions that fill in the fields; CGRectZero is a
 *     constant and CGRectGetMaxY a function of the library;
 *   - a drawing context is a pointer to "struct CGContext".
 */
#if !defined(IOS3_SYSROOT_COREGRAPHICS_H)
#define IOS3_SYSROOT_COREGRAPHICS_H

#include <CoreFoundation/CFBase.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef float CGFloat;

/* ---- geometry ---- */

struct CGPoint {
    CGFloat x;
    CGFloat y;
};
typedef struct CGPoint CGPoint;

struct CGSize {
    CGFloat width;
    CGFloat height;
};
typedef struct CGSize CGSize;

struct CGRect {
    CGPoint origin;
    CGSize size;
};
typedef struct CGRect CGRect;

static inline CGSize CGSizeMake(CGFloat width, CGFloat height) {
    CGSize s;
    s.width = width;
    s.height = height;
    return s;
}

static inline CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat width, CGFloat height) {
    CGRect r;
    r.origin.x = x;
    r.origin.y = y;
    r.size.width = width;
    r.size.height = height;
    return r;
}

extern const CGRect CGRectZero;

/* the largest y of the rectangle */
extern CGFloat CGRectGetMaxY(CGRect rect);

/* ---- drawing ---- */

typedef struct CGContext *CGContextRef;

extern void CGContextScaleCTM(CGContextRef c, CGFloat sx, CGFloat sy);
extern void CGContextSetRGBFillColor(CGContextRef c, CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha);
extern void CGContextMoveToPoint(CGContextRef c, CGFloat x, CGFloat y);
extern void CGContextAddLineToPoint(CGContextRef c, CGFloat x, CGFloat y);
extern void CGContextClosePath(CGContextRef c);
extern void CGContextFillPath(CGContextRef c);
extern void CGContextFillRect(CGContextRef c, CGRect rect);

#if defined(__cplusplus)
}
#endif

#endif /* IOS3_SYSROOT_COREGRAPHICS_H */
