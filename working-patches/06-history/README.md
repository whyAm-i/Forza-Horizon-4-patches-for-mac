# Historical files — do not apply as a batch

This folder preserves the original patch scripts, assembly/payload versions, shim revisions, proposed Wine source changes and grass/water quality comparison. Most scripts hardcode live paths under /Applications or old bottles, restore particular backups, lack whole-file hash guards, or were superseded. Read them as source history; use APPLY.md and the captured installed binaries to reproduce the current setup.

Excluded from the recommended configuration: crowd/foliage removal, geometry-shader/tessellation stripping, FP16/Shader Model/tiled-tier overrides, unvalidated D3DM/MPL flags, Metal-4 A/B settings, indirect-draw suppression, zero MSAA/snow experiments, render-scenario archive edits, and tentative grass/water quality changes. Some of these caused missing geometry or startup failures. Older change-log paragraphs saying “currently applied” were superseded by later rollback entries.

Debugging recorders, exception-rewrite experiments and temporary crash-handler changes are not included as working patches. Their outcome and cleanup are summarized in evidence. No recorder is armed by this archive.