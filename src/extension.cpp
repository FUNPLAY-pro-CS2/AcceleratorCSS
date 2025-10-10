//
// Created by Michal Přikryl on 10.10.2025.
// Copyright (c) 2025 slynxcz. All rights reserved.
//
#include "extension.h"
#include "CMiniDumpComment.hpp"
#include "log.h"
#include "paths.h"

#include <entitysystem.h>
#include <nlohmann/json.hpp>

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <vector>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <dlfcn.h>
#include <pthread.h>

#if defined WIN32
#include <corecrt_io.h>
#else
#include "client/linux/handler/exception_handler.h"
#include "common/linux/linux_libc_support.h"
#include "third_party/lss/linux_syscall_support.h"
#endif

#define VERSION_STRING SEMVER " @ " GITHUB_SHA
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

CGameEntitySystem *GameEntitySystem() { return nullptr; }

class GameSessionConfiguration_t{
};

namespace fs = std::filesystem;

ISmmAPI* g_ISmm = nullptr;
google_breakpad::ExceptionHandler* g_ExceptionHandler = nullptr;
CMiniDumpComment g_MiniDumpComment(95000);

void (*SignalHandler)(int, siginfo_t*, void*);
const int kExceptionSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
const int kNumHandledSignals = std::size(kExceptionSignals);

char crashMap[256];
char crashGamePath[512];
char crashCommandLine[1024];
char dumpStoragePath[512];

struct ManagedAssemblyInfo {
    std::string name;
    std::string version;
    std::chrono::system_clock::time_point registeredAt;
};
std::vector<ManagedAssemblyInfo> g_ManagedAssemblies;
std::mutex g_ManagedAssembliesMutex;

using DumpManagedState_t = void (*)(const char*);
typedef void (*ManagedCrashCallback_t)();
ManagedCrashCallback_t g_ManagedCrashCallback = nullptr;

// ============================================================================
//   Managed Bridge Functions
// ============================================================================

DLL_EXPORT void RegisterManagedAssembly(const char* name, const char* version) {
    if (!name || !version)
        return;

    std::lock_guard<std::mutex> lock(g_ManagedAssembliesMutex);
    ManagedAssemblyInfo info{name, version, std::chrono::system_clock::now()};
    g_ManagedAssemblies.push_back(std::move(info));

    CORE_INFO("[AcceleratorCSS] Registered managed assembly: {} v{}", name, version);
}

DLL_EXPORT void RegisterManagedCrashHandler(void* fnPtr)
{
    if (!fnPtr)
    {
        CORE_WARN("[AcceleratorCSS] RegisterManagedCrashHandler called with null pointer.");
        return;
    }

    g_ManagedCrashCallback = reinterpret_cast<ManagedCrashCallback_t>(fnPtr);
    CORE_INFO("[AcceleratorCSS] Managed crash callback registered at {}", fmt::ptr(fnPtr));
}

DLL_EXPORT void LogManagedEvent(const char* msg) {
    if (msg && *msg)
        CORE_INFO("[AcceleratorCSS:Managed] {}", msg);
}

DLL_EXPORT void DumpManagedState(const char* dumpPath) {
    std::string outPath = dumpPath && *dumpPath
        ? std::string(dumpPath) + ".managed.txt"
        : std::string(dumpStoragePath) + "/managed_state.txt";

    std::ofstream out(outPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        CORE_ERROR("Failed to open managed dump file: {}", outPath);
        return;
    }

    out << "-------- MANAGED ASSEMBLIES --------\n";
    {
        std::lock_guard<std::mutex> lock(g_ManagedAssembliesMutex);
        for (const auto& asmInfo : g_ManagedAssemblies) {
            auto t = std::chrono::system_clock::to_time_t(asmInfo.registeredAt);
            out << fmt::format("{} v{} ({})\n", asmInfo.name, asmInfo.version, std::ctime(&t));
        }
    }

    out << "\n-------- SYSTEM INFO --------\n";
    out << "Build: " << VERSION_STRING << "\n";
    out << "Timestamp: " << BUILD_TIMESTAMP << "\n";
    out << "Map: " << crashMap << "\n";
    out << "GamePath: " << crashGamePath << "\n";
    out.close();

    CORE_INFO("[AcceleratorCSS] Managed state written to {}", outPath);
}

// ============================================================================
//   Breakpad Crash Callback
// ============================================================================

static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void*, bool succeeded) {
    CORE_CRITICAL("[AcceleratorCSS] Crash detected, writing dump...");

    my_strlcpy(dumpStoragePath, descriptor.path(), sizeof(dumpStoragePath));
    my_strlcat(dumpStoragePath, ".txt", sizeof(dumpStoragePath));

    std::ofstream dumpFile(dumpStoragePath, std::ios::out | std::ios::trunc);
    if (!dumpFile.is_open()) {
        CORE_ERROR("Failed to open crash log file: {}", dumpStoragePath);
        return false;
    }

    dumpFile << "-------- GAME INFO --------\n";
    dumpFile << "Map=" << crashMap << "\n";
    dumpFile << "GamePath=" << crashGamePath << "\n";
    dumpFile << "CommandLine=" << crashCommandLine << "\n\n";

    LoggingSystem_GetLogCapture(&g_MiniDumpComment, false);
    const char* pszConsoleHistory = g_MiniDumpComment.GetStartPointer();
    if (pszConsoleHistory[0]) {
        dumpFile << "-------- CONSOLE LOG --------\n";
        dumpFile << pszConsoleHistory << "\n";
    }
    dumpFile.close();

    void* handle = dlopen(nullptr, RTLD_NOW);
    if (handle) {
        auto fn = (DumpManagedState_t)dlsym(handle, "DumpManagedState");
        if (fn) {
            CORE_INFO("[AcceleratorCSS] Executing managed crash callback...");
            fn(descriptor.path());
        }
    }

    CORE_INFO("[AcceleratorCSS] Crash log written to {}", dumpStoragePath);
    return true;
}

// ============================================================================
//   Signal Hooking
// ============================================================================
void* ManagedCrashInvoker(void*)
{
    if (g_ManagedCrashCallback)
    {
        CORE_INFO("[AcceleratorCSS] Invoking managed crash handler (pthread)...");
        g_ManagedCrashCallback();
    }

    _exit(0);
    return nullptr;
}

void SignalHandler_Extended(int sig, siginfo_t* info, void* ucontext)
{
    CORE_CRITICAL("[AcceleratorCSS] Signal {} caught (code={})", sig, info ? info->si_code : 0);

    struct sigaction act {};
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_DFL;
    for (int i = 0; i < kNumHandledSignals; ++i)
        sigaction(kExceptionSignals[i], &act, nullptr);

    if (g_ManagedCrashCallback)
    {
        pthread_t t;
        CORE_INFO("[AcceleratorCSS] Spawning thread for managed dump...");
        if (pthread_create(&t, nullptr, ManagedCrashInvoker, nullptr) == 0)
            pthread_detach(t);
    }

    if (SignalHandler)
        SignalHandler(sig, info, ucontext);
}

// ============================================================================
//   Metamod Plugin
// ============================================================================

PLUGIN_EXPOSE(AcceleratorCSS_MM, acceleratorcss::g_iPlugin);

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0,
                   const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);

namespace acceleratorcss {

AcceleratorCSS_MM g_iPlugin;

bool AcceleratorCSS_MM::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) {
    PLUGIN_SAVEVARS();
    Log::Init();

    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer, IVEngineServer2, INTERFACEVERSION_VENGINESERVER);

    g_ISmm = ismm;

    std::snprintf(crashGamePath, sizeof(crashGamePath), "%s", Paths::GameDirectory().c_str());
    std::snprintf(dumpStoragePath, sizeof(dumpStoragePath), "%s", Paths::Logs().c_str());
    std::snprintf(crashCommandLine, sizeof(crashCommandLine), "%s",
                  CommandLine() ? CommandLine()->GetCmdLine() : "");

    // Create directory
    struct stat st{};
    if (stat(dumpStoragePath, &st) == -1)
        mkdir(dumpStoragePath, 0777);
    else
        chmod(dumpStoragePath, 0777);

    // Install Breakpad
    google_breakpad::MinidumpDescriptor descriptor(dumpStoragePath);
    g_ExceptionHandler = new google_breakpad::ExceptionHandler(descriptor, nullptr, dumpCallback, nullptr, true, -1);

    // Store default handler for chaining
    struct sigaction oact{};
    sigaction(SIGSEGV, nullptr, &oact);
    SignalHandler = oact.sa_sigaction;

    // Install our extended handler
    struct sigaction act{};
    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_sigaction = SignalHandler_Extended;
    act.sa_flags = SA_ONSTACK | SA_SIGINFO;
    for (int i = 0; i < kNumHandledSignals; ++i)
        sigaction(kExceptionSignals[i], &act, nullptr);

    SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &AcceleratorCSS_MM::GameFrame), true);
    SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService,
                SH_MEMBER(this, &AcceleratorCSS_MM::StartupServer), true);

    CORE_INFO("[AcceleratorCSS] Loaded successfully.");
    return true;
}

bool AcceleratorCSS_MM::Unload(char* error, size_t maxlen) {
    Log::Close();
    delete g_ExceptionHandler;
    CORE_INFO("[AcceleratorCSS] Unloaded.");
    return true;
}

void AcceleratorCSS_MM::GameFrame(bool, bool, bool) {}

void AcceleratorCSS_MM::StartupServer(const GameSessionConfiguration_t&, ISource2WorldSession*, const char* pszMapName) {
    if (pszMapName && *pszMapName)
        std::snprintf(crashMap, sizeof(crashMap), "%s", pszMapName);
}

const char* AcceleratorCSS_MM::GetAuthor() { return "Slynx"; }
const char* AcceleratorCSS_MM::GetName() { return "AcceleratorCSS"; }
const char* AcceleratorCSS_MM::GetDescription() { return "Crash handler bridge for CounterStrikeSharp"; }
const char* AcceleratorCSS_MM::GetURL() { return "https://slynxdev.cz/"; }
const char* AcceleratorCSS_MM::GetLicense() { return "GPLv3"; }
const char* AcceleratorCSS_MM::GetVersion() { return VERSION_STRING; }
const char* AcceleratorCSS_MM::GetDate() { return BUILD_TIMESTAMP; }
const char* AcceleratorCSS_MM::GetLogTag() { return "AcceleratorCSS"; }

} // namespace acceleratorcss
