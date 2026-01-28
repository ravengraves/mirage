#!/usr/bin/env python3
# test_write.py - Write WXF data from Python for C to verify
# Tests all types: integers, float, string, bytes, list, tuple (function),
# dict (association), symbol, numpy arrays, and None

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'lang/python'))

import mirage
import numpy as np

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output_file>", file=sys.stderr)
        return 1

    output_file = sys.argv[1]

    # Create a comprehensive test structure that exercises all types
    # The C program will read this in the exact same order
    test_data = [
        # 1. i8 - small integer (explicit)
        mirage.i8(42),
        # 2. i16 - medium integer (explicit)
        mirage.i16(-1000),
        # 3. i32 - larger integer (explicit)
        mirage.i32(100000),
        # 4. i64 - plain int (default)
        9223372036854775807,  # max i64
        # 5. f64 - floating point
        3.141592653589793,
        # 6. string
        "Hello, Mirage!",
        # 7. bytes (binary)
        b"\x00\x01\x02\x03\xff\xfe\xfd",
        # 8. list (becomes List function)
        [1, 2, 3],
        # 9. Expr (becomes WXF function)
        mirage.expr('Plus', 10, 20),
        # 10. dict (becomes association)
        {"x": 1.5, "y": 2.5},
        # 11. symbol
        mirage.sym("Global`mySymbol"),
        # 12. None (becomes Symbol "Null")
        None,
        # 13. bool True (becomes i8 1)
        True,
        # 14. bool False (becomes i8 0)
        False,
        # 15. nested structure
        {
            "name": "test",
            "values": [1, 2, 3],
            "nested": mirage.expr("Fn", "arg1", "arg2"),
        },
        # 16. numpy int64 array
        np.array([10, 20, 30, 40], dtype=np.int64),
        # 17. numpy float64 array 2D
        np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.float64),
        # 18. BigInt (larger than i64)
        10**30,
        # 19. Empty list
        [],
        # 20. Empty dict
        {},
    ]

    # Encode the list wrapper
    data = mirage.encode(test_data)

    # Write to file
    with open(output_file, 'wb') as f:
        f.write(data)

    print(f"Wrote {len(data)} bytes to {output_file}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
