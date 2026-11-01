# Critical Pitfalls & What NOT to Do

[← Back to README](../README.md)

---

> ### 1. NEVER modify `ForzaHorizon4.exe` on disk
> Arxan Anti-Tamper decrypts its `.text` section during execution. Any hex edit or binary patch to `ForzaHorizon4.exe` corrupts the encrypted layout, causing the executable to terminate immediately upon launch.

> ### 2. Avoid arbitrary in-memory code patches; preserve the drift fix's exact guards
> Earlier in-memory hook/detour attempts were unsafe. The road/speed-zone VEH handlers do not rewrite game instructions. The later [drift-zone V3 fix](../working-patches/08-drift-zone/README.md) is a narrowly recorded exception: on the exact tested executable, it verifies the complete runtime signature, changes one entry byte at RVA `0x895250` from `0x5C` to `0xC3`, and observes a vtable while forwarding every callback.

> ### 3. DO NOT strip Geometry or Tessellation Shaders
> Older community workarounds suggested stripping geometry/tessellation stages (`no_gs=1`) to resolve crashes. In Forza Horizon 4, this causes missing terrain, missing road surfaces, or device-removal errors.

> ### 4. DO NOT disable MSAA or Deformable Snow in settings
> Forcing MSAA off or disabling deformable snow via manual config edits breaks rendering state assumptions in D3DMetal and can prevent the world from rendering correctly. Keep standard game settings.

> ### 5. Preserve export ordinals in `d3d12.def`
> If rebuilding `d3d12.dll`, never compile without `d3d12.def`. Forza Horizon 4 links to `GetBehaviorValue` (Ordinal 100) and `D3D12CreateDevice` (Ordinal 101) directly by number. Missing ordinals result in an immediate crash at the splash screen.

> ### 6. Preserve the heap repair’s provenance and page guards
> [Memory V1.1](../working-patches/09-memory-leak/README.md) changes only two game IAT data pointers after exact image/signature/API checks. Its IAT can be executable copy-on-write; preserve execute permission while writing data and restore the original protection. Never turn the repair into an unchecked pointer[-1] read or blanket pointer subtraction. Use a fresh process for disabling or rolling back the pinned shim.
