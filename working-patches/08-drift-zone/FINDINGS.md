# Drift-zone investigation and findings

Recorded 31 October 2026. Addresses are RVAs in the tested FH4 `1.478.564.0` image, base `0x140000000`. They are compatibility observations for this executable, not portable function offsets.

## Original failure

Finishing/leaving every drift zone crashed the game. Some intermediate attempts recorded points before the crash. Captures repeatedly showed access violation `0xC0000005` at RVA `0x8952AC`, with RSP/RBP pointing into executable memory rather than the real thread stack. The observed runtime entry of the containing routine at RVA `0x895250` begins with byte `0x5C` (`POP RSP`). Its runtime bytes differ from the packed on-disk bytes.

The underlying Wine/protection compatibility cause was not established or repaired. The working patch bypasses that routine before the problematic path executes; it does not reconstruct its original body.

## Why the exception-return attempts were discarded

The early implementation imitated the repo's speed-zone VEH approach. Replaying captured contexts showed that it could land at a chosen caller, but that did not prove it restored the actual frame or game state.

The real outer frame was `EntityModel::EventMap::MethodObjectInfo`, RVA `0x4106480–0x41066FC`. With the saved anchor, body RSP is `anchor-0x20`; the helper call return is at `anchor-0x28` and points to RVA `0x41064E1`. Its caller return is at `anchor+0x88`, RVA `0x410630F`.

The old recovery instead used unwind metadata for the fault location at `0x895250–0x895547`. In particular, it restored RDI from `anchor+0x80`, which is the actual MethodObjectInfo frame's saved **R14**. Actual saved RDI is at `anchor+0xA8`. A test against the real saved frame confirmed the mismatch. Later cleanup failures were therefore not a basis for continuing to extend that incorrect unwind algorithm.

V3 does not compile those custom drift exception-return handlers. The old repository speed-zone handler remains unchanged.

## Live event identification

Read-only inspection identified the live global string descriptors:

| RVA | String |
| --- | --- |
| `0x7E56738` | `DriftZoneActivated` |
| `0x7E56760` | `DriftZoneCompleted` |
| `0x7E56788` | `DriftZoneAborted` |
| `0x7E567B0` | `AnyDriftZoneActivated` |
| `0x7E567D8` | `AnyDriftZoneCompleted` |
| `0x7E56800` | `AnyDriftZoneAborted` |

Completion and abort also produce broader notifications; filtering only the first pair missed later execution. The normal abort routine has a direct tail-jump at RVA `0x88F7DA` to the failing routine at `0x895250`, beyond the event-dispatch wrapper. Recorded code has seven direct calls and two tail-jumps to that entry. The direct-call epilogues do not consume RAX.

The final patch uses an ordinary return at the entry, retaining the real return address and ABI state. It does not try to rebuild a stack after corruption. Isolated direct-call and recorded tail-jump tests verify preserved RSP, RBX, RBP, RSI, RDI, R12–R15 and XMM6. Hardware entry-breakpoint tests accepted/read back the debug registers but did not deliver the breakpoint in the tested setup; that mechanism was not installed in FH4.

## V2 startup timing failure

V2 stopped permanently when the reset-entry bytes did not yet match the expected runtime signature. In live PID 924, its log said `reset entry signature mismatch; no runtime patches installed`. A later read in the same running game found the exact expected 20 bytes and the original entry byte still present. Thus a new DLL on disk did not mean its runtime fix had installed.

V2.1 added retries for every transitional signature, without changing memory while unknown. In live PID 1180, it logged waiting at `11:28:57.408` and installation at `11:29:32.168`. A subsequent read confirmed `0xC3` at the entry and the observer vtable pointer in the loaded shim, with adjacent entries unchanged. This readiness behavior is retained in V3.

## Why V2.1 stopped the crash but left zone mode stuck

The owner reported that the game kept running, but points were not recorded and drift-zone mode did not end. The log showed abort notifications repeating about once per frame. The broad filter was skipping ordinary game handlers needed for completion and cleanup.

Read-only inspection of the actual listener metadata identified:

| Event | Original method | Target RVA | Parameters |
| --- | --- | --- | ---: |
| Completion | `OnEventFired` | `0xE4E1F0` | 1 |
| Completion | `DriftZoneCompleted` | `0x89CC20` | 0 |
| Completion | `ReleaseActivityHandle` | `0x4FBDC0` | 0 |
| Abort | `OnEventFired` | `0xE4E1F0` | 1 |
| Abort | `DriftZoneAborted` | `0x89C050` | 0 |
| Abort | `ReleaseActivityHandle` | `0x4FBDC0` | 0 |
| Broader abort | `OnEventFired` | `0xD32D60` | 2 |

The completion method's normal body spans `0x89CC20–0x89D7CE` and ends by restoring its frame and tail-calling the already-bypassed routine at `0x895250`. The abort method tail-jumps to `0x895090`, whose body clears a receiver byte at offset `0x8F0`. `ReleaseActivityHandle` clears a handle at receiver offset `0x130`. The writes are confirmed from runtime instructions; the exact semantic label of every internal field was not reconstructed. Blocking these methods prevented their ordinary state changes.

**V3's correction is to forward all callbacks.** The observer still tracks the four completion/abort names for bounded diagnostics, but none is suppressed. The separate reset-entry bypass prevents the known crash path. Scoring, event propagation and activity release use the original game code.

## Confirmed result and limits

The installed/archive DLL hash is `833b964203e1e157a162852863656e0e4934ef10a3659d3047ebd0422b529f66`. The game EXE remained byte-identical to its original recorded hash. The source's existing road/speed-zone VEH block was verified unchanged, and no saves, provider runtime or graphics settings were changed for V3.