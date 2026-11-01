# Portable verification and rebuild

The released DLL requires no compiler to install. Optional tests/builds use
LLVM-MinGW targeting x86_64 Windows. The tested distribution was
`llvm-mingw-20261020-ucrt-macos-universal`, Clang 23.1.4. Put its tools on PATH or
pass the compiler's absolute path; no personal toolchain path is embedded.

From `working-patches`:

```sh
python3 09-memory-leak/build.py --cc x86_64-w64-mingw32-clang
python3 09-memory-leak/run_tests.py \
  --bottle "$HOME/Library/Application Support/CrossOver/Bottles/New Bottle"
python3 09-memory-leak/verification/test_install.py
```

Use your actual bottle directory. `--cx` selects a custom CrossOver runtime
directory for the runner. It maps files through **existing** bottle drives and
does not create or change mappings. If none maps the build directory, use Run
Command with your reviewed Windows paths. `--provider` optionally supplies a
Windows path to the existing renamed `apd12.dll` for the loader checks. The
runner does not install or launch/open the game.

Generated builds/results stay under ignored `09-memory-leak/build/` by default.
`build.py --out PATH` and `run_tests.py --build-dir PATH` select another directory.
The exact archived DLL is not overwritten; rebuilt hashes can differ. The build
keeps the basename `d3d12.dll`, all seven headers, mandatory `d3d12.def`, and the
Wine builtin marker. Verify ordinal 100 (`GetBehaviorValue`) and 101
(`D3D12CreateDevice`) when changing the build workflow.

## Tests

- `test_aligned_heap.c` includes the actual repair implementation. It tests
  signatures/import guards, restored protections, surrounding bytes, exact caller
  capture through synthetic IAT calls, four request sizes, normal raw frees,
  wrong callers/heaps/flags/sizes, forged or unobserved metadata, failed releases,
  disabled repair, duplicate frees, table saturation and 80000 concurrent pairs.
- `cow_fixture.c` builds an inert PE with large zero-initialized virtual space.
  The test maps it using SEC_IMAGE and does not execute its entry point. This
  reproduces `PAGE_EXECUTE_WRITECOPY` without a game EXE or code dump. Only the
  fixture process's private mapped pages change. Its file is opened read-only.
- Two invalid-pointer child cases install a local exception handler and exit 77
  for the original NTDLL fault. Matching that result is a pass, not a test crash
  to debug in the running game.
- `smoke.c` checks the actual loaded DLL path, the existing apd12 provider, both
  export ordinals and all ten forwarding targets for released and rebuilt shims.
  It does not create a graphics device.
- Existing rendering-input and synthetic drift ABI fixtures are compiled from
  their repository locations against the current integrated source.
- `test_install.py` runs filesystem transaction/refusal tests in temporary
  directories, with fake game/runtime inputs. It never writes a real app/bottle.

Patch log I/O is stubbed only in the allocator/drift fixtures; real heap, mapping,
thread and ABI operations are exercised. The runner writes local build results;
neither opening the archive nor rebuilding it installs anything. Source checks
do not replace gameplay validation on a different game/runtime build.
