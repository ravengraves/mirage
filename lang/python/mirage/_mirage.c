/*  mirage/_mirage.c - thin C wrapper for mirage shared library
 *
 *  Purpose
 *  --------
 *  - Load mirage symbols from a platform-appropriate shared library at import time
 *  - Expose those functions to Python with proper argument/return conversion
 *  - Provide low-level primitives for the high-level Python API
 *
 *  Author:  Alec Graves
 *  Licence: AGPLv3
 */

#define _DARWIN_SOURCE
#define _GNU_SOURCE
#define PY_SSIZE_T_CLEAN

#include <Python.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE dynlib_handle;
#define DYNLIB_FLAG_NOW 0
#define DYNLIB_FLAG_LOCAL 0

static const char *dynlib_error_string(void) {
	static char buf[256];
	DWORD err = GetLastError();
	if(err == 0) {
		return "unknown error";
	}
	DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
				 | FORMAT_MESSAGE_IGNORE_INSERTS,
				 NULL,
				 err,
				 0,
				 buf,
				 (DWORD) sizeof(buf),
				 NULL);
	if(n == 0) {
		snprintf(buf, sizeof(buf), "Windows error %lu",
			 (unsigned long) err);
	} else {
		while(n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
			buf[n - 1] = '\0';
			n -= 1;
		}
	}
	return buf;
}

static dynlib_handle dynlib_open(const char *name, int flags) {
	(void) flags;
	SetLastError(0);
	return LoadLibraryA(name);
}

static void *dynlib_symbol(dynlib_handle handle, const char *sym) {
	SetLastError(0);
	return (void *) GetProcAddress(handle, sym);
}
#else
#include <dlfcn.h>
typedef void *dynlib_handle;
#define DYNLIB_FLAG_NOW RTLD_NOW
#define DYNLIB_FLAG_LOCAL RTLD_LOCAL

static const char *dynlib_error_string(void) {
	const char *err = dlerror();
	return err ? err : "unknown error";
}

static dynlib_handle dynlib_open(const char *name, int flags) {
	return dlopen(name, flags);
}

static void *dynlib_symbol(dynlib_handle handle, const char *sym) {
	return dlsym(handle, sym);
}
#endif

#include "mirage.h"

/* ---------------- 1.  symbol pointers -------------------------------------- */

static dynlib_handle lib_handle = NULL;
static const char *loaded_lib_name = "libmirage";

static mirage_msg *(*p_mirage_create)(int64_t, void *(*)(size_t)) = NULL;
static void (*p_mirage_destroy)(mirage_msg **, void (*)(void *)) = NULL;
static int64_t (*p_mirage_write_start)(mirage_msg *) = NULL;
static int64_t (*p_mirage_read_start)(mirage_msg *) = NULL;
static int64_t (*p_mirage_write_fn)(mirage_msg *, char *, int64_t, int64_t) = NULL;
static int64_t (*p_mirage_write_i8)(mirage_msg *, int8_t) = NULL;
static int64_t (*p_mirage_write_i16)(mirage_msg *, int16_t) = NULL;
static int64_t (*p_mirage_write_i32)(mirage_msg *, int32_t) = NULL;
static int64_t (*p_mirage_write_i64)(mirage_msg *, int64_t) = NULL;
static int64_t (*p_mirage_write_f64)(mirage_msg *, double) = NULL;
static int64_t (*p_mirage_write_string)(mirage_msg *, char *, int64_t) = NULL;
static int64_t (*p_mirage_write_bin)(mirage_msg *, uint8_t *, int64_t) = NULL;
static int64_t (*p_mirage_write_sym)(mirage_msg *, char *, int64_t) = NULL;
static int64_t (*p_mirage_write_bigint)(mirage_msg *, char *, int64_t) = NULL;
static int64_t (*p_mirage_write_bigreal)(mirage_msg *, char *, int64_t) = NULL;
static int64_t (*p_mirage_write_numericarray)(mirage_msg *, mirage_array_type,
					      int64_t, int64_t *, void *) = NULL;
static int64_t (*p_mirage_write_association)(mirage_msg *, int64_t) = NULL;
static int64_t (*p_mirage_write_assoc_rule)(mirage_msg *) = NULL;

static int64_t (*p_mirage_read_fn)(mirage_msg *, char *, int64_t *, int64_t, int64_t *) = NULL;
static int64_t (*p_mirage_read_i8)(mirage_msg *, int8_t *) = NULL;
static int64_t (*p_mirage_read_i16)(mirage_msg *, int16_t *) = NULL;
static int64_t (*p_mirage_read_i32)(mirage_msg *, int32_t *) = NULL;
static int64_t (*p_mirage_read_i64)(mirage_msg *, int64_t *) = NULL;
static int64_t (*p_mirage_read_f64)(mirage_msg *, double *) = NULL;
static int64_t (*p_mirage_read_string)(mirage_msg *, char *, int64_t *, int64_t) = NULL;
static int64_t (*p_mirage_read_bin)(mirage_msg *, uint8_t *, int64_t *, int64_t) = NULL;
static int64_t (*p_mirage_read_sym)(mirage_msg *, char *, int64_t *, int64_t) = NULL;
static int64_t (*p_mirage_read_bigint)(mirage_msg *, char *, int64_t *, int64_t) = NULL;
static int64_t (*p_mirage_read_bigreal)(mirage_msg *, char *, int64_t *, int64_t) = NULL;
static int64_t (*p_mirage_read_numericarray)(mirage_msg *, mirage_array_type *,
					     int64_t *, int64_t *, int64_t, void *, int64_t) = NULL;
static int64_t (*p_mirage_read_association)(mirage_msg *, int64_t *) = NULL;
static int64_t (*p_mirage_read_assoc_rule)(mirage_msg *) = NULL;
static int64_t (*p_mirage_peek_element_type)(mirage_msg *, mirage_element_type *) = NULL;

static const uint8_t *p_mirage_array_type_size = NULL;

/* ------------------------ 2.  load a single symbol ------------------------- */

#if defined(_WIN32)
static const char *const MIRAGE_LIBRARY_CANDIDATES[] = {
	"mirage.dll",
	"libmirage.dll",
};
#elif defined(__APPLE__)
static const char *const MIRAGE_LIBRARY_CANDIDATES[] = {
	"libmirage.dylib",
	"libmirage.so",
};
#else
static const char *const MIRAGE_LIBRARY_CANDIDATES[] = {
	"libmirage.so",
	"libmirage.dylib",
};
#endif

static dynlib_handle
load_library_candidates(const char *logical_name, const char *const *candidates,
			size_t candidate_count, int flags) {
	char attempts[1024] = {0};
	for(size_t i = 0; i < candidate_count; ++i) {
		dynlib_handle handle = dynlib_open(candidates[i], flags);
		if(handle) {
			loaded_lib_name = candidates[i];
			return handle;
		}

		const char *err = dynlib_error_string();
		size_t used = strlen(attempts);
		if(used < sizeof(attempts) - 1) {
			snprintf(attempts + used, sizeof(attempts) - used, "%s%s (%s)",
				 used ? "; " : "", candidates[i], err);
		}
	}

	PyErr_Format(PyExc_RuntimeError,
		     "Failed to load shared library for %s. Tried: %s",
		     logical_name,
		     attempts[0] ? attempts : "(none)");
	return NULL;
}

static int
load_symbol(const char *sym, void **ptr) {
	void *addr = dynlib_symbol(lib_handle, sym);
	if (!addr) {
		PyErr_Format(PyExc_RuntimeError,
			     "Failed to load symbol %s from %s: %s",
			     sym, loaded_lib_name, dynlib_error_string());
		return -1;
	}
	*ptr = addr;
	return 0;
}

/* ----------- 3.  Create a PyCapsule holding a mirage_msg* ------------------ */

static void
capsule_destructor(PyObject *capsule) {
	mirage_msg *msg = (mirage_msg *)PyCapsule_GetPointer(capsule, "mirage_msg");
	if (msg) {
		p_mirage_destroy(&msg, NULL);
	}
}

static PyObject *
capsule_from_msg(mirage_msg *msg) {
	return PyCapsule_New((void *)msg, "mirage_msg", capsule_destructor);
}

static mirage_msg *
msg_from_capsule(PyObject *capsule) {
	if (!PyCapsule_CheckExact(capsule)) {
		PyErr_SetString(PyExc_TypeError, "expected a mirage_msg capsule");
		return NULL;
	}
	return (mirage_msg *)PyCapsule_GetPointer(capsule, "mirage_msg");
}

/* -------------------- 4.  Python exposed API ------------------------------- */

static PyObject *
py_mirage_create(PyObject *self, PyObject *args) {
	int64_t size;
	if (!PyArg_ParseTuple(args, "L", &size)) {
		return NULL;
	}

	mirage_msg *msg = p_mirage_create(size, NULL);
	if (!msg) {
		PyErr_SetString(PyExc_MemoryError, "Failed to allocate mirage_msg");
		return NULL;
	}
	return capsule_from_msg(msg);
}

static PyObject *
py_mirage_write_start(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_start(msg);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_start failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_read_start(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int64_t rc = p_mirage_read_start(msg);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_start failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_fn(PyObject *self, PyObject *args) {
	PyObject *capsule;
	const char *name;
	Py_ssize_t name_len;
	int64_t argc;

	if (!PyArg_ParseTuple(args, "Os#L", &capsule, &name, &name_len, &argc)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_fn(msg, (char *)name, (int64_t)name_len, argc);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_fn failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_i8(PyObject *self, PyObject *args) {
	PyObject *capsule;
	int8_t value;

	if (!PyArg_ParseTuple(args, "Ob", &capsule, &value)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_i8(msg, value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_i8 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_i16(PyObject *self, PyObject *args) {
	PyObject *capsule;
	int16_t value;

	if (!PyArg_ParseTuple(args, "Oh", &capsule, &value)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_i16(msg, value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_i16 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_i32(PyObject *self, PyObject *args) {
	PyObject *capsule;
	int32_t value;

	if (!PyArg_ParseTuple(args, "Oi", &capsule, &value)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_i32(msg, value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_i32 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_i64(PyObject *self, PyObject *args) {
	PyObject *capsule;
	int64_t value;

	if (!PyArg_ParseTuple(args, "OL", &capsule, &value)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_i64(msg, value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_i64 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_f64(PyObject *self, PyObject *args) {
	PyObject *capsule;
	double value;

	if (!PyArg_ParseTuple(args, "Od", &capsule, &value)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_f64(msg, value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_f64 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_string(PyObject *self, PyObject *args) {
	PyObject *capsule;
	const char *value;
	Py_ssize_t length;

	if (!PyArg_ParseTuple(args, "Os#", &capsule, &value, &length)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_string(msg, (char *)value, (int64_t)length);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_string failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_bin(PyObject *self, PyObject *args) {
	PyObject *capsule, *data_obj;
	Py_buffer view;

	if (!PyArg_ParseTuple(args, "OO", &capsule, &data_obj)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	if (PyObject_GetBuffer(data_obj, &view, PyBUF_SIMPLE) < 0) {
		return NULL;
	}

	int64_t rc = p_mirage_write_bin(msg, (uint8_t *)view.buf, (int64_t)view.len);
	PyBuffer_Release(&view);

	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_bin failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_sym(PyObject *self, PyObject *args) {
	PyObject *capsule;
	const char *name;
	Py_ssize_t length;

	if (!PyArg_ParseTuple(args, "Os#", &capsule, &name, &length)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_sym(msg, (char *)name, (int64_t)length);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_sym failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_bigint(PyObject *self, PyObject *args) {
	PyObject *capsule;
	const char *value;
	Py_ssize_t length;

	if (!PyArg_ParseTuple(args, "Os#", &capsule, &value, &length)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_bigint(msg, (char *)value, (int64_t)length);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_bigint failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_bigreal(PyObject *self, PyObject *args) {
	PyObject *capsule;
	const char *value;
	Py_ssize_t length;

	if (!PyArg_ParseTuple(args, "Os#", &capsule, &value, &length)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_bigreal(msg, (char *)value, (int64_t)length);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_bigreal failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_numericarray(PyObject *self, PyObject *args) {
	PyObject *capsule, *dims_obj, *data_obj;
	int array_type;
	Py_buffer view;

	if (!PyArg_ParseTuple(args, "OiOO", &capsule, &array_type, &dims_obj, &data_obj)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	/* Parse dimensions list */
	Py_ssize_t rank = PyList_Size(dims_obj);
	if (rank < 0) {
		PyErr_SetString(PyExc_TypeError, "dims must be a list");
		return NULL;
	}

	int64_t dims[32];
	if (rank > 32) {
		PyErr_SetString(PyExc_ValueError, "rank cannot exceed 32");
		return NULL;
	}

	for (Py_ssize_t i = 0; i < rank; i++) {
		PyObject *item = PyList_GetItem(dims_obj, i);
		dims[i] = PyLong_AsLongLong(item);
		if (PyErr_Occurred()) return NULL;
	}

	/* Get data buffer */
	if (PyObject_GetBuffer(data_obj, &view, PyBUF_SIMPLE) < 0) {
		return NULL;
	}

	int64_t rc = p_mirage_write_numericarray(msg, (mirage_array_type)array_type,
						 (int64_t)rank, dims, view.buf);
	PyBuffer_Release(&view);

	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_numericarray failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_association(PyObject *self, PyObject *args) {
	PyObject *capsule;
	int64_t length;

	if (!PyArg_ParseTuple(args, "OL", &capsule, &length)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_association(msg, length);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_association failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

static PyObject *
py_mirage_write_assoc_rule(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int64_t rc = p_mirage_write_assoc_rule(msg);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_write_assoc_rule failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(rc);
}

/* ---------------------- Read functions ------------------------------------- */

static PyObject *
py_mirage_read_fn(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	char name[256];
	int64_t name_len, argc;

	int64_t rc = p_mirage_read_fn(msg, name, &name_len, 256, &argc);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_fn failed: %lld", (long long)rc);
		return NULL;
	}
	return Py_BuildValue("(s#L)", name, (Py_ssize_t)name_len, argc);
}

static PyObject *
py_mirage_read_i8(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int8_t value;
	int64_t rc = p_mirage_read_i8(msg, &value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_i8 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLong(value);
}

static PyObject *
py_mirage_read_i16(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int16_t value;
	int64_t rc = p_mirage_read_i16(msg, &value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_i16 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLong(value);
}

static PyObject *
py_mirage_read_i32(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int32_t value;
	int64_t rc = p_mirage_read_i32(msg, &value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_i32 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLong(value);
}

static PyObject *
py_mirage_read_i64(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int64_t value;
	int64_t rc = p_mirage_read_i64(msg, &value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_i64 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(value);
}

static PyObject *
py_mirage_read_f64(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	double value;
	int64_t rc = p_mirage_read_f64(msg, &value);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_f64 failed: %lld", (long long)rc);
		return NULL;
	}
	return PyFloat_FromDouble(value);
}

static PyObject *
py_mirage_read_string(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	/* First, peek at the length by reading with max_length=-1 */
	int64_t length;
	int64_t saved_cursor = msg->cursor;

	/* Skip the type byte */
	msg->cursor++;

	/* Read varint length - simple implementation */
	length = 0;
	int shift = 0;
	uint8_t byte;
	do {
		byte = msg->data[msg->cursor++];
		length |= (int64_t)(byte & 0x7F) << shift;
		shift += 7;
	} while (byte & 0x80);

	/* Restore cursor and allocate buffer */
	msg->cursor = saved_cursor;

	char *buf = (char *)malloc(length + 1);
	if (!buf) {
		PyErr_NoMemory();
		return NULL;
	}

	int64_t actual_len;
	int64_t rc = p_mirage_read_string(msg, buf, &actual_len, length + 1);
	if (rc < 0) {
		free(buf);
		PyErr_Format(PyExc_RuntimeError, "mirage_read_string failed: %lld", (long long)rc);
		return NULL;
	}

	PyObject *result = PyUnicode_FromStringAndSize(buf, actual_len);
	free(buf);
	return result;
}

static PyObject *
py_mirage_read_bin(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	/* First, peek at the length */
	int64_t length;
	int64_t saved_cursor = msg->cursor;

	/* Skip the type byte */
	msg->cursor++;

	/* Read varint length */
	length = 0;
	int shift = 0;
	uint8_t byte;
	do {
		byte = msg->data[msg->cursor++];
		length |= (int64_t)(byte & 0x7F) << shift;
		shift += 7;
	} while (byte & 0x80);

	/* Restore cursor and allocate buffer */
	msg->cursor = saved_cursor;

	uint8_t *buf = (uint8_t *)malloc(length);
	if (!buf) {
		PyErr_NoMemory();
		return NULL;
	}

	int64_t actual_len;
	int64_t rc = p_mirage_read_bin(msg, buf, &actual_len, length);
	if (rc < 0) {
		free(buf);
		PyErr_Format(PyExc_RuntimeError, "mirage_read_bin failed: %lld", (long long)rc);
		return NULL;
	}

	PyObject *result = PyBytes_FromStringAndSize((char *)buf, actual_len);
	free(buf);
	return result;
}

static PyObject *
py_mirage_read_sym(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	/* First, peek at the length */
	int64_t length;
	int64_t saved_cursor = msg->cursor;

	/* Skip the type byte */
	msg->cursor++;

	/* Read varint length */
	length = 0;
	int shift = 0;
	uint8_t byte;
	do {
		byte = msg->data[msg->cursor++];
		length |= (int64_t)(byte & 0x7F) << shift;
		shift += 7;
	} while (byte & 0x80);

	/* Restore cursor and allocate buffer */
	msg->cursor = saved_cursor;

	char *buf = (char *)malloc(length + 1);
	if (!buf) {
		PyErr_NoMemory();
		return NULL;
	}

	int64_t actual_len;
	int64_t rc = p_mirage_read_sym(msg, buf, &actual_len, length + 1);
	if (rc < 0) {
		free(buf);
		PyErr_Format(PyExc_RuntimeError, "mirage_read_sym failed: %lld", (long long)rc);
		return NULL;
	}

	PyObject *result = PyUnicode_FromStringAndSize(buf, actual_len);
	free(buf);
	return result;
}

static PyObject *
py_mirage_read_bigint(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	/* First, peek at the length */
	int64_t length;
	int64_t saved_cursor = msg->cursor;

	/* Skip the type byte */
	msg->cursor++;

	/* Read varint length */
	length = 0;
	int shift = 0;
	uint8_t byte;
	do {
		byte = msg->data[msg->cursor++];
		length |= (int64_t)(byte & 0x7F) << shift;
		shift += 7;
	} while (byte & 0x80);

	/* Restore cursor and allocate buffer */
	msg->cursor = saved_cursor;

	char *buf = (char *)malloc(length + 1);
	if (!buf) {
		PyErr_NoMemory();
		return NULL;
	}

	int64_t actual_len;
	int64_t rc = p_mirage_read_bigint(msg, buf, &actual_len, length + 1);
	if (rc < 0) {
		free(buf);
		PyErr_Format(PyExc_RuntimeError, "mirage_read_bigint failed: %lld", (long long)rc);
		return NULL;
	}
	buf[actual_len] = '\0';

	/* Convert to Python int */
	PyObject *result = PyLong_FromString(buf, NULL, 10);
	free(buf);
	return result;
}

static PyObject *
py_mirage_read_bigreal(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	/* First, peek at the length */
	int64_t length;
	int64_t saved_cursor = msg->cursor;

	/* Skip the type byte */
	msg->cursor++;

	/* Read varint length */
	length = 0;
	int shift = 0;
	uint8_t byte;
	do {
		byte = msg->data[msg->cursor++];
		length |= (int64_t)(byte & 0x7F) << shift;
		shift += 7;
	} while (byte & 0x80);

	/* Restore cursor and allocate buffer */
	msg->cursor = saved_cursor;

	char *buf = (char *)malloc(length + 1);
	if (!buf) {
		PyErr_NoMemory();
		return NULL;
	}

	int64_t actual_len;
	int64_t rc = p_mirage_read_bigreal(msg, buf, &actual_len, length + 1);
	if (rc < 0) {
		free(buf);
		PyErr_Format(PyExc_RuntimeError, "mirage_read_bigreal failed: %lld", (long long)rc);
		return NULL;
	}
	buf[actual_len] = '\0';

	/* Return as string - caller can convert to Decimal if needed */
	PyObject *result = PyUnicode_FromStringAndSize(buf, actual_len);
	free(buf);
	return result;
}

static PyObject *
py_mirage_read_numericarray(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	/* Pre-allocate a large buffer - we'll resize the result.
	   Maximum reasonable size is bounded by remaining message length. */
	int64_t max_data = msg->length - msg->cursor;
	if (max_data <= 0) {
		PyErr_SetString(PyExc_RuntimeError, "No data remaining in buffer");
		return NULL;
	}

	uint8_t *data = (uint8_t *)malloc(max_data);
	if (!data) {
		PyErr_NoMemory();
		return NULL;
	}

	mirage_array_type array_type;
	int64_t rank;
	int64_t dims[32];

	int64_t rc = p_mirage_read_numericarray(msg, &array_type, &rank, dims, 32, data, max_data);
	if (rc < 0) {
		free(data);
		PyErr_Format(PyExc_RuntimeError, "mirage_read_numericarray failed: %lld", (long long)rc);
		return NULL;
	}

	/* Calculate actual data size from dims */
	int64_t total = 1;
	for (int64_t i = 0; i < rank; i++) {
		total *= dims[i];
	}
	int elem_size = p_mirage_array_type_size[array_type];
	int64_t data_bytes = total * elem_size;

	/* Build dims list */
	PyObject *dims_list = PyList_New(rank);
	for (int64_t i = 0; i < rank; i++) {
		PyList_SetItem(dims_list, i, PyLong_FromLongLong(dims[i]));
	}

	/* Build result: (array_type, dims, data_bytes) - only copy actual data size */
	PyObject *data_bytes_obj = PyBytes_FromStringAndSize((char *)data, data_bytes);
	free(data);

	return Py_BuildValue("(iNN)", (int)array_type, dims_list, data_bytes_obj);
}

static PyObject *
py_mirage_read_association(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int64_t length;
	int64_t rc = p_mirage_read_association(msg, &length);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_association failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLongLong(length);
}

static PyObject *
py_mirage_read_assoc_rule(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	int64_t rc = p_mirage_read_assoc_rule(msg);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_read_assoc_rule failed: %lld", (long long)rc);
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject *
py_mirage_peek_element_type(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	mirage_element_type etype;
	int64_t rc = p_mirage_peek_element_type(msg, &etype);
	if (rc < 0) {
		PyErr_Format(PyExc_RuntimeError, "mirage_peek_element_type failed: %lld", (long long)rc);
		return NULL;
	}
	return PyLong_FromLong((int)etype);
}

static PyObject *
py_mirage_get_buffer(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	/* Return data up to cursor position (actual written data) */
	return PyBytes_FromStringAndSize((char *)msg->data, msg->cursor);
}

static PyObject *
py_mirage_set_buffer(PyObject *self, PyObject *args) {
	PyObject *capsule;
	Py_buffer view;

	if (!PyArg_ParseTuple(args, "Oy*", &capsule, &view)) {
		return NULL;
	}

	mirage_msg *msg = msg_from_capsule(capsule);
	if (!msg) {
		PyBuffer_Release(&view);
		return NULL;
	}

	if (view.len > msg->max_length) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError, "buffer too large for message");
		return NULL;
	}

	memcpy(msg->data, view.buf, view.len);
	msg->length = view.len;
	msg->cursor = 0;

	PyBuffer_Release(&view);
	Py_RETURN_NONE;
}

static PyObject *
py_mirage_get_length(PyObject *self, PyObject *arg) {
	mirage_msg *msg = msg_from_capsule(arg);
	if (!msg) return NULL;

	return PyLong_FromLongLong(msg->length);
}

/* -------------------- 5.  Module method table ------------------------------ */

static PyMethodDef MirageMethods[] = {
    {"create", py_mirage_create, METH_VARARGS, "Create a mirage message buffer."},
    {"write_start", py_mirage_write_start, METH_O, "Start writing a message."},
    {"read_start", py_mirage_read_start, METH_O, "Start reading a message."},
    {"write_fn", py_mirage_write_fn, METH_VARARGS, "Write a function header."},
    {"write_i8", py_mirage_write_i8, METH_VARARGS, "Write an int8."},
    {"write_i16", py_mirage_write_i16, METH_VARARGS, "Write an int16."},
    {"write_i32", py_mirage_write_i32, METH_VARARGS, "Write an int32."},
    {"write_i64", py_mirage_write_i64, METH_VARARGS, "Write an int64."},
    {"write_f64", py_mirage_write_f64, METH_VARARGS, "Write a float64."},
    {"write_string", py_mirage_write_string, METH_VARARGS, "Write a string."},
    {"write_bin", py_mirage_write_bin, METH_VARARGS, "Write binary data."},
    {"write_sym", py_mirage_write_sym, METH_VARARGS, "Write a symbol."},
    {"write_bigint", py_mirage_write_bigint, METH_VARARGS, "Write a big integer."},
    {"write_bigreal", py_mirage_write_bigreal, METH_VARARGS, "Write a big real."},
    {"write_numericarray", py_mirage_write_numericarray, METH_VARARGS, "Write a numeric array."},
    {"write_association", py_mirage_write_association, METH_VARARGS, "Write association header."},
    {"write_assoc_rule", py_mirage_write_assoc_rule, METH_O, "Write association rule marker."},
    {"read_fn", py_mirage_read_fn, METH_O, "Read a function header."},
    {"read_i8", py_mirage_read_i8, METH_O, "Read an int8."},
    {"read_i16", py_mirage_read_i16, METH_O, "Read an int16."},
    {"read_i32", py_mirage_read_i32, METH_O, "Read an int32."},
    {"read_i64", py_mirage_read_i64, METH_O, "Read an int64."},
    {"read_f64", py_mirage_read_f64, METH_O, "Read a float64."},
    {"read_string", py_mirage_read_string, METH_O, "Read a string."},
    {"read_bin", py_mirage_read_bin, METH_O, "Read binary data."},
    {"read_sym", py_mirage_read_sym, METH_O, "Read a symbol."},
    {"read_bigint", py_mirage_read_bigint, METH_O, "Read a big integer."},
    {"read_bigreal", py_mirage_read_bigreal, METH_O, "Read a big real."},
    {"read_numericarray", py_mirage_read_numericarray, METH_O, "Read a numeric array."},
    {"read_association", py_mirage_read_association, METH_O, "Read association header."},
    {"read_assoc_rule", py_mirage_read_assoc_rule, METH_O, "Read association rule marker."},
    {"peek_type", py_mirage_peek_element_type, METH_O, "Peek at next element type."},
    {"get_buffer", py_mirage_get_buffer, METH_O, "Get the raw buffer bytes."},
    {"set_buffer", py_mirage_set_buffer, METH_VARARGS, "Set the buffer from bytes."},
    {"get_length", py_mirage_get_length, METH_O, "Get current buffer length."},
    {NULL, NULL, 0, NULL}};

/* -------------------------- 6.  Module definition -------------------------- */

static struct PyModuleDef miragemodule = {
    PyModuleDef_HEAD_INIT,
    "mirage._mirage",
    "Thin C wrapper for mirage shared library",
    -1,
    MirageMethods,
    NULL,
    NULL,
    NULL,
    NULL};

/* ----------------------------- 7.  Module init ----------------------------- */

PyMODINIT_FUNC
PyInit__mirage(void) {
	PyObject *m = PyModule_Create(&miragemodule);
	if (!m) {
		return NULL;
	}

	/* Load the shared library */
	lib_handle = load_library_candidates("mirage",
					     MIRAGE_LIBRARY_CANDIDATES,
					     sizeof(MIRAGE_LIBRARY_CANDIDATES) / sizeof(MIRAGE_LIBRARY_CANDIDATES[0]),
					     DYNLIB_FLAG_NOW | DYNLIB_FLAG_LOCAL);
	if (!lib_handle) {
		return NULL;
	}

	/* Load all symbols */
	if (load_symbol("mirage_create", (void **)&p_mirage_create) < 0) return NULL;
	if (load_symbol("mirage_destroy", (void **)&p_mirage_destroy) < 0) return NULL;
	if (load_symbol("mirage_write_start", (void **)&p_mirage_write_start) < 0) return NULL;
	if (load_symbol("mirage_read_start", (void **)&p_mirage_read_start) < 0) return NULL;
	if (load_symbol("mirage_write_fn", (void **)&p_mirage_write_fn) < 0) return NULL;
	if (load_symbol("mirage_write_i8", (void **)&p_mirage_write_i8) < 0) return NULL;
	if (load_symbol("mirage_write_i16", (void **)&p_mirage_write_i16) < 0) return NULL;
	if (load_symbol("mirage_write_i32", (void **)&p_mirage_write_i32) < 0) return NULL;
	if (load_symbol("mirage_write_i64", (void **)&p_mirage_write_i64) < 0) return NULL;
	if (load_symbol("mirage_write_f64", (void **)&p_mirage_write_f64) < 0) return NULL;
	if (load_symbol("mirage_write_string", (void **)&p_mirage_write_string) < 0) return NULL;
	if (load_symbol("mirage_write_bin", (void **)&p_mirage_write_bin) < 0) return NULL;
	if (load_symbol("mirage_write_sym", (void **)&p_mirage_write_sym) < 0) return NULL;
	if (load_symbol("mirage_write_bigint", (void **)&p_mirage_write_bigint) < 0) return NULL;
	if (load_symbol("mirage_write_bigreal", (void **)&p_mirage_write_bigreal) < 0) return NULL;
	if (load_symbol("mirage_write_numericarray", (void **)&p_mirage_write_numericarray) < 0) return NULL;
	if (load_symbol("mirage_write_association", (void **)&p_mirage_write_association) < 0) return NULL;
	if (load_symbol("mirage_write_assoc_rule", (void **)&p_mirage_write_assoc_rule) < 0) return NULL;
	if (load_symbol("mirage_read_fn", (void **)&p_mirage_read_fn) < 0) return NULL;
	if (load_symbol("mirage_read_i8", (void **)&p_mirage_read_i8) < 0) return NULL;
	if (load_symbol("mirage_read_i16", (void **)&p_mirage_read_i16) < 0) return NULL;
	if (load_symbol("mirage_read_i32", (void **)&p_mirage_read_i32) < 0) return NULL;
	if (load_symbol("mirage_read_i64", (void **)&p_mirage_read_i64) < 0) return NULL;
	if (load_symbol("mirage_read_f64", (void **)&p_mirage_read_f64) < 0) return NULL;
	if (load_symbol("mirage_read_string", (void **)&p_mirage_read_string) < 0) return NULL;
	if (load_symbol("mirage_read_bin", (void **)&p_mirage_read_bin) < 0) return NULL;
	if (load_symbol("mirage_read_sym", (void **)&p_mirage_read_sym) < 0) return NULL;
	if (load_symbol("mirage_read_bigint", (void **)&p_mirage_read_bigint) < 0) return NULL;
	if (load_symbol("mirage_read_bigreal", (void **)&p_mirage_read_bigreal) < 0) return NULL;
	if (load_symbol("mirage_read_numericarray", (void **)&p_mirage_read_numericarray) < 0) return NULL;
	if (load_symbol("mirage_read_association", (void **)&p_mirage_read_association) < 0) return NULL;
	if (load_symbol("mirage_read_assoc_rule", (void **)&p_mirage_read_assoc_rule) < 0) return NULL;
	if (load_symbol("mirage_peek_element_type", (void **)&p_mirage_peek_element_type) < 0) return NULL;
	if (load_symbol("mirage_array_type_size", (void **)&p_mirage_array_type_size) < 0) return NULL;

	/* Add constants for element types */
	PyModule_AddIntConstant(m, "FUNCTION", 0);
	PyModule_AddIntConstant(m, "I8", 1);
	PyModule_AddIntConstant(m, "I16", 2);
	PyModule_AddIntConstant(m, "I32", 3);
	PyModule_AddIntConstant(m, "I64", 4);
	PyModule_AddIntConstant(m, "F64", 5);
	PyModule_AddIntConstant(m, "STRING", 6);
	PyModule_AddIntConstant(m, "BINSTRING", 7);
	PyModule_AddIntConstant(m, "SYMBOL", 8);
	PyModule_AddIntConstant(m, "BIGINT", 9);
	PyModule_AddIntConstant(m, "BIGREAL", 10);
	PyModule_AddIntConstant(m, "NUMERICARRAY", 12);
	PyModule_AddIntConstant(m, "ASSOCIATION", 13);
	PyModule_AddIntConstant(m, "RULE", 15);

	/* Add constants for array types */
	PyModule_AddIntConstant(m, "ARRAY_I8", 0);
	PyModule_AddIntConstant(m, "ARRAY_I16", 1);
	PyModule_AddIntConstant(m, "ARRAY_I32", 2);
	PyModule_AddIntConstant(m, "ARRAY_I64", 3);
	PyModule_AddIntConstant(m, "ARRAY_U8", 4);
	PyModule_AddIntConstant(m, "ARRAY_U16", 5);
	PyModule_AddIntConstant(m, "ARRAY_U32", 6);
	PyModule_AddIntConstant(m, "ARRAY_U64", 7);
	PyModule_AddIntConstant(m, "ARRAY_F32", 8);
	PyModule_AddIntConstant(m, "ARRAY_F64", 9);
	PyModule_AddIntConstant(m, "ARRAY_C32", 10);
	PyModule_AddIntConstant(m, "ARRAY_C64", 11);

	return m;
}
