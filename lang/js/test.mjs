/**
 * Mirage.js Tests - Node.js
 */

import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import * as mirage from './mirage.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Test utilities
let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        console.log(`  ✓ ${name}`);
        passed++;
    } catch (e) {
        console.log(`  ✗ ${name}`);
        console.log(`    ${e.message}`);
        failed++;
    }
}

function assert(condition, message) {
    if (!condition) throw new Error(message || 'Assertion failed');
}

function assertEqual(a, b, message) {
    if (a !== b) {
        throw new Error(message || `Expected ${b}, got ${a}`);
    }
}

function assertApprox(a, b, epsilon = 1e-10, message) {
    if (Math.abs(a - b) > epsilon) {
        throw new Error(message || `Expected ~${b}, got ${a}`);
    }
}

function assertArrayEqual(a, b, message) {
    if (a.length !== b.length) {
        throw new Error(message || `Array length mismatch: ${a.length} vs ${b.length}`);
    }
    for (let i = 0; i < a.length; i++) {
        if (a[i] !== b[i]) {
            throw new Error(message || `Array mismatch at index ${i}: ${a[i]} vs ${b[i]}`);
        }
    }
}

async function runTests() {
    console.log('Loading WASM module...');

    // Load WASM
    const wasmPath = join(__dirname, '..', '..', 'build', 'mirage.wasm');
    const wasmBytes = readFileSync(wasmPath);
    await mirage.init(wasmBytes);

    console.log('WASM loaded successfully\n');

    // Exercise the exported C wrapper directly to retain all 32 capacity bits.
    const { instance } = await WebAssembly.instantiate(wasmBytes);
    const wasm = instance.exports;
    function checkNumericRead(dimsSize, dataSize, expected, {
        wire = [56, 58, 0xC2, 0x10, 2, 1, 1, 42],
        dims = [1, 1], data = [42], nullOutputs = 0,
    } = {}) {
        wasm.wasm_reset();
        const msg = wasm.wasm_create(wire.length);
        const memory = new Uint8Array(wasm.memory.buffer);
        memory.set(wire, wasm.wasm_msg_data(msg));
        wasm.wasm_msg_set_length(msg, wire.length);
        assertEqual(wasm.wasm_read_start(msg), 2);
        const dimsBytes = dimsSize >= 0 && dimsSize <= 32767 ? Math.max(6, dimsSize * 2) : 6;
        const dataBytes = dataSize >= 0 && dataSize <= 32767 ? Math.max(2, dataSize) : 2;
        const typePtr = wasm.malloc(4), rankPtr = wasm.malloc(4);
        const dimsPtr = wasm.malloc(dimsBytes), dataPtr = wasm.malloc(dataBytes);
        const outputs = [
            memory.subarray(typePtr, typePtr + 4), memory.subarray(rankPtr, rankPtr + 4),
            memory.subarray(dimsPtr, dimsPtr + dimsBytes), memory.subarray(dataPtr, dataPtr + dataBytes),
        ];
        outputs.forEach(out => out.fill(0xA5));
        const before = outputs.map(out => out.slice());
        const ret = wasm.wasm_read_numericarray(msg,
            nullOutputs & 1 ? 0 : typePtr, nullOutputs & 2 ? 0 : rankPtr,
            nullOutputs & 4 ? 0 : dimsPtr, dimsSize,
            nullOutputs & 8 ? 0 : dataPtr, dataSize);
        assertEqual(ret, expected);
        assertEqual(wasm.wasm_msg_length(msg), wire.length);
        if (expected < 0) {
            assertEqual(wasm.wasm_msg_cursor(msg), 2);
            outputs.forEach((out, i) => assertArrayEqual(out, before[i]));
        } else {
            assertEqual(wasm.wasm_msg_cursor(msg), wire.length);
            new DataView(before[0].buffer).setInt32(0, 4, true); // MIRAGE_ARRAY_U8
            new DataView(before[1].buffer).setInt32(0, dims.length, true);
            if (dimsSize !== -1) {
                const view = new DataView(before[2].buffer);
                dims.forEach((dim, i) => view.setInt16(i * 2, dim, true));
            }
            if (dataSize !== -1) before[3].set(data);
            outputs.forEach((out, i) => assertArrayEqual(out, before[i]));
        }
    }

    console.log('WASM numeric array capacities:');
    test('Packed arrays dispatch through the numeric reader', () => {
        const wire = new Uint8Array([56, 58, 0xC1, 0x02, 1, 2, 42, 0, 0, 0, 255, 255, 255, 255]);
        const result = mirage.decode(wire);
        assert(result instanceof Int32Array);
        assertArrayEqual(result, [42, -1]);
    });
    test('Independent skips and maximum capacities', () => {
        for (const dimsSize of [-1, 2, 32767]) {
            for (const dataSize of [-1, 1, 32767]) {
                checkNumericRead(dimsSize, dataSize, 6);
                const nullOutputs = (dimsSize === -1 ? 4 : 0) | (dataSize === -1 ? 8 : 0);
                checkNumericRead(dimsSize, dataSize, 6, { nullOutputs });
            }
        }
    });
    test('Scalar and empty arrays allow zero capacities', () => {
        for (const dimsSize of [-1, 0]) {
            for (const dataSize of [-1, 1]) {
                checkNumericRead(dimsSize, dataSize, 4, {
                    wire: [56, 58, 0xC2, 0x10, 0, 42], dims: [], nullOutputs: 4,
                });
            }
        }
        checkNumericRead(1, 0, 4, {
            wire: [56, 58, 0xC2, 0x10, 1, 0], dims: [0], data: [],
        });
    });
    test('Reject capacities before narrowing to int16', () => {
        const invalid = [-2147483648, -65537, -65536, -32769, -2,
                         32768, 65535, 65536, 65537, 2147483647];
        for (const size of invalid) {
            for (const dataSize of [-1, 1]) checkNumericRead(size, dataSize, -4);
            for (const dimsSize of [-1, 2]) checkNumericRead(dimsSize, size, -4);
        }
    });
    test('Capacity and NULL errors preserve every output', () => {
        for (const [dimsSize, dataSize] of [[0, 1], [1, 1], [0, -1], [1, -1], [2, 0], [-1, 0]]) {
            checkNumericRead(dimsSize, dataSize, -2);
        }
        for (const nullOutputs of [1, 2, 4, 8]) checkNumericRead(2, 1, -4, { nullOutputs });
        checkNumericRead(2, -1, -4, { nullOutputs: 4 });
        checkNumericRead(-1, 1, -4, { nullOutputs: 8 });
        checkNumericRead(-1, -1, -4, { nullOutputs: 1 });
        checkNumericRead(-1, -1, -4, { nullOutputs: 2 });
    });
    test('Malformed arrays preserve every output in all skip modes', () => {
        for (const dimsSize of [-1, 2]) {
            for (const dataSize of [-1, 1]) {
                checkNumericRead(dimsSize, dataSize, -2, { wire: [56, 58, 0xC2, 0x10, 2, 1, 1] });
                checkNumericRead(dimsSize, dataSize, -2, { wire: [56, 58, 0xC2, 0x10, 2, 1] });
                checkNumericRead(dimsSize, dataSize, -1, { wire: [56, 58, 0xC2, 0x10, 2, 0x80, 0x80, 1, 2] });
                checkNumericRead(dimsSize, dataSize, -1, { wire: [56, 58, 0xC2, 0xFF, 0] });
            }
        }
    });

    // ========================================
    console.log('Basic Types:');
    // ========================================

    test('Integer (i64)', () => {
        const result = mirage.decode(mirage.encode(42));
        assertEqual(result, 42);
    });

    test('Negative integer', () => {
        const result = mirage.decode(mirage.encode(-1000));
        assertEqual(result, -1000);
    });

    test('Large integer', () => {
        const result = mirage.decode(mirage.encode(9007199254740991)); // MAX_SAFE_INTEGER
        assertEqual(result, 9007199254740991);
    });

    test('Float (f64)', () => {
        const result = mirage.decode(mirage.encode(3.14159265358979));
        assertApprox(result, 3.14159265358979);
    });

    test('String', () => {
        const result = mirage.decode(mirage.encode('Hello, JavaScript!'));
        assertEqual(result, 'Hello, JavaScript!');
    });

    test('Empty string', () => {
        const result = mirage.decode(mirage.encode(''));
        assertEqual(result, '');
    });

    test('Null', () => {
        const result = mirage.decode(mirage.encode(null));
        assertEqual(result, null);
    });

    test('Undefined -> null', () => {
        const result = mirage.decode(mirage.encode(undefined));
        assertEqual(result, null);
    });

    test('Boolean true -> i8(1)', () => {
        // Booleans encode as i8 for hardware efficiency
        const result = mirage.decode(mirage.encode(true));
        assertEqual(result, 1);
    });

    test('Boolean false -> i8(0)', () => {
        // Booleans encode as i8 for hardware efficiency
        const result = mirage.decode(mirage.encode(false));
        assertEqual(result, 0);
    });

    // ========================================
    console.log('\nExplicit Integer Types:');
    // ========================================

    test('i8', () => {
        const result = mirage.decode(mirage.encode(mirage.i8(127)));
        assertEqual(result, 127);
    });

    test('i16', () => {
        const result = mirage.decode(mirage.encode(mirage.i16(32000)));
        assertEqual(result, 32000);
    });

    test('i32', () => {
        const result = mirage.decode(mirage.encode(mirage.i32(100000)));
        assertEqual(result, 100000);
    });

    // ========================================
    console.log('\nBigInt:');
    // ========================================

    test('BigInt', () => {
        const big = 10n ** 30n;
        const result = mirage.decode(mirage.encode(big));
        assertEqual(result, big);
    });

    test('Negative BigInt', () => {
        const big = -(10n ** 25n);
        const result = mirage.decode(mirage.encode(big));
        assertEqual(result, big);
    });

    // ========================================
    console.log('\nSymbols:');
    // ========================================

    test('Symbol (via Sym wrapper)', () => {
        const data = mirage.encode(mirage.sym('MySymbol'));
        const result = mirage.decode(data);
        assertEqual(typeof result, 'symbol');
        assertEqual(result.description, 'MySymbol');
    });

    test('JS Symbol', () => {
        const data = mirage.encode(Symbol('TestSym'));
        const result = mirage.decode(data);
        assertEqual(typeof result, 'symbol');
        assertEqual(result.description, 'TestSym');
    });

    // ========================================
    console.log('\nArrays:');
    // ========================================

    test('Simple array', () => {
        const result = mirage.decode(mirage.encode([1, 2, 3]));
        assertArrayEqual(result, [1, 2, 3]);
    });

    test('String array', () => {
        const result = mirage.decode(mirage.encode(['a', 'b', 'c']));
        assertArrayEqual(result, ['a', 'b', 'c']);
    });

    test('Empty array', () => {
        const result = mirage.decode(mirage.encode([]));
        assertArrayEqual(result, []);
    });

    test('Nested array', () => {
        const result = mirage.decode(mirage.encode([[1, 2], [3, 4]]));
        assertEqual(result.length, 2);
        assertArrayEqual(result[0], [1, 2]);
        assertArrayEqual(result[1], [3, 4]);
    });

    // ========================================
    console.log('\nWXF Functions (Expr):');
    // ========================================

    test('Expr with args', () => {
        const data = mirage.encode(mirage.expr('Plus', 1, 2));
        const result = mirage.decode(data);
        assert(result instanceof mirage.Expr, 'Expected Expr instance');
        assertEqual(result.head, 'Plus');
        assertArrayEqual(result.args, [1, 2]);
    });

    test('Expr no args', () => {
        const data = mirage.encode(mirage.expr('Empty'));
        const result = mirage.decode(data);
        assert(result instanceof mirage.Expr, 'Expected Expr instance');
        assertEqual(result.head, 'Empty');
        assertEqual(result.args.length, 0);
    });

    test('Nested Expr', () => {
        const data = mirage.encode(mirage.expr('Outer', mirage.expr('Inner', 1, 2), 3));
        const result = mirage.decode(data);
        assert(result instanceof mirage.Expr, 'Expected Expr instance');
        assertEqual(result.head, 'Outer');
        assert(result.args[0] instanceof mirage.Expr, 'Expected nested Expr');
        assertEqual(result.args[0].head, 'Inner');
        assertArrayEqual(result.args[0].args, [1, 2]);
        assertEqual(result.args[1], 3);
    });

    test('expr() helper', () => {
        const func = mirage.expr('Add', 1, 2);
        assert(func instanceof mirage.Expr, 'Expected Expr instance');
        assertEqual(func.head, 'Add');
        assertArrayEqual(func.args, [1, 2]);
    });

    test('Plain array with strings encodes as List', () => {
        // This is the breaking change - arrays are now always Lists
        const data = mirage.encode(['Hello', 'World']);
        const result = mirage.decode(data);
        assert(Array.isArray(result) && !(result instanceof mirage.Expr), 'Expected plain Array');
        assertArrayEqual(result, ['Hello', 'World']);
    });

    // ========================================
    console.log('\nObjects (Associations):');
    // ========================================

    test('Simple object', () => {
        const result = mirage.decode(mirage.encode({ x: 1, y: 2 }));
        assertEqual(result.x, 1);
        assertEqual(result.y, 2);
    });

    test('Empty object', () => {
        const result = mirage.decode(mirage.encode({}));
        assertEqual(Object.keys(result).length, 0);
    });

    test('Nested object', () => {
        const obj = { outer: { inner: 42 } };
        const result = mirage.decode(mirage.encode(obj));
        assertEqual(result.outer.inner, 42);
    });

    // ========================================
    console.log('\nBinary Data:');
    // ========================================

    test('Binary (Bin wrapper)', () => {
        const data = new Uint8Array([0xDE, 0xAD, 0xBE, 0xEF]);
        const result = mirage.decode(mirage.encode(mirage.bin(data)));
        assertArrayEqual(result, data);
    });

    // ========================================
    console.log('\nTyped Arrays (NumericArray):');
    // ========================================

    test('Int32Array', () => {
        const arr = new Int32Array([10, 20, 30, 40]);
        const result = mirage.decode(mirage.encode(arr));
        assert(result instanceof Int32Array, 'Expected Int32Array');
        assertArrayEqual(Array.from(result), [10, 20, 30, 40]);
    });

    test('Float64Array', () => {
        const arr = new Float64Array([1.5, 2.5, 3.5]);
        const result = mirage.decode(mirage.encode(arr));
        assert(result instanceof Float64Array, 'Expected Float64Array');
        assertApprox(result[0], 1.5);
        assertApprox(result[1], 2.5);
        assertApprox(result[2], 3.5);
    });

    test('Uint8Array', () => {
        const arr = new Uint8Array([1, 2, 3, 4, 5]);
        const result = mirage.decode(mirage.encode(arr));
        assert(result instanceof Uint8Array, 'Expected Uint8Array');
        assertArrayEqual(Array.from(result), [1, 2, 3, 4, 5]);
    });

    test('Int8Array', () => {
        const arr = new Int8Array([-128, 0, 127]);
        const result = mirage.decode(mirage.encode(arr));
        assert(result instanceof Int8Array, 'Expected Int8Array');
        assertArrayEqual(Array.from(result), [-128, 0, 127]);
    });

    test('Float32Array', () => {
        const arr = new Float32Array([1.0, 2.0, 3.0]);
        const result = mirage.decode(mirage.encode(arr));
        assert(result instanceof Float32Array, 'Expected Float32Array');
    });

    // ========================================
    console.log('\nMixed Types:');
    // ========================================

    test('Mixed array', () => {
        const data = [42, 3.14, 'hello', null, [1, 2], { a: 1 }];
        const result = mirage.decode(mirage.encode(data));
        assertEqual(result[0], 42);
        assertApprox(result[1], 3.14);
        assertEqual(result[2], 'hello');
        assertEqual(result[3], null);
        assertArrayEqual(result[4], [1, 2]);
        assertEqual(result[5].a, 1);
    });

    // ========================================
    console.log('\nRound-trip Consistency:');
    // ========================================

    const testValues = [
        42,
        -1000,
        3.14159265358979,
        'Hello, World!',
        null,
        // Note: true/false encode as i8 (1/0) for hardware efficiency
        [1, 2, 3, 4, 5],
        { key: 'value' },
        mirage.expr('Function', 1, 2, 3),
        10n ** 20n,
    ];

    for (const val of testValues) {
        const name = typeof val === 'bigint' ? `BigInt(${val})` :
                     val instanceof mirage.Expr ? `Expr(${val.head})` :
                     Array.isArray(val) ? `Array[${val.length}]` :
                     typeof val === 'object' && val !== null ? 'Object' :
                     String(val);
        test(`Round-trip: ${name}`, () => {
            const result = mirage.decode(mirage.encode(val));
            if (typeof val === 'number' && !Number.isInteger(val)) {
                assertApprox(result, val);
            } else if (typeof val === 'bigint') {
                assertEqual(result, val);
            } else if (val instanceof mirage.Expr) {
                assert(result instanceof mirage.Expr, 'Expected Expr');
                assertEqual(result.head, val.head);
                assertEqual(result.args.length, val.args.length);
            } else if (Array.isArray(val)) {
                assertEqual(result.length, val.length);
            } else if (typeof val === 'object' && val !== null) {
                assertEqual(JSON.stringify(result), JSON.stringify(val));
            } else {
                assertEqual(result, val);
            }
        });
    }

    // ========================================
    // Summary
    // ========================================

    console.log('\n========================================');
    console.log(`Tests: ${passed} passed, ${failed} failed`);
    console.log('========================================');

    if (failed > 0) {
        process.exit(1);
    }
}

runTests().catch(err => {
    console.error('Test error:', err);
    process.exit(1);
});
