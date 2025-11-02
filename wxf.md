# WXF Format

## Introduction
WXF is a standardized binary data format (we are using version 1.0 of WXF). 
It supports encoding of several general datatypes commonly used in engineering and robotics applications. 
It is also very efficient, as a binary format.

The goal is to put this format on all of our microcontrollers and sensor drivers so we can automatically discover and interface with any robotics devices in our system.

Additionally, we want to support easy visualization of data in this format.

## BNF
Here is a grammar for the WXF format. It is meant to be a reference for implementations.

```bnf
# Here is a basic description of the BNF syntax we are using:
# <name> := <definition>
# <several-options> := <option1> | <option2> | <option3>
# <optionally-nothing> := <definition> | ε
# <repeated> := <definition>^<repeated> | ε
# <concatenated> := <definition1>^<definition2>^<definition3>

# Here is the WXF grammar:
<message> := <header>:<sequence>
<header> := 8|8C
<sequence> := <element>|<compressed>
<element> := <fn>|<int8>|<int16>|<int32>|<int64>|<double>|<string>|<binstring>
    |<symbol>|<bigint>|<bigreal>|<packedarray>|<numericarray>
    |<association>|<association-delayed>|<association-rule>
<optelements> :=<element>^<optelement> | ε
<fn> := f^<len>^<symbol>^<optelements>  # len is the number of args (optelements)
<int8> := C^<byte>
<int16> := j^<byte>^<byte>
<int32> := i^<byte>^<byte>^<byte>^<byte>
<int64> := L^<byte>^<byte>^<byte>^<byte>^<byte>^<byte>^<byte>^<byte>
<double> := r^<ieee-double>
<string> := S^<len>^<utf-8>
<binstring> := B^<len>^<optbytes>  # arbitrary binary data
<symbol> := s^<len>^<opt-utf-8>

<bigint> := I^<len>^<bigint-digits>
<bigint-digits> := <utf-8>

# arbitrary-precision floating real number:
<bigreal> := R^<len>^<realnumber>`<precision> 
           | R^<len>^<realnumber>`<precision>*^<exponent>
<realnumber> := <utf-8>  # e.g. "10.2345678912345678912345678"
<precision> := <utf-8>  # number of digits precision (like "26.1" for above)
<exponent> := <utf-8>  # e.g. 1234, if there is an exponent, *10^1234

# Efficient storage of many numeric elements of the same type:
<packedarray> := Á^<packedarray-type>^<array-dims>^<packedarray-data>
<packedarray-type> := <array-i8>|<array-i16>|<array-i32>|<array-i64>
    |<array-f32>|<array-f64>|array-c32>|<array-c64>
<array-dims> := <rank>^<lengths>  # e.g. 2 2 3 for a 2x3 array.
<rank> := <len>
<lengths> := <len>^<lengths> | ε
<packedarray-data> := <upto-64>^<packedarray-data> | ε
# Note, encoding or decoding fp exceptions (inf, NaN) is error for packed array.

# General numeric array type:
<numericarray> := Â^<numericarray-type>^<rank>^<array-dims>^<numericarray-data>
<numericarray-type> := <array-i8>|<array-i16>|<array-i32>|<array-i64>
    |<array-u8>|<array-u16>|<array-u32>|<array-u64>
    |<array-f32|<array-f64>|array-c32>|<array-c64>
<numericarray-data> := <upto-128>^<numericarray-data> | ε

# This is key-value a dictionary type.
<association> := A^<len>^<association-rules>
<association-rules> := <association-item>^<association-rules> | ε
<association-item> := <association-rule>|<association-rule-delayed>
<association-rule> := -^<key>^<value>
<association-rule-delayed> := :^<key>^<value>
<key> := <element>
<value> := <element>

# see table below for numeric array types
<array-i8> := x0
<array-i16> := x1
<array-i32> := x2
<array-i64> := x3
<array-u8> := x10
<array-u16> := x11
<array-u32> := x12
<array-u64> := x13
<array-f32> := x22
<array-f64> := x23
<array-c32> := x33
<array-c64> := x34

<compressed> := <zlib-compressed element>

<len> := <varint>
<byte> := \[0-255]
<optbytes> := <byte>^<optbytes> | ε 
<utf-8> := <utf-8-char>^<optutf-8>
<opt-utf-8> := <utf-8-char>^<utf-8> | ε 
<utf-8-char> := <byte>^<optbyte>^<optbyte>^<optbyte>  # yes, there are more rules
<upto-64> := <byte>^<optbyte>^<optbyte>^<optbyte>
    ^<optbyte>^<optbyte>^<optbyte>^<optbyte>
<upto-128> := <upto-64> | <upto-64>^<upto-64> # 128 bits max, used for c64
<ieee-double> := <byte>^<byte>^<byte>^<byte>^<byte>^<byte>^<byte>^<byte>
<ieee-float> := <byte>^<byte>^<byte>^<byte>
```

Fun examples:
```bnf
# complex number (not in array)
<complex> := f^[2]^Complex^<ieee-double>^<ieee-double>

# complex number in array
# (stored as concatenated real and imaginary parts)
<complex-array>:= Â^x34^1^1^<ieee-double>^<ieee-double>

# number with units
<quantity> := f^[2]^s^[8]^Quantity^<double>^<unit>
<unit> := <utf-8>  # e.g. "m/s"
```

## Header Start

For version 1.0 of WXF format (which we are using), the correct header start value is the character "8", or an integer value of 56.

## Compression

If the `<sequence>` is compressed with zip (zlib), the header is "8C". Otherwise, the header is just "8".

## Element Type Table

| Integer | Hex  | Character (ISO8859-1) | Type                         |
| ------- | ---- |---------------------|------------------------------|
| 102     | 0x66 | f                   | Function                     |
| 67      | 0x43 | C                   | Signed int8  (little endian) |
| 106     | 0x6A | j                   | Signed int16 (little endian) |
| 105     | 0x69 | i                   | Signed int32 (little endian) |
| 76      | 0x4C | L                   | Signed int64 (little endian) |
| 114     | 0x72 | r                   | IEEE-754 Double              |
| 83      | 0x53 | S                   | String                       |
| 66      | 0x42 | B                   | Binary String                |
| 115     | 0x73 | s                   | Symbol                       |
| 73      | 0x49 | I                   | Big int                      |
| 82      | 0x52 | R                   | Big real                     |
| 193     | 0xC1 | Á                   | Packed Array                 |
| 194     | 0xC2 | Â                   | Numeric Array                |
| 65      | 0x41 | A                   | Association                  |
| 58      | 0x3A | :                   | Association Delayed Rule     |
| 45      | 0x2D | -                   | Association Rule             |

## Varints

Variable length integers are encoded such that every byte except the last has its MSB set. This is accomplished by shifting each byte of the number right one and using the first bit of the byte to indicate the varint continues and using only the final 7 bits of each byte for the original number. Additionally, the number is encoded such that the lowest 7 bits is stored in the first varint message index.

Here is an example:
```
257 = b100000001
(little-endian) = (lsb) b00000001 (msb) b00000001
(varint-encoded) = b10000001 b00000010
(rearranged) = b00000010 b10000001
(drop first bit) = b0000010 b0000001
(decoded) = b0000000100000001 = 257
```

## Numeric Array Type Table

| Integer | Hexadecimal | Array Type                        |
| ------- | ----------- | --------------------------------- |
| 0       | 0x0         | int8                              |
| 1       | 0x1         | int16                             |
| 2       | 0x2         | int32                             |
| 3       | 0x3         | int64                             |
| 16      | 0x10        | uint8                             |
| 17      | 0x11        | uint16                            |
| 18      | 0x12        | uint32                            |
| 19      | 0x13        | uint64                            |
| 34      | 0x22        | IEEE-754 float                    |
| 35      | 0x23        | IEEE-754 double                   |
| 51      | 0x33        | IEEE-754 single-precision complex |
| 52      | 0x34        | IEEE-754 double-precision complex |

## Packed Array Type Table

| Integer | Hexadecimal | Array Type                        |
| ------- | ----------- | --------------------------------- |
| 0       | 0x0         | int8                              |
| 1       | 0x1         | int16                             |
| 2       | 0x2         | int32                             |
| 3       | 0x3         | int64                             |
| 34      | 0x22        | IEEE-754 float                    |
| 35      | 0x23        | IEEE-754 double                   |
| 51      | 0x33        | IEEE-754 single-precision complex |
| 52      | 0x34        | IEEE-754 double-precision complex |

> NOTE: Encoding or decoding floating point exception values (inf, NaN) in a _packed array_ IS AN ERROR.
> (numeric arrays can handle these values)