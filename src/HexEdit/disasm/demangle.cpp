#include "disasm/demangle.h"

#include <QByteArray>

#ifdef Q_OS_WIN
// dbghelp.h in this toolchain doesn't declare UnDecorateSymbolName even
// though the import library exports it -- declare it ourselves, matching
// the real Windows SDK signature (DWORD IMAGEAPI UnDecorateSymbolName(PCSTR,
// PSTR, DWORD, DWORD); IMAGEAPI is __stdcall).
extern "C" __declspec(dllimport) unsigned long __stdcall
UnDecorateSymbolName(const char *name, char *outputString,
                      unsigned long maxStringLength, unsigned long flags);
#endif

#if defined(__has_include)
#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define QEXED_HAVE_CXXABI 1
#endif
#endif

#include <cstdlib>

namespace {
constexpr unsigned long kUndnameComplete  = 0;
// "[scope::]name" only -- no access specifier, storage class, return type,
// calling convention, or parameter list.
constexpr unsigned long kUndnameNameOnly = 0x1000;
}

QString demangleSymbolName(const QString &mangledName, DemangleStyle style)
{
    if (mangledName.isEmpty())
        return mangledName;

    const QByteArray utf8 = mangledName.toUtf8();

#ifdef Q_OS_WIN
    // MSVC mangling -- the common case, since this app analyzes Windows
    // binaries, most of which are MSVC-built proprietary software.
    if (mangledName.startsWith(QLatin1Char('?')))
    {
        char buffer[2048];
        const unsigned long flags = style == DemangleStyle::NameOnly ? kUndnameNameOnly : kUndnameComplete;
        const unsigned long len = UnDecorateSymbolName(utf8.constData(), buffer, sizeof(buffer), flags);
        return len > 0 ? QString::fromUtf8(buffer, static_cast<int>(len)) : mangledName;
    }
#endif

    // Itanium C++ ABI mangling -- GCC/Clang/MinGW-built binaries.
    if (mangledName.startsWith(QStringLiteral("_Z")))
    {
#ifdef QEXED_HAVE_CXXABI
        int status = 0;
        char *demangled = abi::__cxa_demangle(utf8.constData(), nullptr, nullptr, &status);
        QString result = (status == 0 && demangled) ? QString::fromUtf8(demangled) : mangledName;
        if (demangled)
            std::free(demangled);

        // __cxa_demangle() has no "name only" mode -- approximate it by
        // truncating at the parameter list. Itanium mangling doesn't encode
        // a return-type prefix for ordinary functions/methods (only for
        // certain template cases), so this is a reasonable approximation
        // for the common case, not a fully general parser.
        if (style == DemangleStyle::NameOnly)
        {
            const int paren = result.indexOf(QLatin1Char('('));
            if (paren >= 0)
                result.truncate(paren);
        }
        return result;
#else
        return mangledName;
#endif
    }

    return mangledName;
}
