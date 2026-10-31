# Verification & Runtime Diagnostics

[← Back to README](../README.md)

---

## 1. Verify Installed Checksums

After deploying the patches, verify that the active files in your bottle match the expected checksums:

```bash
# Check d3d12.dll
shasum -a 256 "$BOTTLE/drive_c/windows/system32/d3d12.dll"
# Expected current V3: 833b964203e1e157a162852863656e0e4934ef10a3659d3047ebd0422b529f66

# Check kernelbase.dll
shasum -a 256 "$BOTTLE/drive_c/windows/system32/kernelbase.dll"
# Expected: 313a9d07019422fe0f56a579667d005d756728e13b66329aaa15321d92b0f182
```

---

## Drift-zone V3 activation

The bounded file `$BOTTLE/drive_c/highball/logs/drift-event-gate-<pid>.txt` must show `FH4_DRIFT_OBSERVER v3` and a successful installation result. A waiting message alone is not activation: the worker waits up to 300 seconds for exact runtime signatures. Drift completion/abort entries must say `forwarding`; older `skipped` records describe an unsuccessful intermediate version that left the zone active.

Check both installed shim copies, then test recorded points, zone exit and subsequent zone entry. The owner reported the final V3 build working; Steam/Xbox achievement unlocks were not independently established. See [validation and portable tests](../working-patches/08-drift-zone/verification/README.md) and [the investigation](../working-patches/08-drift-zone/FINDINGS.md). The observer forwards every original callback and diagnostic capture leaves exception CONTEXT unchanged.

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
