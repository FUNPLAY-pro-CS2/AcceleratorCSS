//
// Created by Michal Přikryl on 10.10.2025.
// Copyright (c) 2025 slynxcz. All rights reserved.
//
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Core.Attributes.Registration;
using CounterStrikeSharp.API.Modules.Commands;

namespace PluginCrasher
{
    public class PluginCrasher : BasePlugin
    {
        public override string ModuleName => "PluginCrasher";
        public override string ModuleVersion => "1.0.0";

        public override void Load(bool hotReload)
        {
            Prints.ServerLog("[PluginCrasher] Loaded and waiting for 'prapele' command.", ConsoleColor.Green);
        }

        public override void Unload(bool hotReload)
        {
            Prints.ServerLog("[PluginCrasher] Unloaded.", ConsoleColor.Yellow);
        }

        [ConsoleCommand("prapele", "Simulates an unsafe access violation")]
        public void CmdCrash(CCSPlayerController? player, CommandInfo info)
        {
            Prints.ServerLog("[PluginCrasher] Simulating unsafe memory write (AccessViolation)...", ConsoleColor.Red);

            unsafe
            {
                byte* ptr = (byte*)0xDEADBEEF;
                *ptr = 42;
            }
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