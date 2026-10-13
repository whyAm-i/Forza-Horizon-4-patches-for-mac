# Method B: Human Purist (Manual Installation)

[← Back to README](../README.md)

This guide covers manually deploying the pre-compiled, hash-checked patches to your CrossOver engine and Steam bottle using the terminal helper `copy_checked.py`.

The helper validates target hashes, creates unique timestamped backups (`*.bak_fh4_archive_*`), and safely refuses to modify files while the game or CrossOver is running.

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

Verify that CrossOver 26.3's installed Apple GPTK runtime is intact, then install the retained provider DLL in both engine and bottle directories:

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

This deploys the proxy shim containing both the **explicit vertex offset fix** and the **VEH open-world crash bypass**:

```bash
# Patch Bottle d3d12.dll
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' \
  "$BOTTLE/drive_c/windows/system32/d3d12.dll" \
  --expected 6d6753139742c5f39961ec64021ede30308cdd991630797a355d3f5db342a816 \
  --expected f141fef749e1e18f16670ab439bf3294da8d7d78bc6e3833c07b6dd5e09995c7 \
  --apply --game-closed

# Patch CrossOver Engine d3d12.dll
python3 tools/copy_checked.py '02-rendering/bottle/d3d12.dll' \
  "$CX/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll" \
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
"HB_TSSHIM_CAPTURE_BINDINGS" = "0"
"HB_TSSHIM_CAPTURE_INDIRECT_ARGS" = "0"
"HB_TSSHIM_SKIP_ALL_INSTANCED" = "0"
```

> [!NOTE]
> `HB_TSSHIM_DEBUG` is triggered by any non-empty value. To disable debug logging, omit the variable or delete the line completely.

---

## Optional: Compiling & Applying from Source

If you prefer compiling the D3D12 proxy shim directly from source:
```bash
brew install mingw-w64
cd "02-rendering/source"

# Compile with strict export definitions
x86_64-w64-mingw32-gcc -shared -O2 -Wall -static-libgcc \
  -o d3d12.dll d3d12.c d3d12.def

# Sign with Wine builtin PE header
python3 mark-builtin.py d3d12.dll

# Verify export ordinals (Ordinal 100 must be GetBehaviorValue, 101 must be D3D12CreateDevice)
x86_64-w64-mingw32-objdump -p d3d12.dll | grep -E "GetBehaviorValue|D3D12CreateDevice"
```
