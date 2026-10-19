#include "watchdog.hpp"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace {

std::atomic<unsigned long long> beats{0};
HANDLE mainThread = nullptr;

void dumpStack(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    SuspendThread(mainThread);
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(mainThread, &ctx);
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 40; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, mainThread, &frame, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0)
            break;
        char buf[sizeof(SYMBOL_INFO) + 512]{};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        const char* name = SymFromAddr(process, frame.AddrPC.Offset, &disp, sym) ? sym->Name : "?";
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &ldisp, &line))
            std::fprintf(f, "%s %s:%lu\n", name, line.FileName, line.LineNumber);
        else
            std::fprintf(f, "%s\n", name);
    }
    ResumeThread(mainThread);
    std::fclose(f);
}

}

void watchdogStart(const std::string& path) {
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &mainThread,
                    THREAD_ALL_ACCESS, FALSE, 0);
    std::thread([path] {
        unsigned long long last = 0;
        int stalled = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            unsigned long long b = beats.load();
            stalled = b == last ? stalled + 1 : 0;
            last = b;
            if (stalled == 6) {
                dumpStack(path);
                return;
            }
        }
    }).detach();
}

void watchdogBeat() { beats.fetch_add(1); }
#else
void watchdogStart(const std::string&) {}
void watchdogBeat() {}
#endif
