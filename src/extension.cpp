//
// Created by Michal Přikryl on 12.06.2025.
// Copyright (c) 2025 slynxcz. All rights reserved.
//
#include "extension.h"
#include "CMiniDumpComment.hpp"
#include "log.h"

#include "dyncall/dyncall/dyncall.h"
#include "funchook.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <cassert>

#include <nlohmann/json.hpp>

#include <entitysystem.h>
#include <entity2/entitysystem.h>

#include <csignal>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <csignal>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#if defined WIN32
#include <corecrt_io.h>
#else
#include "client/linux/handler/exception_handler.h"
#include "common/linux/linux_libc_support.h"
#include "third_party/lss/linux_syscall_support.h"
#include "common/linux/http_upload.h"
#endif

#include "paths.h"
#include "common/path_helper.h"
#include "common/using_std_string.h"
#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "google_breakpad/processor/call_stack.h"
#include "google_breakpad/processor/stack_frame.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"
#include "processor/pathname_stripper.h"
#include "spdlog/fmt/bundled/ranges.h"

#define VERSION_STRING SEMVER " @ " GITHUB_SHA
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

namespace fs = std::filesystem;

size_t g_MaxCallbackTrace = 10;

struct CallbackTraceEntry
{
    std::string name;
    std::string profile;
    std::string callerStack;
};

struct ManagedSig {
    std::string ret;
    std::vector<std::string> args;
};

std::vector<CallbackTraceEntry> g_CallbackTraceBuffer;
size_t g_CallbackTraceIndex = 0;
std::mutex g_CallbackTraceMutex;
static std::unordered_map<void*, ManagedSig> g_Signatures;
static std::unordered_map<void*, std::string> g_HookNames;
static std::unordered_map<void*, funchook_t*> g_Hooks;

ISmmAPI* g_ISmm = nullptr;

static std::string lastMap;
char crashMap[256];
char crashGamePath[512];
char crashCommandLine[1024];
char dumpStoragePath[512];

google_breakpad::ExceptionHandler* exceptionHandler = nullptr;
CMiniDumpComment g_MiniDumpComment(95000);

void (*SignalHandler)(int, siginfo_t*, void*);

const int kExceptionSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
const int kNumHandledSignals = std::size(kExceptionSignals);

bool g_pluginRegistered = false;

auto safeStr = [](const char* str) -> std::string
{
    if (!str)
        return "[null]";
    try
    {
        return std::string(str);
    }
    catch (...)
    {
        return "[invalid string]";
    }
};

struct PluginConfig
{
    bool LightweightMode;
    bool LogCallbacksToConsole;
    int CallbackLogSize;
    const char* FiltersPtr;
};

PluginConfig config{};

static void LogCall(const char* name) {
    CORE_INFO("[ManagedCall] {}", name);
}

// === generic trampoline ===
// protože každá funkce má jiný počet argů, dyncall se použije na znovu-volání originálu
static void* GenericPreLogger(void* fnPtr)
{
    auto it = g_HookNames.find(fnPtr);
    if (it != g_HookNames.end())
        CORE_INFO("[Managed PRE] {}", it->second);
    else
        CORE_INFO("[Managed PRE] {}", fnPtr);

    auto sigIt = g_Signatures.find(fnPtr);
    if (sigIt == g_Signatures.end())
        return nullptr;

    auto& sig = sigIt->second;

    DCCallVM* vm = dcNewCallVM(4096);
    dcMode(vm, DC_CALL_C_DEFAULT);

    // --- placeholder argy ---
    // tady můžeš reálně doplnit hodnoty (např. null pointers)
    for (auto& arg : sig.args)
    {
        if (arg == "Int32" || arg == "UInt32") dcArgInt(vm, 0);
        else if (arg == "Single") dcArgFloat(vm, 0.0f);
        else if (arg == "Double") dcArgDouble(vm, 0.0);
        else if (arg == "Boolean") dcArgBool(vm, false);
        else dcArgPointer(vm, nullptr);
    }

    void* retPtr = nullptr;

    if (sig.ret == "Void")
        dcCallVoid(vm, fnPtr);
    else if (sig.ret == "Int32" || sig.ret == "UInt32")
        retPtr = (void*)(uintptr_t)dcCallInt(vm, fnPtr);
    else if (sig.ret == "Single")
        retPtr = (void*)(uintptr_t)dcCallFloat(vm, fnPtr);
    else if (sig.ret == "Double")
        retPtr = (void*)(uintptr_t)dcCallDouble(vm, fnPtr);
    else if (sig.ret == "Boolean")
        retPtr = (void*)(uintptr_t)dcCallBool(vm, fnPtr);
    else
        retPtr = dcCallPointer(vm, fnPtr);

    dcFree(vm);

    CORE_INFO("[Managed RET] {} -> {}", fnPtr, (uintptr_t)retPtr);
    return retPtr;
}

DLL_EXPORT void RegisterManagedMethodEx(
    const char* name,
    void* fnPtr,
    const char* returnType,
    const char** argTypes,
    int argCount)
{
    if (!name || !fnPtr)
        return;

    ManagedSig sig;
    sig.ret = returnType ? returnType : "void";
    for (int i = 0; i < argCount; ++i)
        sig.args.emplace_back(argTypes[i] ? argTypes[i] : "void*");

    g_Signatures[fnPtr] = sig;
    g_HookNames[fnPtr] = name;

    funchook_t* hook = funchook_create();

    auto trampoline = +[](void* ctx) -> void* {
        return GenericPreLogger(ctx);
    };

    funchook_prepare(hook, &fnPtr, (void*)trampoline);
    if (funchook_install(hook, 0) == 0) {
        g_Hooks[fnPtr] = hook;
        CORE_INFO("[Funchook] Hooked {} @ {}", name, fnPtr);
    } else {
        CORE_ERROR("[Funchook] Failed hook for {} @ {}", name, fnPtr);
    }

    std::vector<std::string> nativeArgs;
    for (auto& arg : sig.args)
    {
        if (arg == "Int32" || arg == "UInt32") nativeArgs.push_back("int");
        else if (arg == "Single") nativeArgs.push_back("float");
        else if (arg == "Double") nativeArgs.push_back("double");
        else if (arg == "Boolean") nativeArgs.push_back("bool");
        else if (arg.find("CCS") != std::string::npos ||
                 arg.find("NativeObject") != std::string::npos)
            nativeArgs.push_back("void*");
        else
            nativeArgs.push_back("void*");
    }

    std::string nativeRet;
    if (sig.ret == "Void") nativeRet = "void";
    else if (sig.ret == "Boolean") nativeRet = "bool";
    else if (sig.ret == "Single") nativeRet = "float";
    else if (sig.ret == "Double") nativeRet = "double";
    else if (sig.ret.find("CCS") != std::string::npos) nativeRet = "void*";
    else nativeRet = "void*";

    CORE_INFO("[ManagedSig] {}({}) -> {}",
              name,
              fmt::join(nativeArgs, ", "),
              nativeRet);
}

// DLL_EXPORT void RegisterCallbackTraceBinary(const void* data, size_t len)
// {
//     if (!data || len < 6) return;
//
//     const char* raw = reinterpret_cast<const char*>(data);
//     uint16_t nameLen = *reinterpret_cast<const uint16_t*>(raw);
//     uint16_t profileLen = *reinterpret_cast<const uint16_t*>(raw + 2);
//     uint16_t stackLen = *reinterpret_cast<const uint16_t*>(raw + 4);
//
//     if (len < 6 + nameLen + profileLen + stackLen) return;
//
//     std::string name(raw + 6, nameLen);
//     std::string profile(raw + 6 + nameLen, profileLen);
//     std::string stack(raw + 6 + nameLen + profileLen, stackLen);
//
//     if (config.LogCallbacksToConsole)
//     {
//         CORE_INFO("[Callback] Name: {}", name);
//     }
//
//     std::lock_guard lock(g_CallbackTraceMutex);
//
//     if (g_CallbackTraceBuffer.empty())
//         return;
//
//     size_t bufferSize = g_CallbackTraceBuffer.size();
//     g_CallbackTraceBuffer[g_CallbackTraceIndex % bufferSize] = {
//         std::move(name), std::move(profile), std::move(stack)
//     };
//     g_CallbackTraceIndex++;
// }

void SetMaxCallbackTrace(size_t newSize)
{
    std::lock_guard lock(g_CallbackTraceMutex);

    if (newSize == 0)
        return;

    g_CallbackTraceBuffer.clear();
    g_CallbackTraceBuffer.resize(newSize);
    g_CallbackTraceIndex = 0;
}

DLL_EXPORT PluginConfig CssPluginRegistered()
{
    static std::string filtersJoined;

    config.LightweightMode = true;

    try
    {
        std::ifstream configFile(AcceleratorCSS::paths::ConfigDirectory());
        if (configFile.is_open())
        {
            nlohmann::json j;
            configFile >> j;

            if (j.contains("LightweightMode") && j["LightweightMode"].is_boolean())
                config.LightweightMode = j["LightweightMode"].get<bool>();

            if (j.contains("LogCallbacksToConsole") && j["LogCallbacksToConsole"].is_boolean())
                config.LogCallbacksToConsole = j["LogCallbacksToConsole"].get<bool>();

            if (j.contains("CallbackLogSize") && j["CallbackLogSize"].is_number_integer())
            {
                config.CallbackLogSize = j["CallbackLogSize"].get<int>();
                SetMaxCallbackTrace(config.CallbackLogSize);
            }
            if (j.contains("ProfileExcludeFilters") && j["ProfileExcludeFilters"].is_array())
            {
                std::ostringstream oss;
                for (const auto& item : j["ProfileExcludeFilters"])
                {
                    if (item.is_string())
                        oss << item.get<std::string>() << ",";
                }

                filtersJoined = oss.str();
                if (!filtersJoined.empty() && filtersJoined.back() == ',')
                    filtersJoined.pop_back();

                config.FiltersPtr = filtersJoined.c_str();
            }

            g_pluginRegistered = true;
        }
    }
    catch (...)
    {
        filtersJoined = "OnTick,CheckTransmit,Display";
        config.FiltersPtr = filtersJoined.c_str();
    }

    return config;
}

static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded)
{
    CORE_CRITICAL("- [ Crash detected! Writing custom crash log... ] -");

    my_strlcpy(dumpStoragePath, descriptor.path(), sizeof(dumpStoragePath));
    my_strlcat(dumpStoragePath, ".txt", sizeof(dumpStoragePath));

    std::ofstream dumpFile(dumpStoragePath, std::ios::out | std::ios::trunc);
    if (!dumpFile.is_open())
    {
        CORE_ERROR("- [ Failed to open crash log file: {} ] -", dumpStoragePath);
        return false;
    }

    dumpFile << "-------- CONFIG BEGIN --------\n";
    dumpFile << "Map=" << crashMap << "\n";
    dumpFile << "GamePath=" << crashGamePath << "\n";
    dumpFile << "CommandLine=" << crashCommandLine << "\n";
    dumpFile << "-------- CONFIG END --------\n\n";

    LoggingSystem_GetLogCapture(&g_MiniDumpComment, false);
    const char* pszConsoleHistory = g_MiniDumpComment.GetStartPointer();

    if (pszConsoleHistory[0])
    {
        dumpFile << "-------- CONSOLE HISTORY BEGIN --------\n";
        dumpFile << pszConsoleHistory;
        dumpFile << "-------- CONSOLE HISTORY END --------\n\n";
    }

    dumpFile << "-------- CALLBACK TRACE BEGIN --------\n";
    {
        std::lock_guard lock(g_CallbackTraceMutex);

        const size_t bufferSize = g_CallbackTraceBuffer.size();
        const size_t validCount = std::min(bufferSize, g_CallbackTraceIndex);

        for (size_t i = 0; i < validCount; ++i)
        {
            size_t idx = (g_CallbackTraceIndex - 1 - i) % bufferSize;
            const auto& entry = g_CallbackTraceBuffer[idx];

            dumpFile << "Name: " << entry.name << "\n";
            dumpFile << "Profile: " << entry.profile << "\n";
            dumpFile << "Stack:\n" << entry.callerStack << "\n";
            dumpFile << "-----------------------------\n";
        }
    }
    dumpFile << "-------- CALLBACK TRACE END --------\n";

    dumpFile.close();

    CORE_INFO("Custom crash log written to: {}", dumpStoragePath);
    return true;
}

CGameEntitySystem* GameEntitySystem() { return nullptr; }

class GameSessionConfiguration_t
{
};

PLUGIN_EXPOSE(AcceleratorCSS_MM, acceleratorcss::g_iPlugin);

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&,
                   ISource2WorldSession*, const char*);

namespace
acceleratorcss
{
    AcceleratorCSS_MM g_iPlugin;

    bool AcceleratorCSS_MM::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
    {
        PLUGIN_SAVEVARS();
        Log::Init();

        GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
        GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService,
                            NETWORKSERVERSERVICE_INTERFACE_VERSION);
        GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer, IVEngineServer, INTERFACEVERSION_VENGINESERVER);

        g_ISmm = ismm;

        std::snprintf(crashGamePath, sizeof(crashGamePath), "%s", Paths::GameDirectory().c_str());
        std::snprintf(dumpStoragePath, sizeof(dumpStoragePath), "%s", Paths::Logs().c_str());

        if (crashGamePath[sizeof(crashGamePath) - 1] != '\0')
        {
            CORE_ERROR("[DEBUG] crashGamePath not null-terminated!");
            return false;
        }
        if (dumpStoragePath[sizeof(dumpStoragePath) - 1] != '\0')
        {
            CORE_ERROR("[DEBUG] dumpStoragePath not null-terminated!");
            return false;
        }

        struct stat st{};
        if (stat(dumpStoragePath, &st) == -1)
        {
            if (mkdir(dumpStoragePath, 0777) == -1)
            {
                CORE_ERROR("Failed to create logs directory: {}", dumpStoragePath);
                g_pluginRegistered = false;
                return false;
            }
        }
        else
        {
            chmod(dumpStoragePath, 0777);
        }

        SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &AcceleratorCSS_MM::GameFrame),
                    true);
        SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService,
                    SH_MEMBER(this, &AcceleratorCSS_MM::StartupServer), true);

        std::snprintf(crashCommandLine, sizeof(crashCommandLine), "%s",
                      CommandLine() ? CommandLine()->GetCmdLine() : "");

        if (late)
        {
            if (auto gs = g_pNetworkServerService->GetIGameServer())
            {
                if (const char* map = gs->GetMapName())
                {
                    StartupServer({}, nullptr, map);
                }
            }
        }

        g_SMAPI->AddListener(this, this);

        try
        {
            std::ifstream configFile(AcceleratorCSS::paths::ConfigDirectory());
            if (configFile.is_open())
            {
                configFile >> g_Config;
                CORE_INFO("Config loaded: {}", AcceleratorCSS::paths::ConfigDirectory());
            }
            else
            {
                CORE_WARN("Could not open config: {}", AcceleratorCSS::paths::ConfigDirectory());
                g_pluginRegistered = false;
            }
        }
        catch (const std::exception& e)
        {
            CORE_ERROR("Failed to parse config: {}", e.what());
            g_pluginRegistered = false;
        }

        // google_breakpad::MinidumpDescriptor descriptor(dumpStoragePath);
        // exceptionHandler = new google_breakpad::ExceptionHandler(descriptor, nullptr, dumpCallback, nullptr, true,
        //                                                          -1);
        //
        // struct sigaction oact{};
        // sigaction(SIGSEGV, nullptr, &oact);
        // SignalHandler = oact.sa_sigaction;

        CORE_INFO("MM plugin loaded.");
        return true;
    }

    bool AcceleratorCSS_MM::Unload(char* error, size_t maxlen)
    {
        Log::Close();
        g_pluginRegistered = false;


        SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &AcceleratorCSS_MM::GameFrame),
                       true);
        SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService,
                       SH_MEMBER(this, &AcceleratorCSS_MM::StartupServer), true);

        delete exceptionHandler;

        CORE_INFO("- [ MM plugin unloaded. ] -");

        return true;
    }

    void AcceleratorCSS_MM::AllPluginsLoaded()
    {
        std::thread([]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            if (g_pluginRegistered)
                CORE_INFO("- [ MM plugin is active and linked. ] -");
            else
                CORE_ERROR("- [ MM plugin did not register itself. ] -");
        }).detach();
    }

    void AcceleratorCSS_MM::GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
    {
        // bool weHaveBeenFuckedOver = false;
        // struct sigaction oact;
        //
        // auto gs = g_pNetworkServerService->GetIGameServer();
        // const char* currentMap = gs ? gs->GetMapName() : nullptr;
        // if (currentMap && *currentMap && lastMap != currentMap)
        // {
        //     std::snprintf(crashMap, sizeof(crashMap), "%s", currentMap);
        //     lastMap = currentMap;
        //     CORE_INFO("- [ Detected map change: {} ] -", currentMap);
        // }
        //
        // for (int i = 0; i < kNumHandledSignals; ++i)
        // {
        //     sigaction(kExceptionSignals[i], NULL, &oact);
        //
        //     if (oact.sa_sigaction != SignalHandler)
        //     {
        //         weHaveBeenFuckedOver = true;
        //         break;
        //     }
        // }
        //
        // if (!weHaveBeenFuckedOver)
        //     return;
        //
        // struct sigaction act;
        // memset(&act, 0, sizeof(act));
        // sigemptyset(&act.sa_mask);
        //
        // for (int i = 0; i < kNumHandledSignals; ++i)
        //     sigaddset(&act.sa_mask, kExceptionSignals[i]);
        //
        // act.sa_sigaction = SignalHandler;
        // act.sa_flags = SA_ONSTACK | SA_SIGINFO;
        //
        // for (int i = 0; i < kNumHandledSignals; ++i)
        //     sigaction(kExceptionSignals[i], &act, NULL);
    }

    void AcceleratorCSS_MM::StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*,
                                          const char* pszMapName)
    {
        if (pszMapName && *pszMapName)
            std::snprintf(crashMap, sizeof(crashMap), "%s", pszMapName);
    }

    const char* AcceleratorCSS_MM::GetAuthor()
    {
        return "Slynx";
    }

    const char* AcceleratorCSS_MM::GetName()
    {
        return "AcceleratorCSS";
    }

    const char* AcceleratorCSS_MM::GetDescription()
    {
        return "Local crash handler for C# plugins";
    }

    const char* AcceleratorCSS_MM::GetURL()
    {
        return "https://slynxdev.cz/";
    }

    const char* AcceleratorCSS_MM::GetLicense()
    {
        return "GPLv3";
    }

    const char* AcceleratorCSS_MM::GetVersion()
    {
        return VERSION_STRING;
    }

    const char* AcceleratorCSS_MM::GetDate()
    {
        return BUILD_TIMESTAMP;
    }

    const char* AcceleratorCSS_MM::GetLogTag()
    {
        return "AcceleratorCSS";
    }
}
