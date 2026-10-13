# Forza Horizon 4 on Apple Silicon

---

## Current Status

Forza Horizon 4 previously failed on Apple Silicon at three critical checkpoints:
1. **Launch Deadlock:** Infinite CPU loop during boot due to unhandled UWP package queries in the exe.
2. **Polygon Stretching:** Exploded vertex spikes across outdoor foliage and geometry due to a D3DMetal packed input layout calculation bug.
3. **Open-World Free Roam Crash:** Illegal instruction crash ~0.035s after entering Free Roam upon crossing any road boundary.

**All three issues are now resolved:**

| Component / Issue | Status | Implementation |
|---|---|---|
| **DRM / Launch Loop** | **Resolved** | Native x86-64 assembly code cave in `kernelbase.dll` emulating UWP VCLibs package family response. |
| **Polygon Stretching** | **Resolved** | Custom D3D12 proxy shim resolving `D3D12_APPEND_ALIGNED_ELEMENT` to explicit byte offsets (passed 384/384 GPU checks). |
| **Open-World Crash** | **Resolved** | Non-invasive Vectored Exception Handler (VEH) in `d3d12.dll` intercepting `0xC000001D` at `0x140D05DA0` and safely returning to caller `0x140D04D8A`. |
| **D3DMetal Export Ordinals** | **Resolved** | Custom `d3d12.def` preserving Ordinal 100 (`GetBehaviorValue`) and Ordinal 101 (`D3D12CreateDevice`). |
| **Free Roam Gameplay** | **Verified Working** | Spawns into open world, discovers roads, and drives continuously across sessions with zero crashes. |

---

## How to Install

### Prerequisites

1. **Apple Silicon Mac(M series)**.
2. **macOS Golden Gate 27+**.
3. **CrossOver 26.3 with Gptk4** (Build `26.3` tested).
4. **Game Installed** Legitimate copy of Steam FH4.

---

### Installation

The patch binaries and install tools are located in `working-patches/`. Choose between automated AI-assisted deployment (Method A) or manual step-by-step terminal installation (Method B). Review the expected hashes and configuration before making changes. Do not apply these binaries to another CrossOver release.

---

### Method A: Install with AI (Recommended)

If you are using an AI coding assistant (such as Antigravity, Cursor, Claude Code, or GitHub Copilot):

1. **Place Patches / Open Project:** Open this repository in your AI coding assistant workspace. If downloading a release archive (e.g., `patches.zip`), drop the zip file inside the `working-patches/` folder (or use the pre-extracted files already present in `working-patches/`).
2. **Quit CrossOver:** Completely quit CrossOver and ensure all bottle processes are stopped.
3. **Prompt the AI:** Copy and paste the following prompt into your AI coding assistant:

> *"Please apply the Forza Horizon 4 patches to my CrossOver Steam bottle and CrossOver engine following the instructions in `working-patches/MANUAL_INSTALLATION.md`. If a patches zip file is present in `working-patches/`, extract it first. Back up existing files, deploy the patched `kernelbase.dll` and `d3d12.dll`, ensure `apd12.dll` is present, configure the `cxbottle.conf` environment variables, and verify file checksums."*

The AI assistant will autonomously:
- Auto-detect your CrossOver application (`$CX`) and Steam bottle (`$BOTTLE`) paths.
- Create timestamped safety backups (`*.bak_fh4_archive_*` and `cxbottle.conf.bak`).
- Deploy the patched `kernelbase.dll` (launch / DRM loop fix) and `d3d12.dll` (vertex stride fix & VEH open-world crash bypass).
- Verify the D3DMetal provider (`apd12.dll`) and Unix symlinks.
- Configure `cxbottle.conf` environment variables (`CX_GRAPHICS_BACKEND`, `HB_D3D12_REAL`, `HB_TSSHIM_EXPLICIT_INPUT_OFFSETS=1`, `HB_TSSHIM_FIX_WORLD_ENTRY=1`).
- Run checksum verification against `working-patches/SHA256SUMS` to confirm a successful installation.

---

### Method B: Human Purist (Manual Installation)

For manual terminal deployment using hash-checked scripts, bottle configuration details, or compiling from source:

**[Step-by-Step Manual Installation Guide](working-patches/MANUAL_INSTALLATION.md)**

---

## Community Feedback & Reporting Issues

Faced a bug which is not listed here(which is basically almost 30% of the textures right now)?

### What Needs to be Tested
- **Hardware Variety:** Performance and stability reports on base M1/M2/M3/M4 chips, as well as Pro, Max, and Ultra variants.
- **Peripherals:** Controller feedback (Xbox, DualSense), force-feedback steering wheels, and keybinding reliability.

### How to Report an Issue
If you encounter a crash (repeatedly, not a one off thing), graphical glitch, or unexpected behavior:
1. **Search Existing Issues:** Check the [GitHub Issues](https://github.com/whyAm-i/Forza-Horizon-4-patches-for-mac/issues) tab to see if the problem has already been reported.
2. **Open a New Issue** with the following details:
   - **System Specs:** Mac model, Apple Silicon chip (e.g. M2 Max) and macOS version.
   - **CrossOver Details:** Exact CrossOver version and bottle settings.
   - **Game Settings:** Resolution, graphics preset, MSAA setting.
   - **Reproduction Steps:** What car were you driving, which road or event triggered the issue, and how consistently it occurs.
   - **Log Files:** Attach `$BOTTLE/drive_c/highball/logs/tsshim-<pid>.log` and CrossOver run logs.

---

## Documentation & Deep Dives

For technical architecture, troubleshooting, and advanced tools, refer to the dedicated guides in `docs/`:

- **[Manual Installation Guide](working-patches/MANUAL_INSTALLATION.md)** — Step-by-step terminal commands, hash-checked copy helpers, and source compilation.
- **[Anatomy of the Patches](docs/ANATOMY_OF_PATCHES.md)** — In-depth breakdown of the code cave, vertex layout stride fix, and VEH exception handler.
- **[Repository & Archive Layout](docs/REPOSITORY_LAYOUT.md)** — Detailed file hierarchy and directory descriptions.
- **[Verification & Runtime Diagnostics](docs/VERIFICATION_AND_DIAGNOSTICS.md)** — Checksum validation and log verification steps.
- **[Shader Lab Diagnostic Suite](docs/SHADER_LAB.md)** — Interactive live shader inspection and PSO bisection tool.
- **[Rollback Instructions](docs/ROLLBACK.md)** — How to cleanly revert patches using automatic backups or stock DLLs.
- **[Critical Pitfalls & What NOT to Do](docs/PITFALLS.md)** — Essential safety rules to avoid crashing or corrupting game files.
- **[CrossOver Provenance & Dependency Audit](docs/CROSSOVER_PROVENANCE_AUDIT.md)** — Exact retained/deleted CrossOver-derived artifacts and the dependency rule used.
