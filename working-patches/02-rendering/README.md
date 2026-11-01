# Rendering compatibility and geometry fix

The current archived DLL includes the **user-confirmed memory repair V1.1** and the **drift-zone V3 fix**. SHA-256: `f3038fdc146ff740fb2070d39b16945623fae3958aab963f995ae13f79d25b51`. Use the [memory upgrade installer](../09-memory-leak/INSTALL.md) for both shim copies, preserving the matched GPTK 4 provider. The [drift guide](../08-drift-zone/README.md) describes the retained drift behavior; earlier road/speed-zone code is unchanged.

The installed `binding-capture-v5` proxy contains the visually confirmed explicit-input-offset workaround. It resolves D3D12_APPEND_ALIGNED_ELEMENT into explicit byte offsets per input slot before pipeline creation/loading. This corrects packed attribute interpretation rather than suppressing trees, foliage or crowds. Existing explicit offsets and app-owned descriptors are preserved. All four creation/load paths are covered.

Evidence: 124/384 mismatches in an independent APPEND-mode baseline, zero in explicit mode and zero with the deployed shim; user confirmed stretching was fixed. The checked-in verification directory contains the probe and hook source; raw run output from the original investigation is not included in Git. See `../07-evidence/diagnostic-deployment-README.md` for the recorded result summary.

The same DLL retains timestamp emulation using CPU QPC stamps (not true GPU timing), shader-model fallback for unrecognized queries, and a depth-to-colour resolve workaround that skips unsupported calls by default. These were already part of the compatibility shim. Its correct Wine builtin marker and export ordinals are essential for loading. Ordinal 101 must point to D3D12CreateDevice; 100 to GetBehaviorValue. Missing the .def file once introduced a splash-screen failure and was corrected.

## Files and dependencies

- `bottle/d3d12.dll`: current proxy, used for both bottle and engine targets.
- `bottle/apd12.dll`: renamed real D3DMetal provider, with internal name adjusted to avoid resolving back to the proxy.
- `source/aligned_heap_fix.h`: bounded allocation-provenance repair and exact runtime guards.
- `source`: current C source, binding_capture.h, descriptor_capture.h, upload_capture.h, input_offsets.h, d3d12.def and Wine-marker tool.
- `source/drift_event_gate.h` and `source/drift_capture.h`: V3 observer/readiness/entry bypass and capture-only diagnostics. Despite the historical filename, V3 forwards every callback; it does not block completion or activity release.
- `verification`: source/probes/recorded evidence. They are not automatically executed by opening the archive.
- Earlier shim binaries are excluded from Git. Roll back using the timestamped backup created during deployment.

The working setup still requires CrossOver 26.3's installed `apple_gptk` runtime. Its `apd12.so` relative symlink into the installed `external/libd3dshared.dylib`, and the installed D3DMetal framework behind that library, matter at runtime. Those stock/proprietary CrossOver application files are prerequisites and are no longer duplicated in this repository. Build instructions and target paths are in APPLY.md.

The drift gameplay test used GPTK 4.0 beta 3 with renamed provider hash `32c942ed368a45e58d35f4da3de3776d1f74e785731de9cf6a5e705a2925488a`, which differs from the older provider archived here. Follow the drift-only instructions to preserve an already-working provider/runtime pair.

The source also incorporates the Vectored Exception Handler (VEH) bypass for the open-world road-discovery illegal-instruction crash at `0x140D05DA0`. Gated by `HB_TSSHIM_FIX_WORLD_ENTRY` (default enabled: `1`), it intercepts `0xC000001D` faults during Free Roam road discovery and safely returns to caller `0x140D04D8A`, resolving the crash without on-disk or memory modifications. Other speculative flags remain off and binding capture disabled for the normal-play profile. See `../07-evidence/world-entry-veh-bypass.md` for verification evidence.

That zero-code-byte description applies to the road VEH handler. The separate V3 drift fix deliberately changes one strictly checked game instruction byte in process memory and one observer vtable pointer; it never edits the game EXE file. See [the revised pitfall guidance](../../docs/PITFALLS.md).
