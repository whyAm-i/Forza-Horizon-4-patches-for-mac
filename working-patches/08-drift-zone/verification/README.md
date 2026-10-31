# Isolated verification

`test_event_gate.c` uses a synthetic private image at the game's recorded image base. It needs **no FH4 EXE, runtime dump or game process**. Small recorded compatibility/call-site byte sequences exercise the actual V3 implementation. The observer's file/directory logging is stubbed in the test so it does not create game/bottle logs. The test does not open another process or install anything.

From `working-patches`, using the tested LLVM-MinGW compiler on PATH:

```sh
mkdir -p 08-drift-zone/build
x86_64-w64-mingw32-gcc -O2 -Wall -Wl,--image-base,0x180000000 \
  -o 08-drift-zone/build/test_event_gate.exe \
  08-drift-zone/verification/test_event_gate.c \
  08-drift-zone/verification/test_gate_abi.S
x86_64-w64-mingw32-gcc -O2 -Wall \
  -o 08-drift-zone/build/smoke.exe 08-drift-zone/verification/smoke.c
```

Run the first EXE through CrossOver's Run Command in a suitable test bottle, or its Wine CLI with your actual Windows path. It checks:

- Full signature readiness, including a delayed transition, and timeout with neither target modified.
- Exact one-byte entry patch, unchanged following bytes, vtable installation/removal and restored protections.
- Direct reset entry and the recorded abort tail-jump.
- Both singleton and broadcast callback call sites.
- Every named drift callback forwarded exactly once with unchanged arguments.
- Activation, unrelated, unknown-caller and invalid-descriptor calls also forwarded.
- Preserved stack and nonvolatile GPRs plus XMM6 on the tested call paths.

`smoke.exe` checks that `d3d12.dll` loads and exports ordinals 100/101 correctly. An optional argument selects a staged DLL path; without it, normal loader search rules apply. It does not create a graphics device or validate gameplay.

Read [../validation.json](../validation.json) for the archived result. Synthetic tests establish ABI/guard behavior, while the separate user report establishes the successful observed gameplay result.
