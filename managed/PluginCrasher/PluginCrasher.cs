//
// Created by Michal Přikryl on 10.10.2025.
// Copyright (c) 2025 slynxcz. All rights reserved.
//
using System.Runtime.InteropServices;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Core.Attributes.Registration;
using CounterStrikeSharp.API.Modules.Commands;

namespace PluginCrasher
{
    public class PluginCrasher : BasePlugin
    {
        public override string ModuleName => "PluginCrasher";
        public override string ModuleVersion => "1.0.0";

        [DllImport("libc")]
        private static extern int raise(int sig);

        private const int SIGSEGV = 11; // Segmentation fault
        private const int SIGABRT = 6;  // Abort (core dump)

        public override void Load(bool hotReload)
        {
            Prints.ServerLog("[PluginCrasher] Loaded and waiting for 'prapele' command.", ConsoleColor.Green);
        }

        public override void Unload(bool hotReload)
        {
            Prints.ServerLog("[PluginCrasher] Unloaded.", ConsoleColor.Yellow);
        }

        [ConsoleCommand("prapele", "Simulates native crash via libc::raise")]
        public void CmdCrash(CCSPlayerController? player, CommandInfo info)
        {
            Prints.ServerLog("[PluginCrasher] Raising SIGSEGV (Segmentation fault)...", ConsoleColor.Red);
            raise(SIGSEGV); // SIGSEGV
        }

        [ConsoleCommand("prapele_abort", "Simulates abort() crash")]
        public void CmdCrashAbort(CCSPlayerController? player, CommandInfo info)
        {
            Prints.ServerLog("[PluginCrasher] Raising SIGABRT (abort)...", ConsoleColor.Red);
            raise(SIGABRT); // SIGABRT
        }
    }

    internal static class Prints
    {
        public static void ServerLog(string msg, ConsoleColor color = ConsoleColor.White)
        {
            Console.ForegroundColor = color;
            Console.WriteLine(msg);
            Console.ResetColor();
        }
    }
}