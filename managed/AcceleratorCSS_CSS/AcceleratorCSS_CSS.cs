//
// Created by Michal Přikryl on 10.10.2025.
// Copyright (c) 2025 slynxcz. All rights reserved.
//

using System.Buffers;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using CounterStrikeSharp.API;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Core.Attributes.Registration;
using CounterStrikeSharp.API.Modules.Commands;
using HarmonyLib;

namespace AcceleratorCSS_CSS;

// ReSharper disable once InconsistentNaming
// ReSharper disable once UnusedType.Global
public class AcceleratorCSS_CSS : BasePlugin
{
    public override string ModuleName => "AcceleratorCSS_CSS";
    public override string ModuleVersion => RuntimeContext.VersionString;
    public override string ModuleAuthor => "Slynx";

    public override void Load(bool hotReload)
    {
        RegisterListener<Listeners.OnMapStart>(OnMapStart);
        RegisterListener<Listeners.OnMetamodAllPluginsLoaded>(OnMetamodAllPluginsLoaded);
    }

    public override void Unload(bool hotReload)
    {
        RemoveListener<Listeners.OnMapStart>(OnMapStart);
        RemoveListener<Listeners.OnMetamodAllPluginsLoaded>(OnMetamodAllPluginsLoaded);
        RuntimeContext.Harmony?.UnpatchAll("AcceleratorCSS_CSS");
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

        RuntimeContext.Harmony = new Harmony("AcceleratorCSS_CSS");
        ManagedCrashDumper.ApplyHarmonyPatches();

        Prints.ServerLog("[AcceleratorCSS_CSS] Managed side initialized (Harmony active).", ConsoleColor.Green);
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
    private const int MaxHistory = 512;
    private static readonly object _lock = new();
    private static readonly Queue<byte[]> _entries = new();
    private static readonly List<string> _stringPool = new();
    private static readonly Dictionary<string, ushort> _stringIndex = new();

    // =====================================================================
    // Harmony patching
    // =====================================================================
    public static void ApplyHarmonyPatches()
    {
        int patched = 0, skipped = 0;

        foreach (var alc in AssemblyLoadContext.All)
        {
            foreach (var asm in alc.Assemblies)
            {
                if (asm.FullName == null) continue;

                if (asm.FullName.StartsWith("System") ||
                    asm.FullName.StartsWith("Microsoft") ||
                    asm.FullName.StartsWith("mscorlib") ||
                    asm.FullName.StartsWith("netstandard") ||
                    asm == typeof(AcceleratorCSS_CSS).Assembly)
                    continue;

                if (!ReferencesCounterStrikeSharpApi(asm))
                    continue;

                foreach (var type in SafeGetTypes(asm))
                {
                    foreach (var m in type.GetMethods(BindingFlags.Public | BindingFlags.NonPublic |
                                                      BindingFlags.Instance | BindingFlags.Static |
                                                      BindingFlags.DeclaredOnly))
                    {
                        if (!IsValidMethod(m))
                        {
                            skipped++;
                            continue;
                        }

                        try
                        {
                            RuntimeContext.Harmony?.Patch(m,
                                prefix: new HarmonyMethod(typeof(ManagedCrashDumper), nameof(OnEnter)));
                            patched++;
                        }
                        catch
                        {
                            skipped++;
                        }
                    }
                }
            }
        }

        Prints.ServerLog($"[AcceleratorCSS_CSS] Harmony patched {patched} methods (skipped {skipped})",
            ConsoleColor.Yellow);
    }

    private static bool IsValidMethod(MethodInfo m)
    {
        if (m.IsAbstract || m.IsConstructor || m.IsGenericMethod || m.IsSpecialName)
            return false;
        if (m.Name.StartsWith("get_") || m.Name.StartsWith("set_") || m.Name.Contains("Invoke"))
            return false;
        var ns = m.DeclaringType?.Namespace;
        if (ns != null && (ns.StartsWith("System") || ns.StartsWith("Microsoft")))
            return false;
        return true;
    }

    private static bool ReferencesCounterStrikeSharpApi(Assembly asm)
    {
        try
        {
            return asm.GetReferencedAssemblies().Any(n =>
                n.Name != null && n.Name.Equals("CounterStrikeSharp.API", StringComparison.OrdinalIgnoreCase));
        }
        catch
        {
            return false;
        }
    }

    private static Type[] SafeGetTypes(Assembly asm)
    {
        try
        {
            return asm.GetTypes();
        }
        catch
        {
            return Array.Empty<Type>();
        }
    }

    // =====================================================================
    // Call logging
    // =====================================================================
    public static void OnEnter(MethodBase __originalMethod)
    {
        int tid = Thread.CurrentThread.ManagedThreadId;
        string method = $"{__originalMethod.DeclaringType?.FullName}::{__originalMethod.Name}";

        ushort idx;
        lock (_lock)
        {
            if (!_stringIndex.TryGetValue(method, out idx))
            {
                idx = (ushort)_stringPool.Count;
                _stringPool.Add(method);
                _stringIndex[method] = idx;
            }

            var data = ArrayPool<byte>.Shared.Rent(6);
            BitConverter.TryWriteBytes(data.AsSpan(0, 4), tid);
            BitConverter.TryWriteBytes(data.AsSpan(4, 2), idx);

            if (_entries.Count >= MaxHistory)
            {
                var old = _entries.Dequeue();
                ArrayPool<byte>.Shared.Return(old);
            }

            _entries.Enqueue(data);
        }
    }

    // =====================================================================
    // Dump writer
    // =====================================================================
    public static void Dump()
    {
        try
        {
            var logDir = Path.Combine(RuntimeContext.GameDirectory, "csgo", "addons", "AcceleratorCSS", "logs");
            Directory.CreateDirectory(logDir);

            var file = $"managed_trace_{DateTime.UtcNow:yyyyMMdd_HHmmss}.txt";
            var path = Path.Combine(logDir, file);

            using var writer = new StreamWriter(path, false);
            writer.WriteLine("============= DUMP START ==============");
            writer.WriteLine("---");
            writer.WriteLine("============= ENVIRONMENT =============");
            writer.WriteLine($"Timestamp: {DateTime.UtcNow:yyyy-MM-dd HH:mm:ss} UTC");
            writer.WriteLine($"Process ID: {Environment.ProcessId}");
            writer.WriteLine($"Map: {RuntimeContext.MapName}");
            writer.WriteLine($"CounterStrikeSharp Version: {RuntimeContext.CssVersion}");
            writer.WriteLine($"AcceleratorCSS Version: {RuntimeContext.VersionString}");
            writer.WriteLine($"CLR Version: {Environment.Version}");
            writer.WriteLine($"OS: {Environment.OSVersion}");
            writer.WriteLine("---");
            writer.WriteLine("======== MANAGED CALL HISTORY ========");

            List<(int tid, ushort idx)> entries;
            lock (_lock)
            {
                entries = _entries
                    .Select(b => (BitConverter.ToInt32(b, 0), BitConverter.ToUInt16(b, 4)))
                    .ToList();
            }

            entries.Reverse();

            var grouped = entries
                .GroupBy(e => e.tid)
                .OrderBy(g => g.Key);

            foreach (var group in grouped)
            {
                writer.WriteLine($"[T{group.Key}] (Newest → Oldest)");

                var counts = new Dictionary<string, int>();

                foreach (var (_, idx) in group)
                {
                    string name = idx < _stringPool.Count ? _stringPool[idx] : "<invalid>";
                    if (counts.TryGetValue(name, out var c))
                        counts[name] = c + 1;
                    else
                        counts[name] = 1;
                }

                int i = 0;
                foreach (var kv in counts)
                {
                    if (kv.Value == 1)
                        writer.WriteLine($"{++i,3}: {kv.Key}");
                    else
                        writer.WriteLine($"{++i,3}: {kv.Key} ×{kv.Value}");
                }

                writer.WriteLine("---");
            }

            writer.WriteLine("============== DUMP END ==============");
            Prints.ServerLog($"[AcceleratorCSS_CSS] Managed dump written → {path}", ConsoleColor.Yellow);
        }
        catch (Exception ex)
        {
            Prints.ServerLog($"[AcceleratorCSS_CSS] Dump failed: {ex}", ConsoleColor.Red);
        }
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

internal class RuntimeContext
{
    public static string GameDirectory { get; private set; } = "";
    public static string MapName { get; set; } = "";
    public static string CssVersion { get; private set; } = "";
    private static string PluginVersion { get; set; } = "";
    private static string GitHash { get; set; } = "";
    public static string VersionString { get; private set; } = "";
    public static readonly object Lock = new();
    public static Dictionary<int, string> LastStacks = new();
    public static Dictionary<int, WeakReference<Thread>> LastThreadRefs = new();
    public static Harmony? Harmony;

    public static void Initialize()
    {
        GameDirectory = Server.GameDirectory;
        MapName = Server.MapName;
        CssVersion = Api.GetVersionString();
        PluginVersion = Environment.GetEnvironmentVariable("SEMVER") ?? "Local";
        GitHash = Environment.GetEnvironmentVariable("GITHUB_SHA_SHORT") ?? "Local";
        VersionString = $"{PluginVersion} @ {GitHash}";
    }
}