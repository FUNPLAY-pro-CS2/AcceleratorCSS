//
// Created by Michal Přikryl on 10.10.2025.
// Copyright (c) 2025 slynxcz. All rights reserved.
//

using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using CounterStrikeSharp.API;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Core.Attributes.Registration;
using CounterStrikeSharp.API.Modules.Commands;

namespace AcceleratorCSS_CSS;

// ReSharper disable once InconsistentNaming
// ReSharper disable once UnusedType.Global
public class AcceleratorCSS_CSS : BasePlugin
{
    public override string ModuleName => "AcceleratorCSS_CSS";
    public override string ModuleVersion => RuntimeContext.VersionString;

    public override void Load(bool hotReload)
    {
        RegisterListener<Listeners.OnMapStart>(OnMapStart);
        RegisterListener<Listeners.OnMetamodAllPluginsLoaded>(OnMetamodAllPluginsLoaded);
        RegisterListener<Listeners.OnTick>(OnTick);
    }

    public override void Unload(bool hotReload)
    {
        RemoveListener<Listeners.OnMapStart>(OnMapStart);
        RemoveListener<Listeners.OnMetamodAllPluginsLoaded>(OnMetamodAllPluginsLoaded);
        RemoveListener<Listeners.OnTick>(OnTick);
    }

    private void OnMapStart(string mapname)
    {
        RuntimeContext.MapName = mapname;
    }

    private void OnMetamodAllPluginsLoaded()
    {
        if (!NativeBridge.Initialize(Server.GameDirectory))
        {
            Prints.ServerLog("[AcceleratorCSS_CSS] Native bridge initialization failed.", ConsoleColor.Red);
            return;
        }

        AssemblyRegistry.Initialize();
        RuntimeContext.Initialize();
        NativeBridge.RegisterCrashCallback();
        Prints.ServerLog("[AcceleratorCSS_CSS] Managed side initialized.", ConsoleColor.Green);
    }

    private void OnTick()
    {
        ManagedCrashDumper.SampleStacks();
    }

    [ConsoleCommand("dumpmanaged", "Force managed crash dump")]
    public void DumpManagedCmd(CCSPlayerController? player, CommandInfo info)
    {
        ManagedCrashDumper.Dump();
        Prints.ServerLog("[AcceleratorCSS_CSS] Manual dump triggered.", ConsoleColor.Green);
    }
}

internal static class AssemblyRegistry
{
    public static void Initialize()
    {
        foreach (var alc in AssemblyLoadContext.All)
        {
            foreach (var asm in alc.Assemblies)
            {
                if (IsSystemAssembly(asm))
                    continue;

                var name = asm.GetName();
                NativeBridge.RegisterAssembly(name.Name ?? "<unknown>", name.Version?.ToString() ?? "unknown");
            }
        }
    }

    private static bool IsSystemAssembly(Assembly asm)
    {
        var n = asm.FullName ?? "";
        return n.StartsWith("System") || n.StartsWith("Microsoft") || n.StartsWith("mscorlib") ||
               n.StartsWith("netstandard");
    }
}

internal static class ManagedCrashDumper
{
    // Vrací poslední snapshoty (kopie)
    private static Dictionary<int, string> GetLastSnapshot()
    {
        lock (RuntimeContext.Lock)
            return new Dictionary<int, string>(RuntimeContext.LastStacks);
    }

    public static void Dump()
    {
        try
        {
            var logDir = Path.Combine(RuntimeContext.GameDirectory, "csgo", "addons", "AcceleratorCSS", "logs");
            Directory.CreateDirectory(logDir);

            var guidPart = Guid.NewGuid().ToString("N")[..4];
            var fileName = $"managed_crash_{DateTime.UtcNow:yyyyMMdd_HHmmss}_{guidPart}.txt";
            var dumpPath = Path.Combine(logDir, fileName);

            using var writer = new StreamWriter(dumpPath, false);

            writer.WriteLine("======== MANAGED CRASH STATE ========");
            writer.WriteLine($"Timestamp: {DateTime.UtcNow:yyyy-MM-dd HH:mm:ss} UTC");
            writer.WriteLine($"Process ID: {Environment.ProcessId}");
            writer.WriteLine($"Map: {RuntimeContext.MapName}");
            writer.WriteLine($"Version: {RuntimeContext.VersionString}");
            writer.WriteLine();

            writer.WriteLine("-------- THREAD SNAPSHOT (ACTUAL) --------");
            var actual = GetAllStackTracesSafe();
            foreach (var (thread, trace) in actual)
            {
                writer.WriteLine($"\n[Thread] ID={thread.ManagedThreadId} | Name={thread.Name ?? "Unnamed"} | State={thread.ThreadState}");
                var frames = trace.GetFrames();
                if (frames.Length == 0)
                {
                    writer.WriteLine("    <no frames>");
                    continue;
                }
                foreach (var frame in frames)
                {
                    var method = frame.GetMethod();
                    if (method == null || method.DeclaringType?.FullName == "AcceleratorCSS_CSS") continue;
                    writer.WriteLine($"    at {method.DeclaringType?.FullName}.{method.Name}");
                }
            }

            writer.WriteLine();
            writer.WriteLine("-------- THREAD SNAPSHOT (LAST KNOWN / FROM SAMPLER) --------");
            var last = GetLastSnapshot();
            if (last.Count == 0)
                writer.WriteLine("    <no last-known snapshots available>");
            else
            {
                foreach (var kv in last.OrderBy(k => k.Key))
                {
                    writer.WriteLine($"\n[Thread] ID={kv.Key}");
                    writer.WriteLine(string.IsNullOrWhiteSpace(kv.Value) ? "    <no frames>" : kv.Value);
                }
            }

            writer.WriteLine();
            writer.WriteLine("-------- LAST KNOWN THREADS (refs) --------");
            var threadRefs = GetLastThreadRefs();
            foreach (var kv in threadRefs.OrderBy(k => k.Key))
            {
                var alive = kv.Value.TryGetTarget(out var t) && t.IsAlive;
                writer.WriteLine($"[ThreadRef] ID={kv.Key} Alive={alive} Name={(t?.Name ?? "<unknown>")}");
            }

            writer.WriteLine();
            writer.WriteLine("-------- ENVIRONMENT --------");
            writer.WriteLine($"CLR Version: {Environment.Version}");
            writer.WriteLine($"OS: {Environment.OSVersion}");
            writer.WriteLine($"Architecture: {RuntimeInformation.ProcessArchitecture}");
            writer.WriteLine($"WorkingSet: {RuntimeInformation.ProcessArchitecture} {Environment.WorkingSet / 1024 / 1024} MB");

            writer.Flush();
            Prints.ServerLog($"[AcceleratorCSS_CSS] Managed crash dump written to {dumpPath}", ConsoleColor.Yellow);
        }
        catch (Exception ex)
        {
            Prints.ServerLog($"[AcceleratorCSS_CSS] Dump failed: {ex}", ConsoleColor.Red);
        }
    }

    private static Func<Dictionary<Thread, StackTrace>>? _getAllStacks;

    public static Dictionary<Thread, StackTrace> GetAllStackTracesSafe()
    {
        try
        {
            var m = typeof(Thread).GetMethod("GetAllStackTraces", BindingFlags.Static | BindingFlags.NonPublic);
            if (m != null)
                _getAllStacks =
                    (Func<Dictionary<Thread, StackTrace>>)Delegate.CreateDelegate(
                        typeof(Func<Dictionary<Thread, StackTrace>>), m);

            return _getAllStacks?.Invoke() ?? new() { { Thread.CurrentThread, new StackTrace(true) } };
        }
        catch
        {
            return new() { { Thread.CurrentThread, new StackTrace(true) } };
        }
    }

    public static void SampleStacks()
    {
        try
        {
            var dict = GetAllStackTracesSafe();

            var next = new Dictionary<int, string>();
            var refs = new Dictionary<int, WeakReference<Thread>>();

            foreach (var (thread, trace) in dict)
            {
                try
                {
                    var sb = new System.Text.StringBuilder();
                    var frames = trace.GetFrames();
                    if (frames != null)
                    {
                        foreach (var f in frames)
                        {
                            var m = f.GetMethod();
                            if (m == null) continue;
                            sb.AppendLine($"    at {m.DeclaringType?.FullName}.{m.Name}");
                        }
                    }
                    next[thread.ManagedThreadId] = sb.ToString();
                    refs[thread.ManagedThreadId] = new WeakReference<Thread>(thread);
                }
                catch
                {
                }
            }

            lock (RuntimeContext.Lock)
            {
                // přepíšeme poslední snapshoty atomicky
                RuntimeContext.LastStacks = next;
                RuntimeContext.LastThreadRefs = refs;
            }
        }
        catch
        {
        }
    }

    private static Dictionary<int, WeakReference<Thread>> GetLastThreadRefs()
    {
        lock (RuntimeContext.Lock)
            return new Dictionary<int, WeakReference<Thread>>(RuntimeContext.LastThreadRefs);
    }
}

public static class NativeBridge
{
    private static nint _libHandle;

    private delegate void RegisterManagedAssemblyDelegate(string name, string version);
    private static RegisterManagedAssemblyDelegate? _registerAssembly;

    private delegate void RegisterManagedCrashHandlerDelegate(nint fnPtr);
    private static RegisterManagedCrashHandlerDelegate? _registerCrashHandler;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void ManagedCrashHandler();

    private static ManagedCrashHandler? _managedCrashHandler;

    public static bool Initialize(string basePath)
    {
        var path = Path.Combine(basePath, "csgo", "addons", "AcceleratorCSS", "bin", "linuxsteamrt64",
            "AcceleratorCSS.so");
        if (!File.Exists(path))
        {
            Prints.ServerLog($"[AcceleratorCSS_CSS] Native lib not found: {path}", ConsoleColor.Red);
            return false;
        }

        try
        {
            _libHandle = NativeLibrary.Load(path);
            _registerAssembly = GetDelegate<RegisterManagedAssemblyDelegate>("RegisterManagedAssembly");
            _registerCrashHandler = GetDelegate<RegisterManagedCrashHandlerDelegate>("RegisterManagedCrashHandler");
            Prints.ServerLog($"[AcceleratorCSS_CSS] Loaded native lib OK ({path})", ConsoleColor.Green);
            return true;
        }
        catch (Exception ex)
        {
            Prints.ServerLog($"[AcceleratorCSS_CSS] Failed to load native lib: {ex}", ConsoleColor.Red);
            return false;
        }
    }

    private static T? GetDelegate<T>(string exportName) where T : class
    {
        try
        {
            var ptr = NativeLibrary.GetExport(_libHandle, exportName);
            return Marshal.GetDelegateForFunctionPointer<T>(ptr);
        }
        catch (Exception ex)
        {
            Prints.ServerLog($"[AcceleratorCSS_CSS] Missing export {exportName}: {ex.Message}", ConsoleColor.Red);
            return null;
        }
    }

    public static void RegisterAssembly(string name, string version)
        => _registerAssembly?.Invoke(name, version);

    private static void RegisterCrashHandler(nint fnPtr)
        => _registerCrashHandler?.Invoke(fnPtr);

    private static void ManagedOnCrash()
    {
        try
        {
            ManagedCrashDumper.Dump();
        }
        catch (Exception ex)
        {
            Prints.ServerLog($"[AcceleratorCSS_CSS] Managed crash dump failed: {ex}", ConsoleColor.Red);
        }
    }

    public static void RegisterCrashCallback()
    {
        _managedCrashHandler = ManagedOnCrash;
        var fnPtr = Marshal.GetFunctionPointerForDelegate(_managedCrashHandler);
        RegisterCrashHandler(fnPtr);
    }
}

public static class Prints
{
    public static void ServerLog(string msg, ConsoleColor color = ConsoleColor.White)
    {
        Console.ForegroundColor = color;
        Console.WriteLine(msg);
        Console.ResetColor();
    }
}

internal static class RuntimeContext
{
    public static string GameDirectory { get; private set; } = "";
    public static string MapName { get; set; } = "";
    private static string PluginVersion { get; set; } = "";
    private static string GitHash { get; set; } = "";
    public static string VersionString { get; private set; } = "";
    public static readonly object Lock = new();
    public static Dictionary<int, string> LastStacks = new();
    public static Dictionary<int, WeakReference<Thread>> LastThreadRefs = new();

    public static void Initialize()
    {
        GameDirectory = Server.GameDirectory;
        MapName = Server.MapName;
        PluginVersion = Environment.GetEnvironmentVariable("SEMVER") ?? "Local";
        GitHash = Environment.GetEnvironmentVariable("GITHUB_SHA_SHORT") ?? "Local";
        VersionString = $"{PluginVersion} @ {GitHash}";

        Prints.ServerLog($"[AcceleratorCSS_CSS] Cached context for crash dump.", ConsoleColor.DarkGray);
    }
}