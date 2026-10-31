# Installing the working drift-zone shim

This is a **shim-only upgrade** for an already-running FH4 setup. It replaces the two `d3d12.dll` copies. Preserve the working `kernelbase.dll`, GPTK provider, `apd12.so`, game settings and saves. For the original launch/geometry setup, see [../APPLY.md](../APPLY.md).

## 1. Check compatibility, close applications, and back up

Use FH4 `1.478.564.0` and the exact CrossOver 26.3 build described in [README.md](README.md). Close FH4, Steam and the bottle's applications before replacing DLLs. An existing game process retains its loaded code; replacing a file does not update that process.

From the repository root:

```sh
cd working-patches
python3 tools/verify_archive.py
CX='/Applications/CrossOver.app/Contents/SharedSupport/CrossOver'
BOTTLE="$HOME/Library/Application Support/CrossOver/Bottles/New Bottle"
ENGINE_DLL="$CX/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll"
BOTTLE_DLL="$BOTTLE/drive_c/windows/system32/d3d12.dll"
shasum -a 256 "$ENGINE_DLL" "$BOTTLE_DLL"
```

Adjust the bottle/application paths before proceeding. Create and verify the separate backup described in [BACKUP_AND_ROLLBACK.md](BACKUP_AND_ROLLBACK.md). The copy helper also makes a unique adjacent backup for each changed target.

## 2. Dry-run both destinations

The following commands upgrade the repo's previous working road/speed-zone shim, SHA-256 `bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44`:

```sh
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' "$ENGINE_DLL" \
  --expected bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' "$BOTTLE_DLL" \
  --expected bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44
```

These are read-only. If a target already matches V3, the helper reports that and does nothing. If the current hash differs, identify that binary and review its provenance before supplying a different `--expected` hash. Do not defeat the check by blindly accepting any current file.

## 3. Install, then verify

After both dry runs are eligible, repeat each exact command with `--apply --game-closed`:

```sh
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' "$ENGINE_DLL" \
  --expected bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44 \
  --apply --game-closed
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' "$BOTTLE_DLL" \
  --expected bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44 \
  --apply --game-closed
shasum -a 256 "$ENGINE_DLL" "$BOTTLE_DLL"
```

Both installed hashes must be:

```text
833b964203e1e157a162852863656e0e4934ef10a3659d3047ebd0422b529f66
```

The helper checks source/target hashes, refuses symlink destinations, backs up each changed file and uses an atomic replacement. **`--game-closed` is your assertion; this helper does not detect or close running applications.** The two calls are separate transactions. If the second fails, restore the first target from its printed backup before launching. Save the printed backup paths and hashes. CrossOver application files may need write permission; ordinary bottle files should not require running the whole recipe as root.

## 4. Keep the working graphics provider

The successful drift test used the renamed GPTK 4.0 beta 3 provider:

```text
apd12.dll SHA-256: 32c942ed368a45e58d35f4da3de3776d1f74e785731de9cf6a5e705a2925488a
engine apd12.so -> ../../external/libd3dshared.dylib
```

This provider differs from the older `02-rendering/bottle/apd12.dll` archive (`f1bcaa4d79e4353e075e7f284ebcb2d44a1b0cb2aa88f01f6341dc3c518b976f`). The drift-only upgrade does **not** replace an already-working GPTK 4 provider with that older baseline. Proprietary runtime files are not added to this patch. Preserve the provider and its supporting runtime as a matched set; the older provider was not the one used for this V3 gameplay confirmation.

## 5. Configuration and activation

The existing bottle environment should retain:

```ini
"CX_GRAPHICS_BACKEND" = "d3dmetal"
"HB_D3D12_REAL" = "apd12.dll"
"HB_TSSHIM_EXPLICIT_INPUT_OFFSETS" = "1"
"HB_TSSHIM_FIX_WORLD_ENTRY" = "1"
```

`HB_TSSHIM_FIX_DRIFT_ZONE` defaults to enabled; explicitly setting it to `"0"` disables the new runtime changes. Setting `HB_TSSHIM_FIX_WORLD_ENTRY=0` also disables them. If you choose to edit configuration, back up `cxbottle.conf` first and edit the existing section rather than adding duplicate sections. Metal 4 (`D3DM_MTL4=1`) was used in the working session; preserve your known-working settings instead of replacing the full snapshot.

Start Steam, then FH4 through the intended CrossOver installation and bottle. The per-session file is:

```text
$BOTTLE/drive_c/highball/logs/drift-event-gate-<Windows PID>.txt
```

Look for `FH4_DRIFT_OBSERVER v3` and `installed: MethodObjectInfo observer forwards every callback`. A `waiting up to 300 seconds` message is not an installation result. The installer must subsequently report success; if it times out or reports a mismatch/error, retain the log and investigate rather than changing offsets.

These bounded drift logs work with verbose graphics debugging off. `HB_TSSHIM_DEBUG=0` still enables verbose logging because the variable is nonempty; remove that variable to turn it off.

## 6. Gameplay check

Finish a drift zone, check that its result/points register and drift-zone mode ends, then enter another zone. Also check aborting/leaving without a completed score. The observer's event records must say **`forwarding`**, never `skipped`. Achievement unlocks are a separate check.

## Optional: build from source

The tested binary used LLVM-MinGW `20261020-ucrt-macos-universal`, targeting `x86_64-w64-windows-gnu`, Clang `23.1.4`. From `working-patches`, with that compiler on PATH:

```sh
mkdir -p 08-drift-zone/build
x86_64-w64-mingw32-gcc -shared -O2 -Wall \
  -o 08-drift-zone/build/d3d12.dll \
  02-rendering/source/d3d12.c 02-rendering/source/d3d12.def
python3 02-rendering/source/mark-builtin.py 08-drift-zone/build/d3d12.dll
x86_64-w64-mingw32-objdump -p 08-drift-zone/build/d3d12.dll
```

All six headers beside `d3d12.c` are needed. Preserve `d3d12.def`: ordinal 100 is `GetBehaviorValue`, ordinal 101 is `D3D12CreateDevice`. Keep the output basename **`d3d12.dll`**; the tested linker did not reliably obtain the internal DLL name from the DEF when a different basename was used. `mark-builtin.py` writes the required Wine builtin marker; it is not code signing. Rebuilt hashes can differ; validate the build instead of assigning it the archived binary's hash.

See [verification/README.md](verification/README.md) for the portable isolated tests. Neither rebuilding nor opening the archive installs anything into the running game.
