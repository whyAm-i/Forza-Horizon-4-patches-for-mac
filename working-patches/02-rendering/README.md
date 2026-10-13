# Rendering compatibility and geometry fix

The installed `binding-capture-v5` proxy contains the visually confirmed explicit-input-offset workaround. It resolves D3D12_APPEND_ALIGNED_ELEMENT into explicit byte offsets per input slot before pipeline creation/loading. This corrects packed attribute interpretation rather than suppressing trees, foliage or crowds. Existing explicit offsets and app-owned descriptors are preserved. All four creation/load paths are covered.

Evidence: 124/384 mismatches in an independent APPEND-mode baseline, zero in explicit mode and zero with the deployed shim; user confirmed stretching was fixed. The checked-in verification directory contains the probe and hook source; raw run output from the original investigation is not included in Git. See `../07-evidence/diagnostic-deployment-README.md` for the recorded result summary.

The same DLL retains timestamp emulation using CPU QPC stamps (not true GPU timing), shader-model fallback for unrecognized queries, and a depth-to-colour resolve workaround that skips unsupported calls by default. These were already part of the compatibility shim. Its correct Wine builtin marker and export ordinals are essential for loading. Ordinal 101 must point to D3D12CreateDevice; 100 to GetBehaviorValue. Missing the .def file once introduced a splash-screen failure and was corrected.

## Files and dependencies

- `bottle/d3d12.dll`: current proxy, used for both bottle and engine targets.
- `bottle/apd12.dll`: renamed real D3DMetal provider, with internal name adjusted to avoid resolving back to the proxy.
- `source`: current C source, binding_capture.h, descriptor_capture.h, upload_capture.h, input_offsets.h, d3d12.def and Wine-marker tool.
- `verification`: source/probes/recorded evidence. They are not automatically executed by opening the archive.
- Earlier shim binaries are excluded from Git. Roll back using the timestamped backup created during deployment.

The working setup still requires CrossOver 26.3's installed `apple_gptk` runtime. Its `apd12.so` relative symlink into the installed `external/libd3dshared.dylib`, and the installed D3DMetal framework behind that library, matter at runtime. Those stock/proprietary CrossOver application files are prerequisites and are no longer duplicated in this repository. Build instructions and target paths are in APPLY.md.

The source also incorporates the Vectored Exception Handler (VEH) bypass for the open-world road-discovery illegal-instruction crash at `0x140D05DA0`. Gated by `HB_TSSHIM_FIX_WORLD_ENTRY` (default enabled: `1`), it intercepts `0xC000001D` faults during Free Roam road discovery and safely returns to caller `0x140D04D8A`, resolving the crash without on-disk or memory modifications. Other speculative flags remain off and binding capture disabled for the normal-play profile. See `../07-evidence/world-entry-veh-bypass.md` for verification evidence.
