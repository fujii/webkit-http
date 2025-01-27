/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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

#include <wtf/PointerPreparations.h>

namespace JSC {

#define FOR_EACH_BASE_PTRTAG_ENUM(v) \
    v(NoPtrTag) \
    v(CFunctionPtrTag) \

#define FOR_EACH_ADDITIONAL_PTRTAG_ENUM(v) \
    v(B3CCallPtrTag) \
    v(B3CompilationPtrTag) \
    v(BytecodePtrTag) \
    v(DisassemblyPtrTag) \
    v(ExceptionHandlerPtrTag) \
    v(JITThunkPtrTag) \
    v(JITStubRoutinePtrTag) \
    v(JSEntryPtrTag) \
    v(JSInternalPtrTag) \
    v(JSSwitchPtrTag) \
    v(LinkBufferPtrTag) \
    v(OperationPtrTag) \
    v(OSRExitPtrTag) \
    v(SlowPathPtrTag) \
    v(WasmEntryPtrTag) \
    v(Yarr8BitPtrTag) \
    v(Yarr16BitPtrTag) \
    v(YarrMatchOnly8BitPtrTag) \
    v(YarrMatchOnly16BitPtrTag) \
    v(YarrBacktrackPtrTag) \

#define FOR_EACH_PTRTAG_ENUM(v) \
    FOR_EACH_BASE_PTRTAG_ENUM(v) \
    FOR_EACH_ADDITIONAL_PTRTAG_ENUM(v) \

enum PtrTag : uintptr_t {
    NoPtrTag,
    CFunctionPtrTag,

#ifndef PTRTAG_ENUM_HASH
#define DECLARE_PTRTAG_ENUM(tag)  tag,
#else
#define DECLARE_PTRTAG_ENUM(tag)  tag = PTRTAG_ENUM_HASH(tag),
#endif
    FOR_EACH_ADDITIONAL_PTRTAG_ENUM(DECLARE_PTRTAG_ENUM)
#undef DECLARE_PTRTAG_ENUM
};

static_assert(static_cast<uintptr_t>(NoPtrTag) == static_cast<uintptr_t>(0), "");
static_assert(static_cast<uintptr_t>(CFunctionPtrTag) == static_cast<uintptr_t>(1), "");

inline const char* ptrTagName(PtrTag tag)
{
#define RETURN_PTRTAG_NAME(_tagName) case _tagName: return #_tagName;
    switch (tag) {
        FOR_EACH_PTRTAG_ENUM(RETURN_PTRTAG_NAME)
    default: return "<unknown>";
    }
#undef RETURN_PTRTAG_NAME
}

#if !USE(POINTER_PROFILING)

inline const char* tagForPtr(const void*) { return "<no tag>"; }

template<typename... Arguments>
inline constexpr PtrTag ptrTag(Arguments&&...) { return NoPtrTag; }

template<typename T, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value && !std::is_same<T, PtrType>::value>>
inline constexpr T tagCodePtr(PtrType ptr, PtrTag) { return bitwise_cast<T>(ptr); }

template<typename T, PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline T tagCodePtr(PtrType ptr) { return bitwise_cast<T>(ptr); }

template<typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline constexpr PtrType tagCodePtr(PtrType ptr, PtrTag) { return ptr; }

template<PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline PtrType tagCodePtr(PtrType ptr) { return ptr; }

template<typename T, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value && !std::is_same<T, PtrType>::value>>
inline constexpr T untagCodePtr(PtrType ptr, PtrTag) { return bitwise_cast<T>(ptr); }

template<typename T, PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline T untagCodePtr(PtrType ptr)  { return bitwise_cast<T>(ptr); }

template<typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline constexpr PtrType untagCodePtr(PtrType ptr, PtrTag) { return ptr; }

template<PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline PtrType untagCodePtr(PtrType ptr) { return ptr; }

template<typename T, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value && !std::is_same<T, PtrType>::value>>
inline constexpr T retagCodePtr(PtrType ptr, PtrTag, PtrTag) { return bitwise_cast<T>(ptr); }

template<typename T, PtrTag, PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline T retagCodePtr(PtrType ptr) { return bitwise_cast<T>(ptr); }

template<typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline constexpr PtrType retagCodePtr(PtrType ptr, PtrTag, PtrTag) { return ptr; }

template<PtrTag, PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline PtrType retagCodePtr(PtrType ptr) { return ptr; }

template<typename T, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value && !std::is_same<T, PtrType>::value>>
inline constexpr T removeCodePtrTag(PtrType ptr) { return bitwise_cast<T>(ptr); }

template<typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline constexpr PtrType removeCodePtrTag(PtrType ptr) { return ptr; }

template<typename T, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value && !std::is_same<T, PtrType>::value>>
inline T tagCFunctionPtr(PtrType ptr, PtrTag) { return bitwise_cast<T>(ptr); }

template<typename T, PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline T tagCFunctionPtr(PtrType ptr) { return bitwise_cast<T>(ptr); }

template<typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline PtrType tagCFunctionPtr(PtrType ptr, PtrTag) { return ptr; }

template<PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline PtrType tagCFunctionPtr(PtrType ptr) { return ptr; }

template<typename T, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value && !std::is_same<T, PtrType>::value>>
inline T untagCFunctionPtr(PtrType ptr, PtrTag) { return bitwise_cast<T>(ptr); }

template<typename T, PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline T untagCFunctionPtr(PtrType ptr) { return bitwise_cast<T>(ptr); }

template<typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline PtrType untagCFunctionPtr(PtrType ptr, PtrTag) { return ptr; }

template<PtrTag, typename PtrType, typename = std::enable_if_t<std::is_pointer<PtrType>::value>>
inline PtrType untagCFunctionPtr(PtrType ptr) { return ptr; }

template<typename PtrType> void assertIsCFunctionPtr(PtrType) { }
template<typename PtrType> void assertIsNullOrCFunctionPtr(PtrType) { }

template<typename PtrType> void assertIsNotTagged(PtrType) { }
template<typename PtrType> void assertIsTagged(PtrType) { }
template<typename PtrType> void assertIsNullOrTagged(PtrType) { }

template<typename PtrType> void assertIsTaggedWith(PtrType, PtrTag) { }
template<typename PtrType> void assertIsNullOrTaggedWith(PtrType, PtrTag) { }

#define CALL_WITH_PTRTAG(callInstructionString, targetRegisterString, tag) \
    callInstructionString " " targetRegisterString "\n"

#endif // !USE(POINTER_PROFILING)

} // namespace JSC

#if USE(APPLE_INTERNAL_SDK) && __has_include(<WebKitAdditions/PtrTagSupport.h>)
#include <WebKitAdditions/PtrTagSupport.h>
#endif
