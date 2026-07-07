#if defined(_WIN32) && defined(_MSC_VER)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <delayimp.h>

#include <cwchar>
#include <cstring>

namespace {

bool isBundledQtDll(const char *name)
{
    if (!name)
        return false;

    const size_t len = std::strlen(name);
    return len > 7
        && _strnicmp(name, "Qt6", 3) == 0
        && _stricmp(name + len - 4, ".dll") == 0;
}

bool qtBinDirectory(wchar_t path[MAX_PATH])
{
    wchar_t exePath[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;

    wchar_t *tail = exePath + n;
    while (tail > exePath && tail[-1] != L'\\' && tail[-1] != L'/')
        --tail;
    *tail = L'\0';

    const size_t needed = std::wcslen(exePath) + std::wcslen(L"qt\\bin") + 1;
    if (needed > MAX_PATH)
        return false;

    wcscpy_s(path, MAX_PATH, exePath);
    wcscat_s(path, MAX_PATH, L"qt\\bin");
    return true;
}

HMODULE loadFromQtBin(const char *dllName)
{
    wchar_t qtBin[MAX_PATH] = {};
    if (!qtBinDirectory(qtBin))
        return nullptr;

    wchar_t dllWide[MAX_PATH] = {};
    const int converted = MultiByteToWideChar(CP_ACP, 0, dllName, -1, dllWide, MAX_PATH);
    if (converted <= 0)
        return nullptr;

    // Qt plugins may depend on Qt DLLs that q22.exe does not import directly
    // (for example Qt6Svg.dll). Keep qt\bin in the process DLL search path so
    // those plugin dependencies resolve after Qt starts loading plugins.
    SetDllDirectoryW(qtBin);

    const size_t needed = std::wcslen(qtBin) + 1 + std::wcslen(dllWide) + 1;
    if (needed > MAX_PATH)
        return nullptr;

    wchar_t path[MAX_PATH] = {};
    wcscpy_s(path, qtBin);
    wcscat_s(path, L"\\");
    wcscat_s(path, dllWide);

    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module)
        return module;

    return LoadLibraryExW(path, nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                          LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

FARPROC WINAPI q22DelayLoadHook(unsigned notify, PDelayLoadInfo info)
{
    if ((notify != dliNotePreLoadLibrary && notify != dliFailLoadLib) ||
        !info || !isBundledQtDll(info->szDll)) {
        return nullptr;
    }

    return reinterpret_cast<FARPROC>(loadFromQtBin(info->szDll));
}

} // namespace

extern "C" const PfnDliHook __pfnDliNotifyHook2 = q22DelayLoadHook;
extern "C" const PfnDliHook __pfnDliFailureHook2 = q22DelayLoadHook;

#endif
