/*
 * Copyright (C) 2016 Yusuke Suzuki <utatane.tea@gmail.com>
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

#include "Instruction.h"
#include "Interpreter.h"
#include "LLIntData.h"
#include "PtrTag.h"
#include "UnlinkedCodeBlock.h"

namespace JSC {

inline Opcode Interpreter::getOpcode(OpcodeID id)
{
    return LLInt::getOpcode(id);
}

inline OpcodeID Interpreter::getOpcodeID(Opcode opcode)
{
#if ENABLE(COMPUTED_GOTO_OPCODES)
    ASSERT(isOpcode(opcode));
#if USE(LLINT_EMBEDDED_OPCODE_ID)
    // The OpcodeID is embedded in the int32_t word preceding the location of
    // the LLInt code for the opcode (see the EMBED_OPCODE_ID_IF_NEEDED macro
    // in LowLevelInterpreter.cpp).
    auto codePtr = MacroAssemblerCodePtr<BytecodePtrTag>::createFromExecutableAddress(opcode);
    int32_t* opcodeIDAddress = codePtr.dataLocation<int32_t*>() - 1;
    OpcodeID opcodeID = static_cast<OpcodeID>(*opcodeIDAddress);
    ASSERT(opcodeID < NUMBER_OF_BYTECODE_IDS);
    return opcodeID;
#else
    return opcodeIDTable().get(opcode);
#endif // USE(LLINT_EMBEDDED_OPCODE_ID)
    
#else // not ENABLE(COMPUTED_GOTO_OPCODES)
    return opcode;
#endif
}

inline OpcodeID Interpreter::getOpcodeID(const Instruction& instruction)
{
    return getOpcodeID(instruction.u.opcode);
}

inline OpcodeID Interpreter::getOpcodeID(const UnlinkedInstruction& instruction)
{
    return instruction.u.opcode;
}

} // namespace JSC
