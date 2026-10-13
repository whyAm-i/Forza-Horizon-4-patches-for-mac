# Shader Lab Diagnostic Suite

[← Back to README](../README.md)

---

## Overview

**Shader Lab** is an interactive macOS terminal utility located in `working-patches/04-shader-lab/Shader Lab.command`. It was created to analyze and isolate Direct3D 12 pipeline state issues without requiring game restarts.

Shader Lab enables live, zero-restart bisection and draw-suppression across **17,128 pipeline state objects** (733 VS, 2,412 PS, 4 GS, 3 HS, 3 DS, 100 CS) using the shim's hot-reloadable `skip.txt`.

---

## Enabling Hot-Reloading

To allow the shim to detect changes to `skip.txt` on the fly, ensure this variable is set in your `cxbottle.conf` under `[EnvironmentVariables]`:

```ini
"HB_TSSHIM_HOT_SKIP" = "1"
```

Also verify that debug logging is enabled if you need verbose shader binding output.

---

## Running Shader Lab

Launch the tool by double-clicking `working-patches/04-shader-lab/Shader Lab.command` in Finder or running from Terminal:

```bash
cd "working-patches/04-shader-lab"
./"Shader Lab.command"
```

### Main Menu Options

- **Menu 1 (Shader Inventory):** Review all loaded shaders stage by stage across vertex, pixel, geometry, hull, domain, and compute pipelines.
- **Menu 2 (Object Identification):** Identify on-screen geometry objects by toggling selective vertex shader suppression.
- **Menu 3 (Interactive Bisection):** Perform binary search bisection over suspected pipeline states to pinpoint problematic shaders.

### Interactive Hotkeys

| Hotkey | Action | Description |
|---|---|---|
| `b` | Baseline | Restore baseline full scene (no suppression). |
| `t` | Test | Reapply current active shader suppression test. |
| `f` | Freeze Recovery | Restore baseline immediately if the scene freezes or renders completely blank. |
| `q` | Quit | Clean up and restore default clean `skip.txt`. |

---

## Normal Gameplay Note

For normal gameplay sessions, Shader Lab is not required. Keep `drive_c/highball/skip.txt` empty (or comment-only) and `HB_TSSHIM_HOT_SKIP` disabled or set to `0` to maximize performance.