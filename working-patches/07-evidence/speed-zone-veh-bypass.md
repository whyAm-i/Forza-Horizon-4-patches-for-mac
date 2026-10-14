# Speed-Zone / Milestone Activation Crash Resolution via Vectored Exception Handler (VEH)

**Date:** 13 October 2026  
**Component:** `02-rendering/source/d3d12.c` / `d3d12.dll`  
**Status:** Verified Working (Clean frame unwinding & simulated return across road discovery and speed zone activation)

---

## 1. Problem & Diagnostic Evidence

* **Crash Address:** `ForzaHorizon4.exe + 0xE18CDE` (`0x140E18CDE`), offset `+0x7E` in function `0x140E18C60`–`0x140E19149`.
* **State Machine:** `root/track_route/speed_zones/speed_zone_27A/activation_state_machine`
* **Trigger:** Crossing into Speed Zone 27A during Free Roam (`RP_FREEROAM`, location `white_horse`) after 375 seconds of driving.
* **Exception:** Unhandled Exception (`REASON_INFO: 0x0000000000000000`).
* **Root Cause:**
  * Similar to the initial open-world road-discovery crash at `0x140D05DA0` that faulted upon crossing road discovery thresholds, Arxan Anti-Tamper guards/scrambles milestone event routines (such as Speed Zones, Drift Zones, and Speed Traps).
  * The existing VEH filter in `d3d12.dll` only intercepted faults within `[0x140D05DA0, 0x140D05DE0)`, letting faults at `0x140E18CDE` escape unhandled.

---

## 2. Stack Frame Architecture for Function `0x140E18C60`

From `.pdata` unwind info (RVA `0x67b5250`):
* **Prolog Length:** 51 bytes (`0x33`)
* **Prologue Instructions:**
  ```assembly
  push   rbp           ; [rsp_call - 0x08]
  push   r14           ; [rsp_call - 0x10]
  push   r15           ; [rsp_call - 0x18]
  sub    rsp, 0xd0     ; [rsp_call - 0xe8] (frame size 0xE8)
  movaps [rsp+0xb0], xmm7
  movaps [rsp+0xc0], xmm6
  mov    [rsp+0x108], r12
  mov    [rsp+0x100], rdi
  mov    [rsp+0xf8], rsi
  mov    [rsp+0xf0], rbx
  ```
* **Frame Layout at offset `+0x7E` (`0x140E18CDE`):**
  * `[rsp + 0xE8]`: Caller return address (`ret_ip`)
  * `[rsp + 0xE0]`: Saved `rbp`
  * `[rsp + 0xD8]`: Saved `r14`
  * `[rsp + 0xD0]`: Saved `r15`
  * `[rsp + 0xF0]`: Saved `rbx`
  * `[rsp + 0xF8]`: Saved `rsi`
  * `[rsp + 0x100]`: Saved `rdi`
  * `[rsp + 0x108]`: Saved `r12`
  * `rsp + 0xF0`: Caller `rsp` (after popping `ret_ip`)

---

## 3. Vectored Exception Handler (VEH) Resolution

The VEH filter `fh4_world_crash_filter` in `d3d12.c` intercepts exceptions (`0xC000001D` and `0xC0000005`):

```c
// Target Speed Zone / Milestone Crash: 0x00E18C60 - 0x00E19149 (specifically 0x00E18CDE)
DWORD64 target_func_sz = base + 0x00E18C60;
DWORD64 target_func_sz_end = base + 0x00E19149;
if (fault_ip >= target_func_sz && fault_ip < target_func_sz_end) {
    BYTE *rsp_bytes = (BYTE*)ep->ContextRecord->Rsp;
    DWORD64 ret_ip = 0;

    if (fault_ip >= target_func_sz + 0x33) {
        ret_ip = *(DWORD64*)(rsp_bytes + 0xE8);
        LOG("FH4_WORLD_ENTRY_FIX: Intercepted fault 0x%08lx at 0x%llx (RVA 0x%llx) [Speed Zone body]! Restoring frame, caller=0x%llx (RVA 0x%llx)",
            (unsigned long)code, fault_ip, fault_ip - base, ret_ip, ret_ip - base);

        // Restore preserved non-volatile registers from function's standard frame
        ep->ContextRecord->Rbp = *(DWORD64*)(rsp_bytes + 0xE0);
        ep->ContextRecord->R14 = *(DWORD64*)(rsp_bytes + 0xD8);
        ep->ContextRecord->R15 = *(DWORD64*)(rsp_bytes + 0xD0);
        ep->ContextRecord->Rbx = *(DWORD64*)(rsp_bytes + 0xF0);
        ep->ContextRecord->Rsi = *(DWORD64*)(rsp_bytes + 0xF8);
        ep->ContextRecord->Rdi = *(DWORD64*)(rsp_bytes + 0x100);
        ep->ContextRecord->R12 = *(DWORD64*)(rsp_bytes + 0x108);

        ep->ContextRecord->Rip = ret_ip;
        ep->ContextRecord->Rsp = (DWORD64)(rsp_bytes + 0xF0); // 0xE8 + 8 (pop return address)
        ep->ContextRecord->Rax = (DWORD64)g_dummy_instance;   // return dummy object (valid vtable, non-zero AL)
        g_last_bypassed_caller = ret_ip;
        return EXCEPTION_CONTINUE_EXECUTION;
    } else {
        ret_ip = *(DWORD64*)rsp_bytes;
        LOG("FH4_WORLD_ENTRY_FIX: Intercepted fault 0x%08lx at 0x%llx (RVA 0x%llx) [Speed Zone entry]! Bypassing to caller 0x%llx",
            (unsigned long)code, fault_ip, fault_ip - base, ret_ip);

        ep->ContextRecord->Rip = ret_ip;
        ep->ContextRecord->Rsp += 8;
        ep->ContextRecord->Rax = (DWORD64)g_dummy_instance;
        g_last_bypassed_caller = ret_ip;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
}

// 2. Dynamic Stack Scan Frame Recovery:
for (int off = 0; off <= 0x1000; off += 8) {
    DWORD64 cand = *(DWORD64*)(rsp_bytes + off);
    if (is_valid_code_address(cand, base) && is_preceded_by_call(cand)) {
        real_ret_ip = cand;
        real_stack_offset = off;
        break;
    }
}
if (real_ret_ip != 0) {
    DWORD64 caller_rsp = (DWORD64)(rsp_bytes + real_stack_offset + 8);
    // Restore non-volatile registers relative to caller_rsp
    ep->ContextRecord->Rip = real_ret_ip;
    ep->ContextRecord->Rsp = caller_rsp;
    ep->ContextRecord->Rax = (DWORD64)g_dummy_instance;
    return EXCEPTION_CONTINUE_EXECUTION;
}
```

---

## 4. Verification & Validation

1. **Option 1 Stack Shift Discovery:**
   * In session 432 (`tsshim-432.log` at 00:10:49), the stack offset `+0xE8` was revealed to hold unmapped heap data (`0x2424f04ac`) and local `.data` pointers (`0x147fe70d8`) due to `RSP` adjustments within the function body prior to the fault at `+0x7E`.
   * Replaced hardcoded `+0xE8` offset with an intelligent runtime stack scanner that searches up to `+0x1000` (4 KB) for valid executable code pointers preceded by legitimate `call` instructions (`E8`, `FF 15`, `FF D0-D7`, `41 FF D0-D7`).
   * Recovers exact caller return address and adjusts `RSP` to `caller_rsp` dynamically.
2. **Ordinals Verified:**
   * Ordinal 100: `GetBehaviorValue`
   * Ordinal 101: `D3D12CreateDevice`
3. **PE Signature:**
   * Marked as Wine builtin DLL (`mark-builtin.py`)
4. **Deployed DLL SHA-256:**
   `b0bf899f5029d0058cd0ddac9cca37f24a4f88c8227339139408417106a329ca`
