#!/usr/bin/env python3
# test_read.py - Read and verify WXF data written by C
# Validates all types match expected values

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'lang/python'))

import mirage
import numpy as np


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input_file>", file=sys.stderr)
        return 1

    input_file = sys.argv[1]

    # Read file
    with open(input_file, 'rb') as f:
        data = f.read()

    print(f"Read {len(data)} bytes from {input_file}")

    # Decode
    result = mirage.decode(data)

    # Should be a list with 20 elements
    assert isinstance(result, list), f"Expected list, got {type(result)}"
    assert len(result) == 20, f"Expected 20 elements, got {len(result)}"
    print(f"Decoded List with {len(result)} elements")

    # 1. i8 - 42
    assert result[0] == 42, f"1. Expected 42, got {result[0]}"
    print(f"  1. i8: {result[0]} ✓")

    # 2. i16 - -1000
    assert result[1] == -1000, f"2. Expected -1000, got {result[1]}"
    print(f"  2. i16: {result[1]} ✓")

    # 3. i32 - 100000
    assert result[2] == 100000, f"3. Expected 100000, got {result[2]}"
    print(f"  3. i32: {result[2]} ✓")

    # 4. i64 - max i64
    assert result[3] == 9223372036854775807, f"4. Expected max i64, got {result[3]}"
    print(f"  4. i64: {result[3]} ✓")

    # 5. f64 - pi
    assert abs(result[4] - 3.141592653589793) < 1e-15, f"5. f64 mismatch: {result[4]}"
    print(f"  5. f64: {result[4]:.15f} ✓")

    # 6. string
    assert result[5] == "Hello from C!", f"6. Expected 'Hello from C!', got {result[5]}"
    print(f"  6. string: \"{result[5]}\" ✓")

    # 7. binary
    expected_binary = bytes([0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF])
    assert result[6] == expected_binary, f"7. Binary mismatch"
    print(f"  7. binary: {result[6].hex()} ✓")

    # 8. list [100, 200, 300]
    assert result[7] == [100, 200, 300], f"8. Expected [100, 200, 300], got {result[7]}"
    print(f"  8. list: {result[7]} ✓")

    # 9. Expr Multiply(5, 7) - should be Expr now
    assert isinstance(result[8], mirage.Expr), f"9. Expected Expr, got {type(result[8])}"
    assert result[8] == mirage.expr('Multiply', 5, 7), f"9. Expected Expr('Multiply', 5, 7), got {result[8]}"
    assert result[8].head == 'Multiply', f"9. Expected head 'Multiply', got {result[8].head}"
    assert result[8].args == (5, 7), f"9. Expected args (5, 7), got {result[8].args}"
    print(f"  9. function: {result[8]} ✓")

    # 10. dict {"a": 10, "b": 20}
    assert result[9] == {"a": 10, "b": 20}, f"10. Expected dict, got {result[9]}"
    print(f"  10. dict: {result[9]} ✓")

    # 11. Symbol "TestSymbol" - should be Sym now
    assert isinstance(result[10], mirage.Sym), f"11. Expected Sym, got {type(result[10])}"
    assert isinstance(result[10], str), f"11. Sym should be str subclass"
    assert result[10] == "TestSymbol", f"11. Expected 'TestSymbol', got {result[10]}"
    print(f"  11. symbol: {result[10]} ✓")

    # 12. None (from Symbol "Null")
    assert result[11] is None, f"12. Expected None, got {result[11]}"
    print(f"  12. None: {result[11]} ✓")

    # 13. True (from Symbol "True")
    assert result[12] is True, f"13. Expected True, got {result[12]}"
    print(f"  13. True: {result[12]} ✓")

    # 14. False (from Symbol "False")
    assert result[13] is False, f"14. Expected False, got {result[13]}"
    print(f"  14. False: {result[13]} ✓")

    # 15. Nested structure
    nested = result[14]
    assert isinstance(nested, dict), f"15. Expected dict, got {type(nested)}"
    assert nested["inner_list"] == [1, 2], f"15. inner_list mismatch"
    assert isinstance(nested["inner_fn"], mirage.Expr), f"15. inner_fn should be Expr"
    assert nested["inner_fn"].head == "Add", f"15. inner_fn name mismatch"
    assert abs(nested["inner_fn"].args[0] - 1.5) < 1e-10, f"15. inner_fn arg1 mismatch"
    assert abs(nested["inner_fn"].args[1] - 2.5) < 1e-10, f"15. inner_fn arg2 mismatch"
    print(f"  15. nested: {nested} ✓")

    # 16. NumericArray i32
    arr16 = result[15]
    assert isinstance(arr16, np.ndarray), f"16. Expected ndarray, got {type(arr16)}"
    assert arr16.dtype == np.int32, f"16. Expected int32, got {arr16.dtype}"
    assert list(arr16) == [1, 2, 3, 4, 5], f"16. Array mismatch"
    print(f"  16. numpy i32 array: {list(arr16)} ✓")

    # 17. NumericArray f64 2D
    arr17 = result[16]
    assert isinstance(arr17, np.ndarray), f"17. Expected ndarray, got {type(arr17)}"
    assert arr17.dtype == np.float64, f"17. Expected float64, got {arr17.dtype}"
    assert arr17.shape == (2, 2), f"17. Expected shape (2,2), got {arr17.shape}"
    expected = np.array([[10.0, 20.0], [30.0, 40.0]])
    assert np.allclose(arr17, expected), f"17. Array mismatch"
    print(f"  17. numpy f64 2D array: {arr17.tolist()} ✓")

    # 18. BigInt
    assert result[17] == -9999999999999999999999999999999, f"18. BigInt mismatch"
    print(f"  18. BigInt: {result[17]} ✓")

    # 19. Empty list
    assert result[18] == [], f"19. Expected [], got {result[18]}"
    print(f"  19. empty list: {result[18]} ✓")

    # 20. Empty dict
    assert result[19] == {}, f"20. Expected {{}}, got {result[19]}"
    print(f"  20. empty dict: {result[19]} ✓")

    print("\nAll C->Python tests passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
