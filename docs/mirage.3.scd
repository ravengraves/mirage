MIRAGE(3) "Mirage" "Mirage Manual"

# NAME

mirage - WXF serialization and deserialization library

# SYNOPSIS

*#include <mirage.h>*

```
mirage_msg *mirage_create(int64_t buffer_size, void *(*alloc)(size_t));
void mirage_destroy(mirage_msg **msg, void (*dealloc)(void *));
int64_t mirage_write_start(mirage_msg *msg);
int64_t mirage_read_start(mirage_msg *msg);
int64_t mirage_write_fn(mirage_msg *msg, char *name, int64_t nlen,
	int64_t argc);
int64_t mirage_write_string(mirage_msg *msg, char *str, int64_t len);
int64_t mirage_write_f64(mirage_msg *msg, double value);
int64_t mirage_read_fn(mirage_msg *msg, char *name, int64_t *nlen,
	int64_t max_name, int64_t *argc);
int64_t mirage_read_string(mirage_msg *msg, char *buf, int64_t *len,
	int64_t max_len);
int64_t mirage_read_f64(mirage_msg *msg, double *value);
int64_t mirage_write_numericarray(mirage_msg *msg, mirage_array_type type,
	int64_t rank, int64_t *dims, void *data);
int64_t mirage_read_numericarray(mirage_msg *msg, mirage_array_type *type,
	int64_t *rank, int64_t *dims, int64_t max_rank, void *data,
	int64_t max_bytes);
```

# DESCRIPTION

Mirage encodes and decodes Wolfram Exchange Format (WXF) messages. It supports
scalars, strings, binary payloads, symbols, big integers, functions,
associations, and numeric arrays. The main API operates on a caller-owned or
library-allocated *mirage_msg* buffer and advances a cursor as fields are
written or read.

The library is suited for inter-process or cross-language serialization where a
self-describing binary format is useful. The repository also includes an
embedded variant with smaller integer widths and no heap allocation.

# WORKFLOW

. Allocate or initialize a message buffer.
. Call *mirage_write_start*() before encoding values.
. Encode values in order with the appropriate write functions.
. Call *mirage_read_start*() to rewind and validate the message.
. Decode values in the same order with the matching read functions.

# SAFETY

When decoding untrusted input, bound all output sizes. In particular,
*mirage_read_numericarray*() should receive a strict _max_bytes_ limit that
matches the destination buffer. Callers that only need metadata may use the
library's skip-data mode macros from the header.

String-like readers and function-name reads use _MIRAGE_SKIP_COPY_ (-1) to
skip copying. A zero capacity accepts only an empty value. Positive capacities
require a destination; capacities below -1 are invalid. Numeric-array dimensions
and payloads are skipped independently with _max_rank_ == -1 and
_max_bytes_ == _MIRAGE_SKIP_DATA_. Failed reads leave the cursor and outputs
unchanged. Wrong tags and malformed encodings return _MIRAGE_ERR_BADDATA_;
truncation and insufficient output capacity return _MIRAGE_ERR_BUFFERSIZE_.

# EXAMPLE

```
#include "mirage.h"
#include <stdio.h>

int main(void) {
	mirage_msg *msg = mirage_create(1024, 0);
	char name[32];
	char text[32];
	int64_t nlen = 0;
	int64_t argc = 0;
	int64_t tlen = 0;
	double value = 0.0;

	mirage_write_start(msg);
	mirage_write_fn(msg, "List", 4, 2);
	mirage_write_string(msg, "Hello", 5);
	mirage_write_f64(msg, 42.0);

	mirage_read_start(msg);
	mirage_read_fn(msg, name, &nlen, sizeof(name), &argc);
	mirage_read_string(msg, text, &tlen, sizeof(text));
	mirage_read_f64(msg, &value);

	text[tlen] = '\0';
	printf("%s %0.1f\n", text, value);
	mirage_destroy(&msg, 0);
	return 0;
}
```

Compile against the installed library with pkg-config:

```
cc -O2 example.c $(pkg-config --cflags --libs mirage)
```

To exercise the repository test programs directly:

```
make all
./build/test
./build/test_all
```

# BUILD

- *make all* builds shared and static Mirage libraries plus test executables.
- *make check* runs the native C tests.
- *make docs* generates documentation, including this man page via *scdoc*(1).
- *make install-lib* installs library files and pkg-config metadata.

The embedded library is installed with the rest of the Mirage library artifacts.

# FILES

_mirage.h_
	Public desktop API.

_mirage_embedded.h_
	Embedded API used by the smaller-footprint variant.

_mirage.pc_
	pkg-config metadata for the main library.

_mirage-embedded.pc_
	pkg-config metadata for the embedded library.

# SEE ALSO

*scdoc*(1), *pkg-config*(1)
