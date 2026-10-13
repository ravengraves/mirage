#!/usr/bin/env python3
"""Regression tests for Python buffer validation; run with the built bindings."""

import unittest

import mirage
from mirage import _mirage as c


def varuint(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def message(data):
    msg = c.create(len(data))
    c.set_buffer(msg, data)
    c.read_start(msg)
    return msg


READERS = (
    (b"S", c.read_string),
    (b"B", c.read_bin),
    (b"s", c.read_sym),
    (b"I", c.read_bigint),
    (b"R", c.read_bigreal),
)

ARRAY_TYPES = (
    (c.ARRAY_I8, 1), (c.ARRAY_I16, 2), (c.ARRAY_I32, 4), (c.ARRAY_I64, 8),
    (c.ARRAY_U8, 1), (c.ARRAY_U16, 2), (c.ARRAY_U32, 4), (c.ARRAY_U64, 8),
    (c.ARRAY_F32, 4), (c.ARRAY_F64, 8), (c.ARRAY_C32, 8), (c.ARRAY_C64, 16),
)


class StringReadBounds(unittest.TestCase):
    def test_malformed_lengths_and_truncated_payloads(self):
        malformed = (
            b"", b"\x80", b"\x80" * 11, b"\x80" * 11 + b"\x00",
            b"\xff" * 9 + b"\x01", varuint((1 << 63) - 1),
            b"\x01", b"\x03ab", varuint(1 << 32),
        )
        for tag, reader in READERS:
            for tail in malformed:
                with self.subTest(tag=tag, tail=tail):
                    wire = b"8:" + tag + tail
                    msg = message(wire)  # Allocation ends exactly at the input.
                    for _ in range(2):
                        with self.assertRaises(RuntimeError):
                            reader(msg)
                        self.assertEqual(c.get_buffer(msg), b"8:")
                    with self.assertRaises(RuntimeError):
                        mirage.decode(wire)

    def test_end_of_message_and_wrong_tag(self):
        for tag, reader in READERS:
            for wire in (b"8:", b"8:C\x01"):
                with self.subTest(tag=tag, wire=wire):
                    msg = message(wire)
                    with self.assertRaises(RuntimeError):
                        reader(msg)
                    self.assertEqual(c.get_buffer(msg), b"8:")

    def test_valid_payloads_and_cursor(self):
        for tag, reader in READERS:
            payloads = (b"0", b"-123456789012345678901234567890", b"1" * 128)
            if tag != b"I":
                payloads += (b"", b"a\x00b", "caf\u00e9".encode(), b"x" * 16384)
            if tag == b"B":
                payloads += (b"\x00\xff\x80",)
            for payload in payloads:
                with self.subTest(tag=tag, length=len(payload)):
                    wire = b"8:" + tag + varuint(len(payload)) + payload + b"C*"
                    msg = message(wire)
                    value = reader(msg)
                    expected = payload if tag == b"B" else (
                        int(payload) if tag == b"I" else payload.decode()
                    )
                    self.assertEqual(value, expected)
                    self.assertEqual(c.read_i8(msg), 42)
                    self.assertEqual(c.get_buffer(msg), wire)
                    with self.assertRaises(RuntimeError):
                        reader(msg)
                    c.set_buffer(msg, b"8:")
                    self.assertEqual(value, expected)

    def test_invalid_text_is_a_python_error(self):
        for tag, reader in READERS:
            if tag == b"B":
                continue
            with self.subTest(tag=tag):
                with self.assertRaises((UnicodeError, ValueError)):
                    reader(message(b"8:" + tag + b"\x01\xff"))
        with self.assertRaises(ValueError):
            c.read_bigint(message(b"8:I\x00"))


class NumericWriteBounds(unittest.TestCase):
    def test_packed_array_dispatch(self):
        import numpy as np

        wire = b"8:\xc1\x02\x01\x02\x2a\x00\x00\x00\xff\xff\xff\xff"
        result = mirage.decode(wire)
        self.assertEqual(result.dtype, np.dtype("int32"))
        self.assertEqual(result.shape, (2,))
        self.assertEqual(result.tolist(), [42, -1])

    def assert_rejected(self, array_type, dims, data, error):
        msg = c.create(512)
        before = c.get_buffer(msg)
        with self.assertRaises(error) as raised:
            c.write_numericarray(msg, array_type, dims, data)
        if error is RuntimeError:
            self.assertEqual(str(raised.exception), "mirage_write_numericarray failed: -4")
        self.assertEqual(c.get_buffer(msg), before)
        self.assertEqual(c.get_length(msg), len(before))

    def test_short_buffers(self):
        data = bytearray(1)
        self.assert_rejected(c.ARRAY_U8, [64], data, ValueError)
        data.extend(b"x")  # Rejection must release the exported buffer.
        for array_type, size in ARRAY_TYPES:
            for dims, count in (([], 1), ([2, 3], 6)):
                with self.subTest(array_type=array_type, dims=dims):
                    self.assert_rejected(array_type, dims, bytes(size * count - 1), ValueError)

    def test_invalid_metadata(self):
        for array_type in (-1, 12, (1 << 31) - 1):
            with self.subTest(array_type=array_type):
                self.assert_rejected(array_type, [], b"x", RuntimeError)
        for dims in ([-1], [0, -1]):
            with self.subTest(dims=dims):
                self.assert_rejected(c.ARRAY_U8, dims, b"x", RuntimeError)
        self.assert_rejected(c.ARRAY_U8, [1] * 33, b"x", ValueError)
        self.assert_rejected(c.ARRAY_U8, [1 << 63], b"x", OverflowError)
        self.assert_rejected(c.ARRAY_U8, [(1 << 63) - 1, 2], b"x", RuntimeError)
        self.assert_rejected(c.ARRAY_U64, [(1 << 63) - 1], b"x", RuntimeError)
        self.assert_rejected(c.ARRAY_C64, [1 << 59], b"x", RuntimeError)

    def test_all_types_scalars_arrays_and_empty_arrays(self):
        for array_type, size in ARRAY_TYPES:
            for dims, count in (([], 1), ([2, 3], 6), ([0], 0), ([2, 0], 0)):
                for exporter in (bytes, bytearray, memoryview):
                    with self.subTest(array_type=array_type, dims=dims, exporter=exporter):
                        payload = bytes(range(size * count))
                        msg = c.create(512)
                        # Extra source bytes are allowed, but must not be serialized.
                        c.write_numericarray(msg, array_type, dims, exporter(payload + b"extra"))
                        c.write_i8(msg, 42)
                        c.read_start(msg)
                        self.assertEqual(c.read_numericarray(msg), (array_type, dims, payload))
                        self.assertEqual(c.read_i8(msg), 42)

    def test_zero_length_source(self):
        for data in (b"", bytearray(), memoryview(b"")):
            msg = c.create(128)
            c.write_numericarray(msg, c.ARRAY_U8, [2, 0], data)
            c.read_start(msg)
            self.assertEqual(c.read_numericarray(msg), (c.ARRAY_U8, [2, 0], b""))

    def test_rejected_destination_releases_source(self):
        data = bytearray(b"a")
        msg = c.create(2)
        with self.assertRaises(RuntimeError):
            c.write_numericarray(msg, c.ARRAY_U8, [1], data)
        data.extend(b"b")
        self.assertEqual(c.get_buffer(msg), b"8:")

    def test_noncontiguous_source(self):
        data = memoryview(bytearray(range(8)))[::2]
        self.assert_rejected(c.ARRAY_U8, [4], data, BufferError)


if __name__ == "__main__":
    unittest.main()
