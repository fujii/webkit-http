/*
 * Copyright (C) 2016-2018 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#if ENABLE(JIT)

#include "CodeLocation.h"
#include "PropertyOffset.h"

namespace JSC {

class JSArray;
class Structure;
class StructureStubInfo;
class VM;

class InlineAccess {
public:

    // This is the maximum between inline and out of line self access cases.
    static constexpr size_t sizeForPropertyAccess()
    {
#if CPU(X86_64)
        return 23;
#elif CPU(X86)
        return 27;
#elif CPU(ARM64)
        return 40;
#elif CPU(ARM)
#if CPU(ARM_THUMB2)
        return 48;
#else
        return 52;
#endif
#elif CPU(MIPS)
        return 72;
#else
#error "unsupported platform"
#endif
    }

    // This is the maximum between inline and out of line property replace cases.
    static constexpr size_t sizeForPropertyReplace()
    {
#if CPU(X86_64)
        return 23;
#elif CPU(X86)
        return 27;
#elif CPU(ARM64)
        return 40;
#elif CPU(ARM)
#if CPU(ARM_THUMB2)
        return 48;
#else
        return 48;
#endif
#elif CPU(MIPS)
        return 72;
#else
#error "unsupported platform"
#endif
    }

    // FIXME: Make this constexpr when GCC is able to compile std::max() inside a constexpr function.
    // https://bugs.webkit.org/show_bug.cgi?id=159436
    //
    // This is the maximum between the size for array length access, and the size for regular self access.
    ALWAYS_INLINE static size_t sizeForLengthAccess()
    {
#if CPU(X86_64)
        size_t size = 26;
#elif CPU(X86)
        size_t size = 27;
#elif CPU(ARM64)
        size_t size = 32;
#elif CPU(ARM)
#if CPU(ARM_THUMB2)
        size_t size = 30;
#else
        size_t size = 32;
#endif
#elif CPU(MIPS)
        size_t size = 56;
#else
#error "unsupported platform"
#endif
        return std::max(size, sizeForPropertyAccess());
    }

    static bool generateSelfPropertyAccess(StructureStubInfo&, Structure*, PropertyOffset);
    static bool canGenerateSelfPropertyReplace(StructureStubInfo&, PropertyOffset);
    static bool generateSelfPropertyReplace(StructureStubInfo&, Structure*, PropertyOffset);
    static bool isCacheableArrayLength(StructureStubInfo&, JSArray*);
    static bool generateArrayLength(StructureStubInfo&, JSArray*);
    static void rewireStubAsJump(StructureStubInfo&, CodeLocationLabel<JITStubRoutinePtrTag>);

    // This is helpful when determining the size of an IC on
    // various platforms. When adding a new type of IC, implement
    // its placeholder code here, and log the size. That way we
    // can intelligently choose sizes on various platforms.
    NO_RETURN_DUE_TO_CRASH static void dumpCacheSizesAndCrash();
};

} // namespace JSC

#endif // ENABLE(JIT)
