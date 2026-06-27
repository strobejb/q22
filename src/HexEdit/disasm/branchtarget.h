#ifndef DISASM_BRANCHTARGET_H
#define DISASM_BRANCHTARGET_H

#include <capstone/capstone.h>

#include <cstdint>
#include <optional>

// Resolves a JMP/Jcc/CALL's static target address, or nullopt for anything
// indirect (register/memory operand, e.g. "jmp eax" or an IAT thunk) or with
// no operand (e.g. "ret") -- those have no statically-known destination.
// Capstone has already folded any relative displacement into operands[0].imm,
// so no manual address math is needed here. Requires CS_OPT_DETAIL to be on.
inline std::optional<uint64_t> branchTargetForInstruction(csh handle, const cs_insn &insn, cs_arch arch)
{
    if (!insn.detail)
        return std::nullopt;
    if (!cs_insn_group(handle, &insn, CS_GRP_JUMP) && !cs_insn_group(handle, &insn, CS_GRP_CALL))
        return std::nullopt;

    switch (arch) {
    case CS_ARCH_X86:
        if (insn.detail->x86.op_count == 1 && insn.detail->x86.operands[0].type == X86_OP_IMM)
            return static_cast<uint64_t>(insn.detail->x86.operands[0].imm);
        break;
    case CS_ARCH_ARM:
        if (insn.detail->arm.op_count == 1 && insn.detail->arm.operands[0].type == ARM_OP_IMM)
            return static_cast<uint64_t>(insn.detail->arm.operands[0].imm);
        break;
    case CS_ARCH_ARM64:
        if (insn.detail->arm64.op_count == 1 && insn.detail->arm64.operands[0].type == ARM64_OP_IMM)
            return static_cast<uint64_t>(insn.detail->arm64.operands[0].imm);
        break;
    default:
        break;
    }
    return std::nullopt;
}

// True for a jump that is taken unconditionally -- execution never falls
// through it, so a linear walk must stop there rather than decoding
// whatever bytes happen to follow (which may belong to unrelated code, a
// different thunk, or padding). Distinct from CS_GRP_JUMP, which also
// covers conditional branches (Jcc/B.cond), and excludes CS_GRP_CALL
// (BL/BLX -- those return, so a walk continues past them). Requires
// CS_OPT_DETAIL to be on.
inline bool isUnconditionalJump(csh handle, const cs_insn &insn, cs_arch arch)
{
    if (!insn.detail)
        return false;
    if (!cs_insn_group(handle, &insn, CS_GRP_JUMP))
        return false;
    if (cs_insn_group(handle, &insn, CS_GRP_CALL))
        return false;

    switch (arch) {
    case CS_ARCH_X86:
        return insn.id == X86_INS_JMP;
    case CS_ARCH_ARM:
        // Plain "B"/"BX" is encoded with cc == ARM_CC_AL ("always");
        // B.<cond> carries one of the real condition codes instead.
        return insn.detail->arm.cc == ARM_CC_AL;
    case CS_ARCH_ARM64:
        // Plain "B" has no condition field at all; "B.<cond>" does.
        return insn.detail->arm64.cc == ARM64_CC_INVALID;
    default:
        return false;
    }
}

#endif // DISASM_BRANCHTARGET_H
