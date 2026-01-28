from .mirage import (
    encode, decode, encode_to, decode_from,
    i8, i16, i32,
    expr, sym,
    Expr, Sym,  # Type classes for isinstance checks
    I8, I16, I32,  # Type classes for isinstance checks
    MirageError,  # Exception class
)

__all__ = [
    # Core functions
    'encode', 'decode', 'encode_to', 'decode_from',

    # Type constructors
    'i8', 'i16', 'i32', 'expr', 'sym',

    # Type classes (for isinstance checks)
    'Expr', 'Sym', 'I8', 'I16', 'I32',

    # Exception
    'MirageError',
]

__version__ = "2.0.0"
