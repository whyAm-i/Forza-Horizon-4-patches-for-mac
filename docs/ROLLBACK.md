# Rollback Instructions

[← Back to README](../README.md)

---

For the current paired memory/shim installer, use [09-memory-leak/BACKUP_AND_ROLLBACK.md](../working-patches/09-memory-leak/BACKUP_AND_ROLLBACK.md). It creates a two-target manifest and verified backups; the instructions below cover the older single-file copy helper.

To restore the original credential DLLs in the engine and bottle, follow the [online patch rollback instructions](../working-patches/10-online/INSTALL.md#roll-back).

## Automatic Backups

Every time you run the deployment tool `copy_checked.py` with `--apply`, it automatically creates a uniquely timestamped backup file before overwriting anything:
```
<filename>.bak_fh4_archive_<timestamp>
```

Restore those backups to return to the exact files that existed before deployment. Stock CrossOver application binaries are intentionally not archived in this repository.

---

## Step-by-Step Rollback

Before reverting files, ensure CrossOver and all bottle processes are completely closed. Restore the exact timestamped backup created by `copy_checked.py` for each destination. The archive does not include previous shim binaries or stock CrossOver DLLs.

### Roll Back `kernelbase.dll`

Restore the exact timestamped `kernelbase.dll.bak_fh4_archive_*` backup made for each target. If a backup is unavailable, reinstall CrossOver 26.3 and recreate the bottle; do not restore an arbitrary binary from another build.

### Roll Back Bottle Configuration (`cxbottle.conf`)

If you backed up `cxbottle.conf` prior to editing:
```bash
cp "$BOTTLE/cxbottle.conf.bak" "$BOTTLE/cxbottle.conf"
```
