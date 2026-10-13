# Anatomy of the Patches

[← Back to README](../README.md)

---

## Table of Contents
- [Launch Deadlock & Black Screen Fix (Code Cave)](#1-launch-deadlock--black-screen-fix-arxan-drm-code-cave)
- [Exploded / Stretched Geometry Fix (Packed Vertex Input Offsets)](#2-exploded--stretched-geometry-fix-packed-vertex-input-offsets)
- [Open-World Free Roam Crash Fix (Vectored Exception Handler Bypass)](#3-open-world-free-roam-crash-fix-vectored-exception-handler-bypass)
- [Direct3D 12 Proxy Shim Architecture & Ordinal Preservation](#4-direct3d-12-proxy-shim-architecture--ordinal-preservation)
- [Bottle Configuration & Hardware Profiler Bypass](#5-bottle-configuration--hardware-profiler-bypass)

---

## 1. Launch Deadlock & Black Screen Fix (Code Cave)

### The Problem
During early boot, the exe queries the Windows API `GetPackagesByPackageFamily` inside `kernelbase.dll` to verify the game's Microsoft Store UWP packages. In Wine, this API is a stub that returns `APPMODEL_ERROR_NO_PACKAGE` (`15700`). Upon receiving this error, exe refuses to initialize the game and enters an infinite fallback loop, deadlocking the main thread at 100% CPU.

Because exe executes this check before loading userland hook DLLs (such as `dinput8.dll`), external shims cannot intercept it in time.

### The Solution
A **Native Assembly Code Cave** is injected directly into unused padding inside `kernelbase.dll` (which is already loaded by Wine during PE initialization). The cave intercepts calls to `GetPackagesByPackageFamily` and writes back the exact UWP package string expected by the game:
```
Microsoft.VCLibs.140.00_14.0.30704.0_x64__8wekyb3d8bbwe
```

```assembly
.intel_syntax noprefix
.globl codecave
codecave:
    push rsi
    push rdi
    test rdx, rdx
    jz skip_count
    mov dword ptr [rdx], 1
skip_count:
    test r9, r9
    jz return_zero
    cmp dword ptr [r9], 57
    jl insufficient
    test r8, r8
    jz insufficient
    mov rax, qword ptr [rsp+0x38]
    test rax, rax
    jz insufficient

    mov qword ptr [r8], rax
    lea rsi, [rip + string_data]
    mov rdi, rax
    mov rcx, 57
    rep movsw
    mov dword ptr [r9], 57
    xor eax, eax
    pop rdi
    pop rsi
    ret

insufficient:
    mov dword ptr [r9], 57
    mov eax, 122
    pop rdi
    pop rsi
    ret

return_zero:
    xor eax, eax
    pop rdi
    pop rsi
    ret

string_data:
    .short 0x004D, 0x0069, 0x0063, 0x0072, 0x006F, 0x0073, 0x006F, 0x0066, 0x0074, 0x002E
    .short 0x0056, 0x0043, 0x004C, 0x0069, 0x0062, 0x0073, 0x002E, 0x0031, 0x0034, 0x0030
    .short 0x002E, 0x0030, 0x0030, 0x005F, 0x0031, 0x0034, 0x002E, 0x0030, 0x002E, 0x0033
    .short 0x0030, 0x0037, 0x0030, 0x0034, 0x002E, 0x0030, 0x005F, 0x0078, 0x0036, 0x0034
    .short 0x005F, 0x005F, 0x0038, 0x0077, 0x0065, 0x006B, 0x0079, 0x0062, 0x0033, 0x0064
    .short 0x0038, 0x0062, 0x0062, 0x0077, 0x0065, 0
```

- **In CrossOver 26.3:** The `.text` section lacks sufficient padding, so the cave is injected into the `.data` padding at raw offset `0x8AB20`, the `.data` section characteristics are updated to `0xE0000040` (`Execute | Read | Write`), and a relative JMP (`E9 EB F0 05 00`) is placed at `GetPackagesByPackageFamily` (`0x2C230`).
- **In Highball (`x64-crossover26.3-r18`):** Injected into `.text` padding at `0x94530`, with JMP (`E9 EB 4B 06 00`) at `0x2F940`.

---

## 2. Exploded / Stretched Geometry Fix (Packed Vertex Input Offsets)

### The Problem
In game scenes featuring outdoor foliage, trees, grass, stones, and fences, vertices exploded into infinite spikes across the screen. An independent GPU probe revealed that when D3D12 input elements specify `AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT` (`0xFFFFFFFF`), D3DMetal's shader converter miscalculates stride offsets for packed attribute elements (particularly instanced attributes and `R16G16_SNORM` / `RGBA16_FLOAT` layouts), resulting in 124 mismatches out of 384 test cases.

### The Solution
Rather than disabling visual assets or reducing graphics settings, our custom D3D12 proxy shim (`working-patches/02-rendering/source/d3d12.c` via `input_offsets.h`) intercepts all pipeline creation and pipeline library load calls:
- `ID3D12Device::CreateGraphicsPipelineState`
- `ID3D12Device2::CreatePipelineState`
- `ID3D12PipelineLibrary::LoadGraphicsPipeline`
- `ID3D12PipelineLibrary1::LoadPipeline`

Before passing the pipeline descriptor to D3DMetal, the shim iterates through the input layout elements, calculates the exact byte size of each DXGI format, and replaces `D3D12_APPEND_ALIGNED_ELEMENT` with explicit per-slot byte offsets.

Gated by:
```ini
"HB_TSSHIM_EXPLICIT_INPUT_OFFSETS" = "1"
```
**Result:** 0 mismatches out of 384 GPU checks; polygon stretching is completely eliminated while maintaining 100% full geometry, crowds, foliage, and ultra settings.

---

## 3. Open-World Free Roam Crash Fix (Vectored Exception Handler Bypass)

### The Problem & Diagnostic Analysis
- **Crash Address:** `ForzaHorizon4.exe + 0xD05DA0` (`0x140D05DA0`)
- **Exception Code:** `0xC000001D` (`STATUS_ILLEGAL_INSTRUCTION`)
- **Caller:** `0x140D04D85` (Return address on stack: `0x140D04D8A`)
- **Call Stack:** `D05DA0 -> D04D8A -> CFF79D -> 40FD513 -> F521F1`
- **Timing:** In Free Roam (`RP_FREEROAM`), ~0.033s - 0.039s after world spawn, the vehicle crosses a road boundary triggering the road discovery state machine (`wait_for_announcement`).
- **Why It Happens:** During early startup, an Arxan Anti-Tamper worker thread writes 17 corrupted bytes over the prologue of `0x140D05DA0`. When road discovery triggers, this invalid code is executed.
- **The Constraint:**
  - Modifying `ForzaHorizon4.exe` on disk corrupts Arxan's encrypted PE sections, causing an immediate crash on launch.
  - Modifying memory pages dynamically (e.g., in-memory detour or page patch) trips Arxan's runtime integrity CRC checks.

### The Solution: Vectored Exception Handler (VEH)
Wine's Windows exception dispatcher passes hardware CPU faults to Vectored Exception Handlers registered via `AddVectoredExceptionHandler` before any structured exception handler or crash dialog is invoked.

Since our `d3d12.dll` shim is loaded inside the game address space, it registers a first-priority VEH handler on startup:

```c
static PVOID g_veh_world_entry_fix = NULL;

static LONG WINAPI fh4_world_crash_filter(PEXCEPTION_POINTERS ep)
{
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        if (ep->ExceptionRecord->ExceptionCode == 0xC000001D /* STATUS_ILLEGAL_INSTRUCTION */) {
            DWORD64 fault_ip = (DWORD64)ep->ExceptionRecord->ExceptionAddress;
            DWORD64 base = (DWORD64)GetModuleHandleA(NULL);
            DWORD64 target_func = base + 0x00D05DA0;
            
            // Check if fault occurred at or within function entry region [0xD05DA0, 0xD05DE0)
            if (fault_ip >= target_func && fault_ip < target_func + 0x40) {
                DWORD64 *rsp_ptr = (DWORD64*)ep->ContextRecord->Rsp;
                DWORD64 ret_ip = *rsp_ptr;
                
                LOG("FH4_WORLD_ENTRY_FIX: Intercepted illegal instruction at 0x%llx (RVA 0x%llx)! Bypassing to caller 0x%llx",
                    fault_ip, fault_ip - base, ret_ip);
                
                // Advance RIP to caller return address and pop stack:
                ep->ContextRecord->Rip = ret_ip;
                ep->ContextRecord->Rsp += 8;
                ep->ContextRecord->Rax = 0; // return status 0 (success)
                
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
```

### Why This Is Clean
1. **0 on-disk modifications:** `ForzaHorizon4.exe` remains 100% byte-identical to original.
2. **0 memory modifications:** No memory pages are patched or made writable; Arxan's page CRCs are never tripped.
3. **Verified in gameplay:** Tested across 9+ consecutive road discoveries in live Free Roam sessions without a single crash or stutter.

Controlled via:
```ini
"HB_TSSHIM_FIX_WORLD_ENTRY" = "1"
```

---

## 4. Direct3D 12 Proxy Shim Architecture & Ordinal Preservation

The proxy architecture forwards all Direct3D 12 calls to Apple's native D3DMetal runtime while intercepting input layouts and exceptions:

```
[ForzaHorizon4.exe]
       │
       ▼ (Loads d3d12.dll via ordinal imports 100 & 101)
[Custom d3d12.dll Proxy Shim]
       │  ├─ Vectored Exception Handler (VEH road-discovery bypass)
       │  ├─ Explicit Packed Vertex Input Layout Resolution
       │  ├─ Timestamp Emulation (CPU QPC query timestamps)
       │  ├─ Depth-to-Color Resolve Workaround
       │  └─ Shader Model Fallback
       ▼
[apd12.dll (Renamed Apple D3DMetal Provider)]
       │
       ▼ (Wine Unix Library)
[apd12.so -> libd3dshared.dylib -> D3DMetal.framework]
       │
       ▼
[Apple Metal API / Apple Silicon GPU]
```

### Export Ordinal Critical Requirement
Forza Horizon 4 links against `d3d12.dll` by ordinal:
- **Ordinal 100:** `GetBehaviorValue`
- **Ordinal 101:** `D3D12CreateDevice`

If these ordinals are not defined via `d3d12.def`, the game crashes with a missing ordinal error before the splash screen appears. Additionally, `mark-builtin.py` writes the Wine builtin PE header marker so Wine loads the DLL seamlessly.

---

## 5. Bottle Configuration & Hardware Profiler Bypass

- **Disable Hardware Profiler:** In the captured setup, the profiler DLL was disabled by renaming it to `TargetHardwareProfiler.dll.bak`. That backup is local-only and is not included in Git; this repository does not provide a profiler replacement.
- **User Settings:** Do not force MSAA or deformable snow off in configuration files; doing so destabilizes the graphics pipeline.
