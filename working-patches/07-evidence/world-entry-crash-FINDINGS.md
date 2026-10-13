# Free-roam call reaches rewritten code without an observed restoration

The bounded recorder captured the original illegal instruction at the same function entry as earlier crashes. It observed the complete 64-byte sample changing from valid code to the known invalid sequence, followed 50.313 seconds later by a direct call into that sequence. No intervening change to the watched sample was observed.

## Timeline

Windows PID 2616, normal game shortcut. The helper first observed all 64 known valid bytes read-only, then loaded the separate diagnostic DLL and armed the recorder. Its start event records wine_debugger=0 and original page protection PAGE_EXECUTE_WRITECOPY (0x80).

| UTC on 2026-10-12 | IST | Observation |
| --- | --- | --- |
| 09:04:41.741–09:04:41.744 | 14:34:41.741–14:34:41.744 | Thread 2644 executes 17 stores from writer addresses 0x140710939 and 0x14979f38c, transforming the entire sampled 64-byte entry into the known invalid state. |
| 09:05:32.057 | 14:35:32.057 | Thread 2648 raises illegal instruction at 0x140d05da0 with that exact 64-byte state still present. |

The captured writes form a consistent chain: every before-sample equals the previous after-sample. The last after-sample equals the fault sample and the prior raw crash's 64-byte sample exactly. These are 17 stores changing the watched sample; writes elsewhere on the page or in the rest of the function are not enumerated by this log.

At the fault, the first stack qword is 0x140d04d8a, the return address from the known direct call at 0x140d04d85. RCX and RDI both contain nonzero 0x231a1f780. EFLAGS is 0x287, with the recorder's single-step trap flag clear. The original illegal-instruction event was logged and returned EXCEPTION_CONTINUE_SEARCH, without suppressing it or changing its context.

The game subsequently generated CrashReport.xml identifying an unhandled exception, the same initial stack RVAs, RP_FREEROAM, 89.921005 seconds uptime and 0.033 seconds driving time. The report's DATE_TIME is 2026/10/12-14:35.32. D3D_HR_ERROR is zero in this report. The user independently reported the crash.

## What the benchmark comparison changes

The previous passive capture completed a benchmark while the same invalid code sample was present afterward, with no restoration observed at 50 ms sampling intervals. Therefore the presence of invalid code alone does not establish an immediate global failure. The free-roam execution path reaches it; the benchmark result does not establish whether that path ran.

The normal caller references Road Discovered, Notification Shown and Road Discovery strings. Together with the crash-report notification-state clues, that supports a road-discovery notification-related call path. It does not establish that disabling notifications would be a correct or sufficient fix; no such change was made.

## What remains unknown

This narrows the operational failure to a call into rewritten code without an observed successful restoration beforehand. It does not establish whether the overwrite is normal protective re-encoding, a wrong transformation, or a state selected after a failed check. It also does not identify the background callback's trigger or prove DRM rejection. OnlineFix64.dll remains an upstream confounder without an independent clean reference.

The writer stack again contains the candidate continuation 0x1490fa1cb and range end 0x140d060e9. That range end matches the faulting function's PE runtime-function boundary. A bounded inspection of the previously captured continuation region shows scattered jump-based control flow; a linear disassembly cannot supply a validated upstream execution history. The remaining useful evidence is the callback/condition that selects this function and produces the replacement buffer, or a clean/Windows comparison of that behavior. Repeating the same world-entry crash without a new observation point would not answer that question.

The recorder changes page permissions and timing. Another thread can write while the page is temporarily writable for a captured store, so absence of an observed restoration is not absolute proof of zero intervening writes. The unchanged fault sample, complete prior write chain, passive reproduction and same game crash report support this interpretation while preserving that limitation.

## Process termination and cleanup

The game terminated before the recorder's normal timeout cleanup, so this real log has no finished event. The helper reports recorder_wait=0 and recorder_exit=0xc0000354 and returns 13 because its recorder thread did not finish normally. This does not invalidate the already flushed write and original-fault evidence. The subsequent process check confirmed that Forza, the helper and the launcher were no longer running; their private diagnostic memory and page protections ended with the process. Do not claim a successful explicit restore/handler removal event for this run.

The original Steam bottle AeDebug handler remains winedbg --auto %ld %ld, Auto 1, for both registry views. No game instructions, graphics settings, shader rules or protection checks were patched. No recorder remains armed from this run.

## Evidence

- forza-2616-76245478.jsonl: 17 write events and their raw contexts, original target exception, complete fixed-size Win64 CONTEXT and bounded stack.
- fault-context.bin, fault-stack.bin, fault-function.bin, fault-registers.json: extracted fault evidence.
- CrashReport.xml: preserved report from this launch.
- capture-comparison.json: independent byte-chain, original-crash, stack-return, context and report comparisons.
- launcher-output.log and run-state.json: helper completion and user-reported phase/outcome.
- ../benchmark-passive/capture-20261012T084652Z/FINDINGS.md: completed benchmark comparison and caller-region analysis.
