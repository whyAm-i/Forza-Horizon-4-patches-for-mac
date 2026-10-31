# Method B: Human Purist (Manual Installation)

[← Back to README](../README.md)

This guide covers manually deploying the pre-compiled, hash-checked patches to your CrossOver engine and Steam bottle using the terminal helper `copy_checked.py`.

The helper validates target hashes and creates unique timestamped backups (`*.bak_fh4_archive_*`). Its `--game-closed` switch is your explicit assertion; it does not inspect or close running processes. Close FH4 and bottle applications yourself.

**Drift-only upgrade:** if the game already launches and renders correctly, use [08-drift-zone/INSTALL.md](08-drift-zone/INSTALL.md). It updates only the two shim copies and preserves the working GPTK 4 provider, launch patch, graphics settings and saves.

---

## Step 1: Verify Archive Integrity

Open Terminal and navigate to `working-patches`:
```bash
# Navigate to working-patches directory:
cd working-patches
python3 tools/verify_archive.py
```
*Expected output: `PASS: ... files/symlinks verified; no live files changed.`*

---

## Step 2: Define Target Paths

Set variables for your CrossOver installation and target bottle:
```bash
CX="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
BOTTLE="$HOME/Library/Application Support/CrossOver/Bottles/Steam"
```
*(Adjust `BOTTLE` if your bottle has a custom name).*

---

## Step 3: Apply Launch / DRM Workaround (`kernelbase.dll`)

Quit CrossOver and ensure all bottle applications are closed. Run the hash-checked copy helper:

```bash
# Patch CrossOver Engine kernelbase.dll
python3 tools/copy_checked.py '01-launch-package/binaries/kernelbase.dll' \
  "$CX/lib/wine/x86_64-windows/kernelbase.dll" \
  --expected ca72a76ce5770996cc9cba6989031a79eb1e63b6d9fca90d59eebab22e39191b \
  --apply --game-closed

# Patch Bottle kernelbase.dll
python3 tools/copy_checked.py '01-launch-package/binaries/kernelbase.dll' \
  "$BOTTLE/drive_c/windows/system32/kernelbase.dll" \
  --expected ca72a76ce5770996cc9cba6989031a79eb1e63b6d9fca90d59eebab22e39191b \
  --apply --game-closed
```

---

## Step 4: Ensure D3DMetal Provider (`apd12.dll`) Exists

For restoration of the original baseline only, verify that CrossOver 26.3's installed Apple GPTK runtime is intact, then install the retained provider DLL in both engine and bottle directories. **Skip these provider copies for an already-working GPTK 4 drift upgrade**; the tested GPTK 4.0 beta 3 provider differs from the older archived file:

```bash
# Engine provider
cp '02-rendering/bottle/apd12.dll' "$CX/lib64/apple_gptk/wine/x86_64-windows/apd12.dll"

# Bottle provider
cp '02-rendering/bottle/apd12.dll' "$BOTTLE/drive_c/windows/system32/apd12.dll"

# Engine Unix symlink (if absent)
ln -s ../../external/libd3dshared.dylib "$CX/lib64/apple_gptk/wine/x86_64-unix/apd12.so" 2>/dev/null || true
```

The symlink target and D3DMetal framework must already exist in the installed CrossOver application. They are prerequisites and are not bundled in this repository.

---

## Step 5: Apply Direct3D 12 Proxy Shim (`d3d12.dll`)

This deploys the current proxy shim containing the **V3 drift fix**, **explicit vertex offset fix**, and earlier **road/speed-zone VEH bypasses**. Its SHA-256 is `833b964203e1e157a162852863656e0e4934ef10a3659d3047ebd0422b529f66`:

```bash
# Patch Bottle d3d12.dll
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' \
  "$BOTTLE/drive_c/windows/system32/d3d12.dll" \
  --expected bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44 \
  --expected 6d6753139742c5f39961ec64021ede30308cdd991630797a355d3f5db342a816 \
  --expected f141fef749e1e18f16670ab439bf3294da8d7d78bc6e3833c07b6dd5e09995c7 \
  --apply --game-closed

# Patch CrossOver Engine d3d12.dll
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' \
  "$CX/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll" \
  --expected bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44 \
  --expected 6d6753139742c5f39961ec64021ede30308cdd991630797a355d3f5db342a816 \
  --expected f141fef749e1e18f16670ab439bf3294da8d7d78bc6e3833c07b6dd5e09995c7 \
  --apply --game-closed
```
*(Note: If updating from stock D3DMetal, supply the stock hash via `--expected 1b7a02cb37ec6b484e2aaa76b5ec9cbb47e63aeec29dbe087d5d1589a3347cfb`).*

---

## Step 6: Configure `cxbottle.conf`

Back up your bottle configuration file:
```bash
cp "$BOTTLE/cxbottle.conf" "$BOTTLE/cxbottle.conf.bak"
```

Open `cxbottle.conf` in a text editor and ensure the `[EnvironmentVariables]` section includes the following keys:
```ini
[EnvironmentVariables]
"CX_GRAPHICS_BACKEND" = "d3dmetal"
"HB_D3D12_REAL" = "apd12.dll"
"HB_TSSHIM_EXPLICIT_INPUT_OFFSETS" = "1"
"HB_TSSHIM_FIX_WORLD_ENTRY" = "1"
"HB_TSSHIM_FIX_DRIFT_ZONE" = "1"
"HB_TSSHIM_CAPTURE_BINDINGS" = "0"
"HB_TSSHIM_CAPTURE_INDIRECT_ARGS" = "0"
"HB_TSSHIM_SKIP_ALL_INSTANCED" = "0"
```

> [!NOTE]
> `HB_TSSHIM_DEBUG` is triggered by any non-empty value. To disable debug logging, omit the variable or delete the line completely.

---

## Verify drift activation and gameplay

Read `drift-event-gate-<pid>.txt` in the bottle's `drive_c/highball/logs` directory. The current marker is `FH4_DRIFT_OBSERVER v3`; wait for its installation result, then confirm named events are `forwarding`. Test scoring, ending zone mode and entering another zone. These bounded logs do not require verbose graphics logging.

## Optional: Compiling & Applying from Source

Follow the [V3 build and verification instructions](08-drift-zone/INSTALL.md#optional-build-from-source). Build into a separate directory, use all six headers, preserve `d3d12.def` and the output basename `d3d12.dll`, and run `mark-builtin.py`. Rebuilt binaries need their own verification and hashes.
