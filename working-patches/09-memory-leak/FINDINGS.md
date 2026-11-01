# Evidence and validation scope

## Original failure

A filtered allocator trace on 31 October 2026 paired 493998 events from the game
process over 10.006 seconds, with zero unmatched returns or unfinished frames.
For tracked large-container request sizes, it found 3002 allocations: 1168 raw
pointer frees succeeded, 1819 aligned interior pointer frees explicitly failed,
and 15 had no free observed in the short window. The last group is not classified
as a leak. Failed requests totaled 8381725 bytes in that window.

One observed pair allocated raw pointer `0x5955a050`, then attempted to free
`0x5955a060` on the same heap; RtlFreeHeap returned zero. Every tracked failure
used an aligned pointer 16 or 32 bytes inside its raw allocation. The allocator
API return sites were game RVAs `0x37fe7c3` and `0x37fe9ad`; these are return
addresses, not function entries.

HeapFree requires the original heap allocation pointer. The observed buffers
match Microsoft STL's large-buffer alignment/back-pointer pattern. This proves
the immediate failed-free mechanism, not why the higher-level game path lost
the raw pointer, or that the same behavior occurs on native Windows. No direct
hook of those heap paths was found in the earlier rendering/drift source.

## V1.1 safeguards and correction

The guarded repair tracks only the supported caller/heap/size combinations.
Release requires a current record and matching alignment/back-pointer/heap size.
Table allocation is bounded, unrecognized frees pass through, and failed raw
releases are still failures. The EXE file is untouched. Two IAT data slots are
replaced; the previous drift entry/vtable changes remain separate and unchanged.

The first live V1 candidate declined activation because its protection guard did
not accept the game's `PAGE_EXECUTE_WRITECOPY` IAT region. Runtime signatures and
API targets already matched. V1.1 accepts the recorded page type, preserves its
execute permission while updating data pointers, and restores protection 0x80.
The portable COW fixture now tests that exact case and surrounding bytes.

The observer's lock serializes its own operations. HeapLock does not extend
coverage to every external call: Wine LFH fast paths can run outside the main
heap critical section. Out-of-band free/reuse with identical metadata cannot be
ruled out by these checks. This is a compatibility repair, not general allocator
hardening or a blanket recovery for arbitrary invalid pointers.

## Live measurements, 1 November

All three samples were from the same game process lifetime:

| Sample | macOS footprint | Windows private commit | Ordinary used + occupied LFH slots |
| --- | ---: | ---: | ---: |
| Parked baseline | 8.304 GB | 2.023 GB | 813.6 MB |
| Lap 1 | 9.656 GB | 2.616 GB | 926.3 MB |
| Lap 2 | 10.135 GB | 2.856 GB | 915.0 MB |

Lap 1 to lap 2 was 41.51 minutes; the user reported driving for most of it.
Measured occupied regular-heap usage decreased about 11.4 MB. Free heap capacity
increased about 219.4 MB. The unlocked censuses completed without metadata
issues and exclude separate large allocations/other heaps.

A later direct read of the candidate's counters found 559372 successful repaired
frees, 2552086740 cumulative requested bytes, zero tracking drops and zero
metadata rejections. Approximately 505 tracked records remained. This counter
snapshot was not atomic with the memory inventory. Cumulative traffic is not
resident memory saved. The log intentionally caps at 96 lines; direct readings
confirmed the repair continued after that cap.

The user's subsequent report was **“the patches work.”** This establishes the
observed user-confirmed result alongside instrumentation. No lap 3 capture or
separate comprehensive race/event regression was recorded for this heap revision.
Total footprint still grew, so these measurements do not prove that every leak
or all cache/capacity growth has been eliminated.

## Packaging checks

The shipped DLL is byte-identical to the installed, gameplay-tested V1.1 build.
The source is the V3 source plus the new header and three integration lines;
earlier rendering/drift headers are unchanged. The included patch reproduces
those changes against V3 commit `626b1c7`.

Portable allocator tests use real Wine heap calls in a synthetic process and an
original inert PE fixture, with no FH4 EXE or dump dependency. They cover both
read-only and executable COW IAT pages, exact return-address capture, guards,
metadata failures, ordinary frees, table saturation and 80000 concurrent pairs
per page type. Invalid inaccessible-pointer cases run in isolated child
processes and preserve the original NTDLL fault behavior.

The installer is separately tested against temporary files, including dry run,
idempotence, backup/rollback, unknown states, symlinks, source changes and failure
after the second target was replaced. See `validation.json` for recorded results.
