# Captured settings, not a universal preset

cxbottle.conf.snapshot is an exact copy of this Steam bottle. Use its values as reference and preserve unrelated settings when editing an existing bottle. It contains machine-specific paths and a duplicate historical DXMT_ENABLE_NVEXT key. Do not replace another bottle's full configuration with it or reproduce the duplicate; use unique keys.

The geometry fix is enabled and binding capture is disabled. Disabling capture did not fix the world-entry crash. `WINEMSYNC=1` and `D3DM_MTL4=0` are current choices, not proven remedies for it. Vendor/device spoofing and MetalFX-disabled state are captured as compatibility context; their individual necessity was not demonstrated here.

`game/UserConfigSelections` is the current complete game configuration and the neighbouring `.bak_*` files are prior authored/experimental baselines. None are save-game files. The confirmed vertex-offset fix does not require crowd or vegetation suppression. Old zero-density/zero-LOD/crowd-off/terrain experiment settings were reverted. The grass/water quality comparison remains unconfirmed; do not promote it to a working patch.

`skip.txt` enables the entire scene (comment-only). Restore it only after backing up a live file and closing any shader-test session. `wine-registry-reference.txt` captures only selected relevant keys and is not a registry import file. Its active RetinaMode is y; historical-retina-off.reg is an earlier alternative, not current state. Do not import it by default.
