#include "core/thread_name.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(__EMSCRIPTEN__)
#else
#include <pthread.h>
#include <cstring>
#endif

namespace otk {

void setThreadName(const char* name) {
#if defined(_WIN32)
    using SetDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto fn = reinterpret_cast<SetDescription>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
    if (!fn) return;
    std::wstring wide;
    for (const char* c = name; *c; ++c) wide.push_back(static_cast<wchar_t>(*c));
    fn(GetCurrentThread(), wide.c_str());
#elif defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__EMSCRIPTEN__)
    (void)name;
#else
    char shortName[16] = {};
    std::strncpy(shortName, name, sizeof(shortName) - 1);
    pthread_setname_np(pthread_self(), shortName);
#endif
}

}
