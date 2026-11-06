# Install or roll back the online credential fix

This procedure targets **CrossOver 26.3 x64** with an exact stock DLL checksum. Use Python 3.9+ and run the commands from the `working-patches` directory. Quit CrossOver and stop all its bottle applications before replacing either file. The engine copy is shared by bottles using that CrossOver installation.

## Generate and review the patch

Set `BOTTLE` to the bottle that runs FH4. Adjust `CX` if you installed CrossOver at a different path.

```sh
CX="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
BOTTLE="$HOME/Library/Application Support/CrossOver/Bottles/Steam"
ENGINE_DLL="$CX/lib/wine/x86_64-windows/advapi32.dll"
BOTTLE_DLL="$BOTTLE/drive_c/windows/system32/advapi32.dll"
STOCK_SHA=0375c2ee0e61be9fc3064a07b52f65e73d8a74b06fd06b59ea9e958365a33940
PATCHED_SHA=754810ac719c869aeec9a4d94b9ae0591e3b27abeb77c88bf8bd6ac8fd71ae5e
shasum -a 256 "$ENGINE_DLL" "$BOTTLE_DLL"
```

If both DLLs already match `PATCHED_SHA`, the patch is installed. For a fresh installation, both should match `STOCK_SHA`. If one is already patched and the other is stock, use the stock file as the generator input and review both targets before applying. A different hash requires identifying the correct runtime or stock file before proceeding.

```sh
WORK="$(mktemp -d "${TMPDIR:-/tmp}/fh4-online.XXXXXX")"
PATCHED="$WORK/advapi32.dll"
python3 10-online/patch_credential_enumeration.py "$ENGINE_DLL" "$PATCHED"
python3 tools/copy_checked.py "$PATCHED" "$ENGINE_DLL" --expected "$STOCK_SHA" --source-sha256 "$PATCHED_SHA"
python3 tools/copy_checked.py "$PATCHED" "$BOTTLE_DLL" --expected "$STOCK_SHA" --source-sha256 "$PATCHED_SHA"
```

The generator requires a stock input. If the engine DLL is already patched, substitute the matching stock bottle DLL or a verified original backup in the generator command. Review both dry runs before applying.

## Apply and verify

```sh
python3 tools/copy_checked.py "$PATCHED" "$ENGINE_DLL" --expected "$STOCK_SHA" --source-sha256 "$PATCHED_SHA" --apply --game-closed &&
python3 tools/copy_checked.py "$PATCHED" "$BOTTLE_DLL" --expected "$STOCK_SHA" --source-sha256 "$PATCHED_SHA" --apply --game-closed &&
shasum -a 256 "$ENGINE_DLL" "$BOTTLE_DLL"
```

Record each backup path printed by the copy helper. Both installed hashes should match `PATCHED_SHA`. If the first copy succeeds and the second fails, restore the first from its verified backup before launching. After a successful installation, launch Steam and FH4 and check an online feature.

## Roll back

Quit CrossOver and stop its bottle applications. Set the two backup variables to the exact original files saved by the installer. Restore only the targets changed by your installation.

```sh
ENGINE_BACKUP="/absolute/path/to/engine/advapi32.dll.bak_fh4_archive_..."
BOTTLE_BACKUP="/absolute/path/to/bottle/advapi32.dll.bak_fh4_archive_..."
python3 tools/copy_checked.py "$ENGINE_BACKUP" "$ENGINE_DLL" --expected "$PATCHED_SHA" --source-sha256 "$STOCK_SHA" --apply --game-closed &&
python3 tools/copy_checked.py "$BOTTLE_BACKUP" "$BOTTLE_DLL" --expected "$PATCHED_SHA" --source-sha256 "$STOCK_SHA" --apply --game-closed &&
shasum -a 256 "$ENGINE_DLL" "$BOTTLE_DLL"
```

Both restored files should match `STOCK_SHA`.
