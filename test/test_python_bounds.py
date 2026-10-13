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

class BindingRegressions(unittest.TestCase):
    def test_signed_i8(self):
        for value in (-128, -1, 0, 127):
            self.assertEqual(mirage.decode(mirage.encode(mirage.i8(value))), value)
        for value in (-129, 128, 255, 1 << 64):
            with self.assertRaises(OverflowError):
                mirage.encode(mirage.i8(value))

    def test_long_function_names(self):
        for name in ("", "x" * 256, "x" * 257, "é" * 8192, "a\0b"):
            value = mirage.expr(name, 42)
            self.assertEqual(mirage.decode(mirage.encode(value)), value)
        for wire in (b"8:f", b"8:f\x00s\x80", b"8:f\x00s\x03ab"):
            msg = message(wire)
            with self.assertRaises(RuntimeError):
                c.read_fn(msg)
            self.assertEqual(c.get_buffer(msg), b"8:")

    def test_numpy_shapes_and_layouts(self):
        import numpy as np
        for dtype in (np.int8, np.int16, np.int32, np.int64, np.uint8,
                      np.uint16, np.uint32, np.uint64, np.float32, np.float64,
                      np.complex64, np.complex128):
            base = np.arange(12, dtype=dtype).reshape(3, 4)
            for value in (np.array(42, dtype=dtype), base, base.T,
                          base[:, ::2], base[:0]):
                result = mirage.decode(mirage.encode(value))
                self.assertEqual(result.shape, value.shape)
                self.assertEqual(result.dtype, value.dtype)
                np.testing.assert_array_equal(result, value)

    def test_empty_shapes_after_overflow(self):
        maximum = (1 << 63) - 1
        for array_type, _ in ARRAY_TYPES:
            for dims in ([0, maximum, 2], [maximum, 0, 2], [maximum, 2, 0]):
                msg = c.create(128)
                c.write_numericarray(msg, array_type, dims, b"")
                c.read_start(msg)
                self.assertEqual(c.read_numericarray(msg), (array_type, dims, b""))
        with self.assertRaises(RuntimeError):
            c.write_numericarray(c.create(128), c.ARRAY_U8, [maximum, 2, 0, -1], b"")

    def test_byte_capacities_and_lengths(self):
        from array import array
        import numpy as np
        for buffer in (bytearray(64), memoryview(array('I', [0] * 16)),
                       memoryview(np.zeros((4, 4), dtype=np.uint32))):
            length = mirage.encode_to(42, buffer)
            self.assertEqual(mirage.decode_from(buffer, length), 42)
            self.assertEqual(mirage.decode(buffer), 42)
            for invalid in (-1, memoryview(buffer).nbytes + 1):
                with self.assertRaises(ValueError):
                    mirage.decode_from(buffer, invalid)
            with self.assertRaises(TypeError):
                mirage.decode_from(buffer, 1.5)
            with self.assertRaises(RuntimeError):
                mirage.decode_from(buffer, 0)
        with self.assertRaises(TypeError):
            mirage.encode_to(42, memoryview(bytes(64)))
        view = memoryview(bytearray(64))[::2]
        for operation in (lambda: mirage.encode_to(42, view),
                          lambda: mirage.decode_from(view), lambda: mirage.decode(view)):
            with self.assertRaises(BufferError):
                operation()
        for value in ("x" * 100, [42, object()]):
            buffer = bytearray(b"unchanged")
            with self.assertRaises(RuntimeError):
                mirage.encode_to(value, buffer)
            self.assertEqual(buffer, b"unchanged")

    def test_capacity_retry_only(self):
        from unittest.mock import patch
        from mirage import mirage as api
        self.assertEqual(mirage.decode(mirage.encode("x" * 100, buffer_size=2)), "x" * 100)
        for error in (RuntimeError("mirage_write_numericarray failed: -4"),
                      RuntimeError("arbitrary failure")):
            with patch.object(api, '_write_value', side_effect=error) as write:
                with self.assertRaises(RuntimeError):
                    mirage.encode(42)
                self.assertEqual(write.call_count, 1)
        with self.assertRaises(RuntimeError) as raised:
            c.write_numericarray(c.create(128), -1, [], b"")
        self.assertEqual(raised.exception.code, -4)
        with self.assertRaises(RuntimeError) as raised:
            c.write_string(c.create(2), "x")
        self.assertEqual(raised.exception.code, c.ERR_BUFFERSIZE)

    def test_array_allocation_failures(self):
        # Isolate CPython's allocator fault injection from the test runner.
        import subprocess
        import sys
        try:
            import _testcapi
        except ImportError:
            self.skipTest("requires CPython _testcapi")
        if not hasattr(_testcapi, 'set_nomemory'):
            self.skipTest("requires CPython allocation fault injection")
        subprocess.run([sys.executable, '-c', '''
import _testcapi
from mirage import _mirage as c
# Exercise list, dimension integer, payload bytes and tuple allocations.
for dims in ([1] * 32, [1000], [1000] * 31 + [0]):
    payload = b'x' * (0 if 0 in dims else 1000 if dims == [1000] else 1)
    for fail in range(40):
        msg = c.create(2048)
        c.write_numericarray(msg, c.ARRAY_U8, dims, payload)
        c.read_start(msg)
        failed = False
        _testcapi.set_nomemory(fail, fail + 1)
        try:
            result = c.read_numericarray(msg)
        except MemoryError:
            failed = True
        finally:
            _testcapi.remove_mem_hooks()
        if not failed:
            assert result == (c.ARRAY_U8, dims, payload)
            assert fail > 0
            break
        assert c.get_buffer(msg) == b'8:'
        assert c.read_numericarray(msg) == (c.ARRAY_U8, dims, payload)
    else:
        raise AssertionError('allocation sweep never succeeded')
'''], check=True)


if __name__ == "__main__":
    unittest.main()
