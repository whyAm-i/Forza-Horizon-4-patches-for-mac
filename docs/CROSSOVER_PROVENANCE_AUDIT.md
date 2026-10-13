# CrossOver provenance and dependency audit

Audit date: 2026-10-13

## Rule used

An item was flagged when either:

- `working-patches/manifest.json` recorded its source under `CrossOver.app` or a CrossOver bottle; or
- it was a binary/source-diff snapshot derived from the CrossOver Wine tree, including the archived Highball `x64-crossover` material.

“Explicitly depended on by the working setup” means the normal install/runtime path in `working-patches/APPLY.md` directly consumes the item. A manifest entry, historical note, rollback-only use, duplicate copy, or disaster-recovery convenience does not count as a working dependency.

Configuration and save data under `03-settings/` were inspected but are not code, so they were left in place. Plain documentation that mentions CrossOver was also left in place.

## Retained: explicit working dependencies

| Path | Why it is retained |
|---|---|
| `working-patches/01-launch-package/binaries/kernelbase.dll` | Patched CrossOver Wine binary required for the launch/package-family workaround. One copy now serves both engine and bottle targets. |
| `working-patches/02-rendering/bottle/apd12.dll` | Real D3DMetal provider loaded by the working proxy through `HB_D3D12_REAL=apd12.dll`. One copy now serves both targets. |
| `working-patches/02-rendering/bottle/d3d12.dll` | Current project-built proxy captured from the working bottle. It contains the geometry and VEH fixes and now serves both targets. It is not a stock CrossOver DLL, but was flagged because its recorded source was a CrossOver bottle. |

The proxy still has a runtime dependency on CrossOver 26.3's installed Apple GPTK files (`libd3dshared.dylib`, D3DMetal framework, and related loader layout). Those prerequisites are no longer copied into this repository.

## Deleted: no explicit working dependency

129 archived entries totaling 151,338,905 bytes were removed:

| Removed path or group | Entries | Reason |
|---|---:|---|
| `working-patches/01-launch-package/binaries/bottle-kernelbase.dll` | 1 | Byte-identical duplicate of the retained patched binary. |
| `working-patches/01-launch-package/binaries/kernelbase-original.dll` | 1 | Stock rollback-only CrossOver binary. |
| `working-patches/01-launch-package/binary-delta.json` | 1 | Generated dump of stock/patched CrossOver byte ranges; not consumed by the working install. |
| `working-patches/01-launch-package/highball/` | 2 | Inactive Highball `x64-crossover` binary snapshots. |
| `working-patches/01-launch-package/reference/0001-crossover-fh4-getpackagesbypackagefamily.patch` | 1 | Unused CrossOver source-diff proposal, explicitly not the application path. |
| `working-patches/01-launch-package/reference/0022-highball-fh4-getpackagesbypackagefamily.patch` | 1 | Unused Highball/CrossOver source-diff proposal. |
| `working-patches/02-rendering/bottle/d3d12_d3dmetal.dll` | 1 | Byte-identical duplicate of retained `apd12.dll`. |
| `working-patches/02-rendering/bottle/dxgi.dll` | 1 | Captured stock bottle DLL with no working-package reference. |
| `working-patches/02-rendering/rollback/` | 2 | Stock rollback-only CrossOver binaries. Deployment already creates exact backups. |
| `working-patches/02-rendering/runtime/apple_gptk/` | 56 | Copied CrossOver/GPTK runtime, old runtime, frameworks, libraries, Wine wrappers, and symlinks. The normal path needs only the installed CrossOver prerequisite plus the two retained files. |
| `working-patches/06-history/highball-engine-source/` | 62 | Nested historical Highball repository based on CrossOver sources, including patch series and packed Git history; not consumed by the FH4 working path. |

## Result

The repository no longer vendors a complete or rollback copy of the CrossOver/GPTK runtime. It retains only the three binaries directly consumed by the verified working setup. Installation and rollback documentation now uses those single retained copies and the deployment tool's timestamped backups.
