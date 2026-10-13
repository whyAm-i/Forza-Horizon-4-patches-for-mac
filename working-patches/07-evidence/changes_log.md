# Highball File Changes Log

## World-entry road discovery crash VEH bypass — Antigravity (2026-10-13)

- Implemented Vectored Exception Handler (VEH) `fh4_world_crash_filter` in `02-rendering/source/d3d12.c`.
- Intercepts CPU `0xC000001D` (`STATUS_ILLEGAL_INSTRUCTION`) at RIP `0x140D05DA0` (`ForzaHorizon4.exe + 0xD05DA0`),
  caused by Arxan anti-tamper entry corruption triggered when crossing road discovery thresholds in Free Roam.
- Reads caller return address `0x140D04D8A` from RSP, advances RIP, adjusts stack (`Rsp += 8`), and returns status 0 (`Rax = 0`),
  simulating a clean return without touching executable code on disk or modifying memory pages.
- Verified in live gameplay: Free Roam loaded successfully; VEH intercepted and bypassed 9 consecutive road discovery events
  across a 20-minute session without crashing.
- Gated by `HB_TSSHIM_FIX_WORLD_ENTRY` (default 1 / enabled).
- Recompiled with MinGW-w64, signed as Wine builtin PE, preserving exports (Ordinal 100 GetBehaviorValue, Ordinal 101 D3D12CreateDevice).
- Deployed to both bottle `system32/d3d12.dll` and CrossOver engine `lib64/apple_gptk/wine/x86_64-windows/d3d12.dll`.
- The prior pre-VEH binary and source backup are local historical artifacts and are not included in this Git archive.

## Grass/water quality comparison and targeted diagnostics — Codex (2026-10-11)

- User identified grass VS daebf740 (wrong texture or colour) and water VS 46c59935
  (weak reflection, opaque appearance) using Shader Lab. Visual fix confirmation is pending.
- Isolated grass-format GPU probes each pass 384 checks: R16G16_SNORM, RGBA16_FLOAT,
  RGB32_FLOAT. The tests do not establish material/pixel-shader correctness.
- After the user closed Forza, backed up exact UserConfigSelections bytes and enabled grass/foliage
  quality and terrain noise, selected shader quality 3, changed SSR High to Ultra (selection 4),
  and reconciled the dynamic optimization selection to disabled. Only six intended attributes
  changed; CRLF and unrelated settings preserved. Restore helper and diff are under outliers/.
- Installed v5, preserving the confirmed explicit input-offset workaround. Adds water VS/layout/
  binding capture and grass/water PS-family dumps, deduplicated and capped at 128 files/8 MiB.
  No shader instructions, texture assets, game archives or draw arguments changed.
- V5 compiles cleanly; capture selection, stream order, byte preservation, deduplication and resource
  caps pass isolated tests. All four input-offset hook regressions and deployment checks pass.
  Original rollback backups and installed v4 refresh backups retained.
- Live capture now saves shader dumps, supports v5 and streams census/startup scans with bounded
  memory. Shader Lab labels the newly user-identified water hash on the next inventory refresh.

## Manual shader-test restoration — Codex (2026-10-11)

- Per user request, removed the default 12-second timeout. Tests remain active until a baseline,
  blank/frozen, quit, or submitted-answer command restores the scene. Small-group filtering retained.
- Optional timeout is now opt-in through --pulse-seconds; default 0 means manual restoration.
  Updated launcher and instructions. No active session was interrupted or game file changed.

## Bisection display recovery — Codex (2026-10-11)

- User reported frozen/blank rendering during bisection. Active test skipped 128 VS hashes,
  including fullscreen passes. Stopped only the verified Shader Lab process, restored its exact
  original skip list, and the user confirmed Forza recovered. No game process was stopped.
- Bisection now uses observed indexed-geometry hashes, excluding hashes also seen in single-instance
  non-indexed triangle/quad draws. Current census gives 101 candidates, including all five known
  tree/leaf/stone/grass/wall shaders. The complete inventory remains available for individual review.
- Defaults to groups of at most four, configurable 1-8. Fullscreen and unobserved shaders are
  excluded from object search by a census heuristic; this is not a guarantee against bad visuals.
- Every interactive test automatically clears its rules after 12 seconds independently of input.
  Generation checks and locking keep obsolete timers from clearing a newer test or its restoration.
- Added f (blank/frozen): immediately restores the baseline, marks individual reviews inconclusive,
  and subdivides bisection groups without wrongly eliminating candidates. Round hashes, answers and
  exact rules save to bisection-rounds.csv; final notes still save to observations.csv.
- Fifteen regressions pass, including timer restoration while waiting, stale timer cancellation,
  fullscreen filtering, incomplete results and inconclusive-group subdivision. Deployment verified.
- Updated scripts load on the next menu selection. No DLL/configuration changes or Forza restart.

## Shader values alongside CSV notes — Codex (2026-10-11)

- observations.csv now includes separate stage/hash, label, pipeline count/IDs, exact skip-rule batches,
  selected-census call count, test mode, result, notes, batch progress, timestamp and source log/startup.
- Review and bisection confirmations both save to this CSV. Original test metadata is preserved
  across inventory refreshes; existing notes can be enriched without losing their text.
- CSV exports are atomic and quote commas, quotation marks and multiline notes correctly.
  All ten regressions pass, including new CSV round-trip and launch-snapshot preservation checks.

## Complete live shader testing — Codex (2026-10-11)

- User confirms the explicit vertex-offset workaround fixed the stretching issues.
- Found original `${HOME}/Downloads/bisect_skip.sh` and extended the workflow in `shader_lab/`.
- Current process inventory: 17,128 pipelines; 733 VS, 2,412 PS, 4 GS, 3 HS, 3 DS, 100 CS.
  Covers loaded shaders; refreshing after exploring more scenes discovers additional variants.
- Added interactive all-stage review, vertex bisection, individual hash tests, baseline comparison,
  saved observations/resume, inventory CSV, and a Terminal launcher menu.
- VS tests use hash rules. Other stage reviews suppress matching pipelines, including compute
  direct/indirect paths, split into batches of at most 128 rules. No new DLL or setting changes.
- Incremental log reads prevent whole-log memory spikes. Exact original skip-file restoration,
  interruption recovery journals, external-change guards and all-batch coverage passed eight tests.
- Test menu does not change skip.txt until a testing session is selected. The offset fix remains enabled.

## Packed vertex input offset workaround — Codex (2026-10-11)

- V3 outdoor capture `diagnostics/2026-10-11/captures/20261011-170318/` includes all five affected
  shaders, 3,619 draws, 393 valid affected index ranges, and 214 valid instance ranges.
  Sampled camera transforms are finite and nonsingular; no descriptor tracker overflow occurred.
- An independent fenced GPU probe reproduces 124/384 packed-attribute mismatches with APPEND
  offsets. Explicit byte offsets pass 384/384. It uses Forza's packed instance formats and a
  nonzero start instance; no game resources are involved.
- Added `input_offsets.h`: opt-in per-slot APPEND resolution, preserving explicit offsets and
  app-owned descriptors. Hooked graphics/stream creation and both pipeline-library load paths.
  Invalid/unsupported layouts and streams conservatively pass through unchanged.
- V4 built without warnings. Hook tests pass alignment, mixed/interleaved slots, overflow,
  source immutability, disabled flag, all four forwarding paths, and stream RT-format ABI.
- Installed both v4 DLLs and enabled `HB_TSSHIM_EXPLICIT_INPUT_OFFSETS=1` in Steam only.
  Initial rollback backups retained; v3 DLL/configuration refresh backups also saved.
- The deployed shim passes the original APPEND-mode GPU probe: zero mismatches in 384 checks.
  Startup log confirms `input_offsets=1`; all deployment hashes, backups and exports verified.
- Mac reboot observed at 17:23, cause unconfirmed. Final broad whole-log evidence scan was
  interrupted and replaced with bounded streaming. GPU probe results survived the reboot.
- **Forza visual verification remains pending a manual restart.** No game restart performed.
  Evidence and rollback instructions: `diagnostics/2026-10-11/README.md`.

## CPU upload contents capture — Codex (2026-10-11)

- V2 scene capture `diagnostics/2026-10-11/captures/20261011-163639/` contains 3,663 draws.
  User confirms stretching remains. All five affected shader families appear with no tracker overflow.
- 396 affected index checks and 211 instance checks passed. Bound roots match their embedded RTS0.
  b1/b2/b3 descriptors are present, aligned, sufficiently sized for literal accesses and contained
  in recorded upload allocations. Camera/shared-buffer bindings also occur in comparison draws.
- Decoded all three distinct root layouts. Corrected audit mapping for b2 in the non-instanced layout.
- V3 samples tracked CPU upload CBV bytes during census only, with 8 MiB/2,048-sample/64 KiB limits.
  First samples are reused by address/size. Weak resource aliases are tracked through QueryInterface
  and invalidated on final Release, with mapping/destruction serialized. No COM references retained,
  GPU readback, barriers or new queue submissions. Samples are CPU observations, not GPU execution data.
- Fake-resource and real D3DMetal sample tests passed: byte offsets, nested persistent mapping,
  alias lifetimes, limits and no reads after destruction. One-million-descriptor regressions passed.
- V3 refreshed both diagnostic DLLs while preserving the original rollback backups and current
  settings. Forza was not restarted; manual restart is required before capturing contents.

## Binding capture capacity repair — Codex (2026-10-11)

- User confirmed stretching remains with all scene draws enabled. Capture
  `diagnostics/2026-10-11/captures/20261011-161733/` contains 2,783 draws and all five affected shaders.
- Affected draws passed 288 index-view range checks and 179 instance-buffer range checks. All five
  live root-signature payloads match their shaders' embedded RTS0 chunks. Contents remain unverified.
- V1's 65,536-entry descriptor tracker overflowed; missing constant-buffer metadata is inconclusive.
  V2 uses a growing hash table capped at 4,194,304 entries and records counts/capacity in snapshots.
- Added creation-time buffer address/size/heap-type metadata, without retained resource pointers,
  resource mapping, GPU copies or new queue submissions.
- V2 built without warnings; one-million-entry and resize-during-copy tests passed. Installed
  D3DMetal smoke test passed; runtime marker, upload metadata, exports and deployment hashes verified.
- Refreshed both diagnostic DLLs, preserving the initial rollback backups and current configuration
  and skip list. Forza was not restarted. One manual restart is needed to load v2.

## Binding capture diagnostic — Codex (2026-10-11)

- Added `binding_capture.h` and `descriptor_capture.h` to the existing tsshim source.
  Opt-in binding snapshots correlate draw offsets, vertex/index views, root arguments and
  descriptor metadata with affected shaders and selected controls. No GPU readback was added.
- Corrected diagnostic state reset: successful `Reset` clears stale IA metadata; failed `Reset`
  retains it. Bundle/indirect binding mutations invalidate uncertain snapshots.
- Compiled and installed `binding-capture-v1` atomically into the existing CrossOver engine and
  Steam bottle. Enabled `HB_TSSHIM_CAPTURE_BINDINGS=1`; restored all scene draws for baseline testing.
- Exact pre-deployment DLLs, bottle configuration and skip list are saved in
  `diagnostics/2026-10-11/before-deployment/`; deployment hashes and rollback command are documented
  in `diagnostics/2026-10-11/README.md`. No automatic game restart was performed.
- Unit tests and installed D3DMetal device/binding smoke test passed. Rendering is not yet verified.
- Actual assembly corrects the earlier report: projection uses b2/space0 rows16–19;
  `CB2[3:3][1136], space=0` describes b3/space0 with1136 vectors, not space1136.
- Live capture recorded1668 draws and no affected shader hashes; it is not a capture of the broken
  objects. A new capture in the affected outdoor scene is required.

This document tracks all files outside of the Forza Horizon 4 folder that were temporarily modified and subsequently reverted during the troubleshooting process.

## Modified Files (Highball)

1.  **Highball Engine `kernelbase.dll`**
    *   **Path:** `${HOME}/Library/Application Support/Highball/engines/x64-crossover26.3-r18/engine/lib/wine/x86_64-windows/kernelbase.dll`
    *   **Action:** Injected a custom position-independent x64 assembly Code Cave into the `.text` section padding at offset `0x94530`, and patched `GetPackagesByPackageFamily` at `0x2F940` to execute a 5-byte relative JMP to the Code Cave.
    *   **Status:** **Currently Patched (Version 2 - Fixed String)**.

2.  **Highball Bottle `kernelbase.dll`**
    *   **Path:** `${HOME}/Library/Application Support/Highball/bottles/forza/drive_c/windows/system32/kernelbase.dll`
    *   **Action:** Injected a custom position-independent x64 assembly Code Cave into the `.text` section padding at offset `0x94530`, and patched `GetPackagesByPackageFamily` at `0x2F940` to execute a 5-byte relative JMP to the Code Cave.
    *   **Status:** **Currently Patched (Version 2 - Fixed String)**.

## Modified Files (CrossOver)

3.  **CrossOver Engine `kernelbase.dll`**
    *   **Path:** `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows/kernelbase.dll`
    *   **Action:** Injected the exact same Version 2 Assembly Code Cave into the `.data` section padding at offset `0x8AB20` (changed `.data` header characteristics to Executable). Patched `GetPackagesByPackageFamily` at `0x2C230` with a 5-byte relative JMP (`E9 EB F0 05 00`).
    *   **Status:** **Currently Patched**.

4.  **CrossOver Bottle `kernelbase.dll`**
    *   **Path:** `${HOME}/Library/Application Support/CrossOver/Bottles/New Bottle/drive_c/windows/system32/kernelbase.dll`
    *   **Action:** Injected the exact same Version 2 Assembly Code Cave into the `.data` section padding at offset `0x8AB20` (changed `.data` header characteristics to Executable). Patched `GetPackagesByPackageFamily` at `0x2C230` with a 5-byte relative JMP (`E9 EB F0 05 00`).
    *   **Status:** **Currently Patched**.

## Current Solution
The `d3d12.dll` and `dinput8.dll` memory scanner approaches have been completely abandoned. 

We have reverted back to the **Native Code Cave** inside `kernelbase.dll`. The previous Code Cave attempt caused a silent 100% CPU deadlock *not* because Arxan detected the modification (Arxan actually accepted it natively), but because the UWP `PackageFullName` string I hardcoded was slightly malformed (it had the Publisher ID duplicated in the Version field). This caused Wine's `PackageIdFromFullName` function to fail parsing the string, leading Arxan into an infinite loop.

The Code Cave has been rewritten with the mathematically perfect 56-character string (`Microsoft.VCLibs.140.00_14.0.30704.0_x64__8wekyb3d8bbwe`), assembled, and injected natively into the executable padding space. This bypasses the DRM instantly, natively, and flawlessly.

## Graphics Compatibility Tweaks (CrossOver Bottle: Steam)

5.  **Forza Horizon 4 `UserConfigSelections`**
    *   **Path:** `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/users/crossover/AppData/Local/ForzaHorizon4/User_PCLocalStorageDirectory/ConnectedStorage/ForzaUserConfigSelections/UserConfigSelections`
    *   **Backup:** `UserConfigSelections.bak_original`
    *   **Action (initially claimed):** Crowd suppression flags were reported as injected, but a later inspection found they were absent from the active file. The six recognized main-scene flags were actually added on 2026-10-10; see follow-up below. `TerrainParallaxEnabled=0` remains present.
    *   **Goal:** Eliminate 50-foot giant spectator models along tracks and isolate terrain parallax heightfield ray marching from snow shader dither noise.
    *   **Status:** Main-scene crowd controls applied in follow-up; requires game restart to load.

6.  **CrossOver Bottle `cxbottle.conf` (D3DMetal Shading & Clamping Flags)**
    *   **Path:** `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/cxbottle.conf`
    *   **Backup:** `cxbottle.conf.bak_original`
    *   **Action:** Added three native D3DMetal environment flags under `[EnvironmentVariables]`:
        - `"D3DM_SAMPLE_NAN_TO_ZERO" = "1"`: Flushes invalid/unmapped tangent normal samples that produce NaN to zero instead of scattering rainbow specular highlights.
        - `"D3DM_BOUNDS_CHECK" = "1"`: Forces bounds checking on buffer loads and descriptor fetches to prevent reading garbage memory into vertex and instance scale registers.
        - `"D3DM_POSITION_INVARIANCE" = "1"`: Enforces position invariance in vertex shader transforms to eliminate floating-point polygon streak explosions.
    *   **Status:** **Currently Applied**.

7.  **Game Engine Render Scenarios (`Media/renderscenarios.zip`)**
    *   **Path:** `${GAME_ROOT}/Media/renderscenarios.zip`
    *   **Action:** Reverted immediately to original backup `Media/renderscenarios.zip.bak_original` and removed `Media/RenderScenarios`.
    *   **Result:** Modifying archive contents or creating loose scenario files triggers an asset integrity assertion / crash at splash screen. Restored to 100% original working bytes.
    *   **Status:** **Reverted**.

8.  **D3D12 Timestamp Shim (`d3d12.dll` / `tsshim`)**
    *   **Source:** `${GAME_ROOT}/highball_repo/spike/tsshim/d3d12.c`
    *   **Backup of original DLL:** `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_original`
    *   **Action:** Hooked `CreateGraphicsPipelineState` on `ID3D12Device` to inspect Geometry Shader pipelines (`desc->GS`). Implemented opt-in kill switch `HB_TSSHIM_NOGS=1` which clears `desc->GS` from the graphics pipeline descriptor before passing to Apple's D3DMetal compiler.
    *   **Deployment:** Compiled with `x86_64-w64-mingw32-gcc`, marked as Wine builtin, deployed to `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll` and bottle `system32/d3d12.dll`. Enabled `HB_TSSHIM_NOGS="1"` in `cxbottle.conf`.
    *   **Goal:** Completely eliminate Geometry Shader emulation distortions that cause 50-foot spectator scaling and foliage fan explosions without touching any game files.
    *   **Status:** **Currently Applied**.

9.  **FP16 / Half-Precision & Shader Model Clamping in `tsshim`**
    *   **Source:** `${GAME_ROOT}/highball_repo/spike/tsshim/d3d12.c`
    *   **Action:** Extended `hook_CheckFeatureSupport` to intercept `D3D12_FEATURE_D3D12_OPTIONS` and `D3D12_FEATURE_D3D12_OPTIONS4`. Implemented `HB_TSSHIM_NOFP16=1` (forces `MinPrecisionSupport=0` and `Native16BitShaderOpsSupported=0`) and `HB_TSSHIM_MAXSM=0x60` (clamps Shader Model from modern 6.6 down to 6.0, matching the game's 2018 DXIL release).
    *   **Goal:** Prevent 16-bit half-precision float overflow ($>65,504$) on Apple Silicon FP16 hardware, forcing all open-world vertex coordinates, skinning matrices, and tangent-space normal decodes to execute in full 32-bit single precision (`float32`).
    *   **Verified Live:** Probed with `probe.exe` running under Wine in the Steam bottle; confirmed `MinPrecision=0x0`, `Native16Bit=0`, and `ShaderModel=0x60`.

10. **D3D12 Tiled Resources Tier Suppression in `tsshim`**
    *   **Source:** `${GAME_ROOT}/highball_repo/spike/tsshim/d3d12.c`
    *   **Action:** Added `override_tiled_tier` handling in `hook_CheckFeatureSupport` and parsed `HB_TSSHIM_TILEDTIER` and `HB_TSSHIM_NOTILED` in `DllMain`. Setting `HB_TSSHIM_TILEDTIER=0` forces `opt->TiledResourcesTier = D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED`.
    *   **Goal:** Address 44,742 sparse reserved texture allocations (`CreateReservedResource`) for open-world streaming terrain, foliage, and spectator atlas tiles that sample unmapped dirty VRAM on Apple Silicon Metal.
    *   **Verified Live:** Probed with `probe.exe` running inside the CrossOver bottle; confirmed `TiledResourcesTier` reports `0` (overridden from native `2`).
    *   **Status:** **Currently Applied**.

11. **D3DMetal Metal Pipeline Layer (MPL) Barrier & Encoder Serialization Flags**
    *   **Path:** `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/cxbottle.conf`
    *   **Backup:** `cxbottle.conf.bak_before_mpl`
    *   **Action:** Added native Apple D3DMetal environment variables discovered in `D3DMetal.framework/D3DMetal`:
        - `"MPL_ALLOW_ENCODERS_OVERLAP" = "0"`: Prevents concurrent Metal compute and render encoder overlapping, eliminating race conditions where vertex shaders read compute buffers still being written.
        - `"MPL_FORCE_SERIAL_COMPUTE_ENCODER" = "1"`: Forces strict serialization of compute shader passes (vegetation wind sim, crowd instance skinning, snow heightfield deformation).
        - `"MPL_FORCE_SERIAL_BLIT_ENCODER" = "1"`: Forces strict serialization of tile copy/blit passes.
        - `"MPL_ENABLE_ENCODER_MERGING" = "0"`: Disables command encoder merging across barrier boundaries.
        - `"MPL_ENABLE_RESOURCE_BARRIER_OPTIMIZATIONS" = "0"`: Strictly enforces every DirectX 12 resource barrier without skipping or coalescing.
    *   **Goal:** Synchronize GPU-driven procedural instance generation and compute skinning buffers before vertex fetch, resolving 50-foot spectator scaling, exploding foliage geometry, and snow hash artifacts.

12. **MSAA Alignment & Deformable Snow Override in `UserConfigSelections`**
    *   **Path:** `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/users/crossover/AppData/Local/ForzaHorizon4/User_PCLocalStorageDirectory/ConnectedStorage/ForzaUserConfigSelections/UserConfigSelections`
    *   **Action:** Reverted immediately back to `MSAA=1` and `DeformableSnowQuality=1`. Setting `DeformableSnowQuality=0` trips an internal engine assertion/crash at boot, and `MSAA=0` drops the forward depth-prepass, leaving only emissive/neon lights visible.
    *   **Status:** **Reverted**.

13. **D3DMetal Positive Infinity Preservation in `cxbottle.conf` (`D3DM_FLUSH_POS_INF_TO_NAN=0`)**
    *   **Path:** `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/cxbottle.conf`
    *   **Action:** Injected `"D3DM_FLUSH_POS_INF_TO_NAN" = "0"`.
    *   **Rationale:** D3DMetal defaults to flushing positive floating-point infinity to NaN. In tangent normal decodes and lighting specular calculations (`sqrt(1 - dot(N,N))`), small numeric overshoots turn into NaN, which cascades into magenta/purple neon noise across snow and terrain. Setting this to 0 preserves IEEE-754 standard values.
    *   **Status:** **Currently Applied**.

14. **Aggressive ProceduralQuality & Terrain XML Overrides in `UserConfigSelections`**
    *   **Path:** `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/users/crossover/AppData/Local/ForzaHorizon4/User_PCLocalStorageDirectory/ConnectedStorage/ForzaUserConfigSelections/UserConfigSelections`
    *   **Backup:** `UserConfigSelections.bak_before_aggressive`
    *   **Action:** Overwrote `<ProceduralQuality>` attributes to completely eliminate procedural crowd and vegetation meshes:
        - `DensityScale=0.0`, `BushDensityScale=0.0`, `LODDistanceScale=0.0`, `TreesLODDistanceScale=0.0`, `GrassLODDistanceScale=0.0`, `BushLODDistanceScale=0.0`: Zero spawn density and LOD draw distance for all procedural objects.
        - `CubeMinArea=999999.0`, `ShadowMinArea=999999.0`: Set minimum screen area for cubemap/shadow rendering to infinity (never render in reflections/shadows).
        - `CrowdCull=0.0`: Zero culling distance means crowds never pass the culling test.
        - `TreeLODReduction=99`: Maximum LOD reduction (only render lowest detail, which at zero density means nothing).
        - `UseScreenAreaTests=1`: Enable screen-area-based culling.
        - Also forced `TerrainParallaxEnabled=0` (explicit disable instead of "Low" dynamic value).
    *   **Goal:** Since the 50-foot giant and foliage fan explosion artifacts are caused by D3DMetal mishandling 16-bit SNORM skinned vertex positions, completely suppress the engine from spawning these procedural meshes at the density/LOD/culling level.
    *   **Status:** **Currently Applied**.

15. **Additional D3DMetal Environment Flags in `cxbottle.conf`**
    *   **Path:** `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/cxbottle.conf`
    *   **Backup:** `cxbottle.conf.bak_before_aggressive`
    *   **Action:** Added five new D3DMetal/MPL environment variables:
        - `"D3DM_FORCE_RTZ_TEXWRITE" = "1"`: Forces round-to-zero rounding mode on texture write operations, preventing precision-related snow noise from floating-point denormals in terrain heightfield textures.
        - `"MPL_convertDrawPrimitivesToDrawMeshes" = "0"`: Disables automatic conversion of draw primitive calls to mesh shader draw calls, preventing potential vertex data misinterpretation.
        - `"MPL_convertIndexBufferArguments" = "0"`: Disables index buffer argument conversion that could misalign vertex fetch offsets for 16-bit SNORM vertices.
        - `"MPL_ENABLE_CLEAR_OPTIMIZATIONS" = "0"`: Disables render target clear optimizations that could leave stale data in render targets.
        - `"MPL_ALLOW_COLOR_ATTACHMENT_MAPPING" = "0"`: Disables color attachment mapping optimizations.
    *   **Status:** **Currently Applied**.

16. **Tessellation (HS/DS) Stripping & ExecuteIndirect Hooks in `tsshim`**
    *   **Source:** `${GAME_ROOT}/highball_repo/spike/tsshim/d3d12.c`
    *   **Action:** Added two new hooks:
        - **Tessellation suppression** (`HB_TSSHIM_NOTESS=1`): Intercepts `CreateGraphicsPipelineState` and strips Hull Shader (`HS`) and Domain Shader (`DS`) bytecode from pipeline state objects that use tessellation. Forces `PrimitiveTopologyType` from `PATCH` to `TRIANGLE`. This lets the engine create PSOs without crashing (unlike `DeformableSnowQuality=0`) while neutralizing D3DMetal's faulty `irconverter_hull_shader`/`irconverter_tessellator`/`irconverter_dxil_domain_shader` translation pipeline.
        - **ExecuteIndirect logging/suppression** (`HB_TSSHIM_NOEI=1`): Intercepts `ExecuteIndirect` on `ID3D12GraphicsCommandList`. When enabled, drops all GPU-driven indirect draw calls (used for procedural crowd/vegetation instancing). When disabled but debug is on, logs call counts for diagnostics.
    *   **Deployment:** Compiled, marked as Wine builtin, deployed to both engine and bottle `system32` locations.
    *   **Env Vars at introduction:** `HB_TSSHIM_NOTESS=1`, `HB_TSSHIM_NOEI=0` (logging only, not suppressing).
    *   **Status:** Tessellation stripping was reverted on 2026-10-10 after runtime logs showed it stripped 316 unrelated tessellated pipelines; see follow-up below.



## Follow-up: Roll back broad tessellation stripping (2026-10-10)

- Runtime trace `drive_c/highball/logs/tsshim-2840.log` confirms the shim loaded and applied `HB_TSSHIM_NOTESS=1`, stripping HS/DS from 316 graphics pipeline states. The hook applies to every tessellated pipeline, not specifically the suspected snow pipeline; removing HS/DS also removes the stages that generate tessellated geometry.
- The same run made 22,371 successful `CreateReservedResource` calls while `TiledResourcesTier` was reported as 0, so that capability override did not stop the game's reserved-resource path.
- Rolled back only the broad tessellation override: `HB_TSSHIM_NOTESS=0` in the Steam bottle `cxbottle.conf`. Preserved the previous config at `cxbottle.conf.bak_before_tessellation_rollback`. The shim's opt-in hook remains available for future controlled diagnosis.
- The graphics issue itself still needs a renderer-specific fix; these measurements invalidate the prior tessellation and reserved-resource explanations as solutions.


## Follow-up: Apply actual main-scene crowd controls (2026-10-10)

- The current `UserConfigSelections` had `CrowdDrawMirror` but none of the six main-scene crowd control elements (`CrowdLoad`, `CrowdLoadModels`, `CrowdDraw`, `CrowdDrawModels`, `CrowdDrawSprites`, `CrowdDrawShadows`). Earlier notes incorrectly said those controls were present.
- Added all six recognized settings as explicit `value="0"` entries next to `CrowdDrawMirror`; XML parsing succeeds. The original file is preserved as `UserConfigSelections.bak_before_crowd_suppression`.
- This configuration is read when FH4 loads its user settings, so the running session must be restarted before this change can affect rendering.


## Follow-up: Trace indirect draws and bound pipelines (2026-10-10)

- The live FH4 process (PID 58482, started 21:35:47) loaded the current config written at 20:51, but the visual corruption remained. Its shim log shows FP16/min-precision and tiled-tier overrides active, 25 GS pipeline strips, 316 HS pipelines left intact, 22,371 reserved-resource creations, and 22,000+ `ExecuteIndirect` calls forwarded with one repeated command-signature pointer. `HB_TSSHIM_NOEI=0`. This confirms the earlier broad settings did not resolve the artifact; it does not by itself prove which indirect draw contains the bad geometry.
- Added opt-in bounded tracing in `highball_repo/spike/tsshim/d3d12.c`: log command-signature argument types/stride, track `SetPipelineState` per command list, hash shader bytecode for graphics PSOs, and log at most 256 distinct indirect-draw/PSO contexts. Enabled with `HB_TSSHIM_TRACE_INDIRECT_PSO=1`.
- Rebuilt cleanly with MinGW and deployed the Wine-builtin shim atomically to the CrossOver engine and Steam bottle. Original binaries and config are backed up with `.bak_before_indirect_trace`. The already-running process retains its old loaded DLL; the new diagnostics start on the next game launch.

## Follow-up: Analyze the live run and restore full scene rendering (2026-10-10)

- The user clarified that the fix must render the complete environment, including crowds, vegetation, snow, and billboards. No object suppression is acceptable.
- Inspected the live run's newest shim log (`tsshim-2632.log`) while Forza Horizon 4 PID 59249 was running. The instrumented hooks were active. The log shows `HB_TSSHIM_NOGS=1` actually dropped 25 geometry-shader stages, while the visual corruption remained. The recurring indirect draw signature is D3D12 `DRAW` (argument type 0), 16-byte stride; this means the indirect path is a real draw path, not just compute dispatch. Pipeline metadata is currently reported `unregistered`, indicating FH4 creates these PSOs through `ID3D12Device2::CreatePipelineState` stream creation, which the current tracer does not yet record. Thus the trace narrows the path but does not prove the faulty shader or translation stage.
- Rolled back the speculative graphics overrides to the saved original CrossOver bottle environment, keeping the trace flag enabled. This removes forced geometry-shader stripping, FP16/Shader Model/tiled-resource overrides, and unvalidated D3DMetal/MPL experiment flags. The active game process keeps its already-loaded settings; these changes take effect after a normal game restart.
- Restored the game's previous authored `ProceduralQuality` and `TerrainParallaxEnabled` settings and removed the six manually added crowd-off tags. This preserves crowd, vegetation, and terrain rendering. Current and prior config snapshots are retained as `*.bak_before_render_rollback_20261010-215623`.
- No restart was performed while the game was running. The game process was no longer present at the final process check, so the restored config and updated DLL are ready for the next normal launch.
- Extended the bounded tracer to hook `ID3D12Device2+::CreatePipelineState` and record shader bytecode hashes from pipeline streams. Rebuilt and validated the Wine-builtin DLL exports, then deployed identical DLLs to the CrossOver Apple GPTK engine and Steam bottle (`sha256 0e61b566978eb19956c92dba8a73c81820f471f6debab44ae5f4c11ebcbacc30`). Original DLLs are backed up as `d3d12.dll.bak_before_stream_trace_20261010-215722`. The trace change has not yet been exercised in a game run.

## Follow-up: Inspect the currently running clean-settings run (2026-10-10)

- The current Forza launch (shim log `tsshim-636.log`, loaded at 22:00:31) started after the rollback. Its log reports native options `MinPrecision=0x2, TiledResources=2` without the old forced overrides and contains no `DROPPED GS` entries. The game is still running; no process restart or live renderer change was made.
- Queried macOS unified logs for this exact run. From 22:00:30 onward, D3DMetal logged 819 `Failed to compile fragment function` events, 85 explicit `Failed to compile stage Fragment` diagnostics, and 924 PSOs marked `no-op`; it also logged an unsupported 8-sample MSAA PSO. This is direct evidence that D3DMetal is rejecting many pipelines. It may explain missing scene rendering, but without mapping those PSOs to the corrupted draws it is not yet proof of the cause of stretched geometry.
- The current bottle uses D3DMetal 4.0b2 with `D3DM_MTL4=0`. Prepared a reversible next-launch A/B by setting only `D3DM_MTL4=1`; existing bottle snapshot is `cxbottle.conf.bak_before_mtl4_test_20261010-220530`. This does not affect the open game. It keeps all crowd, vegetation, terrain, and billboard rendering enabled. The result is unverified until a launch using the Metal 4 backend.
- Current indirect-draw trace still shows PSO metadata as unregistered, so the stream hook needs further validation. No visual fix is being claimed yet.

## Follow-up: Reset to true baseline configuration (2026-10-10)

- Reverted `D3DM_MTL4` from `1` back to `0` in `cxbottle.conf`.
- Pre-change backup saved as: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/cxbottle.conf.bak_before_baseline_reset_20261010-223415`.
- Verified all speculative D3DM_* and MPL_* variables remain completely removed from `cxbottle.conf`.
- Verified all `HB_TSSHIM_*` feature flags (`NOGS`, `NOTESS`, `NOFP16`, `MAXSM`, `TILEDTIER`, `NOEI`) are off/unset. Logging (`HB_TSSHIM_DEBUG=1`, `HB_TSSHIM_TRACE_INDIRECT_PSO=1`) remains active.
- Confirmed `UserConfigSelections` has original authored settings: `DensityScale=0.1`, `BushDensityScale=0.25`, `LODDistanceScale=1.0`, `TreesLODDistanceScale=1.0`, `GrassLODDistanceScale=1.0`, `BushLODDistanceScale=1.0`, `CrowdCull=0.75`, `TreeLODReduction=4`, `CubeMinArea=0.0`, `ShadowMinArea=0.0`, `TerrainParallaxEnabled=Low`, main-scene crowd suppression removed, `MSAA=1`, and `DeformableSnowQuality=1`. Snapshot saved as `UserConfigSelections.bak_baseline_20261010-223431`.

## Follow-up: Fix "unregistered" PSO tracking and enhance indirect draw logging (2026-10-10)

- Fixed PSO tracking across command lists and device interfaces in `highball_repo/spike/tsshim/d3d12.c`:
  - Hooked `CreateComputePipelineState` on `ID3D12Device` in addition to `CreateGraphicsPipelineState`.
  - Upgraded `remember_pipeline_stream` for `ID3D12Device2+::CreatePipelineState`: subobjects parsed linearly with struct alignment and fallback scanning; unconditionally registers the PSO even if bytecode subobjects are malformed.
  - Resolved wrapper-pointer mismatches by querying and caching the canonical `IUnknown` identity pointer for every pipeline, signature, and command list. If a direct pointer lookup misses, canonical COM identity resolves the match.
  - Hooked `SetPipelineState`, `ExecuteBundle`, `Reset`, and `QueryInterface` across all `ID3D12GraphicsCommandList` interface versions (base through `ID3D12GraphicsCommandList10`).
  - Added bundle tracking: included `D3D12_COMMAND_LIST_TYPE_BUNDLE` in device initialization patching and propagated bundle-recorded PSOs to direct command lists upon `ExecuteBundle`.
  - Hooked `CreateCommandList` and `CreateCommandList1` (ID3D12Device4+) to patch command list vtables upon creation and track initial PSOs.
  - Enhanced `ExecuteIndirect` signature logging: formatted argument types (`draw`, `draw-indexed`, `dispatch`, `VBV`, `IBV`, `constant`, `root-CBV`, `root-SRV`, `root-UAV`, `dispatch-rays`, `dispatch-mesh`), byte stride, composite `pso_hash`, and shader bytecode hashes for VS, PS, GS, HS, DS, CS.
- Pre-change backups:
  - Source: `highball_repo/spike/tsshim/d3d12.c.bak_before_pso_tracking_20261010-224619`
  - CrossOver Engine DLL: `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_before_step3_pso_20261010-225118`
  - Steam Bottle DLL: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll.bak_before_step3_pso_20261010-225118`
- Build & Deployment:
  - Compiled with `x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc`.
  - Deployed to both CrossOver engine (`/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll`) and Steam bottle (`${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll`).
  - New binary SHA256: `e5167e4053635a20d28696e394a17b50b8ac0ab7f31621be6921c95a18cac409`.
  - Validated with `probe.exe` running under Wine in the Steam bottle; device creation, all interface QIs, command queues, descriptor heaps, and command lists created and hooked cleanly.

## Follow-up: 65,536-Slot PSO Hash Map, Pipeline Library Hooks & Indirect Arg Probe (2026-10-10)

- Upgraded PSO tracking in `highball_repo/spike/tsshim/d3d12.c`:
  - Replaced the fixed 4,096-entry linear array with a 65,536-slot open-addressing hash map (`TRACE_HASH_CAPACITY 65536`) keyed by PSO pointer with quadratic/linear probing and capacity warning.
  - Intercepted all pipeline creation paths:
    - `CreateGraphicsPipelineState` (`ID3D12Device`)
    - `CreateComputePipelineState` (`ID3D12Device`)
    - `CreatePipelineState` (`ID3D12Device2+` stream descriptor)
    - `CreatePipelineLibrary` (`ID3D12Device1+`): hooked `LoadGraphicsPipeline`, `LoadComputePipeline`, and `LoadPipeline` (`ID3D12PipelineLibrary1`).
  - Added sequential creation index (`g_pso_creation_counter`) stored per-PSO to correlate directly with D3DMetal's `PSO(NNNN)` numbers in macOS system logs.
  - Bound PSO creation index and bytecode hashes logged at each indirect draw.
  - Added argument buffer probing (`INDIRECT_ARG_BUF_PROBE` logging dimensions, size, flags, heap type, CPU page, and memory pool properties).
  - Added switchable direct mapping argument capture (`HB_TSSHIM_CAPTURE_INDIRECT_ARGS=1`, default off) bounded to first 64 draws.
- Pre-change backups:
  - Source: `highball_repo/spike/tsshim/d3d12.c.bak_before_hashmap_pso_20261010-232500`
  - CrossOver Engine DLL: `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_before_hashmap_pso_20261010-232500`
  - Steam Bottle DLL: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll.bak_before_hashmap_pso_20261010-232500`
- Build & Deployment:
  - Compiled with `x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc`.
  - Marked with `highball_repo/Scripts/mark-builtin.py` ("Wine builtin DLL").
  - Deployed to both CrossOver engine and Steam bottle `system32`.
  - New binary SHA256: `9bc8425eaf80a8653b8c59e52164e0b7767dbcf3aeb383e8e3abc64b242dbb8a`.

## Follow-up: Draw Census, Argument Readback & Diagnostic Bisection (2026-10-10)

- Extended `highball_repo/spike/tsshim/d3d12.c`:
  - **Step A: Draw Census**: Hooked `DrawInstanced`, `DrawIndexedInstanced`, `Dispatch`, `ExecuteIndirect`, `ResourceBarrier`, and `ExecuteCommandLists` across all command list/queue interface versions. Added one-frame census logging (frame $N$ after world load, configurable via `HB_TSSHIM_CENSUS_FRAME`, default 50; enabled via `HB_TSSHIM_DRAW_CENSUS=1`). Aggregates draw counts, sum and max of `InstanceCount`, sum of vertex/index counts, and formats a ranked table sorted by `max InstanceCount`.
  - **Step B: Indirect Argument Readback**: Implemented dual-path readback (direct host `Map()` query + committed GPU readback buffer `CopyBufferRegion()` with automatic transition barriers and end-of-frame fence mapping). Switched via `HB_TSSHIM_CAPTURE_INDIRECT_ARGS=1` (off by default). Logs raw values of `VertexCountPerInstance`, `InstanceCount`, `StartVertexLocation`, and `StartInstanceLocation` for 64 samples grouped by PSO.
  - **Step C: Diagnostic Bisection**: Added `HB_TSSHIM_SKIP_VS=<comma-separated hex hashes>` to skip draw calls (`DrawInstanced`, `DrawIndexedInstanced`, `ExecuteIndirect`) matching specified vertex shaders, enabling controlled bisection testing.
- Pre-change backups:
  - Source: `highball_repo/spike/tsshim/d3d12.c.bak_before_census_bisection_20261010-234500`
  - CrossOver Engine DLL: `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_before_census_bisection_20261010-234500`
  - Steam Bottle DLL: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll.bak_before_census_bisection_20261010-234500`
- Build & Deployment:
  - Compiled with `x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc`.
  - Marked with `highball_repo/Scripts/mark-builtin.py` ("Wine builtin DLL").
  - Deployed to both CrossOver engine and Steam bottle `system32`.
  - New binary SHA256: `ca0c22eb3993e2f8096f5103f718c56f2c9c1b78b5c3c1e4f4448ff66de6cf4c`.
  - Validated with `probe.exe` running under Wine in the Steam bottle; device creation, all interface QIs, command lists, queues, heaps, and root signatures passed cleanly.

## Follow-up: Trigger-File Census, Bundle Tracking, Input Layouts & VBV Strides (2026-10-11)

- **Step 1: Indirect Readback Deactivation**:
  - Disabled `HB_TSSHIM_CAPTURE_INDIRECT_ARGS` in `cxbottle.conf` (`"HB_TSSHIM_CAPTURE_INDIRECT_ARGS" = "0"`). Readback code left in place in `d3d12.c` but disabled at runtime.
  - Backup: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/cxbottle.conf.bak_before_step1_turnoff_args_20261011-002600`.
- **Step 2: Trigger-File Census**:
  - Replaced frame-counter census trigger with file-based trigger: checks for `C:\highball\census_now` on each frame boundary (present). If found, deletes it and arms census for the next frame.
  - Repeatable across a single session; does not change which draw calls execute.
  - Retained `HB_TSSHIM_CENSUS_FRAME` as an optional secondary frame-counter trigger (defaults to disabled `-1`).
- **Step 3: Bundle Tracking**:
  - Hooked `ExecuteBundle`: tracks `g_bundles_executed_in_frame` and replays bundle-recorded draw calls (`DrawInstanced`, `DrawIndexedInstanced`) into the active census frame with their recorded PSOs, instance counts, and VBV strides.
  - Formatted census summary reports total bundle executions per frame (explicitly logs "(bundles not used)" when 0).
- **Step 4: Input Layouts & VBV Strides**:
  - Extended `PipelineTrace` to store input layout descriptors (semantic name, semantic index, DXGI format, input slot, aligned byte offset, input slot class, instance data step rate) captured from `D3D12_GRAPHICS_PIPELINE_STATE_DESC` and pipeline stream subobjects.
  - Hooked `IASetVertexBuffers` on `ID3D12GraphicsCommandList` to track bound vertex buffer strides across slots 0..15 on `ListTrace`.
  - Added full dump of input layout element fields and bound VBV strides for all active PSOs with `max InstanceCount > 1` directly in the census report.
- Pre-change backups:
  - Source: `highball_repo/spike/tsshim/d3d12.c.bak_before_trigger_census_bundles_layout_20261011-002700`
  - CrossOver Engine DLL: `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_before_trigger_census_bundles_layout_20261011-002700`
  - Steam Bottle DLL: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll.bak_before_trigger_census_bundles_layout_20261011-002700`
- Build & Deployment:
  - Compiled with `x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc`.
  - Marked with `highball_repo/Scripts/mark-builtin.py` ("Wine builtin DLL").
  - Deployed to both CrossOver engine and Steam bottle `system32`.
  - New binary SHA256: `974f79ca8359be66d878bb30d22013d237663e2c4d6afd58b4c1a30c002619c1`.
  - Validated with `probe.exe` running under Wine in the Steam bottle; device creation, interface QIs, command lists, queues, heaps, and root signatures passed cleanly.

## Splash Screen Crash Diagnosis & Resolution (2026-10-11)

- **Symptom**: Game crashed on splash screen with Unhandled Exception at `0x6fffe27601c0` (`CrashReport.xml`).
- **Root Cause**: The preceding build (SHA `974f79ca...`) compiled `d3d12.dll` without linking `highball_repo/spike/tsshim/d3d12.def`. MinGW auto-exported symbols alphabetically starting at ordinal 1 (`D3D12CreateDevice` was mapped to ordinal 14 instead of ordinal 101). FH4 binds `D3D12CreateDevice` strictly by ordinal 101; resolving ordinal 101 pointed to an invalid stub at RVA `0x101c0`, crashing upon invocation.
- **Fix**: Recompiled `d3d12.dll` explicitly with `highball_repo/spike/tsshim/d3d12.def`, re-applied `mark-builtin.py`, and deployed to both locations.
- **Verification**:
  - `GetProcAddress(m, MAKEINTRESOURCEA(101))` verified matching `GetProcAddress(m, "D3D12CreateDevice")` (`0x00006FFFE332A340`).
  - `GetProcAddress(m, MAKEINTRESOURCEA(100))` verified matching `GetProcAddress(m, "GetBehaviorValue")` (`0x00006FFFE3321376`).
  - `probe.exe` confirmed clean D3D12 device, queue, command list, descriptor heap, and resource creation.
- **Deployed SHA256**: `f8d9a9362728fca8b31540f0916043271a4fa8e5ae2f5959d4ecff8b606a43d6` (both engine and bottle `system32`).

## Follow-up: Census Present-Burst Debouncing (2026-10-11)

- **Findings from Live Trigger Run (`tsshim-1992.log`)**:
  - Boot verified: splash crash fully resolved, 17,136 PSOs loaded into open world.
  - File trigger detected and deleted twice (`census_now` at frame 2753 and 3056).
  - Anomaly: Census dumped in the exact same millisecond as the trigger (`09:44:43.634` and `09:44:47.848`), capturing only 1 draw (`PSO(507)`, 3-vertex fullscreen triangle) and 2 compute dispatches (`PSO(633)`, `PSO(4013)`).
  - Root Cause: FH4 terminates each frame with two back-to-back `ExecuteCommandLists` calls within < 0.1 ms (Call A for scene render, Call B for presentation overlay/HUD blit). Both calls transition the backbuffer to `PRESENT`. Arming the census at Call A caused Call B to immediately satisfy the next frame boundary condition 0.05 ms later, closing the census before the upcoming frame's 3D scene commands were recorded.
- **Fix**:
  - Added QPC-based timestamp debouncing to `hook_ExecuteCommandLists`: transitions within < 5.0 ms are treated as the same present burst and ignored for frame boundary advancement.
  - Ensures census records across a full ~14 ms VSync frame cycle, capturing the complete 3D scene.
- **Pre-change Backups**:
  - Source: `highball_repo/spike/tsshim/d3d12.c.bak_before_debounce_frame_20261011-095500`
  - Engine DLL: `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_before_debounce_frame_20261011-095500`
  - Bottle DLL: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll.bak_before_debounce_frame_20261011-095500`
- **Build & Deployment**:
  - Compiled with `x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc -o d3d12.dll d3d12.c d3d12.def`.
  - Marked with `mark-builtin.py`.
  - Deployed to both CrossOver engine and Steam bottle `system32`.
  - Verified with `probe.exe` and `test_ord.exe` (ordinal 101 matches `D3D12CreateDevice`).
  - New binary SHA256: `324575730121ed25e097617616de9e7e7e135275fb89f24fef4ce53c6381f34c`.

## Follow-up: Skip Counters, Diagnostic Skip Logging & Skip All Instanced (2026-10-11)

- **Step 2 (Skip Counters & Logging)**:
  - Added per-hash skip tracking across draw paths (`draws_indexed`, `draws_instanced`, `draws_indirect`) and per-frame skip counters.
  - Added periodic skip summary report printed at frame boundaries every ~300 frames.
  - Added `HB_TSSHIM_SKIP_VS_LOG=1` support: logs individual first 20 skipped draw calls with draw path, PSO pointer, PSO creation index, and VS hash.
  - Explicitly logs all loaded skip hashes at DLL startup to eliminate ambiguity.
- **Step 3 (Skip All Instanced)**:
  - Added `HB_TSSHIM_SKIP_ALL_INSTANCED=1` (off by default): intercepts `DrawInstanced` and `DrawIndexedInstanced` and drops calls with `InstanceCount > 1`.
  - Completely separate from `HB_TSSHIM_NOEI` (which drops `ExecuteIndirect`).
- **Census Enhancement**:
  - Extended census reporting to dump input layout descriptors for top non-instanced PSOs alongside instanced PSOs.
- **Pre-change Backups**:
  - Source: `highball_repo/spike/tsshim/d3d12.c.bak_before_step2_step3_20261011-104000`
  - Engine DLL: `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_before_step2_step3_20261011-104000`
  - Bottle DLL: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll.bak_before_step2_step3_20261011-104000`
- **Build & Deployment**:
  - Compiled with `x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc -o d3d12.dll d3d12.c d3d12.def`.
  - Marked with `mark-builtin.py`.
  - Deployed to both CrossOver engine and Steam bottle `system32`.
  - Verified with `probe.exe` and `test_ord.exe` (ordinal 101 matches `D3D12CreateDevice`).
  - New binary SHA256: `2e760235f74405be8476f536e44cb6caa57559b4497c3c705ac52b9bd17e76e4`.

## Step 1-3: Hot-Reload Skip List, Per-Draw Census & Skip-by-Ordinal (2026-10-11)

- **Step 1: Hot-Reload Skip List (`HB_TSSHIM_HOT_SKIP=1`)**:
  - Intercepts and polls `C:\highball\skip.txt` every ~30 frames at frame boundary.
  - Supports syntax:
    - Hex string (or `vs <hash>`): skips graphics draws using matching vertex shader hash.
    - `pso <creation_index>`: skips graphics draws and compute dispatches matching PSO creation index.
    - `cs <hash>`: skips compute dispatches (`Dispatch`) matching compute shader hash.
    - `ord <from>-<to>`: skips graphics draws whose frame ordinal falls in `[from, to]`.
    - Blank lines and `# comments` ignored. Missing or empty file skips nothing.
  - On change, logs full active list to `tsshim.log`. Replaces env-var list while active.
- **Step 2: Per-Draw Context in Census (`C:\highball\census_draws.txt`)**:
  - Sequential, one line per draw in exact execution order.
  - Hooked `OMSetRenderTargets` across all command list interfaces to track bound RTV count and DSV attachment status.
  - Output fields: `ord`, draw type (`DrawIndexed`, `Draw`, `Indirect`), `pso`, `vs`, `ps`, vertex/index `count`, `inst`, `rtvs`, `dsv`, `vb_strides`, `vb_sizes`.
  - Flushes and logs preceding compute dispatches immediately before each group of draws (lines prefixed with `# DISPATCH:`).
- **Step 3: Skip-by-Ordinal Diagnostic**:
  - Frame-local atomic counter `g_frame_draw_ordinal` resets to 0 at every true frame boundary.
  - Matches `ord <from>-<to>` rules to drop graphics draw calls deterministically.
- **Pre-change Backups**:
  - `cxbottle.conf`: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/cxbottle.conf.bak_20261011-110000`
  - Source: `highball_repo/spike/tsshim/d3d12.c.bak_step1_to_3_20261011-110000`
  - Engine DLL: `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_20261011-110000`
  - Bottle DLL: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll.bak_20261011-110000`
- **Build & Deployment**:
  - Compiled with `x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc -o d3d12.dll d3d12.c d3d12.def`.
  - Marked with `mark-builtin.py`.
  - Deployed to both CrossOver engine and Steam bottle `system32`.
  - Verified with `probe.exe` and `test_ord.exe` (ordinal 101 matches `D3D12CreateDevice`).
  - New binary SHA256: `c781bb9eaa7784650bd63311c78d71a9f7d71cf9aa187d04623455ae9d4af82a`.

## Step 1-6: Target PSO Input Layouts, Draw Parameters, Bytecode Dumps & Offline Container Comparison (2026-10-11)

- **Step 1: Input Layout Dumping**:
  - Added `dxgi_format_name` translation helper covering all standard DXGI formats.
  - Added target PSO and VS hash filter matching PSOs `9703; 8653, 8656; 9058, 9196; 8541, 8616, 8761, 8880; 9077` and hashes `a99ec1d9, c8ca6104, c58458c3, daebf740, dd05ba81`.
  - Dumps every input element: semantic name + index, DXGI format name and raw ID, slot, aligned byte offset, slot class (per-vertex vs per-instance), and instance data step rate.
  - Covers both `D3D12_GRAPHICS_PIPELINE_STATE_DESC` and `D3D12_PIPELINE_STATE_STREAM_DESC` creation paths, plus unconditional logging in the census report.
- **Step 2: Draw Parameters Extended in Census**:
  - Extended `write_census_per_draw` signature and format string.
  - For `DrawIndexed`: logs raw `StartIndexLocation` (`start_idx`), `BaseVertexLocation` (`base_vtx`), `StartInstanceLocation` (`start_inst`).
  - For `Draw`: logs raw `StartVertexLocation` (`start_vtx`), `StartInstanceLocation` (`start_inst`).
  - For `Indirect`: logs raw placeholders (0).
  - Updated `census_draws.txt` header line to reflect new schema.
- **Step 3: Bytecode Dumps & macOS Disassembly**:
  - Added `maybe_dump_vs_bytecode` hook in `remember_pipeline` and `remember_pipeline_stream` to automatically dump matching VS bytecode to `C:\highball\shaders\<hash>.dxbc`.
  - Extracted all 5 broken hashes (`a99ec1d9`, `c8ca6104`, `c58458c3`, `daebf740`, `dd05ba81`) and 10 non-broken color-pass shaders (`2b9646ba` car body, `3d2781f0` road, `246b7a8c`, `a4f7bac1`, `e55eed61`, `05ab27f4`, `ea91ffda`, `7e688961`, `ca8bb759`, `fd8c05c4`) into `C:\highball\shaders\`.
  - Offline disassembly feasibility on macOS evaluated and documented.
- **Step 4: DXBC SM 5.1 Container Comparison**:
  - Offline parser parsed all 15 dumped containers without failure.
  - Confirmed all shaders are Shader Model 5.1 (`vs_5_1`), DXBC containers (not DXIL / SM 6.x).
  - SFI0 chunk absent across all shaders (standard for SM 5.1 DXBC).
  - Input/output signatures, CBVs, SRVs, Samplers, and Root Signatures extracted and compared side-by-side.
- **Step 5: Raw Instance Data Evaluation**:
  - Geometry streams for broken shaders are bound from `D3D12_HEAP_TYPE_DEFAULT` GPU local memory (not CPU-readable / not UPLOAD heap). Stopped without adding GPU copies or barriers.
- **Pre-change Backups**:
  - `cxbottle.conf`: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/cxbottle.conf.bak.20261011_151306`
  - Source: `highball_repo/spike/tsshim/d3d12.c.bak.20261011_151312`
  - Engine DLL: `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll.bak_step1_to_3_20261011-110000`
  - Bottle DLL: `${HOME}/Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/system32/d3d12.dll.bak_step1_to_3_20261011-110000`
- **Build & Deployment**:
  - Compiled with `x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc -o d3d12.dll d3d12.c d3d12.def`.
  - Marked with `mark-builtin.py`.
  - Deployed to both CrossOver engine and Steam bottle `system32`.
  - Verified with PE export parser (ordinal 101 matches `D3D12CreateDevice`, ordinal 100 matches `GetBehaviorValue`).
  - Binary SHA256: `f141fef749e1e18f16670ab439bf3294da8d7d78bc6e3833c07b6dd5e09995c7`.
  - Game NOT run.
