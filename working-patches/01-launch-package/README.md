# Launch/package-family compatibility workaround

The retained patched CrossOver binary changes only 135 bytes in 64 differing runs relative to the audited original. The same retained `binaries/kernelbase.dll` is used for both the engine and Steam-bottle targets; the duplicate bottle copy, stock original, and generated binary-delta dump were removed because the working install does not consume them.

The installed GetPackagesByPackageFamily entry starts at raw offset `0x2c230` with `E9 EB F0 05 00`. It jumps to the captured code cave at raw offset `0x8ab20`; the PE section mapping must be respected when calculating the actual jump address. A section characteristic changes from read/write to executable/read/write. The payload reports one VCLibs package identity:

`Microsoft.VCLibs.140.00_14.0.30704.0_x64__8wekyb3d8bbwe`

The UTF-16 string is 56 characters including its terminating null, not 57. The captured historical machine-code payload reports/copies a 57-WCHAR buffer, including adjacent zero padding. Do not silently edit its bytes or infer a corrected version from conflicting old comments. This archive preserves the existing behavior; a new implementation needs separate validation.

This bypasses the observed launch/package-family retry path in this setup. It does not establish complete AppModel consistency, multiplayer compatibility or a solution to rewritten-code crashes.

Use the hash-checked installation in `../APPLY.md`. Historical assembly sources, payloads, test binaries, duplicate snapshots, and the stock CrossOver rollback binary are not included in the Git archive. Roll back with the timestamped backup created by `copy_checked.py` or reinstall the matching CrossOver build.
