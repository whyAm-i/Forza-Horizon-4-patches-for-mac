# Applying the captured setup correctly

These instructions restore the **captured CrossOver 26.3 setup**, including the VEH world-entry crash bypass and geometry fix. Use the matched binaries already in this archive. Original sources/scripts are reference material; running every historical patch would combine incompatible experiments.

## 1. Prepare and verify

1. Quit Forza and close applications in the Steam bottle through CrossOver. An existing process retains its loaded DLLs; live replacement is not an application method.
2. Confirm the CrossOver version is 26.3, build `26.3.0.39832`. If different, stop: the fixed offsets and retained patched binaries are version-specific.
3. Back up the bottle and the CrossOver application before changing either. Keep saves; do not overwrite `ConnectedStorage` wholesale.
4. Verify this archive. Review dry-run output before adding the explicit write switches.

```sh
cd working-patches
python3 tools/verify_archive.py
CX='/Applications/CrossOver.app/Contents/SharedSupport/CrossOver'
BOTTLE="$HOME/Library/Application Support/CrossOver/Bottles/Steam"
```

Paths below assume the same installation layout. Substitute your bottle name if necessary. The helper deliberately refuses symlink destinations, missing destinations and mismatched hashes. Resolve a Wine symlink to its actual intended file first; do not silently replace an unknown Wine layout. CrossOver application files may need administrator write permission. Ordinary bottle files should not require running the entire recipe as root.

## 2. Restore the launch/package workaround

The original expected CrossOver kernelbase SHA-256 is `ca72a76ce5770996cc9cba6989031a79eb1e63b6d9fca90d59eebab22e39191b`; patched is `313a9d07019422fe0f56a579667d005d756728e13b66329aaa15321d92b0f182`.

Dry-run each target:

```sh
python3 tools/copy_checked.py '01-launch-package/binaries/kernelbase.dll' "$CX/lib/wine/x86_64-windows/kernelbase.dll" --expected ca72a76ce5770996cc9cba6989031a79eb1e63b6d9fca90d59eebab22e39191b
python3 tools/copy_checked.py '01-launch-package/binaries/kernelbase.dll' "$BOTTLE/drive_c/windows/system32/kernelbase.dll" --expected ca72a76ce5770996cc9cba6989031a79eb1e63b6d9fca90d59eebab22e39191b
```

To apply an eligible dry run, repeat its exact command with `--apply --game-closed`. If already identical, the helper reports “Already matches” and writes nothing. If the current hash is different, identify the version or modifications first; do not use an arbitrary hash just to bypass the check. The helper can take multiple `--expected` options for specifically reviewed prior versions.

Read `01-launch-package/README.md` for the actual changed ranges and package identity. Do not combine this snapshot with the alternative inline/code-cave scripts.

## 3. Restore the D3D12 proxy and matched provider

The main shim is `02-rendering/bottle/d3d12.dll`. Use that one retained file for both the bottle and engine targets. SHA-256: `bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44` (with Speed Zone dynamic stack scan VEH bypass; road-discovery-only VEH hash was `facc38fb8affbf579153bcc54de3b063fb99fd9be1513988f9e2eda1ee8ad1e6`; pre-VEH v5 hash was `6d6753139742c5f39961ec64021ede30308cdd991630797a355d3f5db342a816`).

Before installing the shim, confirm the CrossOver application already contains its stock `lib64/apple_gptk/external/libd3dshared.dylib` and D3DMetal framework. These proprietary runtime files are prerequisites and are not bundled. The retained `02-rendering/bottle/apd12.dll` hashes to `f1bcaa4d79e4353e075e7f284ebcb2d44a1b0cb2aa88f01f6341dc3c518b976f`; install it in both the engine and bottle. The engine's `wine/x86_64-unix/apd12.so` must be a relative symlink to `../../external/libd3dshared.dylib`.

Known prior shim hashes:
- Road-discovery-only VEH shim: `facc38fb8affbf579153bcc54de3b063fb99fd9be1513988f9e2eda1ee8ad1e6`
- Pre-VEH v5 shim: `6d6753139742c5f39961ec64021ede30308cdd991630797a355d3f5db342a816`
- Initial deployment backup: `f141fef749e1e18f16670ab439bf3294da8d7d78bc6e3833c07b6dd5e09995c7`

```sh
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' "$BOTTLE/drive_c/windows/system32/d3d12.dll" --expected 52bdcd0abe16b7b99af4f127c7f04a0fde1195da5fa6c19cb9b4f12f371e6c5e --expected facc38fb8affbf579153bcc54de3b063fb99fd9be1513988f9e2eda1ee8ad1e6 --expected 6d6753139742c5f39961ec64021ede30308cdd991630797a355d3f5db342a816 --expected f141fef749e1e18f16670ab439bf3294da8d7d78bc6e3833c07b6dd5e09995c7
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' "$CX/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll" --expected 52bdcd0abe16b7b99af4f127c7f04a0fde1195da5fa6c19cb9b4f12f371e6c5e --expected facc38fb8affbf579153bcc54de3b063fb99fd9be1513988f9e2eda1ee8ad1e6 --expected 1b7a02cb37ec6b484e2aaa76b5ec9cbb47e63aeec29dbe087d5d1589a3347cfb --expected 6d6753139742c5f39961ec64021ede30308cdd991630797a355d3f5db342a816 --expected f141fef749e1e18f16670ab439bf3294da8d7d78bc6e3833c07b6dd5e09995c7
```

Other prior shim revisions are captured in history. If installing onto a stock file with a different hash, verify that it belongs to the exact supported CrossOver build rather than bypassing the guard. Do not mix a new D3DMetal provider with this recovery snapshot. The game-folder root should not have an additional `d3d12.dll` overriding these targets.

Copy the retained provider to the engine's `wine/x86_64-windows` directory and bottle `system32`, backing up existing files first. Recreate the engine symlink only when its installed CrossOver target exists:

```sh
# Only if the link is absent and ../../external/libd3dshared.dylib exists:
ln -s ../../external/libd3dshared.dylib "$CX/lib64/apple_gptk/wine/x86_64-unix/apd12.so"
```

The rest of the Apple GPTK runtime must come from the installed CrossOver 26.3 application. If that runtime is missing or damaged, repair/reinstall CrossOver instead of restoring copied application binaries from this repository.

## 4. Set the required bottle environment

Back up `cxbottle.conf`, then edit its existing `[EnvironmentVariables]` section. Ensure each key occurs once. Do not paste a second section or blindly replace the complete snapshot: it contains machine-specific paths and a duplicate historical DXMT key.

```ini
"CX_GRAPHICS_BACKEND" = "d3dmetal"
"HB_D3D12_REAL" = "apd12.dll"
"HB_TSSHIM_EXPLICIT_INPUT_OFFSETS" = "1"
"HB_TSSHIM_FIX_WORLD_ENTRY" = "1"
"HB_TSSHIM_CAPTURE_BINDINGS" = "0"
"HB_TSSHIM_CAPTURE_INDIRECT_ARGS" = "0"
"HB_TSSHIM_SKIP_ALL_INSTANCED" = "0"
```

The full snapshot also preserves vendor/device identification, Metal backend choices and diagnostics. They are captured setup choices, not separately proven fixes. Keep broad suppression/clamping flags absent or disabled. `HB_TSSHIM_DEBUG` is enabled by a **nonempty variable**, so setting it to `"0"` does not disable logging: remove the entry to disable it. Logging is needed if you want to validate the startup marker or use Shader Lab.

For Shader Lab, also set `HB_TSSHIM_HOT_SKIP=1`. Create the bottle's `drive_c/highball/logs` directory if absent. Keep `drive_c/highball/skip.txt` empty or comment-only during ordinary play; the archived file is the full-scene baseline. Back up a live skip file before restoring it, and do not edit it while a Shader Lab test is running.

The active Wine registry excerpt has native `onlinefix64` and `steam_api64` overrides. Preserve these only when restoring the captured OnlineFix-based setup; they are not needed for a separate clean official installation.

## 5. Game settings and support files

`03-settings/game/UserConfigSelections` captures the current user settings, not a required whole-file patch. Use it as a comparison reference. Earlier grass/water quality changes are unconfirmed and remain in history. Keep the captured working MSAA/deformable-snow selections when comparing; previous attempts to force them off broke rendering/startup. Do not disable crowds, grass, trees, shadows or terrain to claim the stretching issue is fixed.

`05-game-support` preserves the active third-party Steam/OnlineFix files and Wine helper DLLs. Restore only the files that belong to this same game build after backing up their destinations. Do not mix HOODLUM and OnlineFix variants, rename `steam_api64.of` to `.dll`, or delete OnlineFix alone: the current `steam_api64.dll` imports it. The current game folder has no `steamclient64.dll` or `hlm.ini`; those obsolete files were not included. Steam's installed client is a separate prerequisite.

The local `TargetHardwareProfiler.dll.bak` disabled-profiler backup is not included in Git. Keeping the profiler disabled is captured state; its necessity has not been independently established here. Do not rename a local backup back while claiming to reproduce the captured setup. OnlineFix multiplayer operation is not verified by this archive.

## 6. Rebuild, if needed

Use the archived current source, **all four .h dependencies and d3d12.def**. `d3d12.def` is mandatory: missing it previously caused a splash crash because the game's ordinal 101 import was wrong.

```sh
cd '02-rendering/source'
x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc -o d3d12.rebuilt.dll d3d12.c d3d12.def
python3 mark-builtin.py d3d12.rebuilt.dll
x86_64-w64-mingw32-objdump -p d3d12.rebuilt.dll
```

Confirm ordinal 100 is GetBehaviorValue and ordinal 101 is D3D12CreateDevice. A newly compiled DLL need not have the same hash as the archived binary; the archived binary was copied, not rebuilt during packaging. Validate a new build before installing it. The historical build-tsshim.sh expects the original repository layout and is not the standalone archive build command.

## 7. Verify after a normal restart

- Confirm engine and bottle kernelbase/shim/provider hashes match the archive.
- With logging enabled, the newest startup marker should show `binding-capture-v5; bindings=0; input_offsets=1`.
- Keep all scene draws enabled. Inspect the formerly stretched trees, leaves, stones, grass and walls. The offset change fixes layout interpretation; it is not object removal.
- The open-world road-discovery illegal instruction at `0x140D05DA0` is bypassed by the VEH handler. In `tsshim-<pid>.log`, confirm the interception message appears during Free Roam: `FH4_WORLD_ENTRY_FIX: Intercepted illegal instruction at 0x140d05da0 (RVA 0xd05da0)! Bypassing to caller 0x140d04d8a`. See `07-evidence/world-entry-veh-bypass.md` for full evidence.

## Roll back

Each successful helper copy prints a unique `*.bak_fh4_archive_*` file. Close Forza again and copy that exact backup to its original destination, after checking the destination still matches the installed archive hash. Restore the separately saved `cxbottle.conf` and skip file only if they have not received later desired edits. Restoring stock kernelbase may reintroduce the launch problem; restoring a pre-offset shim may reintroduce stretched geometry.

The copy helper accepts an external backup only with `--source-sha256 HASH` supplied; obtain the saved backup hash from its successful-copy output. Use `--expected` for the installed destination hash and dry-run before `--apply --game-closed`.
