# Forza Horizon 4 Arxan DRM Bypass on macOS (Wine/CrossOver)

## The Problem
When running Forza Horizon 4 on macOS through Wine (Highball or CrossOver), the game gets stuck on a black screen while the CPU usage spikes to 100%. This is caused by Arxan Anti-Tamper's UWP (Universal Windows Platform) validation loop.

Arxan natively calls the Windows API `GetPackagesByPackageFamily` inside `kernelbase.dll` to verify the game's Microsoft Store packages. In Wine, this function is merely a stub that returns `APPMODEL_ERROR_NO_PACKAGE` (15700). Upon receiving this error, Arxan refuses to initialize the game and instead enters an infinite fallback loop, deadlocking the main thread at 100% CPU.

## The Solution
To bypass the DRM, we must force `GetPackagesByPackageFamily` to return exactly the UWP package string that the game expects:
`Microsoft.VCLibs.140.00_14.0.30704.0_x64__8wekyb3d8bbwe`

Because Arxan verifies the in-memory execution environment, using external shim DLLs (like `dinput8.dll` or `d3d12.dll`) is unreliable because the game deadlocks *before* it initializes them. The most foolproof solution is a **Native Assembly Code Cave** injected directly into the empty padding space of `kernelbase.dll` itself.

---

## 1. The Payload Source Code (GAS x64 Assembly)

The following assembly logic replicates the exact behavior of the UWP API.
*Note: The string must be exactly 56 characters (114 bytes with UTF-16 null terminator).*

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

Compiled to 208 bytes of raw machine code, it results in the following hex string:
`56574885d27406c702010000004d85c97447418339397c324d85c0742d488b4424384885c07423498900488d352d0000004889c748c7c13900000066f3a541c7013900000031c05f5ec341c70139000000b87a0000005f5ec331c05f5ec34d006900630072006f0073006f00660074002e00560043004c006900620073002e003100340030002e00300030005f00310034002e0030002e00330030003700300034002e0030005f007800360034005f005f003800770065006b007900620033006400380062006200770065000000`

---

## 2. Applying the Bypass to Highball

In Highball (`x64-crossover26.3-r18`), the `.text` executable section of `kernelbase.dll` has a generous amount of padding (zero-bytes) at the end, making it extremely easy to inject the Code Cave without altering PE Header Characteristics.

*   **JMP Site:** `0x2F940` (Offset of `GetPackagesByPackageFamily`)
*   **Cave Site:** `0x94530` (Empty padding in `.text` section)
*   **JMP Instruction:** `E9 EB 4B 06 00` (Jumps `0x64BEB` bytes forward)

### Python Patcher Script (Highball)
```python
import shutil

file_paths = [
    "${HOME}/Library/Application Support/Highball/engines/x64-crossover26.3-r18/engine/lib/wine/x86_64-windows/kernelbase.dll",
    "${HOME}/Library/Application Support/Highball/bottles/forza/drive_c/windows/system32/kernelbase.dll"
]

cave_data = bytes.fromhex("56574885d27406c702010000004d85c97447418339397c324d85c0742d488b4424384885c07423498900488d352d0000004889c748c7c13900000066f3a541c7013900000031c05f5ec341c70139000000b87a0000005f5ec331c05f5ec34d006900630072006f0073006f00660074002e00560043004c006900620073002e003100340030002e00300030005f00310034002e0030002e00330030003700300034002e0030005f007800360034005f005f003800770065006b007900620033006400380062006200770065000000")

for file_path in file_paths:
    shutil.copy2(file_path, file_path + ".bak")
    with open(file_path, "r+b") as f:
        # Inject Code Cave
        f.seek(0x94530)
        f.write(cave_data)
        # Inject JMP
        f.seek(0x2F940)
        f.write(bytes.fromhex("E9EB4B0600"))
    print(f"Patched {file_path}")
```

---

## 3. Applying the Bypass to CrossOver

CrossOver's `kernelbase.dll` is compiled differently. The `.text` section has **zero bytes** of padding. Therefore, we must inject the Code Cave into the padding of the `.data` section, and mathematically alter the PE Section Headers to flag the `.data` section as `Executable`. 

*   **JMP Site:** `0x2C230` (Offset of `GetPackagesByPackageFamily`)
*   **Cave Site:** `0x8AB20` (Empty padding in `.data` section)
*   **JMP Instruction:** `E9 EB F0 05 00` (Jumps `0x5F0EB` bytes forward)

### Python Patcher Script (CrossOver)
```python
import struct
import shutil

file_paths = [
    "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows/kernelbase.dll",
    "${HOME}/Library/Application Support/CrossOver/Bottles/New Bottle/drive_c/windows/system32/kernelbase.dll"
]

cave_data = bytes.fromhex("56574885d27406c702010000004d85c97447418339397c324d85c0742d488b4424384885c07423498900488d352d0000004889c748c7c13900000066f3a541c7013900000031c05f5ec341c70139000000b87a0000005f5ec331c05f5ec34d006900630072006f0073006f00660074002e00560043004c006900620073002e003100340030002e00300030005f00310034002e0030002e00330030003700300034002e0030005f007800360034005f005f003800770065006b007900620033006400380062006200770065000000")

for file_path in file_paths:
    shutil.copy2(file_path, file_path + ".bak")
    
    with open(file_path, "r+b") as f:
        data = bytearray(f.read())
    
    # 1. Change .data section characteristics to e0000040 (Execute | Read | Write)
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    sec_table = e_lfanew + 24 + opt_size
    
    for i in range(num_sections):
        sec = sec_table + i * 40
        name = data[sec:sec+8].rstrip(b"\x00")
        if name == b".data":
            # Overwrite Characteristics at offset 36
            struct.pack_into("<I", data, sec + 36, 0xe0000040)
            break
            
    # 2. Inject Code Cave into .data padding (Raw Offset 0x8ab20)
    cave_offset = 0x8ab20
    data[cave_offset:cave_offset+len(cave_data)] = cave_data
    
    # 3. Inject relative JMP at GetPackagesByPackageFamily (Raw Offset 0x2c230)
    jmp_offset = 0x2c230
    jmp_patch = bytes.fromhex("E9EBF00500")
    data[jmp_offset:jmp_offset+5] = jmp_patch
    
    with open(file_path, "wb") as f:
        f.write(data)
        
    print(f"Patched {file_path}")
```
