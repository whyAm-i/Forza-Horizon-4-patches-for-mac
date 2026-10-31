# Drift-zone exit fix

This is the current D3D12 shim, including the earlier geometry, launch-provider forwarding, road-discovery and speed-zone compatibility code. It is already integrated into `../02-rendering`; do not apply the older experimental drift versions alongside it.

| Item | Value |
| --- | --- |
| Installable DLL | `../02-rendering/bottle/d3d12.dll` |
| SHA-256 | `833b964203e1e157a162852863656e0e4934ef10a3659d3047ebd0422b529f66` |
| Game tested | FH4 `1.478.564.0`, x64 |
| Game EXE SHA-256 | `b838a79a65c6d1e12f46deaba3584e4c88ebccdfdc4214b4535ece4ba3c0abbe` |
| CrossOver tested | `26.3`, build `26.3.0.39832`, Apple Silicon |
| Graphics environment | macOS 27, GPTK 4.0 beta 3, D3DMetal / Metal 4 |
| Test bottle name | `New Bottle`; substitute your own bottle path |

Read [INSTALL.md](INSTALL.md) for installation and optional rebuilding, [BACKUP_AND_ROLLBACK.md](BACKUP_AND_ROLLBACK.md) for backups/restoration, and [FINDINGS.md](FINDINGS.md) for the investigation and failed approaches. [validation.json](validation.json) distinguishes the local tests from the user's gameplay confirmation.

## What the fix does

After D3D12 device creation, a worker waits for the exact supported runtime code signatures. It checks every 250 ms for up to 300 seconds. Unknown/transitional code is left untouched. The game architecture, image size, timestamp, method bytes, call sites and original vtable entries must match.

Once ready, it changes **one byte in process memory** at game RVA `0x895250`: `0x5C` becomes `0xC3` (`RET`). This bypasses the observed failing routine before it corrupts the stack. It saves the original byte, flushes the instruction cache, restores page protection, and rolls back if installation cannot finish. **The game EXE file on disk is never modified.**

An observer wraps `MethodObjectInfo`'s vtable slot at RVA `0x5CD0BE8`. It records bounded diagnostics for drift completion/abort events and **always forwards every callback exactly once** with its original arguments. In particular, the game's `DriftZoneCompleted`, `DriftZoneAborted`, `OnEventFired`, and `ReleaseActivityHandle` handlers run normally. The observer does not submit or fabricate scores or achievements.

The previous custom drift exception-unwind handlers are not compiled into this build. Diagnostic capture preserves the exception context; the original repo's road/speed-zone VEH block remains unchanged.

## Files and provenance

- `../02-rendering/source/d3d12.c`: integrated shim source.
- `../02-rendering/source/drift_event_gate.h`: V3 observer, guarded entry patch, readiness retry and restoration. The filename is retained from the investigation; V3 does not gate/drop callbacks.
- `../02-rendering/source/drift_capture.h`: capture-only diagnostics.
- `../02-rendering/bottle/d3d12.dll`: exact tested, user-confirmed binary. The existing `source/d3d12.dll` duplicate is kept byte-identical for the repo's established layout.
- `drift-zone-v3.patch`: reviewable source diff against pre-drift commit `a5b6f5df77f657965722d899a73899a90b8484e0`. Current source is already integrated; do not apply the diff again.
- `verification/`: portable synthetic-fixture callback/ABI test, assembly register probe, and loader smoke-test source. No EXE, full game code dump, minidump, save file or runtime log is bundled.

The source/binary were copied from the installed V3 build, not rebuilt while packaging. Earlier attempts and their DLL backups remain local investigation history; they are not advertised as working patches.

## Scope

The retained entry bypass does not implement the original routine's body. The working claim is the observed setup and user's gameplay confirmation, not a general guarantee for other executables, runtimes, or all event/achievement behavior. Keep the exact signature checks; do not reuse these offsets on another FH4 build.

This archive update does not reinstall anything into the already-working bottle, change the provider, edit settings, or alter saves.
