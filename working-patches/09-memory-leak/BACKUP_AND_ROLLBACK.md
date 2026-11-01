# Backup and rollback

Keep the exact manifest printed by `install.py`, its two DLL backups, and the
containing directory. Default location:

```text
$BOTTLE/fh4-patch-backups/memory-v1.1-<timestamp>-<unique-id>/
  manifest.json
  engine-d3d12.dll
  bottle-d3d12.dll
```

The record contains target paths, before/after checksums and unchanged game and
provider identities. Backups are installation-specific and are not committed to
this repository. The bundled release is a new combined shim; the historical V3
binary remains available through Git history, not as a second deployment target.

## Read-only rollback check

From `working-patches`, use your actual saved path:

```sh
RESTORE='/absolute/path/to/memory-v1.1-.../manifest.json'
python3 09-memory-leak/install.py --rollback "$RESTORE"
```

Rollback derives its targets from that record and verifies both current release
files, both exact backups, game hash, provider hashes and the expected target
layout. A changed destination or backup is refused. Do not edit hashes merely
to bypass a refusal.

## Restore after closing applications

Close FH4, Steam and bottle applications, then:

```sh
python3 09-memory-leak/install.py --rollback "$RESTORE" --apply --game-closed
```

The installer saves verified copies of the current shim in a `rollback-safety-*`
subfolder before restoring the two original files. If rollback fails partway,
it attempts to undo the rollback using those safety copies. The manifest records
`restored`, `transaction_undone`, or `recovery_required` as appropriate. Verify
the final two hashes against the intended state before restarting.

`transaction_undone` means the attempted transaction was reversed; inspect the
recorded state before retrying. `recovery_required` includes per-target errors.
Retain all files, review current hashes against the manifest, and use the exact
verified backups for manual recovery. The generic `../tools/copy_checked.py`
can restore an external backup with `--source-sha256` plus the reviewed current
`--expected` target hash; its two separate calls are not a pair transaction.
Do not launch with unresolved mixed versions.

Restoring pre-memory V3 removes this heap repair while retaining the earlier
drift/geometry fixes. Restoring the pre-drift road/speed shim also removes the
drift fix. A mixed pre-install state is restored exactly as recorded, so inspect
both original checksums rather than assuming they were identical.

For a temporary diagnostic comparison, set `HB_TSSHIM_FIX_ALIGNED_HEAP=0` before
a fresh launch. That disables only the heap repair in the current binary; it
does not restore DLL files. No EXE, saves, graphics settings, registry values or
provider files need restoring because this installer never changes them.
