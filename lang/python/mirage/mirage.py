# mirage/mirage.py
# -----------------
# High-level Python API for Mirage WXF serialization.
#
# Type mappings (Python -> WXF):
#   list          -> List function
#   tuple         -> List function
#   Expr(name, *args) -> WXF function
#   dict          -> Association
#   str           -> String
#   bytes         -> Binary
#   int           -> i64 (or BigInt if outside i64 range)
#   i8(n)         -> i8
#   i16(n)        -> i16
#   i32(n)        -> i32
#   float         -> f64
#   bool          -> i8 (0 or 1)
#   None          -> Symbol "Null"
#   numpy.ndarray -> NumericArray
#   sym(name)     -> Symbol
#
# Type mappings (WXF -> Python):
#   List function   -> list
#   Other functions -> Expr (name, arg1, arg2, ...)
#   Association     -> dict
#   String          -> str
#   Binary          -> bytes
#   i8/i16/i32/i64  -> int
#   f64             -> float
#   NumericArray    -> numpy.ndarray
#   Symbol          -> Sym (subclass of str)
#   BigInt          -> int
#   BigReal         -> str

from __future__ import annotations
from typing import Any
from . import _mirage as _c

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False


class MirageError(RuntimeError):
    """Exception raised for Mirage encoding/decoding errors."""
    pass


# Integer type wrappers - subclass int for native behavior
class I8(int):
    """8-bit signed integer for WXF encoding."""
    pass


class I16(int):
    """16-bit signed integer for WXF encoding."""
    pass


class I32(int):
    """32-bit signed integer for WXF encoding."""
    pass


def i8(value: int) -> I8:
    """Create an 8-bit signed integer for WXF encoding."""
    return I8(value)


def i16(value: int) -> I16:
    """Create a 16-bit signed integer for WXF encoding."""
    return I16(value)


def i32(value: int) -> I32:
    """Create a 32-bit signed integer for WXF encoding."""
    return I32(value)


# Symbol type - subclass str for native behavior
class Sym(str):
    """WXF Symbol that behaves like a string.

    Symbols are like identifiers/atoms in Mathematica.
    Example: sym("True"), sym("Global`x")
    """
    pass


def sym(name: str) -> Sym:
    """Create a WXF symbol."""
    return Sym(name)


# Expr type - subclass tuple for WXF function expressions
class Expr(tuple):
    """WXF Function expression.

    Represents a WXF function call like Plus[1, 2].
    First element must be the function name (string), followed by arguments.
    """
    def __new__(cls, name, *args):
        if not isinstance(name, str):
            raise TypeError(
                f"Expr head must be a string, got {type(name).__name__}. "
                f"Example: Expr('Plus', 1, 2)"
            )
        return super().__new__(cls, (name, *args))

    @property
    def head(self):
        """Function name (first element)."""
        return self[0]

    @property
    def args(self):
        """Function arguments (all elements except first)."""
        return self[1:]


def expr(name: str, *args) -> Expr:
    """Create a WXF function expression."""
    return Expr(name, *args)


# NumPy dtype to mirage array type mapping
if HAS_NUMPY:
    _NUMPY_TO_MIRAGE = {
        np.dtype('int8'): _c.ARRAY_I8,
        np.dtype('int16'): _c.ARRAY_I16,
        np.dtype('int32'): _c.ARRAY_I32,
        np.dtype('int64'): _c.ARRAY_I64,
        np.dtype('uint8'): _c.ARRAY_U8,
        np.dtype('uint16'): _c.ARRAY_U16,
        np.dtype('uint32'): _c.ARRAY_U32,
        np.dtype('uint64'): _c.ARRAY_U64,
        np.dtype('float32'): _c.ARRAY_F32,
        np.dtype('float64'): _c.ARRAY_F64,
        np.dtype('complex64'): _c.ARRAY_C32,
        np.dtype('complex128'): _c.ARRAY_C64,
    }

    _MIRAGE_TO_NUMPY = {
        _c.ARRAY_I8: np.dtype('int8'),
        _c.ARRAY_I16: np.dtype('int16'),
        _c.ARRAY_I32: np.dtype('int32'),
        _c.ARRAY_I64: np.dtype('int64'),
        _c.ARRAY_U8: np.dtype('uint8'),
        _c.ARRAY_U16: np.dtype('uint16'),
        _c.ARRAY_U32: np.dtype('uint32'),
        _c.ARRAY_U64: np.dtype('uint64'),
        _c.ARRAY_F32: np.dtype('float32'),
        _c.ARRAY_F64: np.dtype('float64'),
        _c.ARRAY_C32: np.dtype('complex64'),
        _c.ARRAY_C64: np.dtype('complex128'),
    }


def _estimate_size(obj: Any, depth: int = 0) -> int:
    """Estimate the buffer size needed to encode an object."""
    if depth > 100:
        raise MirageError("Object too deeply nested (max depth 100)")

    if obj is None:
        return 8  # Symbol "Null"
    elif isinstance(obj, bool):
        return 2  # i8 (0 or 1)
    elif isinstance(obj, I8):
        return 2  # i8
    elif isinstance(obj, I16):
        return 4  # i16
    elif isinstance(obj, I32):
        return 6  # i32
    elif isinstance(obj, int):
        # Plain int always uses i64, or BigInt if outside range
        if -9223372036854775808 <= obj <= 9223372036854775807:
            return 10  # i64
        else:
            # BigInt - decimal string representation
            return len(str(obj)) + 10
    elif isinstance(obj, float):
        return 9  # f64
    elif isinstance(obj, Sym):
        # Check Sym before str since Sym subclasses str
        return len(obj) + 10
    elif isinstance(obj, str):
        encoded = obj.encode('utf-8')
        return len(encoded) + 10  # data + type + varint length
    elif isinstance(obj, bytes):
        return len(obj) + 10
    elif isinstance(obj, Expr):
        # Check Expr before tuple since Expr subclasses tuple
        total = len(obj[0]) + 10  # function name + header
        for item in obj[1:]:
            total += _estimate_size(item, depth + 1)
        return total
    elif isinstance(obj, tuple):
        # Plain tuples now encode as WXF List
        total = 10  # List header
        for item in obj:
            total += _estimate_size(item, depth + 1)
        return total
    elif isinstance(obj, list):
        total = 10  # List function header
        for item in obj:
            total += _estimate_size(item, depth + 1)
        return total
    elif isinstance(obj, dict):
        total = 10  # association header
        for k, v in obj.items():
            total += 2  # rule marker
            total += _estimate_size(k, depth + 1)
            total += _estimate_size(v, depth + 1)
        return total
    elif HAS_NUMPY and isinstance(obj, np.ndarray):
        # data bytes + header (tag + array_type + rank varint) + dims varints
        return obj.nbytes + 64 + 16 * max(obj.ndim, 1)
    else:
        raise MirageError(f"Cannot encode type: {type(obj).__name__}")


def _write_value(msg, obj: Any, depth: int = 0):
    """Recursively write a Python object to the message buffer."""
    if depth > 100:
        raise MirageError("Object too deeply nested (max depth 100)")

    if obj is None:
        _c.write_sym(msg, "Null")
    elif isinstance(obj, bool):
        _c.write_i8(msg, 1 if obj else 0)
    elif isinstance(obj, I8):
        _c.write_i8(msg, int(obj))
    elif isinstance(obj, I16):
        _c.write_i16(msg, int(obj))
    elif isinstance(obj, I32):
        _c.write_i32(msg, int(obj))
    elif isinstance(obj, int):
        # Plain int always uses i64, or BigInt if outside range
        if -9223372036854775808 <= obj <= 9223372036854775807:
            _c.write_i64(msg, obj)
        else:
            # BigInt for values outside i64 range
            _c.write_bigint(msg, str(obj))
    elif isinstance(obj, float):
        _c.write_f64(msg, obj)
    elif isinstance(obj, Sym):
        # Check Sym before str since Sym subclasses str
        _c.write_sym(msg, str(obj))
    elif isinstance(obj, str):
        _c.write_string(msg, obj)
    elif isinstance(obj, bytes):
        _c.write_bin(msg, obj)
    elif isinstance(obj, Expr):
        # Check Expr before tuple since Expr subclasses tuple
        name = obj[0]
        args = obj[1:]
        _c.write_fn(msg, name, len(args))
        for arg in args:
            _write_value(msg, arg, depth + 1)
    elif isinstance(obj, tuple):
        # Plain tuples encode as WXF List
        _c.write_fn(msg, "List", len(obj))
        for item in obj:
            _write_value(msg, item, depth + 1)
    elif isinstance(obj, list):
        _c.write_fn(msg, "List", len(obj))
        for item in obj:
            _write_value(msg, item, depth + 1)
    elif isinstance(obj, dict):
        _c.write_association(msg, len(obj))
        for key, value in obj.items():
            _c.write_assoc_rule(msg)
            _write_value(msg, key, depth + 1)
            _write_value(msg, value, depth + 1)
    elif HAS_NUMPY and isinstance(obj, np.ndarray):
        # Ensure contiguous C-order array
        arr = obj if obj.flags.c_contiguous else np.ascontiguousarray(obj)
        dtype = arr.dtype

        if dtype not in _NUMPY_TO_MIRAGE:
            raise MirageError(f"Unsupported numpy dtype: {dtype}")

        array_type = _NUMPY_TO_MIRAGE[dtype]
        dims = list(arr.shape) if arr.ndim > 0 else []
        _c.write_numericarray(msg, array_type, dims, arr)
    else:
        raise MirageError(f"Cannot encode type: {type(obj).__name__}")


def _read_value(msg, depth: int = 0) -> Any:
    """Recursively read a Python object from the message buffer."""
    if depth > 100:
        raise MirageError("Object too deeply nested (max depth 100)")

    etype = _c.peek_type(msg)

    if etype == _c.FUNCTION:
        name, argc = _c.read_fn(msg)
        args = [_read_value(msg, depth + 1) for _ in range(argc)]

        # Special: List -> Python list
        if name == "List":
            return args
        else:
            # Other functions -> Expr
            return Expr(name, *args)

    elif etype == _c.I8:
        return _c.read_i8(msg)

    elif etype == _c.I16:
        return _c.read_i16(msg)

    elif etype == _c.I32:
        return _c.read_i32(msg)

    elif etype == _c.I64:
        return _c.read_i64(msg)

    elif etype == _c.F64:
        return _c.read_f64(msg)

    elif etype == _c.STRING:
        return _c.read_string(msg)

    elif etype == _c.BINSTRING:
        return _c.read_bin(msg)

    elif etype == _c.SYMBOL:
        name = _c.read_sym(msg)
        # Special symbols
        if name == "Null":
            return None
        elif name == "True":
            return True
        elif name == "False":
            return False
        return Sym(name)

    elif etype == _c.BIGINT:
        return _c.read_bigint(msg)

    elif etype == _c.BIGREAL:
        return _c.read_bigreal(msg)

    elif etype == _c.NUMERICARRAY:
        array_type, dims, data = _c.read_numericarray(msg)

        if not HAS_NUMPY:
            raise MirageError("NumPy required to decode numeric arrays")

        dtype = _MIRAGE_TO_NUMPY.get(array_type)
        if dtype is None:
            raise MirageError(f"Unknown array type: {array_type}")

        arr = np.frombuffer(data, dtype=dtype)
        return arr.reshape(tuple(dims))

    elif etype == _c.ASSOCIATION:
        length = _c.read_association(msg)
        result = {}
        for _ in range(length):
            _c.read_assoc_rule(msg)
            key = _read_value(msg, depth + 1)
            value = _read_value(msg, depth + 1)
            result[key] = value
        return result

    elif etype == _c.RULE:
        # Shouldn't happen in normal flow, but handle it
        _c.read_assoc_rule(msg)
        key = _read_value(msg, depth + 1)
        value = _read_value(msg, depth + 1)
        return {key: value}

    else:
        raise MirageError(f"Unknown element type: {etype}")


def encode(obj: Any, buffer_size: int | None = None) -> bytes:
    """Encode a Python object to WXF bytes.

    Type mappings:
        list        -> List function
        tuple       -> List function (same as list)
        Expr(name, *args) -> WXF function
        dict        -> Association
        str         -> String
        bytes       -> Binary
        int         -> i64 (or BigInt if outside i64 range)
        i8(n)       -> i8
        i16(n)      -> i16
        i32(n)      -> i32
        float       -> f64
        bool        -> i8 (1 or 0) for hardware efficiency
        None        -> Symbol "Null"
        numpy.ndarray -> NumericArray
        Sym(name)   -> Symbol

    Args:
        obj: Python object to encode
        buffer_size: Optional buffer size (auto-estimated if not provided)

    Returns:
        WXF-encoded bytes

    Raises:
        MirageError: If encoding fails

    Examples:
        >>> encode([1, 2, 3])
        b'8:...'
        >>> encode({"x": 1.0, "y": 2.0})
        b'8:...'
        >>> encode(Expr('Plus', 1, 2))  # Plus[1, 2]
        b'8:...'
    """
    if buffer_size is None:
        buffer_size = max(_estimate_size(obj) * 2 + 1024, 4096)

    while True:
        try:
            msg = _c.create(buffer_size)
            _c.write_start(msg)
            _write_value(msg, obj)
            return _c.get_buffer(msg)
        except RuntimeError as e:
            if getattr(e, "code", None) == _c.ERR_BUFFERSIZE and buffer_size < 1024 * 1024 * 1024:  # Max 1GB
                buffer_size *= 2
            else:
                raise


def decode(data: bytes | bytearray | memoryview) -> Any:
    """Decode WXF bytes to a Python object.

    Type mappings:
        List function -> list
        Other functions -> Expr(name, arg1, arg2, ...)
        Association   -> dict
        String        -> str
        Binary        -> bytes
        i8/i16/i32/i64 -> int (note: bool encodes as i8)
        f64           -> float
        NumericArray  -> numpy.ndarray
        Symbol "Null" -> None
        Symbol "True" -> True
        Symbol "False" -> False
        Other Symbols -> Sym (subclass of str)
        BigInt        -> int
        BigReal       -> str

    Args:
        data: WXF-encoded bytes

    Returns:
        Decoded Python object

    Raises:
        MirageError: If decoding fails

    Examples:
        >>> decode(encode([1, 2, 3]))
        [1, 2, 3]
        >>> decode(encode({"x": 1.0}))
        {'x': 1.0}
        >>> decode(encode(Expr('Plus', 1, 2)))
        Expr('Plus', 1, 2)
    """
    data = _byte_view(data)
    msg = _c.create(max(data.nbytes, 2))
    _c.set_buffer(msg, data)
    _c.read_start(msg)
    return _read_value(msg)


def _byte_view(buffer):
    view = memoryview(buffer)
    if not view.c_contiguous:
        raise BufferError("buffer must be C-contiguous")
    return view.cast('B')


def encode_to(obj: Any, buffer: bytearray | memoryview) -> int:
    """Encode a Python object and copy it into an existing writable buffer.

    Encoding uses temporary message storage and bytes.
    Serialization failures leave the destination unchanged.

    Args:
        obj: Python object to encode
        buffer: Writable C-contiguous buffer; capacity is measured in bytes

    Returns:
        Number of bytes written

    Raises:
        RuntimeError: If encoding fails or buffer is too small
        TypeError: If the buffer is read-only
        BufferError: If the buffer is not C-contiguous
    """
    buffer = _byte_view(buffer)
    if buffer.readonly:
        raise TypeError("output buffer must be writable")
    msg = _c.create(max(buffer.nbytes, 2))
    _c.write_start(msg)
    _write_value(msg, obj)

    data = _c.get_buffer(msg)
    length = len(data)
    if length > buffer.nbytes:
        raise MirageError("output buffer is too small")
    buffer[:length] = data
    return length


def decode_from(buffer: bytes | bytearray | memoryview, length: int | None = None) -> Any:
    """Decode a Python object from a C-contiguous buffer.

    Input is copied into owned message storage, and decoded values own their
    payloads. 
    Noncontiguous buffers are rejected.

    Args:
        buffer: Buffer containing WXF data
        length: Valid byte count, between zero and buffer.nbytes (default)

    Returns:
        Decoded Python object
    """
    buffer = _byte_view(buffer)
    length = buffer.nbytes if length is None else int(length)
    if not 0 <= length <= buffer.nbytes:
        raise ValueError("length must be between zero and buffer byte capacity")
    return decode(buffer[:length])
