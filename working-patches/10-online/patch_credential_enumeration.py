"""Stage the macOS empty Keychain enumeration fix for CrossOver 26.3 x64."""

import argparse
import hashlib
from pathlib import Path
import struct


STOCK_SHA = "0375c2ee0e61be9fc3064a07b52f65e73d8a74b06fd06b59ea9e958365a33940"
PATCHED_SHA = "754810ac719c869aeec9a4d94b9ae0591e3b27abeb77c88bf8bd6ac8fd71ae5e"
HOOK_RVA = 0x1FBE8
CAVE_RVA = 0x245A0
CONTINUE_RVA = 0x1FBF3
ORIGINAL = bytes.fromhex("41 8b 54 24 68 81 fa ea 00 00 00")


def jump(source, target):
    return b"\xe9" + struct.pack("<i", target - source - 5)


def patch(data):
    if hashlib.sha256(data).hexdigest() != STOCK_SHA:
        raise ValueError("Expected the inspected CrossOver 26.3 x64 advapi32.dll")
    pe = struct.unpack_from("<I", data, 60)[0]
    section = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    virtual_size, virtual_address, raw_size, raw_start = struct.unpack_from("<4I", data, section + 8)
    if data[section:section + 8] != b".text\0\0\0" or CAVE_RVA != virtual_address + virtual_size:
        raise ValueError("Unexpected code section layout")

    hook = raw_start + HOOK_RVA - virtual_address
    cave = raw_start + CAVE_RVA - virtual_address
    # Replay the host GetLastError load, normalize only ERROR_NOT_FOUND, then
    # restore the original ERROR_MORE_DATA comparison and resume its branch.
    stub = ORIGINAL[:5] + bytes.fromhex("81 fa 90 04 00 00 75 02 31 d2") + ORIGINAL[5:]
    stub += jump(CAVE_RVA + len(stub), CONTINUE_RVA)
    if data[hook:hook + len(ORIGINAL)] != ORIGINAL or cave + len(stub) > raw_start + raw_size:
        raise ValueError("Unexpected instructions or insufficient code padding")
    if any(data[cave:cave + len(stub)]):
        raise ValueError("Code padding is occupied")

    result = bytearray(data)
    result[hook:hook + len(ORIGINAL)] = jump(HOOK_RVA, CAVE_RVA).ljust(len(ORIGINAL), b"\x90")
    result[cave:cave + len(stub)] = stub
    struct.pack_into("<I", result, section + 8, virtual_size + len(stub))
    if hashlib.sha256(result).hexdigest() != PATCHED_SHA:
        raise ValueError("Output does not match the verified credential fix")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = patch(args.original.read_bytes())
    with args.output.open("xb") as output:
        output.write(result)
    print(hashlib.sha256(result).hexdigest(), args.output)
