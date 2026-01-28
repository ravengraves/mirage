/**
 * Mirage.js - JavaScript bindings for Mirage WXF serialization
 *
 * Type Mappings (JavaScript → WXF):
 * - number (safe integer) → i64
 * - number (float) → f64
 * - BigInt → BigInteger
 * - string → String
 * - null/undefined → Symbol "Null"
 * - true/false → i8 (1/0) for hardware efficiency
 * - Array → List function
 * - Expr(name, ...args) → WXF function
 * - Object (plain) → Association
 * - Uint8Array (via Bin) → Binary
 * - TypedArray → NumericArray
 * - Symbol → WXF Symbol
 *
 * Type Mappings (WXF → JavaScript):
 * - List function → Array
 * - Other functions → Expr (name, arg1, arg2, ...)
 * - Association → Object
 * - String → string
 * - Binary → Uint8Array
 * - i8/i16/i32/i64 → number
 * - f64 → number
 * - BigInteger → BigInt
 * - Symbol → Symbol
 * - NumericArray → TypedArray
 */

// Element type constants (must match mirage.h)
const MIRAGE_FUNCTION = 0;
const MIRAGE_I8 = 1;
const MIRAGE_I16 = 2;
const MIRAGE_I32 = 3;
const MIRAGE_I64 = 4;
const MIRAGE_F64 = 5;
const MIRAGE_STRING = 6;
const MIRAGE_BINSTRING = 7;
const MIRAGE_SYMBOL = 8;
const MIRAGE_BIGINT = 9;
const MIRAGE_BIGREAL = 10;
const MIRAGE_NUMERICARRAY = 12;
const MIRAGE_ASSOCIATION = 13;
const MIRAGE_RULE = 15;

// Array type constants
const MIRAGE_ARRAY_I8 = 0;
const MIRAGE_ARRAY_I16 = 1;
const MIRAGE_ARRAY_I32 = 2;
const MIRAGE_ARRAY_I64 = 3;
const MIRAGE_ARRAY_U8 = 4;
const MIRAGE_ARRAY_U16 = 5;
const MIRAGE_ARRAY_U32 = 6;
const MIRAGE_ARRAY_U64 = 7;
const MIRAGE_ARRAY_F32 = 8;
const MIRAGE_ARRAY_F64 = 9;

// TypedArray to WXF array type mapping
const TYPED_ARRAY_MAP = new Map([
    [Int8Array, MIRAGE_ARRAY_I8],
    [Int16Array, MIRAGE_ARRAY_I16],
    [Int32Array, MIRAGE_ARRAY_I32],
    [BigInt64Array, MIRAGE_ARRAY_I64],
    [Uint8Array, MIRAGE_ARRAY_U8],
    [Uint16Array, MIRAGE_ARRAY_U16],
    [Uint32Array, MIRAGE_ARRAY_U32],
    [BigUint64Array, MIRAGE_ARRAY_U64],
    [Float32Array, MIRAGE_ARRAY_F32],
    [Float64Array, MIRAGE_ARRAY_F64],
]);

// WXF array type to TypedArray constructor
const ARRAY_TYPE_MAP = new Map([
    [MIRAGE_ARRAY_I8, Int8Array],
    [MIRAGE_ARRAY_I16, Int16Array],
    [MIRAGE_ARRAY_I32, Int32Array],
    [MIRAGE_ARRAY_I64, BigInt64Array],
    [MIRAGE_ARRAY_U8, Uint8Array],
    [MIRAGE_ARRAY_U16, Uint16Array],
    [MIRAGE_ARRAY_U32, Uint32Array],
    [MIRAGE_ARRAY_U64, BigUint64Array],
    [MIRAGE_ARRAY_F32, Float32Array],
    [MIRAGE_ARRAY_F64, Float64Array],
]);

// Element size for each array type
const ARRAY_ELEM_SIZE = new Map([
    [MIRAGE_ARRAY_I8, 1], [MIRAGE_ARRAY_U8, 1],
    [MIRAGE_ARRAY_I16, 2], [MIRAGE_ARRAY_U16, 2],
    [MIRAGE_ARRAY_I32, 4], [MIRAGE_ARRAY_U32, 4],
    [MIRAGE_ARRAY_I64, 8], [MIRAGE_ARRAY_U64, 8],
    [MIRAGE_ARRAY_F32, 4], [MIRAGE_ARRAY_F64, 8],
]);

/** Wrapper class for explicit WXF Symbol encoding */
class Sym {
    constructor(name) {
        this.name = String(name);
    }
    toString() { return this.name; }
}

/** Wrapper class for binary data */
class Bin {
    constructor(data) {
        this.data = data instanceof Uint8Array ? data : new Uint8Array(data);
    }
}

/** Wrapper class for WXF function expressions */
class Expr extends Array {
    constructor(name, ...args) {
        if (typeof name !== 'string') {
            throw new TypeError(
                `Expr head must be a string, got ${typeof name}. ` +
                `Example: new Expr('Plus', 1, 2)`
            );
        }
        super(name, ...args);
        this.head = name;
        this.args = args;
    }
}

/** Wrapper classes for explicit integer sizes */
class I8 extends Number { constructor(v) { super(v); this.value = v | 0; } valueOf() { return this.value; } }
class I16 extends Number { constructor(v) { super(v); this.value = v | 0; } valueOf() { return this.value; } }
class I32 extends Number { constructor(v) { super(v); this.value = v | 0; } valueOf() { return this.value; } }

/** Helper functions */
const sym = (name) => new Sym(name);
const bin = (data) => new Bin(data);
const expr = (name, ...args) => new Expr(name, ...args);
const i8 = (v) => new I8(v);
const i16 = (v) => new I16(v);
const i32 = (v) => new I32(v);

class MirageError extends Error {
    constructor(message) {
        super(message);
        this.name = 'MirageError';
    }
}

let wasm = null;
let memory = null;
let exports = null;

/**
 * Initialize the WASM module
 * @param {ArrayBuffer|Uint8Array} wasmBytes - The WASM binary
 */
async function init(wasmBytes) {
    if (wasm) return; // Already initialized

    const wasmModule = await WebAssembly.compile(wasmBytes);

    // No imports needed - WASM module has its own memory
    const imports = {};

    wasm = await WebAssembly.instantiate(wasmModule, imports);
    exports = wasm.exports;
    memory = exports.memory;

    if (!memory) {
        throw new MirageError('WASM module does not export memory');
    }
}

/**
 * Initialize from file path (Node.js)
 * @param {string} wasmPath - Path to mirage.wasm
 */
async function initFromFile(wasmPath) {
    const fs = await import('fs');
    const wasmBytes = fs.readFileSync(wasmPath);
    await init(wasmBytes);
}

/** Get a view of WASM memory */
function getMemory() {
    return new Uint8Array(memory.buffer);
}

/** Get DataView for reading/writing typed values */
function getDataView() {
    return new DataView(memory.buffer);
}

/** Write string to WASM memory, return pointer */
function writeString(str) {
    const encoder = new TextEncoder();
    const bytes = encoder.encode(str);
    const ptr = exports.wasm_heap_used() + 0x100000; // Offset past heap array
    // Actually, we need to allocate via malloc
    const strPtr = exports.malloc(bytes.length + 1);
    if (!strPtr) throw new MirageError('Out of memory');
    const mem = getMemory();
    mem.set(bytes, strPtr);
    mem[strPtr + bytes.length] = 0; // Null terminate
    return [strPtr, bytes.length];
}

/** Read string from WASM memory */
function readString(ptr, len) {
    const mem = getMemory();
    const bytes = mem.slice(ptr, ptr + len);
    return new TextDecoder().decode(bytes);
}

/** Write a value to the message buffer */
function writeValue(msg, obj, depth = 0) {
    if (depth > 100) throw new MirageError('Object too deeply nested');

    if (obj === null || obj === undefined) {
        const [ptr, len] = writeString('Null');
        const ret = exports.wasm_write_sym(msg, ptr, len);
        if (ret < 0) throw new MirageError(`write_sym failed: ${ret}`);
    }
    else if (obj === true) {
        // Encode as i8(1) for hardware efficiency
        const ret = exports.wasm_write_i8(msg, 1);
        if (ret < 0) throw new MirageError(`write_i8 failed: ${ret}`);
    }
    else if (obj === false) {
        // Encode as i8(0) for hardware efficiency
        const ret = exports.wasm_write_i8(msg, 0);
        if (ret < 0) throw new MirageError(`write_i8 failed: ${ret}`);
    }
    else if (obj instanceof I8) {
        const ret = exports.wasm_write_i8(msg, obj.valueOf());
        if (ret < 0) throw new MirageError(`write_i8 failed: ${ret}`);
    }
    else if (obj instanceof I16) {
        const ret = exports.wasm_write_i16(msg, obj.valueOf());
        if (ret < 0) throw new MirageError(`write_i16 failed: ${ret}`);
    }
    else if (obj instanceof I32) {
        const ret = exports.wasm_write_i32(msg, obj.valueOf());
        if (ret < 0) throw new MirageError(`write_i32 failed: ${ret}`);
    }
    else if (typeof obj === 'bigint') {
        const str = obj.toString();
        const [ptr, len] = writeString(str);
        const ret = exports.wasm_write_bigint(msg, ptr, len);
        if (ret < 0) throw new MirageError(`write_bigint failed: ${ret}`);
    }
    else if (typeof obj === 'number') {
        if (Number.isInteger(obj) && Number.isSafeInteger(obj)) {
            // Split i64 into lo/hi for WASM32
            const big = BigInt(obj);
            const lo = Number(big & 0xFFFFFFFFn);
            const hi = Number((big >> 32n) & 0xFFFFFFFFn);
            const ret = exports.wasm_write_i64_lo(msg, lo, hi);
            if (ret < 0) throw new MirageError(`write_i64 failed: ${ret}`);
        } else {
            const ret = exports.wasm_write_f64(msg, obj);
            if (ret < 0) throw new MirageError(`write_f64 failed: ${ret}`);
        }
    }
    else if (typeof obj === 'string') {
        const [ptr, len] = writeString(obj);
        const ret = exports.wasm_write_string(msg, ptr, len);
        if (ret < 0) throw new MirageError(`write_string failed: ${ret}`);
    }
    else if (typeof obj === 'symbol') {
        const name = obj.description || obj.toString().slice(7, -1);
        const [ptr, len] = writeString(name);
        const ret = exports.wasm_write_sym(msg, ptr, len);
        if (ret < 0) throw new MirageError(`write_sym failed: ${ret}`);
    }
    else if (obj instanceof Sym) {
        const [ptr, len] = writeString(obj.name);
        const ret = exports.wasm_write_sym(msg, ptr, len);
        if (ret < 0) throw new MirageError(`write_sym failed: ${ret}`);
    }
    else if (obj instanceof Bin) {
        const dataPtr = exports.malloc(obj.data.length);
        if (!dataPtr) throw new MirageError('Out of memory');
        getMemory().set(obj.data, dataPtr);
        const ret = exports.wasm_write_bin(msg, dataPtr, obj.data.length);
        if (ret < 0) throw new MirageError(`write_bin failed: ${ret}`);
    }
    else if (ArrayBuffer.isView(obj) && !(obj instanceof DataView)) {
        // TypedArray -> NumericArray
        const arrayType = TYPED_ARRAY_MAP.get(obj.constructor);
        if (arrayType === undefined) {
            throw new MirageError(`Unsupported typed array: ${obj.constructor.name}`);
        }

        // Write dims (1D array) - using i16 for embedded implementation
        const dimsPtr = exports.malloc(2); // One i16
        if (!dimsPtr) throw new MirageError('Out of memory');
        getDataView().setInt16(dimsPtr, obj.length, true);

        // Write data
        const dataPtr = exports.malloc(obj.byteLength);
        if (!dataPtr) throw new MirageError('Out of memory');
        new Uint8Array(memory.buffer).set(new Uint8Array(obj.buffer, obj.byteOffset, obj.byteLength), dataPtr);

        const ret = exports.wasm_write_numericarray(msg, arrayType, 1, dimsPtr, dataPtr);
        if (ret < 0) throw new MirageError(`write_numericarray failed: ${ret}`);
    }
    else if (obj instanceof Expr) {
        // Check Expr before Array.isArray since Expr extends Array
        const [namePtr, nameLen] = writeString(obj.head);
        const ret = exports.wasm_write_fn(msg, namePtr, nameLen, obj.args.length);
        if (ret < 0) throw new MirageError(`write_fn failed: ${ret}`);
        for (const arg of obj.args) {
            writeValue(msg, arg, depth + 1);
        }
    }
    else if (Array.isArray(obj)) {
        // Plain arrays encode as WXF List
        const [namePtr, nameLen] = writeString('List');
        const ret = exports.wasm_write_fn(msg, namePtr, nameLen, obj.length);
        if (ret < 0) throw new MirageError(`write_fn failed: ${ret}`);
        for (const item of obj) {
            writeValue(msg, item, depth + 1);
        }
    }
    else if (typeof obj === 'object' && obj !== null) {
        // Plain object -> Association
        const entries = Object.entries(obj);
        let ret = exports.wasm_write_association(msg, entries.length);
        if (ret < 0) throw new MirageError(`write_association failed: ${ret}`);

        for (const [key, value] of entries) {
            ret = exports.wasm_write_assoc_rule(msg);
            if (ret < 0) throw new MirageError(`write_assoc_rule failed: ${ret}`);
            writeValue(msg, key, depth + 1);
            writeValue(msg, value, depth + 1);
        }
    }
    else {
        throw new MirageError(`Cannot encode type: ${typeof obj}`);
    }
}

/** Read a value from the message buffer */
function readValue(msg, depth = 0) {
    if (depth > 100) throw new MirageError('Object too deeply nested');

    const etype = exports.wasm_peek_element_type(msg);
    if (etype < 0) throw new MirageError(`peek_element_type failed: ${etype}`);

    const dv = getDataView();
    const mem = getMemory();

    switch (etype) {
        case MIRAGE_FUNCTION: {
            // Allocate buffers for name and argc
            const nameBuf = exports.malloc(256);
            const nameLenPtr = exports.malloc(4);
            const argcPtr = exports.malloc(4);

            const ret = exports.wasm_read_fn(msg, nameBuf, nameLenPtr, 256, argcPtr);
            if (ret < 0) throw new MirageError(`read_fn failed: ${ret}`);

            const nameLen = dv.getInt32(nameLenPtr, true);
            const argc = dv.getInt32(argcPtr, true);
            const name = readString(nameBuf, nameLen);

            const args = [];
            for (let i = 0; i < argc; i++) {
                args.push(readValue(msg, depth + 1));
            }

            // Special: List -> JavaScript array
            if (name === 'List') {
                return args;
            }
            // Other functions -> Expr
            return new Expr(name, ...args);
        }

        case MIRAGE_I8: {
            const outPtr = exports.malloc(1);
            const ret = exports.wasm_read_i8(msg, outPtr);
            if (ret < 0) throw new MirageError(`read_i8 failed: ${ret}`);
            return dv.getInt8(outPtr);
        }

        case MIRAGE_I16: {
            const outPtr = exports.malloc(2);
            const ret = exports.wasm_read_i16(msg, outPtr);
            if (ret < 0) throw new MirageError(`read_i16 failed: ${ret}`);
            return dv.getInt16(outPtr, true);
        }

        case MIRAGE_I32: {
            const outPtr = exports.malloc(4);
            const ret = exports.wasm_read_i32(msg, outPtr);
            if (ret < 0) throw new MirageError(`read_i32 failed: ${ret}`);
            return dv.getInt32(outPtr, true);
        }

        case MIRAGE_I64: {
            const loPtr = exports.malloc(4);
            const hiPtr = exports.malloc(4);
            const ret = exports.wasm_read_i64(msg, loPtr, hiPtr);
            if (ret < 0) throw new MirageError(`read_i64 failed: ${ret}`);
            const lo = dv.getUint32(loPtr, true);
            const hi = dv.getInt32(hiPtr, true);
            const val = (BigInt(hi) << 32n) | BigInt(lo);
            // Convert to number if safe
            if (val >= BigInt(Number.MIN_SAFE_INTEGER) && val <= BigInt(Number.MAX_SAFE_INTEGER)) {
                return Number(val);
            }
            return val;
        }

        case MIRAGE_F64: {
            const outPtr = exports.malloc(8);
            const ret = exports.wasm_read_f64(msg, outPtr);
            if (ret < 0) throw new MirageError(`read_f64 failed: ${ret}`);
            return dv.getFloat64(outPtr, true);
        }

        case MIRAGE_STRING: {
            const buf = exports.malloc(32000);
            const lenPtr = exports.malloc(4);
            const ret = exports.wasm_read_string(msg, buf, lenPtr, 32000);
            if (ret < 0) throw new MirageError(`read_string failed: ${ret}`);
            const len = dv.getInt32(lenPtr, true);
            return readString(buf, len);
        }

        case MIRAGE_BINSTRING: {
            const buf = exports.malloc(32000);
            const lenPtr = exports.malloc(4);
            const ret = exports.wasm_read_bin(msg, buf, lenPtr, 32000);
            if (ret < 0) throw new MirageError(`read_bin failed: ${ret}`);
            const len = dv.getInt32(lenPtr, true);
            return new Uint8Array(mem.slice(buf, buf + len));
        }

        case MIRAGE_SYMBOL: {
            const buf = exports.malloc(256);
            const lenPtr = exports.malloc(4);
            const ret = exports.wasm_read_sym(msg, buf, lenPtr, 256);
            if (ret < 0) throw new MirageError(`read_sym failed: ${ret}`);
            const len = dv.getInt32(lenPtr, true);
            const name = readString(buf, len);

            // Handle special symbols
            if (name === 'Null') return null;
            if (name === 'True') return true;
            if (name === 'False') return false;
            return Symbol.for(name);
        }

        case MIRAGE_BIGINT: {
            const buf = exports.malloc(1024);
            const lenPtr = exports.malloc(4);
            const ret = exports.wasm_read_bigint(msg, buf, lenPtr, 1024);
            if (ret < 0) throw new MirageError(`read_bigint failed: ${ret}`);
            const len = dv.getInt32(lenPtr, true);
            const str = readString(buf, len);
            return BigInt(str);
        }

        case MIRAGE_NUMERICARRAY: {
            const atypePtr = exports.malloc(4);
            const rankPtr = exports.malloc(4);
            const dimsPtr = exports.malloc(32); // 16 dimensions max (i16 each)
            const dataPtr = exports.malloc(32767); // 32KB max (embedded limitation)

            const ret = exports.wasm_read_numericarray(msg, atypePtr, rankPtr, dimsPtr, 16, dataPtr, 32767);
            if (ret < 0) throw new MirageError(`read_numericarray failed: ${ret}`);

            const atype = dv.getInt32(atypePtr, true);
            const rank = dv.getInt32(rankPtr, true);

            // Read dimensions (i16 each for embedded implementation)
            let totalElements = 1;
            const dims = [];
            for (let i = 0; i < rank; i++) {
                const dim = dv.getInt16(dimsPtr + i * 2, true);
                dims.push(dim);
                totalElements *= dim;
            }

            // Create appropriate typed array
            const TypedArrayCtor = ARRAY_TYPE_MAP.get(atype);
            if (!TypedArrayCtor) {
                throw new MirageError(`Unsupported array type: ${atype}`);
            }

            const elemSize = ARRAY_ELEM_SIZE.get(atype);
            const byteLength = totalElements * elemSize;

            // Copy data to new typed array
            const result = new TypedArrayCtor(totalElements);
            const srcBytes = new Uint8Array(memory.buffer, dataPtr, byteLength);
            new Uint8Array(result.buffer).set(srcBytes);

            return result;
        }

        case MIRAGE_ASSOCIATION: {
            const lenPtr = exports.malloc(4);
            let ret = exports.wasm_read_association(msg, lenPtr);
            if (ret < 0) throw new MirageError(`read_association failed: ${ret}`);
            const len = dv.getInt32(lenPtr, true);

            const result = {};
            for (let i = 0; i < len; i++) {
                ret = exports.wasm_read_assoc_rule(msg);
                if (ret < 0) throw new MirageError(`read_assoc_rule failed: ${ret}`);
                const key = readValue(msg, depth + 1);
                const value = readValue(msg, depth + 1);
                result[key] = value;
            }
            return result;
        }

        default:
            throw new MirageError(`Unknown element type: ${etype}`);
    }
}

/**
 * Encode a JavaScript value to WXF bytes
 * @param {any} obj - The value to encode
 * @returns {Uint8Array} - The encoded WXF bytes
 */
function encode(obj) {
    if (!wasm) throw new MirageError('WASM not initialized. Call init() first.');

    // Reset heap for fresh allocation
    exports.wasm_reset();

    // Create message buffer (32KB max for embedded implementation)
    const msg = exports.wasm_create(32767);
    if (!msg) throw new MirageError('Failed to create message buffer');

    try {
        let ret = exports.wasm_write_start(msg);
        if (ret < 0) throw new MirageError(`write_start failed: ${ret}`);

        writeValue(msg, obj);

        // Get result bytes
        const length = exports.wasm_msg_length(msg);
        const dataPtr = exports.wasm_msg_data(msg);

        // Copy to new Uint8Array
        const result = new Uint8Array(length);
        result.set(new Uint8Array(memory.buffer, dataPtr, length));

        return result;
    } finally {
        exports.wasm_destroy(msg);
    }
}

/**
 * Decode WXF bytes to a JavaScript value
 * @param {Uint8Array|ArrayBuffer} data - The WXF bytes to decode
 * @returns {any} - The decoded value
 */
function decode(data) {
    if (!wasm) throw new MirageError('WASM not initialized. Call init() first.');

    if (data instanceof ArrayBuffer) {
        data = new Uint8Array(data);
    }

    // Reset heap for fresh allocation
    exports.wasm_reset();

    // Create message buffer
    const msg = exports.wasm_create(data.length + 1024);
    if (!msg) throw new MirageError('Failed to create message buffer');

    try {
        // Copy data into message buffer
        const dataPtr = exports.wasm_msg_data(msg);
        new Uint8Array(memory.buffer).set(data, dataPtr);
        exports.wasm_msg_set_length(msg, data.length);

        let ret = exports.wasm_read_start(msg);
        if (ret < 0) throw new MirageError(`read_start failed: ${ret}`);

        return readValue(msg);
    } finally {
        exports.wasm_destroy(msg);
    }
}

// Export for CommonJS (Node.js)
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        init,
        initFromFile,
        encode,
        decode,
        Expr,
        Sym,
        Bin,
        I8,
        I16,
        I32,
        expr,
        sym,
        bin,
        i8,
        i16,
        i32,
        MirageError,
    };
}

// Export for ES modules
export {
    init,
    initFromFile,
    encode,
    decode,
    Expr,
    Sym,
    Bin,
    I8,
    I16,
    I32,
    expr,
    sym,
    bin,
    i8,
    i16,
    i32,
    MirageError,
};
