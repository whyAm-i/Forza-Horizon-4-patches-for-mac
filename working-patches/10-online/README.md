# Online credential enumeration fix

**Owner-confirmed working on 6 November 2026** with FH4 under CrossOver 26.3 on Apple Silicon. This separate patch corrects `CredEnumerateW` in the x64 Wine `advapi32.dll`. See [installation and rollback](INSTALL.md).

## What failed

Xbox sign-in and the initial HTTPS requests succeeded, but FH4 repeatedly fetched device tokens and failed to reach Live configuration. The credential trace showed repeated `CredEnumerateW` failures. A read-only probe found that enumeration returned `ERROR_NOT_FOUND` (1168) even with a positive match count for the device key, device token, and title ticket. Direct `CredReadW` calls successfully read all eight Xbox entries in the probe.

Wine merges registry credentials and host credentials. On macOS, the host Keychain lookup can report `ERROR_NOT_FOUND` when its own search is empty. That status was causing the combined lookup to fail even when registry credentials matched. This matches [WineHQ bug 60617](https://bugs.winehq.org/show_bug.cgi?id=60617) and the error handling in [Wine's credential implementation](https://raw.githubusercontent.com/wine-mirror/wine/wine-11.0/dlls/advapi32/cred.c#L991-L1024).

## What the patch changes

The generator accepts the exact stock DLL by SHA-256 and checks the original instructions and empty code padding. It redirects the error path after the host enumeration IOCTL at RVA `0x1FBE8` to a 26-byte stub at RVA `0x245A0`. The stub converts host `ERROR_NOT_FOUND` into a successful lookup with zero additional matches. It then resumes the original `ERROR_MORE_DATA` comparison at RVA `0x1FBF3`. The caller still returns `ERROR_NOT_FOUND` when the combined result is empty.

This is a shared CrossOver runtime change. The verified deployment uses the same patched DLL in the engine and the selected bottle. The package generates that DLL from the local runtime and ships no stock or patched `advapi32.dll`.

## Verification

The staged probe verified the three specific Xbox lookups, the broad Xbox lookup, and a missing-entry control. After installation, a separate process successfully enumerated the device key, device token, and title ticket. The owner then reported online play was working flawlessly. See [validation.json](validation.json) for the observed results and exact hashes.

The experimental FH5-style Winsock wrapper was loaded during diagnosis, but its fallbacks never fired in the captured FH4 run. The credential patch has no dependency on that wrapper. The validation covers the reported setup and does not establish support for other CrossOver builds or every online mode.
