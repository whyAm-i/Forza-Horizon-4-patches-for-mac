# Verification & Runtime Diagnostics

[← Back to README](../README.md)

---

## 1. Verify Installed Checksums

After deploying the patches, verify that the active files in your bottle match the expected checksums:

```bash
# Check d3d12.dll
shasum -a 256 "$BOTTLE/drive_c/windows/system32/d3d12.dll"
# Expected: facc38fb8affbf579153bcc54de3b063fb99fd9be1513988f9e2eda1ee8ad1e6

# Check kernelbase.dll
shasum -a 256 "$BOTTLE/drive_c/windows/system32/kernelbase.dll"
# Expected: 313a9d07019422fe0f56a579667d005d756728e13b66329aaa15321d92b0f182
```

---

## 2. Verify Startup & VEH Interception Logs

Launch Forza Horizon 4 through CrossOver. Once you reach the festival and load into Free Roam (open world), drive across any new road to trigger road discovery.

Then inspect the latest log generated in `$BOTTLE/drive_c/highball/logs/tsshim-<pid>.log`:

### A. Startup Banner
Confirms the proxy shim initialized and registered the Vectored Exception Handler:
```text
binding-capture-v5; bindings=0; input_offsets=1
HB_TSSHIM_FIX_WORLD_ENTRY=1: Registered VEH for 0x140D05DA0 (handle=...)
```

### B. Road Discovery Interception
Confirms that the illegal instruction caused by Arxan's prologue corruption was caught and bypassed safely without crashing:
```text
FH4_WORLD_ENTRY_FIX: Intercepted illegal instruction at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a
```
*Each time you discover a new road in Free Roam, this entry confirms the illegal instruction was caught and execution resumed smoothly.*

---

## 3. Verifying Archive Integrity

To ensure that no files in `working-patches/` have been corrupted or modified unexpectedly:

```bash
cd "working-patches"
python3 tools/verify_archive.py
```

Expected output:
```text
PASS: ... files/symlinks verified; no live files changed.
```
