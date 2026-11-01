# Aligned-buffer memory fix V1.1

**User-confirmed working, 1 November 2026.** The current `02-rendering` DLL
includes this repair together with the existing geometry, world/speed-zone and
drift V3 fixes. The exact gameplay-tested DLL was copied into this repository;
it was not replaced by a new build during packaging.

| Item | Supported/tested value |
| --- | --- |
| FH4 | 1.478.564.0, AMD64; exact EXE checksum checked by installer |
| CrossOver | 26.3.0.39832 |
| Graphics runtime | GPTK 4.0 beta 3 / D3DMetal, Metal 4 enabled in the tested setup |
| Current shim SHA-256 | `f3038fdc146ff740fb2070d39b16945623fae3958aab963f995ae13f79d25b51` |
| Matched installed apd12.dll | `32c942ed368a45e58d35f4da3de3776d1f74e785731de9cf6a5e705a2925488a` |
| Kill switch | Set `HB_TSSHIM_FIX_ALIGNED_HEAP=0` before launch |

Start with [INSTALL.md](INSTALL.md). [BACKUP_AND_ROLLBACK.md](BACKUP_AND_ROLLBACK.md)
covers restoration; [FINDINGS.md](FINDINGS.md) distinguishes measured results
from the user's confirmation; [verification/README.md](verification/README.md)
explains the self-contained tests and optional rebuild.

## What it repairs

The game's allocator sometimes passed a 32-byte-aligned interior pointer to
HeapFree instead of the original HeapAlloc pointer. Wine rejected these calls,
leaving the underlying buffers allocated. The trace directly recorded 1819 such
failures in ten seconds, concentrated in 4/8 KiB C++ container buffers.

V1.1 observes the game's allocation/free import data pointers. It requires the
supported image, exact runtime signatures, original API targets, call sites,
heap, request size and a bounded live allocation record. It releases the raw
pointer only after validating alignment, heap-block validity, requested size
and the stored raw back-pointer. Unknown inputs retain the original API behavior.
It does not edit the EXE file or add a game instruction patch.

The table is fixed at 1 MiB; the log is capped at 96 lines. Full-table cases drop
tracking rather than guessing. After activation the shim is pinned until exit,
so a restart is required for binary rollback or a changed kill switch.

The supported IAT page includes `PAGE_EXECUTE_WRITECOPY` (0x80). V1.1 preserves
execute permission during its two data-pointer writes and restores the original
protection. The first V1 candidate safely declined that page type; it is not
distributed here.

## Package contents

- `../02-rendering/{bottle,source}/d3d12.dll`: identical current release copies.
- `../02-rendering/source/aligned_heap_fix.h`: repair and activation guards.
- `../02-rendering/source/d3d12.c`: three integration additions; prior fixes retained.
- `install.py`: parameterized dry-run installer, verified backups, two-target
  transaction recovery and dry-run rollback. Python 3.9+; no compiler needed.
- `build.py`, `run_tests.py`, `verification/`: optional source build and fixtures.
- `memory-leak-v1.1.patch`: source diff against V3 commit
  `626b1c7`. Source is already integrated; do not apply it again to this checkout.
- `validation.json`: portable summaries, source/binary provenance and owner report.

No local game EXE, memory dump, full runtime code page, helper EXE, allocator log,
save, personal bottle configuration or old diagnostic deployment manifest is
needed or bundled. The COW test builds its own inert PE fixture.

## Scope of the working claim

The repair is active and successfully frees the observed buffers. During a
41.5-minute interval spent mostly driving, measured occupied regular-heap usage
stayed roughly flat. Total macOS footprint still rose; free heap capacity and
other caches can remain resident. This is not a claim that all memory growth
or every possible leak is eliminated. The higher-level reason for the original
pointer mismatch, and behavior on different builds/runtimes, remain unresolved.

Operations from other modules/direct NTDLL calls are outside this IAT observer's
bookkeeping. The checks cannot prove ownership after arbitrary out-of-band
free/reuse with identical metadata. Preserve the guards; do not generalize this
into a blanket pointer[-1] recovery or subtraction from failed frees.
