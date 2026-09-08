#!/usr/bin/env python3

import ctypes
import os
import sys

COPYRIGHT = 0x1539
EXPECTED = b"\x7f 1982 Sinclair Research Ltd"

if len(sys.argv) != 3:
    sys.exit(f"usage: {sys.argv[0]} <library> <rom directory>")

library, rom_dir = (os.path.abspath(path) for path in sys.argv[1:])

lib = ctypes.CDLL(library)
lib.readbyte.argtypes = [ctypes.c_uint16]
lib.readbyte.restype = ctypes.c_uint8

argv = (ctypes.c_char_p * 6)(
    b"fusex", b"--machine", b"48", b"--rom-dir", rom_dir.encode(), b"--no-banner"
)
if lib.fuse_init(6, argv) != 0:
    sys.exit("fuse_init failed")

message = bytearray()
address = COPYRIGHT
while True:
    code = lib.readbyte(address)
    message.append(code & 0x7F)
    if code & 0x80:
        break
    address += 1

lib.fuse_end()

if bytes(message) != EXPECTED:
    sys.exit(f"expected {EXPECTED!r} at {COPYRIGHT:#06x}, read {bytes(message)!r}")

print(f"ok: {bytes(message)!r}")
