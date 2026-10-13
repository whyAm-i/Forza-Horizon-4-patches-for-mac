# Repository and archive layout

[← Back to README](../README.md)

This page describes the files committed to Git. The archive manifest and checksum list cover the same tracked package files. Runtime logs, Shader Lab output, OS metadata, and locally ignored historical captures are not part of a checkout.

```text
.
├── README.md
├── docs/
│   ├── ANATOMY_OF_PATCHES.md
│   ├── CROSSOVER_PROVENANCE_AUDIT.md
│   ├── PITFALLS.md
│   ├── REPOSITORY_LAYOUT.md
│   ├── ROLLBACK.md
│   ├── SHADER_LAB.md
│   └── VERIFICATION_AND_DIAGNOSTICS.md
└── working-patches/
    ├── 01-launch-package/
    │   ├── README.md
    │   └── binaries/kernelbase.dll
    ├── 02-rendering/
    │   ├── README.md
    │   ├── bottle/{apd12.dll,d3d12.dll}
    │   ├── source/                    # proxy source and build dependencies
    │   └── verification/              # source probes and checked-in results
    ├── 03-settings/                  # settings and configuration references
    ├── 04-shader-lab/                 # tool, launcher, and documentation
    ├── 05-game-support/               # captured support files
    ├── 06-history/README.md           # history overview only
    ├── 07-evidence/                   # reports, notices, licenses, and provenance
    ├── APPLY.md
    ├── MANUAL_INSTALLATION.md
    ├── README.md
    ├── SHA256SUMS
    ├── manifest.json
    └── tools/{copy_checked.py,verify_archive.py}
```

## What is and is not included

- `01-launch-package` and `02-rendering` contain the two retained patched DLLs, their required runtime provider, current source, and selected verification artifacts. Previous shim binaries and launch experiments are excluded from Git.
- `03-settings` contains the current settings snapshots and references. Older game-setting backups and alternate registry exports are excluded.
- `04-shader-lab` contains the utility and its source. Census data, observations, test output, and logs are generated or local artifacts and are excluded from Git.
- `05-game-support` contains the captured support files. These are not independently established crash fixes.
- `06-history` contains its overview only. Historical scripts and snapshots are excluded from Git and must not be applied.
- `07-evidence` contains the checked-in investigation reports and notices. Runtime logs and other local diagnostic captures are excluded.
- `manifest.json` and `SHA256SUMS` cover the tracked package files. `tools/verify_archive.py` verifies this Git-delivered set; ignored local files do not affect the result.

The installed CrossOver 26.3 Apple GPTK runtime remains a prerequisite and is not bundled here. See [the provenance audit](CROSSOVER_PROVENANCE_AUDIT.md) for the dependency rationale.
