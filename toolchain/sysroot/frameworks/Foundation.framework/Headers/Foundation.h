/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <Foundation/Foundation.h> for the sysroot (toolchain/sysroot/frameworks).
 *
 * Why a file of our own: Apple publishes no source for Foundation's headers (the open-source CF-Lite
 * covers CoreFoundation only). This file is written for this repository and declares only the part
 * of Foundation that its sources use: NowPlaying (nowplaying/src), the lock-screen plugin
 * (nowplaying/lockplugin), the DockRemote tweak (tweak/DockRemote) and the volume probe
 * (audiod/probe/voltest.m), and the Objective-C program of the toolchain's start-up smoke test
 * (docs/HANDOFF.md; the declarations that only it needs are marked "smoke test"). It is an
 * allow-list: a class, method, function or constant that is not here was not needed by any of these
 * sources. See ../../../README.md, "Stage 3", for how to add one.
 *
 * Interface facts stated here, all part of how a caller is compiled:
 *   - the 32-bit sizes of the base types: NSInteger is int, NSUInteger is unsigned int,
 *     NSTimeInterval is double, unichar is unsigned short; NSNotFound is the largest NSInteger;
 *   - NSRange is "struct _NSRange" of two NSUIntegers (its tag is part of type encodings);
 *   - the NSObject protocol, with its methods in the order and with the types that the protocol
 *     descriptions in this repository's own objects record (a class of ours that adopts a protocol
 *     derived from NSObject carries that description);
 *   - the root class NSObject has one instance variable, the class pointer (4 bytes);
 *   - for every method declared: its selector and the types of its result and arguments, which
 *     decide how a message is sent; for the string constants: their symbol names;
 *   - the state structure of fast enumeration (for ... in), which arrays support;
 *   - NSException's exception type is exported by Foundation (the objc_exception attribute);
 *   - NSUTF8StringEncoding is 4; NSOrderedAscending, NSOrderedSame, NSOrderedDescending are -1, 0, 1.
 */
#if !defined(FIDELIO_SYSROOT_FOUNDATION_H)
#define FIDELIO_SYSROOT_FOUNDATION_H

#include <CoreFoundation/CoreFoundation.h>
#include <objc/objc.h>

#if defined(__cplusplus)
#define FOUNDATION_EXPORT extern "C"
#else
#define FOUNDATION_EXPORT extern
#endif

/* ---- base types and values ------------------------------------------------------------------------- */

typedef int NSInteger;
typedef unsigned int NSUInteger;
typedef double NSTimeInterval;
typedef unsigned short unichar;

enum { NSNotFound = 0x7fffffff };   /* the largest NSInteger */

typedef NSInteger NSComparisonResult;
enum {
    NSOrderedAscending = -1,
    NSOrderedSame = 0,
    NSOrderedDescending = 1
};

typedef struct _NSRange {
    NSUInteger location;
    NSUInteger length;
} NSRange;

typedef struct _NSZone NSZone;

typedef NSUInteger NSStringEncoding;
enum { NSUTF8StringEncoding = 4 };

@class NSArray, NSCharacterSet, NSData, NSDate, NSDictionary, NSError, NSString, NSURL, Protocol;

FOUNDATION_EXPORT void NSLog(NSString *format, ...);

/* keys of the dictionary that -[NSFileManager attributesOfItemAtPath:error:] returns */
FOUNDATION_EXPORT NSString *const NSFileModificationDate;   /* an NSDate */
FOUNDATION_EXPORT NSString *const NSFileSize;               /* an NSNumber */

/* ---- the root: protocol and class ------------------------------------------------------------------- */

/* The method list of this protocol is recorded in the objects of every class of ours that adopts a
   protocol derived from it, so it is given in full, in that order. */
@protocol NSObject
- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;
- (Class)superclass;
- (Class)class;
- (id)self;
- (NSZone *)zone;
- (id)performSelector:(SEL)aSelector;
- (id)performSelector:(SEL)aSelector withObject:(id)object;
- (id)performSelector:(SEL)aSelector withObject:(id)object1 withObject:(id)object2;
- (BOOL)isProxy;
- (BOOL)isKindOfClass:(Class)aClass;
- (BOOL)isMemberOfClass:(Class)aClass;
- (BOOL)conformsToProtocol:(Protocol *)aProtocol;
- (BOOL)respondsToSelector:(SEL)aSelector;
- (id)retain;
- (oneway void)release;
- (id)autorelease;
- (NSUInteger)retainCount;
- (NSString *)description;
@end

@interface NSObject <NSObject> {
    Class isa;
}
+ (id)alloc;
+ (Class)class;
- (id)init;
- (void)dealloc;
- (id)copy;
- (id)mutableCopy;
@end

/* ---- memory and errors -------------------------------------------------------------------------------- */

@interface NSAutoreleasePool : NSObject
- (void)drain;                         /* smoke test */
@end

/* Caught (@catch) by our sources. Foundation exports the exception type of this class: the attribute makes a
   @catch refer to Foundation's _OBJC_EHTYPE_$_NSException instead of defining one in the object. */
__attribute__((__objc_exception__))
@interface NSException : NSObject
+ (void)raise:(NSString *)name format:(NSString *)format, ...;   /* smoke test */
- (NSString *)name;                                              /* smoke test */
@end

@interface NSError : NSObject          /* only passed on and described */
@end

/* ---- strings ------------------------------------------------------------------------------------------- */

@interface NSString : NSObject
+ (id)string;
+ (id)stringWithString:(NSString *)string;
+ (id)stringWithUTF8String:(const char *)nullTerminatedCString;
+ (id)stringWithFormat:(NSString *)format, ...;
+ (id)stringWithContentsOfFile:(NSString *)path encoding:(NSStringEncoding)enc error:(NSError **)error;

- (NSUInteger)length;
- (unichar)characterAtIndex:(NSUInteger)index;
- (const char *)UTF8String;
- (const char *)fileSystemRepresentation;
- (NSUInteger)lengthOfBytesUsingEncoding:(NSStringEncoding)enc;
- (NSData *)dataUsingEncoding:(NSStringEncoding)encoding;

- (BOOL)isEqualToString:(NSString *)aString;
- (BOOL)hasPrefix:(NSString *)aString;
- (BOOL)hasSuffix:(NSString *)aString;
- (NSComparisonResult)compare:(NSString *)string;
- (NSRange)rangeOfString:(NSString *)aString;

- (NSString *)stringByAppendingString:(NSString *)aString;
- (NSString *)stringByAppendingPathComponent:(NSString *)str;
- (NSString *)stringByTrimmingCharactersInSet:(NSCharacterSet *)set;
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target withString:(NSString *)replacement;
- (NSString *)stringByReplacingCharactersInRange:(NSRange)range withString:(NSString *)replacement;
- (NSArray *)componentsSeparatedByString:(NSString *)separator;
- (NSArray *)componentsSeparatedByCharactersInSet:(NSCharacterSet *)separator;

- (int)intValue;
- (NSInteger)integerValue;
- (long long)longLongValue;
- (BOOL)boolValue;

- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile encoding:(NSStringEncoding)enc error:(NSError **)error;
@end

@interface NSMutableString : NSString
- (void)appendString:(NSString *)aString;
- (void)appendFormat:(NSString *)format, ...;
@end

@interface NSCharacterSet : NSObject
+ (id)whitespaceCharacterSet;
+ (id)whitespaceAndNewlineCharacterSet;
+ (id)newlineCharacterSet;
@end

/* ---- numbers, dates, data ------------------------------------------------------------------------------ */

@interface NSValue : NSObject
@end

@interface NSNumber : NSValue
- (BOOL)boolValue;
- (NSInteger)integerValue;
- (long long)longLongValue;
- (unsigned long long)unsignedLongLongValue;
@end

@interface NSDate : NSObject
+ (id)date;
- (NSTimeInterval)timeIntervalSinceDate:(NSDate *)anotherDate;
- (NSTimeInterval)timeIntervalSince1970;
- (BOOL)isEqualToDate:(NSDate *)otherDate;
- (NSComparisonResult)compare:(NSDate *)other;
@end

@interface NSData : NSObject
+ (id)data;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;
@end

@interface NSMutableData : NSData
+ (id)dataWithLength:(NSUInteger)length;
- (void *)mutableBytes;
@end

/* ---- collections --------------------------------------------------------------------------------------- */

/* for (x in collection): the compiler sends countByEnumeratingWithState:objects:count: itself, with a
   state of this layout */
typedef struct {
    unsigned long state;
    id *itemsPtr;
    unsigned long *mutationsPtr;
    unsigned long extra[5];
} NSFastEnumerationState;

@protocol NSFastEnumeration
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id *)stackbuf count:(NSUInteger)len;
@end

@interface NSArray : NSObject <NSFastEnumeration>
+ (id)array;
+ (id)arrayWithObjects:(id)firstObj, ...;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
- (NSUInteger)indexOfObject:(id)anObject;
- (NSString *)componentsJoinedByString:(NSString *)separator;
@end

@interface NSMutableArray : NSArray
- (void)addObject:(id)anObject;
- (void)replaceObjectAtIndex:(NSUInteger)index withObject:(id)anObject;
- (void)removeAllObjects;
- (void)sortUsingFunction:(NSInteger (*)(id, id, void *))compare context:(void *)context;
@end

@interface NSDictionary : NSObject
+ (id)dictionary;
+ (id)dictionaryWithContentsOfFile:(NSString *)path;
+ (id)dictionaryWithObjectsAndKeys:(id)firstObject, ...;
- (id)objectForKey:(id)aKey;
@end

@interface NSMutableDictionary : NSDictionary
- (void)setObject:(id)anObject forKey:(id)aKey;
@end

@interface NSSet : NSObject
- (id)anyObject;
@end

@interface NSIndexPath : NSObject      /* UIKit adds the table rows and sections */
@end

/* ---- files, bundles, timers ------------------------------------------------------------------------------ */

@interface NSFileManager : NSObject
+ (NSFileManager *)defaultManager;
- (BOOL)fileExistsAtPath:(NSString *)path;
- (NSDictionary *)attributesOfItemAtPath:(NSString *)path error:(NSError **)error;
- (BOOL)moveItemAtPath:(NSString *)srcPath toPath:(NSString *)dstPath error:(NSError **)error;
- (BOOL)removeItemAtPath:(NSString *)path error:(NSError **)error;
@end

@interface NSFileHandle : NSObject
+ (id)fileHandleForWritingAtPath:(NSString *)path;
- (unsigned long long)seekToEndOfFile;
- (void)writeData:(NSData *)data;
- (void)closeFile;
@end

@interface NSBundle : NSObject
+ (NSBundle *)mainBundle;
- (NSString *)pathForResource:(NSString *)name ofType:(NSString *)ext;
@end

@interface NSProcessInfo : NSObject    /* smoke test */
+ (NSProcessInfo *)processInfo;
- (NSArray *)arguments;
- (NSString *)processName;
@end

@interface NSTimer : NSObject
+ (NSTimer *)scheduledTimerWithTimeInterval:(NSTimeInterval)ti target:(id)aTarget selector:(SEL)aSelector
                                   userInfo:(id)userInfo repeats:(BOOL)yesOrNo;
- (void)invalidate;
@end

#endif /* FIDELIO_SYSROOT_FOUNDATION_H */
