# Install the combined memory-fix shim

This is a shim-only upgrade for an already-working FH4 setup using the exact
tested CrossOver/GPTK versions in [README.md](README.md). The installer updates
the two existing `d3d12.dll` files as a pair. It preserves the working launch DLL,
provider/runtime, settings, registry and saves. It never starts or stops apps.

## 1. Select paths and verify the archive

From the repository root:

```sh
cd working-patches
python3 tools/verify_archive.py
CX='/Applications/CrossOver.app/Contents/SharedSupport/CrossOver'
BOTTLE="$HOME/Library/Application Support/CrossOver/Bottles/New Bottle"
GAME='/absolute/path/to/Forza Horizon 4'
```

For an extracted release without `.git`, use `shasum -a 256 -c SHA256SUMS` instead of the Git-inventory verifier. The installer itself does not require Git.

Replace `BOTTLE` and `GAME` with your actual directories. `--cx` accepts the
CrossOver runtime directory shown above, including for a custom app location.
Python 3.9+ is sufficient; no build tools are needed for the released DLL.

The installer verifies the CrossOver bundle build, exact supported game hash,
both provider hashes and both existing shim hashes. Supported upgrade sources
are the repo's V3 shim (`833b9642…`) and pre-drift road/speed shim (`bc9d107e…`).
Already-current copies are accepted; unknown variants are refused. This is not
a bootstrap installer for arbitrary stock runtimes: use the base setup guide
first, with the matched GPTK 4 provider already installed.

The game checksum must be:

```text
b838a79a65c6d1e12f46deaba3584e4c88ebccdfdc4214b4535ece4ba3c0abbe
```

The provider at both locations must be the already-working renamed GPTK 4.0
beta 3 `apd12.dll`, SHA-256 `32c942ed368a45e58d35f4da3de3776d1f74e785731de9cf6a5e705a2925488a`.
The older `02-rendering/bottle/apd12.dll` in this historical archive is **not**
the tested provider for this upgrade. Do not copy it over the working runtime.
The required proprietary runtime comes from your installed CrossOver/GPTK setup.

## 2. Dry-run the upgrade

```sh
python3 09-memory-leak/install.py --cx "$CX" --bottle "$BOTTLE" --game "$GAME"
```

This performs only reads and prints the two paths and checksums. If both already
match, it reports `Already installed` and changes nothing. A mismatch is a reason
to identify your game/runtime/shim version, not to remove the guard.

## 3. Close the game, then apply

Close FH4, Steam and bottle applications yourself. A loaded process keeps its
old DLL; changing the file does not update an existing game session.

```sh
python3 09-memory-leak/install.py --cx "$CX" --bottle "$BOTTLE" --game "$GAME" \
  --apply --game-closed
```

`--game-closed` asserts that the applications are closed. The installer also
checks macOS process names for FH4; it does not run a Windows process helper or
terminate anything. If process enumeration fails or finds FH4, it refuses.

Backups default to a unique folder under `$BOTTLE/fh4-patch-backups`. An optional
`--backup-dir '/path/to/backups'` selects a different parent. The installer backs
up and verifies both original DLLs before changing either, publishes a recovery
manifest, atomically replaces each target, then verifies the final files and
unchanged game/providers. The pair is a recoverable transaction, not one atomic
filesystem operation. If a step fails, it attempts to restore all changed targets.
An externally changed target is not overwritten during recovery.

Save the printed **Rollback manifest** path. Application-file permissions may
require adjusting write access; do not blindly run the whole workflow as root.
If recovery reports `recovery_required`, retain the manifest/backups and resolve
that state before launching. See [BACKUP_AND_ROLLBACK.md](BACKUP_AND_ROLLBACK.md).

## 4. Verify installation and activation

```sh
shasum -a 256 \
  "$CX/lib64/apple_gptk/wine/x86_64-windows/d3d12.dll" \
  "$BOTTLE/drive_c/windows/system32/d3d12.dll"
```

Both must match:

```text
f3038fdc146ff740fb2070d39b16945623fae3958aab963f995ae13f79d25b51
```

Retain the existing D3DMetal, explicit input offset and world/drift settings.
There is no new registry setting or required environment edit. The heap fix is
enabled by default; `HB_TSSHIM_FIX_ALIGNED_HEAP=0` disables it before launch.

Launch normally, reach free roam, then inspect the newest per-process file:

```text
$BOTTLE/drive_c/highball/logs/aligned-heap-fix-<Windows PID>.txt
```

Look for `FH4_ALIGNED_HEAP_FIX v1.1: installed guarded allocation-provenance repair`
and increasing `repaired` totals while driving. A disabled/unsupported or timeout
message is not activation. Logs stop after 96 lines by design; a stopped log by
itself does not mean the repair stopped. These logs work without verbose graphics
debugging. Remove `HB_TSSHIM_DEBUG` to disable verbose graphics logs; the string
`"0"` still enables that separate debug flag.

Check normal rendering, races, speed zones and drift scoring/exit/re-entry. For
memory comparison, repeat the same route and distinguish occupied heap usage
from retained free capacity. Cumulative `requested_bytes` is allocation traffic,
not an amount of resident RAM saved. Existing free pages need not immediately
leave the process footprint.

## Optional source build

Use [verification/README.md](verification/README.md). Build into `09-memory-leak/build`;
do not overwrite the archived tested DLL. Rebuilt DLLs may have different hashes
and are not accepted by this release installer without a separately reviewed
release update.
