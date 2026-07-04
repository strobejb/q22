#ifndef DISASM_DEMANGLE_H
#define DISASM_DEMANGLE_H

#include <QString>

enum class DemangleStyle
{
    // Full signature: access specifier, storage class, return type,
    // calling convention, parameter list -- everything UnDecorateSymbolName()/
    // __cxa_demangle() normally produce. Meant for places with room to show
    // it, e.g. a disassembly header comment.
    Full,
    // Just "[Namespace::]Class::method" -- no "public:"/"static", no return
    // type, no calling convention, no parameter list. Meant for compact
    // list/dropdown display where the full signature is mostly noise.
    NameOnly,
};

// Demangles a C++ export name for display -- MSVC mangling ("?"-prefixed,
// the common case for Windows binaries) via dbghelp's UnDecorateSymbolName(),
// Itanium ABI mangling ("_Z"-prefixed, GCC/Clang/MinGW-built binaries) via
// __cxa_demangle() when the toolchain provides <cxxabi.h>. Returns the input
// unchanged if it isn't recognized or the relevant demangler is unavailable.
QString demangleSymbolName(const QString &mangledName, DemangleStyle style = DemangleStyle::Full);

#endif // DISASM_DEMANGLE_H
