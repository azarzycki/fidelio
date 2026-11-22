/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root.
 *
 * notice.s: the source-availability notice inside the toolchain's start-up objects.
 *
 * crt1.o, dylib1.o and bundle1.o are built from Apple's open-source Csu release, whose files are under
 * the Apple Public Source License (1.1 for start.s and crt.c, 2.0 for dyld_glue.s). The linker copies
 * one of the three objects into every executable, dylib and bundle. For code distributed in object
 * form those licences ask for a notice "in the code itself", saying that the source code is available
 * under the licence and where to obtain it. This file is that notice: one NUL-terminated ASCII string,
 * which build-startfiles.sh assembles into each object as the last part of the same assembly unit,
 * after the Csu sources. The Csu sources themselves are used as published.
 *
 * This file is this project's own work, not part of Csu. It defines no code and no exported symbol.
 *
 *   Section  __TEXT,__csu_notice: a section of its own, so that the text can be found in a linked
 *            binary by name (otool -l FILE lists the section) and tools/startcheck.py can check it.
 *            Type "regular": plain bytes, which the linker copies as they are.
 *   Kept     the section attribute no_dead_strip and .no_dead_strip on the symbol: a link with
 *            -dead_strip keeps the text although nothing refers to it. Without both marks it
 *            removes the text; either mark alone keeps it.
 *   Symbol   __csu_source_notice, local to the object (no .globl): it is in no export list, and a
 *            program may define a symbol of the same name; the link map lists it with its address
 *            and size. "strip -x" removes the symbol and leaves the text.
 *
 * Two texts, because the objects differ in what they contain. CRT is the macro of the Csu Makefile
 * that marks the build of crt1.o (start.s, crt.c and dyld_glue.s); dyld_glue.s tests it the same way.
 * Without it the object is dylib1.o or bundle1.o: dyld_glue.s alone.
 *
 * The same two texts are in tools/startcheck.py, which compares them with the section of every binary
 * that tools/build-arm.sh builds, and in toolchain/THIRD-PARTY-NOTICES.md. Change them together.
 */
	.section __TEXT,__csu_notice,regular,no_dead_strip
	.no_dead_strip __csu_source_notice
__csu_source_notice:
#if CRT
	.asciz "This program contains start-up code built from Apple's open-source Csu release, tag Csu-79: start.s and crt.c under the Apple Public Source License version 1.1, dyld_glue.s under the Apple Public Source License version 2.0. The source code is available under the terms of those licenses at https://github.com/apple-oss-distributions/Csu (commit fd4097d7a8b07792f55d74271d09adedb4748b94)."
#else
	.asciz "This library contains glue code built from Apple's open-source Csu release, tag Csu-79: dyld_glue.s under the Apple Public Source License version 2.0. The source code is available under the terms of that license at https://github.com/apple-oss-distributions/Csu (commit fd4097d7a8b07792f55d74271d09adedb4748b94)."
#endif
/* Back to the text section: nothing is assembled after this file today, and if something ever is,
 * it must not land in the section of the notice. The directive itself emits no bytes. */
	.text
