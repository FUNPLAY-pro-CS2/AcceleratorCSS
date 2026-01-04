# AcceleratorCSS

**Binary crash handler and managed trace detour for CounterStrikeSharp**

---

## Overview

`AcceleratorCSS` is a hybrid crash handling and managed tracing system for **Counter-Strike 2** servers. It consists of two tightly integrated components:

- **`AcceleratorCSS.so`** – native Metamod module providing low-level crash interception, signal handling and Breakpad dump generation.
- **`AcceleratorCSS_CSS.dll`** – managed C# plugin for CounterStrikeSharp that dynamically detours every plugin method and records a lightweight binary call history for crash diagnostics.

No configuration, no setup – just drop in the binaries and start your server.

---

## Features

- **Automatic detouring** of nearly all CounterStrikeSharp plugin methods using Harmony
- **Managed call history tracking** across all threads
- **Breakpad integration** for native crash dumps
- **Readable crash log output** with newest calls on top
- **Thread-aware grouping** and repetition aggregation (e.g. `×255`)
- **Zero configuration** – no `config.json`, no setup files
- **Linux only** (Windows build planned)

---

## Example Dump

```
============= DUMP START ==============
---
============= ENVIRONMENT =============
Timestamp: 2025-10-11 13:14:53 UTC
Process ID: 47
Map: de_mirage
CounterStrikeSharp Version: 1.0.340+Branch.main.Sha.4869acac41d3cd988e23a692d728d7d499c76cfd.4869aca
AcceleratorCSS Version: Local @ Local
CLR Version: 8.0.3
OS: Unix 6.8.0.83
---
======== MANAGED CALL HISTORY ========
[T1] (Newest → Oldest)
  1: PluginCrasher.Prints::ServerLog
  2: PluginCrasher.PluginCrasher::CmdCrash
  3: AdminESP.AdminESP::ResetESPIfPlayerControlsPawn ×255
  4: AdminESP.AdminESP::CheckTransmitListener ×255
---
============== DUMP END ==============
```

This shows the newest managed calls at the top, grouped per thread and aggregated for readability.

---

## Internal Flow

```
┌──────────────────────────┐
│CounterStrikeSharp Plugins│
│ (Admin, Fun etc.)        │
└─────────────┬────────────┘
              │ Harmony detours all managed calls
              ▼
┌──────────────────────────┐
│ AcceleratorCSS_CSS.dll   │
│  - Tracks method calls   │
│  - Buffers binary data   │
│  - Dumps on crash signal │
└─────────────┬────────────┘
              │ Native bridge via P/Invoke
              ▼
┌──────────────────────────┐
│ AcceleratorCSS.so        │
│  - Handles SIGSEGV etc.  │
│  - Invokes managed dump  │
│  - Writes Breakpad dump  │
└──────────────────────────┘
```

Everything runs automatically on server start once both modules are loaded.

---

## Output Location

All dumps are written to:

```
addons/AcceleratorCSS/logs/
```

Typical files:

```
managed_trace_2025-10-11_131453.txt
crash_dump.dmp
```

---

## Build Instructions

### Requirements

- HL2SDK-CS2
- Metamod:Source (CS2)
- funchook
- Google Breakpad
- spdlog
- .NET 8 SDK
- Latest CounterStrikeSharp

### Build with Docker + CMake

```bash
git clone https://github.com/FUNPLAY-pro-CS2/AcceleratorCSS.git
cd AcceleratorCSS
git submodule update --init --recursive
docker compose -f docker/docker-compose.yml up
```

---

## Usage

Directory structure:

```
addons/
└── AcceleratorCSS/
    ├── bin/linuxsteamrt64/AcceleratorCSS.so
    └── logs/
└── counterstrikesharp/
    ├── plugins/AcceleratorCSS_CSS/
    │           ├── AcceleratorCSS_CSS.dll
    │           └── 0Harmony.dll
    └── shared/0Harmony/0Harmony.dll
```

Run the server — the system starts automatically.  
Manual managed dump can be triggered with:

```
dumpmanaged
```

---

## License

[GPLv3](https://www.gnu.org/licenses/gpl-3.0.en.html)

## Author

**Michal "Slynx" Přikryl**  
[slynxdev.cz](https://slynxdev.cz)