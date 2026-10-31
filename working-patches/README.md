# Forza Horizon 4 — working patch archive

Created 12 October 2026 from this Mac's installed files, local sources and recorded verification. The active patches target the existing **CrossOver 26.3 / Steam bottle / Apple Silicon** setup. CrossOver itself and its stock Apple GPTK runtime are prerequisites; unused application/runtime copies and the separate Highball snapshot were removed by the provenance audit.

Updated 31 October 2026 with the user-confirmed **drift-zone V3** shim. `02-rendering` contains the integrated current source and exact tested DLL; [08-drift-zone](08-drift-zone/README.md) documents the shim-only upgrade, backup/rollback and findings. The successful drift test used a matched GPTK 4.0 beta 3 runtime; preserve that working provider when upgrading rather than restoring the older archived provider.

**“Working” means the specific behavior was verified.** The open-world road-discovery illegal-instruction crash (`0x140D05DA0`) is resolved via the Vectored Exception Handler (VEH) bypass patch in `02-rendering`. The VEH intercepts `0xC000001D` faults during Free Roam road discovery and safely returns to caller `0x140D04D8A` without modifying game code on disk or in memory. See `07-evidence/world-entry-veh-bypass.md` and `07-evidence/world-entry-crash-FINDINGS.md`.

## What is included

| Folder | What it does | Verification / status |
|---|---|---|
| `01-launch-package` | Preserves the installed `kernelbase.dll` VCLibs package-family response used to get past the launch/DRM compatibility loop | Existing launch workaround. One retained file serves both CrossOver locations. Not a world-entry crash fix. |
| `02-rendering` | D3D12 proxy, timestamp compatibility, shader-model fallback, depth-resolve workaround, correct export ordinals, explicit packed vertex-input offsets, and VEH bypass for the open-world road-discovery crash (`0x140D05DA0`) | Offset fix passed 384/384 checks and confirmed stopping stretching. VEH bypass verified intercepting `0xC000001D` and preventing open-world crash across live driving sessions. |
| `03-settings` | Exact current bottle/game settings, full-scene skip baseline, selected Wine registry reference and game-setting backups | Snapshot of current configuration, not a universal preset. Binding capture is disabled; the geometry fix remains enabled. |
| `04-shader-lab` | Live shader inventory, small-group object identification and reversible draw suppression | Diagnostic utility with saved test results; not required for ordinary play. |
| `05-game-support` | Current OnlineFix/Steam API and auxiliary compatibility files | Preserved dependencies. Not established as a crash fix; no multiplayer-stability claim. |
| `06-history` | Earlier assembly, patch scripts and replaced shim revisions | Historical/reference only. Many changes failed, were superseded or were reverted. Do not run as a batch. |
| `07-evidence` | Selected investigation findings, verification notes, notices and licenses | Supporting records; runtime logs and other local diagnostic captures are excluded. |
| `08-drift-zone` | V3 installation, backup/rollback, findings, source diff and portable callback/ABI test | User reported “it's working flawlessly” after the final gameplay test. All normal callbacks are forwarded; the exact failing entry is bypassed in memory. |

In the captured game folder, the live EXE was byte-for-byte identical to its `ForzaHorizon4.exe.bak` (SHA-256 `b838a79a65c6d1e12f46deaba3584e4c88ebccdfdc4214b4535ece4ba3c0abbe`). **There is no on-disk EXE patch to apply.** The drift entry change is applied only inside the running process, after its exact runtime signature matches. `kernel32.dll` also matches its saved backup; the old GetPackagePath export-edit scripts are not part of this installed workaround. The latest crash log still reported the missing GetPackagePath implementation during startup.

## Start here

Open Terminal and enter:

```sh
cd working-patches
python3 tools/verify_archive.py
```

Then read `APPLY.md`. It gives the correct targets and order, hash-checked copy commands, configuration values, and rollback steps. Every copying helper defaults to a read-only dry run. `--apply --game-closed` explicitly enables a write and creates a unique backup first.

The Git archive contains the launch DLL, current proxy and an older baseline provider; you do not need to rebuild or run old hardcoded patch scripts. The latest drift deployment preserves its matched GPTK 4 provider as described in the drift guide. It relies on the installed CrossOver 26.3 Apple GPTK runtime and must not be applied to a different release. A source-based rebuild of Wine needs the matching source revision and is outside this binary-restoration recipe.

## Current rendering choices

- `HB_D3D12_REAL=apd12.dll`: proxy forwards to the distinctly named real D3DMetal provider.
- `HB_TSSHIM_EXPLICIT_INPUT_OFFSETS=1`: keep the visually confirmed geometry fix.
- `HB_TSSHIM_FIX_WORLD_ENTRY=1`: enables the VEH road-discovery crash bypass at `0x140D05DA0` (default 1 / enabled).
- The drift fix defaults to enabled with the world-entry fix. `HB_TSSHIM_FIX_DRIFT_ZONE=0` disables its runtime changes. Its bounded log uses `FH4_DRIFT_OBSERVER v3`; drift callbacks must say `forwarding`, not `skipped`.
- `HB_TSSHIM_CAPTURE_BINDINGS=0`: current normal-play diagnostic state.
- `HB_TSSHIM_CAPTURE_INDIRECT_ARGS=0`, `HB_TSSHIM_SKIP_ALL_INSTANCED=0`: no argument readback or global instance suppression.
- `skip.txt` contains only a full-scene baseline comment: no shaders suppressed.
- Geometry/tessellation stripping, FP16 clamping, tiled-tier clamping and speculative MPL/D3DM flags were reverted. Do not re-enable them from older notes.

## Integrity and ownership

`manifest.json` and `SHA256SUMS` cover the files tracked in this Git archive. `tools/verify_archive.py` checks that set; locally ignored captures and generated output are outside the package. `07-evidence/snapshot-state.json` is a historical pre-audit capture record and intentionally names artifacts that are not in this Git archive. Stock CrossOver/GPTK runtime dependencies are not vendored; repair or reinstall the exact CrossOver build if they are damaged.
