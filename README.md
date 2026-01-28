# Mirage 🔮
Mirage is a serialization/deserialization library using [a flexible binary self-describing data format](wxf.md) for transmitting robot thoughts.

## WXF Data Format 🖼
This library encodes and decodes "Wolfram Exchange Format" / "WXF", a data format developed in 2017 by Wolfram Research.
WXF supports serialization of several datatypes important for scientific computing including tensors, binary strings, and associative arrays.

## Rationale 🖌
WXF, as a self-describing format, is a good fit for exchange of data in large distributed systems where schmeas might not be known ahead of time.
This also makes the format suitable in robotics development where many programs in multiple programming languages might be running on a system and passing messages to each other.
The flexible nature of WXF allows individual programs to be adjusted on-the-fly without needing to recompile all nodes or re-generate schemas in multiple programming languages.

The goal is to put this format in all of our firmware, sensor drivers, and other programs so we can automatically discover and interface with any hardware and software in our robotics systems.

Additionally, we want to support easy visualization of data in this format.

## Safety Limits
When decoding untrusted input, always cap payloads to the storage you have
available. In C, pass `max_bytes` to `mirage_read_numericarray` based on your
output buffer size (or `MIRAGE_SKIP_DATA` to read metadata only). On
microcontrollers, keep these caps at `INT16_MAX` (32767) or smaller.
`MIRAGE_MAX_TENSOR_BYTES` defines a desktop-scale reference limit (two 8k RGBA
frames).

## Example (C) 🚀
```C
#include "mirage.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(void) {
    // ------ 1. Allocate message buffer (1024 bytes, malloc) --------------
    mirage_msg *msg = mirage_create(1024, 0);
    if(!msg) {
        perror("mirage_create");
        return 1;
    }

    // ------ 2. Write message data  ---------------------------------------
    // List:
        // string "hello"
        // float64 count
    i64 err = mirage_write_start(msg);       // Write message start

    err |= mirage_write_fn(msg, "List",      // "List" function with 2 args
            sizeof("List")-1, 2);
    err |= mirage_write_string(msg, "Hello", // First argument: Hello string
        sizeof("Hello")-1);
    err |= mirage_write_f64(msg, 42.0);      // Second Argument: double

    if (err < 0) {
        fprintf(stderr, "error writing message data\n");
        return 1;
    }

    // ------ 3. Read the packet back.  ------------------------------------
    err |= mirage_read_start(msg); // rewind cursor and validate start

    // Read function header
    char name[32];
    i64 nlen, argc;
    err |= mirage_read_fn(msg, name, &nlen, 32, &argc);
    // Read each argument in the same order: string, double
    char buf[64];
    i64 blen;
    err |= mirage_read_string(msg, buf, &blen, sizeof(buf) - 1);
    f64 out2;
    err |= mirage_read_f64(msg, &out2);

    if(err < 0) {
        fprintf(stderr, "error reading message data\n");
        return 1;
    }

    buf[blen] = '\0';
    printf("%s, %f\n", buf, out2);

    // ------ 4. Free buffer (if needed)  ----------------------------------
    // mirage_destroy(&msg, 0);
    return 0;
}
```

Output:
```
Hello, 42.000000
```

## Python API 🐍
The Python bindings live in `lang/python` and link against the same `libmirage` built by `make`.

Install (after building the C library):
```
make
pip install ./lang/python    # or pip install -e ./lang/python for local edits
```

Example (mirrors `test/test_write.py` + `test/test_read.py`):
```python
import mirage, numpy as np

message = [
    mirage.i8(42),                  # explicit 8-bit
    mirage.i16(-1000),              # explicit 16-bit
    9223372036854775807,            # plain ints auto-pick i64 / BigInt
    3.141592653589793,
    "Hello, Mirage!",
    b"\x00\x01\x02\x03\xff\xfe\xfd",
    [1, 2, 3],                      # List
    mirage.expr("Plus", 10, 20),   # WXF function
    {"x": 1.5, "y": 2.5},        # Association
    mirage.sym("Global`mySymbol"), # Symbol
    None, True, False,
    np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.float64),
]

blob = mirage.encode(message)
round_trip = mirage.decode(blob)

assert isinstance(round_trip[7], mirage.Expr)
assert round_trip[9] == mirage.sym("Global`mySymbol")
```

Quick mapping:
- `list` and `tuple` → WXF List. Use `mirage.expr(name, *args)` for functions instead of tuples.
- Plain `int` → i64 (or BigInt if outside 64‑bit). Use `mirage.i8/i16/i32` to force width.
- `mirage.sym(name)` produces a Symbol. "Null"/"True"/"False" decode to `None`/`True`/`False`; other symbols decode to `mirage.Sym` (a `str` subclass).
- `numpy.ndarray` round-trips as NumericArray with dtype and shape preserved.

See the Python tests in `test/test_write.py` and `test/test_read.py` for fuller coverage.

## Build and Install 📖

```
make
make check
sudo make install
```

Uninstall
```
sudo make uninstall
```

## License ⚖

Copyright © 2025 Alec Graves

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
