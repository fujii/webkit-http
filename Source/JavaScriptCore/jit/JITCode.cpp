/*
 * Copyright (C) 2008-2018 Apple Inc. All rights reserved.
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

#include "config.h"
#include "JITCode.h"

#include "LLIntThunks.h"
#include "JSCInlines.h"
#include "ProtoCallFrame.h"
#include <wtf/PrintStream.h>

namespace JSC {

JITCode::JITCode(JITType jitType)
    : m_jitType(jitType)
{
}

JITCode::~JITCode()
{
}

const char* JITCode::typeName(JITType jitType)
{
    switch (jitType) {
    case None:
        return "None";
    case HostCallThunk:
        return "Host";
    case InterpreterThunk:
        return "LLInt";
    case BaselineJIT:
        return "Baseline";
    case DFGJIT:
        return "DFG";
    case FTLJIT:
        return "FTL";
    default:
        CRASH();
        return "";
    }
}

void JITCode::validateReferences(const TrackedReferences&)
{
}

JSValue JITCode::execute(VM* vm, ProtoCallFrame* protoCallFrame)
{
    auto scope = DECLARE_THROW_SCOPE(*vm);
    void* entryAddress;
    entryAddress = addressForCall(MustCheckArity).executableAddress();
    JSValue result = JSValue::decode(vmEntryToJavaScript(entryAddress, vm, protoCallFrame));
    return scope.exception() ? jsNull() : result;
}

DFG::CommonData* JITCode::dfgCommon()
{
    RELEASE_ASSERT_NOT_REACHED();
    return 0;
}

DFG::JITCode* JITCode::dfg()
{
    RELEASE_ASSERT_NOT_REACHED();
    return 0;
}

FTL::JITCode* JITCode::ftl()
{
    RELEASE_ASSERT_NOT_REACHED();
    return 0;
}

FTL::ForOSREntryJITCode* JITCode::ftlForOSREntry()
{
    RELEASE_ASSERT_NOT_REACHED();
    return 0;
}

JITCodeWithCodeRef::JITCodeWithCodeRef(JITType jitType)
    : JITCode(jitType)
{
}

JITCodeWithCodeRef::JITCodeWithCodeRef(CodeRef<JSEntryPtrTag> ref, JITType jitType)
    : JITCode(jitType)
    , m_ref(ref)
{
}

JITCodeWithCodeRef::~JITCodeWithCodeRef()
{
    if ((Options::dumpDisassembly() || (isOptimizingJIT(jitType()) && Options::dumpDFGDisassembly()))
        && m_ref.executableMemory())
        dataLog("Destroying JIT code at ", pointerDump(m_ref.executableMemory()), "\n");
}

void* JITCodeWithCodeRef::executableAddressAtOffset(size_t offset)
{
    RELEASE_ASSERT(m_ref);
    assertIsTaggedWith(m_ref.code().executableAddress(), JSEntryPtrTag);
    if (!offset)
        return m_ref.code().executableAddress();

    char* executableAddress = untagCodePtr<char*, JSEntryPtrTag>(m_ref.code().executableAddress());
    return tagCodePtr<JSEntryPtrTag>(executableAddress + offset);
}

void* JITCodeWithCodeRef::dataAddressAtOffset(size_t offset)
{
    RELEASE_ASSERT(m_ref);
    ASSERT(offset <= size()); // use <= instead of < because it is valid to ask for an address at the exclusive end of the code.
    return m_ref.code().dataLocation<char*>() + offset;
}

unsigned JITCodeWithCodeRef::offsetOf(void* pointerIntoCode)
{
    RELEASE_ASSERT(m_ref);
    intptr_t result = reinterpret_cast<intptr_t>(pointerIntoCode) - m_ref.code().executableAddress<intptr_t>();
    ASSERT(static_cast<intptr_t>(static_cast<unsigned>(result)) == result);
    return static_cast<unsigned>(result);
}

size_t JITCodeWithCodeRef::size()
{
    RELEASE_ASSERT(m_ref);
    return m_ref.size();
}

bool JITCodeWithCodeRef::contains(void* address)
{
    RELEASE_ASSERT(m_ref);
    return m_ref.executableMemory()->contains(address);
}

DirectJITCode::DirectJITCode(JITType jitType)
    : JITCodeWithCodeRef(jitType)
{
}

DirectJITCode::DirectJITCode(JITCode::CodeRef<JSEntryPtrTag> ref, JITCode::CodePtr<JSEntryPtrTag> withArityCheck, JITType jitType)
    : JITCodeWithCodeRef(ref, jitType)
    , m_withArityCheck(withArityCheck)
{
    ASSERT(m_ref);
    ASSERT(m_withArityCheck);
}

DirectJITCode::~DirectJITCode()
{
}

void DirectJITCode::initializeCodeRef(JITCode::CodeRef<JSEntryPtrTag> ref, JITCode::CodePtr<JSEntryPtrTag> withArityCheck)
{
    RELEASE_ASSERT(!m_ref);
    m_ref = ref;
    m_withArityCheck = withArityCheck;
    ASSERT(m_ref);
    ASSERT(m_withArityCheck);
}

JITCode::CodePtr<JSEntryPtrTag> DirectJITCode::addressForCall(ArityCheckMode arity)
{
    switch (arity) {
    case ArityCheckNotRequired:
        RELEASE_ASSERT(m_ref);
        return m_ref.code();
    case MustCheckArity:
        RELEASE_ASSERT(m_withArityCheck);
        return m_withArityCheck;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return CodePtr<JSEntryPtrTag>();
}

NativeJITCode::NativeJITCode(JITType jitType)
    : JITCodeWithCodeRef(jitType)
{
}

NativeJITCode::NativeJITCode(CodeRef<JSEntryPtrTag> ref, JITType jitType)
    : JITCodeWithCodeRef(ref, jitType)
{
}

NativeJITCode::~NativeJITCode()
{
}

void NativeJITCode::initializeCodeRef(CodeRef<JSEntryPtrTag> ref)
{
    ASSERT(!m_ref);
    m_ref = ref;
}

JITCode::CodePtr<JSEntryPtrTag> NativeJITCode::addressForCall(ArityCheckMode arity)
{
    RELEASE_ASSERT(m_ref);
    switch (arity) {
    case ArityCheckNotRequired:
        return m_ref.code();
    case MustCheckArity:
        return m_ref.code();
    }
    RELEASE_ASSERT_NOT_REACHED();
    return CodePtr<JSEntryPtrTag>();
}

#if ENABLE(JIT)
RegisterSet JITCode::liveRegistersToPreserveAtExceptionHandlingCallSite(CodeBlock*, CallSiteIndex)
{
    return { };
}
#endif

} // namespace JSC

namespace WTF {

void printInternal(PrintStream& out, JSC::JITCode::JITType type)
{
    out.print(JSC::JITCode::typeName(type));
}

} // namespace WTF

