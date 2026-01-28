using Test

# Add package to load path
push!(LOAD_PATH, joinpath(@__DIR__, "..", "src"))

using Mirage

@testset "Mirage.jl" begin

    @testset "Basic Types" begin
        # Integers
        @test decode(encode(42)) == 42
        @test decode(encode(Int8(7))) == 7
        @test decode(encode(Int16(1000))) == 1000
        @test decode(encode(Int32(100000))) == 100000
        @test decode(encode(Int64(9223372036854775807))) == 9223372036854775807

        # Float
        @test decode(encode(3.14159)) ≈ 3.14159

        # String
        @test decode(encode("Hello, Julia!")) == "Hello, Julia!"

        # Nothing/Null
        @test decode(encode(nothing)) === nothing

        # Bool - encodes as i8 for hardware efficiency, decodes as Int
        @test decode(encode(true)) == 1   # true → i8(1) → 1
        @test decode(encode(false)) == 0  # false → i8(0) → 0
    end

    @testset "Symbols" begin
        # Julia Symbol -> WXF Symbol
        @test decode(encode(:MySymbol)) == :MySymbol

        # Sym wrapper
        data = encode(Sym("Global`x"))
        result = decode(data)
        @test result == Symbol("Global`x")
    end

    @testset "Collections" begin
        # Vector -> List
        @test decode(encode([1, 2, 3])) == [1, 2, 3]
        @test decode(encode(["a", "b", "c"])) == ["a", "b", "c"]

        # Empty vector
        @test decode(encode([])) == []

        # Dict -> Association
        d = Dict("x" => 1, "y" => 2)
        result = decode(encode(d))
        @test result["x"] == 1
        @test result["y"] == 2

        # Empty dict
        @test decode(encode(Dict())) == Dict()

        # Nested
        nested = Dict("list" => [1, 2, 3], "value" => 42)
        result = decode(encode(nested))
        @test result["list"] == [1, 2, 3]
        @test result["value"] == 42
    end

    @testset "WXF Functions (MirageExpr)" begin
        # MirageExpr with args
        data = encode(expr("Plus", 1, 2))
        result = decode(data)
        @test result isa MirageExpr
        @test result.head == "Plus"
        @test result.args == (1, 2)

        # Function with no args
        result = decode(encode(expr("Empty")))
        @test result isa MirageExpr
        @test result.head == "Empty"
        @test length(result.args) == 0

        # Nested functions
        data = encode(expr("Outer", expr("Inner", 1, 2), 3))
        result = decode(data)
        @test result isa MirageExpr
        @test result.head == "Outer"
        @test result.args[1] isa MirageExpr
        @test result.args[1].head == "Inner"
        @test result.args[1].args == (1, 2)
        @test result.args[2] == 3

        # Plain tuple now encodes as List
        data = encode((1, 2, 3))
        result = decode(data)
        @test result isa Vector
        @test result == [1, 2, 3]
    end

    @testset "Binary Data" begin
        binary = Bin(UInt8[0xDE, 0xAD, 0xBE, 0xEF])
        result = decode(encode(binary))
        @test result == UInt8[0xDE, 0xAD, 0xBE, 0xEF]
    end

    @testset "BigInt" begin
        big = BigInt(10)^30
        result = decode(encode(big))
        @test result == big
    end

    @testset "Numeric Arrays" begin
        # 1D Int64 array
        arr = Int64[10, 20, 30, 40]
        result = decode(encode(arr))
        @test result == arr

        # 1D Float64 array
        arr = Float64[1.0, 2.0, 3.0]
        result = decode(encode(arr))
        @test result ≈ arr

        # 2D array
        arr = Float64[1.0 2.0; 3.0 4.0]
        result = decode(encode(arr))
        @test size(result) == (2, 2)
        @test result ≈ arr

        # Int32 array
        arr = Int32[1, 2, 3, 4, 5]
        result = decode(encode(arr))
        @test result == arr
    end

    @testset "Mixed Types" begin
        data = [
            42,
            3.14,
            "hello",
            nothing,
            [1, 2, 3],
            Dict("a" => 1),
        ]
        result = decode(encode(data))
        @test result[1] == 42
        @test result[2] ≈ 3.14
        @test result[3] == "hello"
        @test result[4] === nothing
        @test result[5] == [1, 2, 3]
        @test result[6]["a"] == 1
    end

    @testset "Error Handling" begin
        # MirageExpr head must be a string
        @test_throws MethodError MirageExpr(123, 1, 2)

        # Tuples now encode as Lists (no error)
        @test decode(encode((1, 2, 3))) == [1, 2, 3]
        @test decode(encode(())) == []
    end

    @testset "Round-trip Consistency" begin
        # Various objects should survive encode/decode
        # Note: Bool encodes as i8, so true→1, false→0
        objects = [
            42,
            -1000,
            3.14159265358979,
            "Hello, World!",
            nothing,
            # Bool excluded: encodes as i8, decodes as Int
            [1, 2, 3, 4, 5],
            Dict("key" => "value"),
            expr("Function", 1, 2, 3),
            :Symbol,
            BigInt(10)^20,
        ]

        for obj in objects
            result = decode(encode(obj))
            if obj isa AbstractFloat
                @test result ≈ obj
            elseif obj isa MirageExpr
                @test result isa MirageExpr
                @test result.head == obj.head
                @test result.args == obj.args
            else
                @test result == obj
            end
        end
    end

end

println("\nAll Julia tests passed!")
