#!/usr/bin/env python3

# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Read from stdin a set of colon separated http headers:

    :path: /foo/bar
    content-type: application/grpc

Write a set of strings containing a hpack encoded http2 frame that
represents said headers.
"""

import argparse
import json
import sys


def append_never_indexed(payload_line, n, count, key, value, value_is_huff):
    payload_line.append(0x10)
    assert len(key) <= 126
    payload_line.append(len(key))
    payload_line.extend(ord(c) for c in key)
    assert len(value) <= 126
    payload_line.append(len(value) + (0x80 if value_is_huff else 0))
    payload_line.extend(value)


def append_inc_indexed(payload_line, n, count, key, value, value_is_huff):
    payload_line.append(0x40)
    assert len(key) <= 126
    payload_line.append(len(key))
    payload_line.extend(ord(c) for c in key)
    assert len(value) <= 126
    payload_line.append(len(value) + (0x80 if value_is_huff else 0))
    payload_line.extend(value)


def append_pre_indexed(payload_line, n, count, key, value, value_is_huff):
    assert not value_is_huff
    payload_line.append(0x80 + 61 + count - n)


def esc_c(line):
    out = '"'
    last_was_hex = False
    for c in line:
        if 32 <= c < 127:
            if c in hex_bytes and last_was_hex:
                out += '""'
            if c != ord('"'):
                out += chr(c)
            else:
                out += '\\"'
            last_was_hex = False
        else:
            out += "\\x%02x" % c
            last_was_hex = True
    return out + '"'


def output_c(payload_bytes):
    for line in payload_bytes:
        print((esc_c(line)))


def output_hex(payload_bytes):
    all_bytes = []
    for line in payload_bytes:
        all_bytes.extend(line)
    print(("{%s}" % ", ".join("0x%02x" % c for c in all_bytes)))


def output_hexstr(payload_bytes):
    all_bytes = []
    for line in payload_bytes:
        all_bytes.extend(line)
    print(("%s" % "".join("%02x" % c for c in all_bytes)))


_COMPRESSORS = {
    "never": append_never_indexed,
    "inc": append_inc_indexed,
    "pre": append_pre_indexed,
}

_OUTPUTS = {
    "c": output_c,
    "hex": output_hex,
    "hexstr": output_hexstr,
}

argp = argparse.ArgumentParser("Generate header frames")
argp.add_argument(
    "--set_end_stream", default=False, action="store_const", const=True
)
argp.add_argument(
    "--no_framing", default=False, action="store_const", const=True
)
argp.add_argument(
    "--compression", choices=sorted(_COMPRESSORS.keys()), default="never"
)
argp.add_argument("--huff", default=False, action="store_const", const=True)
argp.add_argument("--output", default="c", choices=sorted(_OUTPUTS.keys()))
args = argp.parse_args()

# parse input, fill in vals
vals = []
for line in sys.stdin:
    line = line.strip()
    if line == "":
        continue
    if line[0] == "#":
        continue
    key_tail, value = line[1:].split(":")
    key = (line[0] + key_tail).strip()
    value = value.strip().encode("ascii")
    if args.huff:
        from hpack.huffman import HuffmanEncoder
        from hpack.huffman_constants import REQUEST_CODES
        from hpack.huffman_constants import REQUEST_CODES_LENGTH

        value = HuffmanEncoder(REQUEST_CODES, REQUEST_CODES_LENGTH).encode(
            value
        )
    vals.append((key, value))

# generate frame payload binary data
payload_bytes = []
if not args.no_framing:
    payload_bytes.append([])  # reserve space for header
payload_len = 0
n = 0
for key, value in vals:
    payload_line = []
    _COMPRESSORS[args.compression](
        payload_line, n, len(vals), key, value, args.huff
    )
    n += 1
    payload_len += len(payload_line)
    payload_bytes.append(payload_line)

# fill in header
if not args.no_framing:
    flags = 0x04  # END_HEADERS
    if args.set_end_stream:
        flags |= 0x01  # END_STREAM
    payload_bytes[0].extend(
        [
            (payload_len >> 16) & 0xFF,
            (payload_len >> 8) & 0xFF,
            (payload_len) & 0xFF,
            # header frame
            0x01,
            # flags
            flags,
            # stream id
            0x00,
            0x00,
            0x00,
            0x01,
        ]
    )

hex_bytes = [ord(c) for c in "abcdefABCDEF0123456789"]

# dump bytes
_OUTPUTS[args.output](payload_bytes)
