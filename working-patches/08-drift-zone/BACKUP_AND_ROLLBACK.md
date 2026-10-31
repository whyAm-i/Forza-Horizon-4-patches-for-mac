# Backup and rollback

Close FH4, Steam and bottle applications before installation or restoration. Restore the two corresponding DLL backups as a pair. Process-memory changes disappear when that game process exits; no patched EXE file needs restoration.

## Separate verified backup

After setting `CX`, `BOTTLE`, `ENGINE_DLL`, and `BOTTLE_DLL` as in [INSTALL.md](INSTALL.md):

```sh
TASK_BACKUP="$HOME/FH4-drift-backup-$(date +%Y%m%d-%H%M%S)"
mkdir "$TASK_BACKUP"
cp -p "$ENGINE_DLL" "$TASK_BACKUP/engine-d3d12.dll"
cp -p "$BOTTLE_DLL" "$TASK_BACKUP/bottle-d3d12.dll"
cp -p "$BOTTLE/cxbottle.conf" "$TASK_BACKUP/cxbottle.conf"
(cd "$TASK_BACKUP" && shasum -a 256 engine-d3d12.dll bottle-d3d12.dll cxbottle.conf > SHA256SUMS)
(cd "$TASK_BACKUP" && shasum -a 256 -c SHA256SUMS)
```

Keep this directory and its checksum list outside the patch package. Save the original target hashes printed during installation. The existing `copy_checked.py` helper additionally creates an adjacent backup named `d3d12.dll.bak_fh4_archive_<timestamp>_<unique suffix>` before each changed copy. An already-matching target is left alone and does not generate another backup.

## Hash-checked restoration

From `working-patches`, set the backup paths and **their actual recorded hashes**:

```sh
ENGINE_BACKUP="$TASK_BACKUP/engine-d3d12.dll"
BOTTLE_BACKUP="$TASK_BACKUP/bottle-d3d12.dll"
ENGINE_BACKUP_SHA='REPLACE_WITH_RECORDED_ENGINE_BACKUP_SHA256'
BOTTLE_BACKUP_SHA='REPLACE_WITH_RECORDED_BOTTLE_BACKUP_SHA256'
V3_SHA='833b964203e1e157a162852863656e0e4934ef10a3659d3047ebd0422b529f66'
python3 tools/copy_checked.py "$ENGINE_BACKUP" "$ENGINE_DLL" \
  --source-sha256 "$ENGINE_BACKUP_SHA" --expected "$V3_SHA"
python3 tools/copy_checked.py "$BOTTLE_BACKUP" "$BOTTLE_DLL" \
  --source-sha256 "$BOTTLE_BACKUP_SHA" --expected "$V3_SHA"
```

Review both dry runs, then repeat each command with `--apply --game-closed`. The helper also backs up the current V3 target before restoring the older copy. Verify both final hashes against the saved backup list before restarting. If a live target no longer has V3's hash, investigate later changes before restoring anything.

Only restore `cxbottle.conf` if you changed it for installation and have reviewed any later desired edits. This shim-only upgrade does not require overwriting configuration, saves, providers, or the complete bottle.

## Local investigation backups retained on the test machine

These paths are relative to the original game folder and are not distributed binaries:

| Local backup | State | SHA-256 |
| --- | --- | --- |
| `drift-zone-work/backup-20261030-193739/{engine,bottle}-d3d12.dll` | Pre-drift repo road/speed-zone shim | `bc9d107e70aeb7e401b77303896b17d53c274fd92415b181f58ace378fb81a44` |
| `drift-zone-work/backup-followup-20261031-114550/{engine,bottle}-d3d12.dll` | V2.1 immediately before V3; prevented the crash but left zone mode stuck | `a8ca1765ef02654fd7fc4c1871ac1df57e2c51f189e5f01c0d166b3442bdfaea` |

V2.1 is retained as failed-experiment evidence, not a recommended working rollback. The old pre-drift source and DLLs also remain available in Git commit `a5b6f5df77f657965722d899a73899a90b8484e0`. Restoring that baseline removes the drift fix and can reintroduce the drift exit crash.
