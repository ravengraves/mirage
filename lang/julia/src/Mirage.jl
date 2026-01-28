"""
    Mirage

Julia bindings for Mirage WXF serialization library.

# Type Mappings (Julia → WXF)
- `Vector` → List function
- `Tuple` → List function
- `MirageExpr(name, args...)` or `expr(name, args...)` → WXF function
- `Dict` → Association
- `String` → String
- `Vector{UInt8}` (with Bin wrapper) → Binary
- `Int` → i64
- `Int8/Int16/Int32` → i8/i16/i32
- `Float64` → f64
- `Bool` → i8 (1 or 0) for hardware efficiency
- `Nothing` → Symbol "Null"
- `Symbol` → WXF Symbol
- `Array{T,N}` → NumericArray

# Type Mappings (WXF → Julia)
- List function → Vector
- Other functions → MirageExpr (name, arg1, arg2, ...)
- Association → Dict
- String → String
- Binary → Vector{UInt8}
- i8/i16/i32/i64 → Int (note: Bool encodes as i8, decodes as Int)
- f64 → Float64
- Symbol "Null" → nothing
- Symbol "True"/"False" → true/false (from external sources)
- Other Symbols → Symbol
- NumericArray → Array{T,N}
"""
module Mirage

export encode, decode, MirageExpr, expr, Sym, Bin, MirageError

# Find the library path
const LIBMIRAGE_PATH = let
    paths = [
        joinpath(@__DIR__, "..", "..", "..", "build", "libmirage.so"),
        joinpath(@__DIR__, "..", "..", "..", "build", "libmirage.dylib"),
        "/usr/local/lib/libmirage.so",
        "/usr/local/lib/libmirage.dylib",
    ]
    found = nothing
    for p in paths
        if isfile(p)
            found = p
            break
        end
    end
    if found === nothing
        error("Could not find libmirage. Please run 'make' in the mirage directory.")
    end
    found
end

# --------------------------------------------------------------------------
# Error handling
# --------------------------------------------------------------------------

struct MirageError <: Exception
    msg::String
end

Base.showerror(io::IO, e::MirageError) = print(io, "MirageError: ", e.msg)

# --------------------------------------------------------------------------
# Wrapper types for explicit encoding
# --------------------------------------------------------------------------

"""
    Sym(name::String)

Wrapper to encode a string as a WXF Symbol instead of a String.
"""
struct Sym
    name::String
end

Base.show(io::IO, s::Sym) = print(io, "Sym(", repr(s.name), ")")
Base.:(==)(a::Sym, b::Sym) = a.name == b.name

"""
    Bin(data::Vector{UInt8})

Wrapper to encode bytes as WXF Binary instead of a numeric array.
"""
struct Bin
    data::Vector{UInt8}
end

Base.show(io::IO, b::Bin) = print(io, "Bin(", length(b.data), " bytes)")
Base.:(==)(a::Bin, b::Bin) = a.data == b.data

"""
    MirageExpr(head::String, args...)

WXF function expression.

Represents a WXF function call like Plus[1, 2].
First element must be the function name (string), followed by arguments.

# Examples
```julia
MirageExpr("Plus", 1, 2)
expr("Multiply", 5, 7)
```
"""
struct MirageExpr
    head::String
    args::Tuple

    function MirageExpr(head::String, args...)
        new(head, args)
    end
end

Base.show(io::IO, e::MirageExpr) = print(io, "MirageExpr(\"", e.head, "\", ", join(repr.(e.args), ", "), ")")
Base.:(==)(a::MirageExpr, b::MirageExpr) = a.head == b.head && a.args == b.args

# Allow iteration over MirageExpr (head, arg1, arg2, ...)
Base.iterate(e::MirageExpr, state=1) = state == 1 ? (e.head, 2) : (state - 1 <= length(e.args) ? (e.args[state - 1], state + 1) : nothing)
Base.length(e::MirageExpr) = 1 + length(e.args)
Base.getindex(e::MirageExpr, i::Int) = i == 1 ? e.head : e.args[i - 1]

"""
    expr(head::String, args...) -> MirageExpr

Create a WXF function expression.

# Examples
```julia
expr("Plus", 1, 2)
expr("List", [1, 2, 3])
```
"""
expr(head::String, args...) = MirageExpr(head, args...)

# --------------------------------------------------------------------------
# C struct definitions
# --------------------------------------------------------------------------

mutable struct MirageMsg
    max_length::Int64
    length::Int64
    cursor::Int64
    data::Ptr{UInt8}
end

# Element type enum values
const MIRAGE_FUNCTION = 0
const MIRAGE_I8 = 1
const MIRAGE_I16 = 2
const MIRAGE_I32 = 3
const MIRAGE_I64 = 4
const MIRAGE_F64 = 5
const MIRAGE_STRING = 6
const MIRAGE_BINSTRING = 7
const MIRAGE_SYMBOL = 8
const MIRAGE_BIGINT = 9
const MIRAGE_BIGREAL = 10
const MIRAGE_NUMERICARRAY = 12
const MIRAGE_ASSOCIATION = 13
const MIRAGE_RULE = 15

# Array type enum values
const MIRAGE_ARRAY_I8 = 0
const MIRAGE_ARRAY_I16 = 1
const MIRAGE_ARRAY_I32 = 2
const MIRAGE_ARRAY_I64 = 3
const MIRAGE_ARRAY_U8 = 4
const MIRAGE_ARRAY_U16 = 5
const MIRAGE_ARRAY_U32 = 6
const MIRAGE_ARRAY_U64 = 7
const MIRAGE_ARRAY_F32 = 8
const MIRAGE_ARRAY_F64 = 9
const MIRAGE_ARRAY_C32 = 10
const MIRAGE_ARRAY_C64 = 11

# Julia type to array type mapping
const JULIA_TO_ARRAY_TYPE = Dict(
    Int8 => MIRAGE_ARRAY_I8,
    Int16 => MIRAGE_ARRAY_I16,
    Int32 => MIRAGE_ARRAY_I32,
    Int64 => MIRAGE_ARRAY_I64,
    UInt8 => MIRAGE_ARRAY_U8,
    UInt16 => MIRAGE_ARRAY_U16,
    UInt32 => MIRAGE_ARRAY_U32,
    UInt64 => MIRAGE_ARRAY_U64,
    Float32 => MIRAGE_ARRAY_F32,
    Float64 => MIRAGE_ARRAY_F64,
    ComplexF32 => MIRAGE_ARRAY_C32,
    ComplexF64 => MIRAGE_ARRAY_C64,
)

const ARRAY_TYPE_TO_JULIA = Dict(v => k for (k, v) in JULIA_TO_ARRAY_TYPE)

# --------------------------------------------------------------------------
# Low-level C bindings using @ccall with literal library path
# --------------------------------------------------------------------------

function c_create(size::Int64)
    ptr = ccall((:mirage_create, LIBMIRAGE_PATH), Ptr{MirageMsg}, (Int64, Ptr{Cvoid}), size, C_NULL)
    ptr == C_NULL && throw(MirageError("Failed to create message buffer"))
    return ptr
end

function c_destroy(msg::Ptr{MirageMsg})
    msg_ref = Ref(msg)
    ccall((:mirage_destroy, LIBMIRAGE_PATH), Cvoid, (Ptr{Ptr{MirageMsg}}, Ptr{Cvoid}), msg_ref, C_NULL)
end

function c_write_start(msg::Ptr{MirageMsg})
    ret = ccall((:mirage_write_start, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg},), msg)
    ret < 0 && throw(MirageError("write_start failed: $ret"))
    return ret
end

function c_read_start(msg::Ptr{MirageMsg})
    ret = ccall((:mirage_read_start, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg},), msg)
    ret < 0 && throw(MirageError("read_start failed: $ret"))
    return ret
end

function c_write_fn(msg::Ptr{MirageMsg}, name::String, argc::Int64)
    ret = ccall((:mirage_write_fn, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Cstring, Int64, Int64), msg, name, Int64(-1), argc)
    ret < 0 && throw(MirageError("write_fn failed: $ret"))
    return ret
end

function c_write_i8(msg::Ptr{MirageMsg}, v::Int8)
    ret = ccall((:mirage_write_i8, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Int8), msg, v)
    ret < 0 && throw(MirageError("write_i8 failed: $ret"))
    return ret
end

function c_write_i16(msg::Ptr{MirageMsg}, v::Int16)
    ret = ccall((:mirage_write_i16, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Int16), msg, v)
    ret < 0 && throw(MirageError("write_i16 failed: $ret"))
    return ret
end

function c_write_i32(msg::Ptr{MirageMsg}, v::Int32)
    ret = ccall((:mirage_write_i32, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Int32), msg, v)
    ret < 0 && throw(MirageError("write_i32 failed: $ret"))
    return ret
end

function c_write_i64(msg::Ptr{MirageMsg}, v::Int64)
    ret = ccall((:mirage_write_i64, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Int64), msg, v)
    ret < 0 && throw(MirageError("write_i64 failed: $ret"))
    return ret
end

function c_write_f64(msg::Ptr{MirageMsg}, v::Float64)
    ret = ccall((:mirage_write_f64, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Float64), msg, v)
    ret < 0 && throw(MirageError("write_f64 failed: $ret"))
    return ret
end

function c_write_string(msg::Ptr{MirageMsg}, s::String)
    ret = ccall((:mirage_write_string, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Cstring, Int64), msg, s, Int64(-1))
    ret < 0 && throw(MirageError("write_string failed: $ret"))
    return ret
end

function c_write_bin(msg::Ptr{MirageMsg}, data::Vector{UInt8})
    ret = ccall((:mirage_write_bin, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{UInt8}, Int64), msg, data, length(data))
    ret < 0 && throw(MirageError("write_bin failed: $ret"))
    return ret
end

function c_write_sym(msg::Ptr{MirageMsg}, name::String)
    ret = ccall((:mirage_write_sym, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Cstring, Int64), msg, name, Int64(-1))
    ret < 0 && throw(MirageError("write_sym failed: $ret"))
    return ret
end

function c_write_bigint(msg::Ptr{MirageMsg}, s::String)
    ret = ccall((:mirage_write_bigint, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Cstring, Int64), msg, s, Int64(-1))
    ret < 0 && throw(MirageError("write_bigint failed: $ret"))
    return ret
end

function c_write_association(msg::Ptr{MirageMsg}, len::Int64)
    ret = ccall((:mirage_write_association, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Int64), msg, len)
    ret < 0 && throw(MirageError("write_association failed: $ret"))
    return ret
end

function c_write_assoc_rule(msg::Ptr{MirageMsg})
    ret = ccall((:mirage_write_assoc_rule, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg},), msg)
    ret < 0 && throw(MirageError("write_assoc_rule failed: $ret"))
    return ret
end

function c_write_numericarray(msg::Ptr{MirageMsg}, atype::Int, rank::Int64, dims::Vector{Int64}, data::Ptr{Cvoid})
    ret = ccall((:mirage_write_numericarray, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Cint, Int64, Ptr{Int64}, Ptr{Cvoid}),
                msg, Cint(atype), rank, dims, data)
    ret < 0 && throw(MirageError("write_numericarray failed: $ret"))
    return ret
end

function c_peek_element_type(msg::Ptr{MirageMsg})
    etype = Ref{Cint}(0)
    ret = ccall((:mirage_peek_element_type, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{Cint}), msg, etype)
    ret < 0 && throw(MirageError("peek_element_type failed: $ret"))
    return Int(etype[])
end

function c_read_fn(msg::Ptr{MirageMsg})
    name = Vector{UInt8}(undef, 256)
    nlen = Ref{Int64}(0)
    argc = Ref{Int64}(0)
    ret = ccall((:mirage_read_fn, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{UInt8}, Ptr{Int64}, Int64, Ptr{Int64}),
                msg, name, nlen, Int64(256), argc)
    ret < 0 && throw(MirageError("read_fn failed: $ret"))
    return (String(name[1:nlen[]]), argc[])
end

function c_read_i8(msg::Ptr{MirageMsg})
    v = Ref{Int8}(0)
    ret = ccall((:mirage_read_i8, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Ptr{Int8}), msg, v)
    ret < 0 && throw(MirageError("read_i8 failed: $ret"))
    return v[]
end

function c_read_i16(msg::Ptr{MirageMsg})
    v = Ref{Int16}(0)
    ret = ccall((:mirage_read_i16, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Ptr{Int16}), msg, v)
    ret < 0 && throw(MirageError("read_i16 failed: $ret"))
    return v[]
end

function c_read_i32(msg::Ptr{MirageMsg})
    v = Ref{Int32}(0)
    ret = ccall((:mirage_read_i32, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Ptr{Int32}), msg, v)
    ret < 0 && throw(MirageError("read_i32 failed: $ret"))
    return v[]
end

function c_read_i64(msg::Ptr{MirageMsg})
    v = Ref{Int64}(0)
    ret = ccall((:mirage_read_i64, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Ptr{Int64}), msg, v)
    ret < 0 && throw(MirageError("read_i64 failed: $ret"))
    return v[]
end

function c_read_f64(msg::Ptr{MirageMsg})
    v = Ref{Float64}(0.0)
    ret = ccall((:mirage_read_f64, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg}, Ptr{Float64}), msg, v)
    ret < 0 && throw(MirageError("read_f64 failed: $ret"))
    return v[]
end

function c_read_string(msg::Ptr{MirageMsg})
    buf = Vector{UInt8}(undef, 65536)
    len = Ref{Int64}(0)
    ret = ccall((:mirage_read_string, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{UInt8}, Ptr{Int64}, Int64),
                msg, buf, len, Int64(65536))
    ret < 0 && throw(MirageError("read_string failed: $ret"))
    return String(buf[1:len[]])
end

function c_read_bin(msg::Ptr{MirageMsg})
    buf = Vector{UInt8}(undef, 65536)
    len = Ref{Int64}(0)
    ret = ccall((:mirage_read_bin, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{UInt8}, Ptr{Int64}, Int64),
                msg, buf, len, Int64(65536))
    ret < 0 && throw(MirageError("read_bin failed: $ret"))
    return buf[1:len[]]
end

function c_read_sym(msg::Ptr{MirageMsg})
    buf = Vector{UInt8}(undef, 256)
    len = Ref{Int64}(0)
    ret = ccall((:mirage_read_sym, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{UInt8}, Ptr{Int64}, Int64),
                msg, buf, len, Int64(256))
    ret < 0 && throw(MirageError("read_sym failed: $ret"))
    return String(buf[1:len[]])
end

function c_read_bigint(msg::Ptr{MirageMsg})
    buf = Vector{UInt8}(undef, 1024)
    len = Ref{Int64}(0)
    ret = ccall((:mirage_read_bigint, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{UInt8}, Ptr{Int64}, Int64),
                msg, buf, len, Int64(1024))
    ret < 0 && throw(MirageError("read_bigint failed: $ret"))
    return parse(BigInt, String(buf[1:len[]]))
end

function c_read_bigreal(msg::Ptr{MirageMsg})
    buf = Vector{UInt8}(undef, 1024)
    len = Ref{Int64}(0)
    ret = ccall((:mirage_read_bigreal, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{UInt8}, Ptr{Int64}, Int64),
                msg, buf, len, Int64(1024))
    ret < 0 && throw(MirageError("read_bigreal failed: $ret"))
    return String(buf[1:len[]])
end

function c_read_association(msg::Ptr{MirageMsg})
    len = Ref{Int64}(0)
    ret = ccall((:mirage_read_association, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{Int64}), msg, len)
    ret < 0 && throw(MirageError("read_association failed: $ret"))
    return len[]
end

function c_read_assoc_rule(msg::Ptr{MirageMsg})
    ret = ccall((:mirage_read_assoc_rule, LIBMIRAGE_PATH), Int64, (Ptr{MirageMsg},), msg)
    ret < 0 && throw(MirageError("read_assoc_rule failed: $ret"))
    return ret
end

function c_read_numericarray_full(msg::Ptr{MirageMsg})
    atype = Ref{Cint}(0)
    rank = Ref{Int64}(0)
    dims = Vector{Int64}(undef, 16)
    buf = Vector{UInt8}(undef, 1024 * 1024)  # 1MB max

    ret = ccall((:mirage_read_numericarray, LIBMIRAGE_PATH), Int64,
                (Ptr{MirageMsg}, Ptr{Cint}, Ptr{Int64}, Ptr{Int64}, Int64, Ptr{UInt8}, Int64),
                msg, atype, rank, dims, Int64(16), buf, Int64(length(buf)))
    ret < 0 && throw(MirageError("read_numericarray failed: $ret"))

    jtype = get(ARRAY_TYPE_TO_JULIA, Int(atype[]), nothing)
    jtype === nothing && throw(MirageError("Unknown array type: $(atype[])"))

    r = Int(rank[])
    shape = r == 0 ? () : tuple(dims[1:r]...)
    nelems = r == 0 ? 1 : prod(shape)

    # Reinterpret bytes as the correct type
    data = reinterpret(jtype, buf[1:nelems * sizeof(jtype)])

    if r == 0
        return data[1]  # Scalar
    else
        # WXF uses row-major, Julia uses column-major
        arr = reshape(copy(data), reverse(shape)...)
        if r > 1
            return permutedims(arr, reverse(1:r))
        end
        return arr
    end
end

# --------------------------------------------------------------------------
# High-level encoding
# --------------------------------------------------------------------------

function estimate_size(obj, depth=0)
    depth > 100 && throw(MirageError("Object too deeply nested"))

    if obj === nothing
        return 8
    elseif obj isa Bool
        return 2
    elseif obj isa Int8
        return 2
    elseif obj isa Int16
        return 4
    elseif obj isa Int32
        return 6
    elseif obj isa Int64 || obj isa Int
        return 10
    elseif obj isa BigInt
        return length(string(obj)) + 10
    elseif obj isa Float64 || obj isa Float32
        return 9
    elseif obj isa String
        return sizeof(obj) + 10
    elseif obj isa Symbol
        return length(string(obj)) + 10
    elseif obj isa Sym
        return length(obj.name) + 10
    elseif obj isa Bin
        return length(obj.data) + 10
    elseif obj isa MirageExpr
        # Check MirageExpr before Tuple since we want explicit handling
        total = length(obj.head) + 10
        for arg in obj.args
            total += estimate_size(arg, depth + 1)
        end
        return total
    elseif obj isa AbstractVector
        total = 10
        for item in obj
            total += estimate_size(item, depth + 1)
        end
        return total
    elseif obj isa Tuple
        # Plain tuples encode as WXF List
        total = 10
        for item in obj
            total += estimate_size(item, depth + 1)
        end
        return total
    elseif obj isa AbstractDict
        total = 10
        for (k, v) in obj
            total += 2 + estimate_size(k, depth + 1) + estimate_size(v, depth + 1)
        end
        return total
    elseif obj isa AbstractArray
        return sizeof(obj) + 64 + 16 * max(ndims(obj), 1)
    else
        throw(MirageError("Cannot encode type: $(typeof(obj))"))
    end
end

function write_value(msg::Ptr{MirageMsg}, obj, depth=0)
    depth > 100 && throw(MirageError("Object too deeply nested"))

    if obj === nothing
        c_write_sym(msg, "Null")
    elseif obj isa Bool
        c_write_i8(msg, obj ? Int8(1) : Int8(0))
    elseif obj isa Int8
        c_write_i8(msg, obj)
    elseif obj isa Int16
        c_write_i16(msg, obj)
    elseif obj isa Int32
        c_write_i32(msg, obj)
    elseif obj isa Int64 || obj isa Int
        c_write_i64(msg, Int64(obj))
    elseif obj isa BigInt
        c_write_bigint(msg, string(obj))
    elseif obj isa Float64
        c_write_f64(msg, obj)
    elseif obj isa Float32
        c_write_f64(msg, Float64(obj))
    elseif obj isa String
        c_write_string(msg, obj)
    elseif obj isa Symbol
        c_write_sym(msg, string(obj))
    elseif obj isa Sym
        c_write_sym(msg, obj.name)
    elseif obj isa Bin
        c_write_bin(msg, obj.data)
    elseif obj isa MirageExpr
        # Check MirageExpr before Tuple for explicit handling
        c_write_fn(msg, obj.head, length(obj.args))
        for arg in obj.args
            write_value(msg, arg, depth + 1)
        end
    elseif obj isa Tuple
        # Plain tuples encode as WXF List
        c_write_fn(msg, "List", length(obj))
        for item in obj
            write_value(msg, item, depth + 1)
        end
    elseif obj isa AbstractDict
        # Dict -> Association
        c_write_association(msg, length(obj))
        for (k, v) in obj
            c_write_assoc_rule(msg)
            write_value(msg, k, depth + 1)
            write_value(msg, v, depth + 1)
        end
    elseif obj isa AbstractVector && !(eltype(obj) <: Number)
        # Generic vector -> List
        c_write_fn(msg, "List", length(obj))
        for item in obj
            write_value(msg, item, depth + 1)
        end
    elseif obj isa AbstractArray && eltype(obj) <: Number
        # Numeric array
        atype = get(JULIA_TO_ARRAY_TYPE, eltype(obj), nothing)
        if atype === nothing
            # Convert to Float64
            obj = convert(Array{Float64}, obj)
            atype = MIRAGE_ARRAY_F64
        end

        arr = collect(obj)  # Ensure contiguous

        # WXF uses row-major order, Julia uses column-major
        if ndims(arr) > 1
            arr = permutedims(arr, reverse(1:ndims(arr)))
        end

        dims = Int64[size(arr)...]  # After permutation, dims are already reversed

        c_write_numericarray(msg, atype, ndims(arr), dims, Ptr{Cvoid}(pointer(arr)))
    else
        throw(MirageError("Cannot encode type: $(typeof(obj))"))
    end
end

function read_value(msg::Ptr{MirageMsg}, depth=0)
    depth > 100 && throw(MirageError("Object too deeply nested"))

    etype = c_peek_element_type(msg)

    if etype == MIRAGE_FUNCTION
        name, argc = c_read_fn(msg)
        args = [read_value(msg, depth + 1) for _ in 1:argc]
        if name == "List"
            return args
        else
            # Other functions -> MirageExpr
            return MirageExpr(name, args...)
        end
    elseif etype == MIRAGE_I8
        return Int(c_read_i8(msg))
    elseif etype == MIRAGE_I16
        return Int(c_read_i16(msg))
    elseif etype == MIRAGE_I32
        return Int(c_read_i32(msg))
    elseif etype == MIRAGE_I64
        return c_read_i64(msg)
    elseif etype == MIRAGE_F64
        return c_read_f64(msg)
    elseif etype == MIRAGE_STRING
        return c_read_string(msg)
    elseif etype == MIRAGE_BINSTRING
        return c_read_bin(msg)
    elseif etype == MIRAGE_SYMBOL
        name = c_read_sym(msg)
        if name == "Null"
            return nothing
        elseif name == "True"
            return true
        elseif name == "False"
            return false
        else
            return Symbol(name)
        end
    elseif etype == MIRAGE_BIGINT
        return c_read_bigint(msg)
    elseif etype == MIRAGE_BIGREAL
        return c_read_bigreal(msg)
    elseif etype == MIRAGE_NUMERICARRAY
        return c_read_numericarray_full(msg)
    elseif etype == MIRAGE_ASSOCIATION
        len = c_read_association(msg)
        result = Dict{Any,Any}()
        for _ in 1:len
            c_read_assoc_rule(msg)
            k = read_value(msg, depth + 1)
            v = read_value(msg, depth + 1)
            result[k] = v
        end
        return result
    else
        throw(MirageError("Unknown element type: $etype"))
    end
end

# --------------------------------------------------------------------------
# Public API
# --------------------------------------------------------------------------

"""
    encode(obj) -> Vector{UInt8}

Encode a Julia object to WXF bytes.

# Examples
```julia
encode([1, 2, 3])
encode(Dict("x" => 1.0, "y" => 2.0))
encode(expr("Plus", 1, 2))  # WXF function
encode((1, 2, 3))  # Tuple encodes as List
```
"""
function encode(obj)
    size = max(estimate_size(obj) * 2 + 1024, 4096)

    msg = c_create(size)
    try
        c_write_start(msg)
        write_value(msg, obj)

        # Get buffer contents
        msg_data = unsafe_load(msg)
        result = Vector{UInt8}(undef, msg_data.length)
        unsafe_copyto!(pointer(result), msg_data.data, msg_data.length)
        return result
    finally
        c_destroy(msg)
    end
end

"""
    decode(data::Vector{UInt8}) -> Any

Decode WXF bytes to a Julia object.

# Examples
```julia
decode(encode([1, 2, 3]))  # => [1, 2, 3]
decode(encode(Dict("x" => 1.0)))  # => Dict("x" => 1.0)
```
"""
function decode(data::Vector{UInt8})
    msg = c_create(length(data))
    try
        # Copy data into message buffer
        msg_data = unsafe_load(msg)
        unsafe_copyto!(msg_data.data, pointer(data), length(data))

        # Update length field
        msg_struct = unsafe_load(msg)
        msg_struct.length = length(data)
        unsafe_store!(msg, msg_struct)

        c_read_start(msg)
        return read_value(msg)
    finally
        c_destroy(msg)
    end
end

end # module
