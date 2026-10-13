# Open-World Road Discovery Crash Resolution via Vectored Exception Handler (VEH)

**Date:** 13 October 2026  
**Component:** `02-rendering/source/d3d12.c` / `d3d12.dll`  
**Status:** Verified Working (Intercepted and recovered across multiple in-game road discoveries in Free Roam)

---

## 1. Problem & Diagnostic Evidence

* **Crash Address:** `ForzaHorizon4.exe + 0xD05DA0` (`0x140D05DA0`)
* **Caller:** `ForzaHorizon4.exe + 0xD04D85` (`0x140D04D85`), return address `0x140D04D8A`
* **Exception Code:** `0xC000001D` (`STATUS_ILLEGAL_INSTRUCTION`)
* **Context:** In Free Roam (`RP_FREEROAM`), ~0.033s - 0.039s after world spawn, the player car crosses a road threshold, triggering the "Road Discovered" announcement state machine (`wait_for_announcement`).
* **Root Cause:**
  * During initial process startup, an Arxan Anti-Tamper worker thread executes 17 stores overwriting the entry of `0x140D05DA0` with invalid instruction opcodes.
  * Modifying `ForzaHorizon4.exe` on disk corrupts Arxan's encrypted `.text` section (triggering an immediate termination during launch).
  * Direct in-memory code patching of `.text` pages trips Arxan's runtime integrity CRC guards.
  * The in-game Benchmark mode never traverses road discovery boundaries, explaining why the benchmark completed without crashing while free-roam failed immediately.

---

## 2. Solution: Direct3D 12 Vectored Exception Handler (VEH)

Wine's Windows exception dispatcher delivers hardware CPU faults to Vectored Exception Handlers registered via `AddVectoredExceptionHandler` before any structured exception handlers or crash dialogs are invoked.

Because `d3d12.dll` is an already-trusted compatibility shim loaded directly into the game address space, registering a first-priority VEH allows intercepting the CPU `0xC000001D` fault dynamically:

```c
static PVOID g_veh_world_entry_fix = NULL;
static LONG WINAPI fh4_world_crash_filter(PEXCEPTION_POINTERS ep)
{
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        if (ep->ExceptionRecord->ExceptionCode == 0xC000001D /* STATUS_ILLEGAL_INSTRUCTION */) {
            DWORD64 fault_ip = (DWORD64)ep->ExceptionRecord->ExceptionAddress;
            DWORD64 base = (DWORD64)GetModuleHandleA(NULL);
            DWORD64 target_func = base + 0x00D05DA0;
            
            // Check if fault occurred at or within the function entry region [0xD05DA0, 0xD05DE0)
            if (fault_ip >= target_func && fault_ip < target_func + 0x40) {
                DWORD64 *rsp_ptr = (DWORD64*)ep->ContextRecord->Rsp;
                DWORD64 ret_ip = *rsp_ptr;
                
                LOG("FH4_WORLD_ENTRY_FIX: Intercepted illegal instruction at 0x%llx (RVA 0x%llx)! Bypassing to caller 0x%llx",
                    fault_ip, fault_ip - base, ret_ip);
                
                // Advance RIP to caller return address and pop return address from stack:
                ep->ContextRecord->Rip = ret_ip;
                ep->ContextRecord->Rsp += 8;
                ep->ContextRecord->Rax = 0; // return status 0 (success / no error)
                
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
```

### Safety and Non-Invasiveness
1. **Zero on-disk modification:** `ForzaHorizon4.exe` remains 100% byte-identical to the original executable.
2. **Zero page modification:** No memory pages are patched, hooked, or made writable, completely avoiding Arxan's page CRC checks.
3. **Environment-variable gating:** Controlled by `HB_TSSHIM_FIX_WORLD_ENTRY` (defaults to enabled `1` if unset). Can be disabled by setting `HB_TSSHIM_FIX_WORLD_ENTRY=0`.
4. **Clean unregistration:** Automatically deregistered on `DLL_PROCESS_DETACH`.

---

## 3. Runtime Verification Evidence

During live gameplay in CrossOver on Apple Silicon (M4), the player successfully spawned into the open world and drove freely. The highball log (`tsshim-2248.log`) confirmed the VEH intercepted and bypassed the crash across 9 consecutive road discovery events:

* `00:27:45.763` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
* `00:29:53.236` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
* `00:29:58.978` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
* `00:37:52.899` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
* `00:38:29.330` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
* `00:39:17.352` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
* `00:44:18.645` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
* `00:44:22.667` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
* `00:44:43.088` — Intercepted at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a

In all occurrences, the game seamlessly continued driving without stutter or crash.

---

## 4. Archive Files & Hashes

* **Patched Source:** `02-rendering/source/d3d12.c` (`SHA-256: 769648db4fb5ef9b8e1f8ef08fd18fbdbeb2e47f3a242f55e7fa31985688ad90`)
* **Patched Binaries:**
  * `02-rendering/bottle/d3d12.dll` (`SHA-256: facc38fb8affbf579153bcc54de3b063fb99fd9be1513988f9e2eda1ee8ad1e6`), used for both bottle and engine targets
* **Pre-VEH Rollback Copies:**
  * `02-rendering/previous-v5/d3d12.dll` (`SHA-256: 6d6753139742c5f39961ec64021ede30308cdd991630797a355d3f5db342a816`)
  * `06-history/shim-revisions/d3d12.c.bak_before_veh_bypass_20261013` (`SHA-256: e551d77d81dfe8debb5dbaa2f519cfd49ff99d3f7f9b128687cc68fff3797c57`)
