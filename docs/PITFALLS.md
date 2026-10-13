# Critical Pitfalls & What NOT to Do

[← Back to README](../README.md)

---

> ### 1. NEVER modify `ForzaHorizon4.exe` on disk
> Arxan Anti-Tamper decrypts its `.text` section during execution. Any hex edit or binary patch to `ForzaHorizon4.exe` corrupts the encrypted layout, causing the executable to terminate immediately upon launch.

> ### 2. NEVER perform in-memory code patching or detours on game code
> Attempting to write `VirtualProtect` + hook bytes into the game's executable memory pages trips Arxan's periodic runtime integrity CRC checks, resulting in a crash shortly thereafter. The Vectored Exception Handler (VEH) in our proxy shim works precisely because it modifies **zero** bytes of memory.

> ### 3. DO NOT strip Geometry or Tessellation Shaders
> Older community workarounds suggested stripping geometry/tessellation stages (`no_gs=1`) to resolve crashes. In Forza Horizon 4, this causes missing terrain, missing road surfaces, or device-removal errors.

> ### 4. DO NOT disable MSAA or Deformable Snow in settings
> Forcing MSAA off or disabling deformable snow via manual config edits breaks rendering state assumptions in D3DMetal and can prevent the world from rendering correctly. Keep standard game settings.

> ### 5. Preserve export ordinals in `d3d12.def`
> If rebuilding `d3d12.dll`, never compile without `d3d12.def`. Forza Horizon 4 links to `GetBehaviorValue` (Ordinal 100) and `D3D12CreateDevice` (Ordinal 101) directly by number. Missing ordinals result in an immediate crash at the splash screen.
