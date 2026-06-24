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

#endif // DISASM_BRANCHTARGET_H
