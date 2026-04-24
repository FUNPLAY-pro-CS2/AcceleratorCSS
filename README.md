# AcceleratorCSS

**Binary crash handler and managed trace detour for CounterStrikeSharp**

---

## Overview

`AcceleratorCSS` is a hybrid crash handling and managed tracing system for **Counter-Strike 2** servers.  
It consists of two tightly integrated components:

- **`AcceleratorCSS.so`** – native Metamod module providing low-level crash interception, signal handling and Breakpad dump generation.  
- **`AcceleratorCSS_CSS.dll`** – managed C# plugin for CounterStrikeSharp that dynamically detours plugin methods and records a lightweight binary call history for crash diagnostics.

Just drop in the binaries and start your server.

---

## Features

- Automatic detouring of nearly all CounterStrikeSharp plugin methods using Harmony  
- Managed call history tracking across all threads  
- Breakpad integration for native crash dumps  
- Readable crash log output with newest calls on top  
- Thread-aware grouping and repetition aggregation (e.g. `×255`)  
- Optional hook filtering via config.json  
- Linux only (Windows build planned)  

---

## Configuration (Optional)

By default, AcceleratorCSS detours **all plugin methods**.

You can exclude specific methods from detouring using:

```
addons/AcceleratorCSS/config.json
```

### Example

```json
{
  "disabled_hooks": [
    "PluginCrasher.PluginCrasher::CmdCrash"
  ]
}
```

### Format

```
Namespace.Class::Method
```

### Wildcards

You can use `*`:

```json
{
  "disabled_hooks": [
    "KvLib::*::*",
    "SkyboxChanger.Plugin::*"
  ]
}
```

### Behavior

- Disabled methods:
  - are **not detoured**
  - run completely untouched
- Other methods:
  - remain fully traced
- Native crash handler:
  - always active (cannot be disabled)

---

## Example Dump

```
============= DUMP START ==============
---
============= ENVIRONMENT =============
Timestamp: 2025-10-11 13:14:53 UTC
Process ID: 47
Map: de_mirage
CounterStrikeSharp Version: 1.0.340+Branch.main
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

---

## Internal Flow

```
┌──────────────────────────┐
│CounterStrikeSharp Plugins│
│ (Admin, Fun etc.)        │
└─────────────┬────────────┘
              │ Harmony detours
              ▼
┌──────────────────────────┐
│ AcceleratorCSS_CSS.dll   │
│  - Tracks method calls   │
│  - Buffers binary data   │
│  - Dumps on crash signal │
└─────────────┬────────────┘
              │ Native bridge (P/Invoke)
              ▼
┌──────────────────────────┐
│ AcceleratorCSS.so        │
│  - Handles SIGSEGV etc.  │
│  - Invokes managed dump  │
│  - Writes Breakpad dump  │
└──────────────────────────┘
```

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
    ├── logs/
    └── config.json (optional)
└── counterstrikesharp/
    ├── plugins/AcceleratorCSS_CSS/
    │           ├── AcceleratorCSS_CSS.dll
    │           └── 0Harmony.dll
    └── shared/0Harmony/0Harmony.dll
```

Start the server – everything initializes automatically.

Manual managed dump:

```
dumpmanaged
```

---

## License

GPLv3  
https://www.gnu.org/licenses/gpl-3.0.en.html

---

## Author

**Michal "Slynx" Přikryl**  
https://slynxdev.cz
