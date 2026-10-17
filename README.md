# Mirage 🔮

[![Mirage CI](https://github.com/ravengraves/mirage/actions/workflows/ci.yml/badge.svg)](https://github.com/ravengraves/mirage/actions/workflows/ci.yml)

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

## C reader compatibility

String, binary, symbol, bigint, bigreal, and function-name readers use `-1`
(`MIRAGE_SKIP_COPY`) to skip copying while validating and consuming the value.
Zero is an actual capacity and accepts only an empty value. Positive
capacities require a destination, and capacities below `-1` are invalid.
Numeric array reads can independently skip reading dimensions
(with `max_rank == -1`) and payloads (with `max_bytes == MIRAGE_SKIP_COPY`).

Failed reads preserve the entry cursor and all outputs. Invalid arguments return
`MIRAGE_ERR_BADINPUT`; truncated input or insufficient capacity returns
`MIRAGE_ERR_BUFFERSIZE`; wrong tags, overflowing varints and malformed encodings
return `MIRAGE_ERR_BADDATA`. Varints accept the inclusive signed maximum in at
most nine bytes (standard) or three bytes (embedded).
Any zero array dimension makes the payload empty.
`mirage_create` requires at least two bytes for the message header.

Only `MIRAGE_NUMERICARRAY` and `MIRAGE_RULE` are written/decoded
(`PACKEDARRAY` / `RULE_DELAYED` are transformed because this implementation
focuses on simple data representation rather than full WXF compatibility).

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
- `list` and `tuple` -> WXF List. Use `mirage.expr(name, *args)` for functions instead of tuples.
- Plain `int` -> i64 (or BigInt if outside 64-bit). Use `mirage.i8/i16/i32` to force width.
- `mirage.sym(name)` produces a Symbol. "Null"/"True"/"False" decode to `None`/`True`/`False`; other symbols decode to `mirage.Sym` (a `str` subclass).
- `numpy.ndarray` round-trips as NumericArray with dtype and shape preserved.

`encode_to` and `decode_from` accept C-contiguous buffers, including typed memoryviews;
capacities and explicit lengths are measured in bytes.
Output buffers must be writable. Noncontiguous buffers are rejected.

Encoding with the Python API uses temporary message storage and copies the result through Python bytes into the destination, keeping it unchanged on serialization failure.
Decoding copies input into owned message storage and creates independently owned payloads.
Contiguous NumPy inputs pass directly to the C writer without an intermediate copy.

See the Python tests in `test/test_write.py` and `test/test_read.py` for fuller coverage.

## Build and Install 📖

```
make
make check
sudo make install
```

Install documentation separately (requires `scdoc`):
```sh
sudo make install-docs
```

Uninstall
```
sudo make uninstall
```

`make check` runs the C runtime regression tests (including `test_fuzz` and
`test_fuzz_embedded`), static analysis, and ASan/UBSan. It does not run timed
libFuzzer checks. `make check-slow` adds libFuzzer checks, with a default
budget of 10 seconds per binary.

These C checks need GCC, Clang with sanitizer support, clang-tidy, Cppcheck,
and Ninja. The Ubuntu CI workflow installs them before running checks.

Python checks are separate. After building and installing the bindings:

```
make check-python
```

GitHub Actions runs `make check` and `make check-python`; slow checks are local
or explicitly invoked. Ninja also exposes `check` and `check-slow`.

## Analysis

Static analysis and sanitizer targets are available from the repo root:
```
make compile_commands.json
make tidy
make clang-analyze
make fanalyzer
make cppcheck
make sanitize-check
make analysis
```

`make cppcheck` uses the Ninja-generated `compile_commands.json`, generating it
when needed without compiling the project. It checks `src/mirage.c`,
`src/mirage_embedded.c`, their included headers, and the seven C test programs in
`build.ninja` (including `test/test_all.c`). The Python implementation is outside
this database.
Install [Cppcheck 2.11 or newer](https://sourceforge.net/p/cppcheck/news/2023/06/cppcheck-211/)
and Ninja; on Debian/Ubuntu:

```
sudo apt-get install cppcheck ninja-build
```

The target enables error, warning, performance, portability, and missing project
include checks, with exhaustive value-flow analysis, all preprocessor
configurations, and inconclusive findings. Review inconclusive findings for
false positives. System include diagnostics are suppressed because Cppcheck
models the standard library. Style and unused-function checks are off by default;
unused public library functions need not be defects. To include style checks:

```
make cppcheck CPPCHECK_EXTRA_FLAGS=--enable=style
```

Override `CPPCHECK`, `CPPCHECK_FLAGS`, or `CPPCHECK_EXTRA_FLAGS` as needed.
`make cppcheck` exits nonzero on findings or tool failures, so use it directly
as a CI gate. `make analysis`, `make check`, and `make check-slow` also run it through the existing
advisory analysis group, which reports findings and continues. The compilation
database uses flags from `build.ninja`; Make-only `CFLAGS`/`CPPFLAGS` overrides
are not reflected in it.

`make sanitize-check` rebuilds into `build-sanitize/` with Clang ASan+UBSan and runs the full C runtime suite, including the fuzz regression executables, leaving the normal `build/` directory alone.

The fuzz-style tests include invalid message pointers/fields for both APIs and
embedded array regressions for NULL destinations, negative capacities, skip
modes, and unaligned buffers. The embedded array tests check all 12 types
against independently constructed wire bytes, including scalar and empty arrays.

Optional libFuzzer targets exercise numeric-array readers and writers in both
implementations with ASan/UBSan. Clang with libFuzzer and Python 3 are required:

```
make fuzz-check
make fuzz-check FUZZ_SECONDS=60 FUZZ_SEED=20261013
```

The default runs each of four targets for ten seconds. Deterministic seeds,
evolved corpora and failure artifacts live in `build-fuzz/`; remove that directory
to start with fresh corpora. Replay a failure by passing its artifact path to
the matching `build-fuzz/read_standard`, `read_embedded`, `write_standard`, or
`write_embedded` executable. Time-limited fuzzing does not guarantee exhaustive
coverage or identical run counts.

## License ⚖

MIT License

