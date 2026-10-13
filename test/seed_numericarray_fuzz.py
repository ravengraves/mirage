"""Generate deterministic seeds in the build directory for numeric-array fuzzing."""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
types = [(0, 1), (1, 2), (2, 4), (3, 8), (16, 1), (17, 2),
         (18, 4), (19, 8), (34, 4), (35, 8), (51, 8), (52, 16)]
for api in ("standard", "embedded"):
    reader = root / ("read_" + api + "_corpus")
    writer = root / ("write_" + api + "_corpus")
    reader.mkdir(parents=True, exist_ok=True)
    writer.mkdir(parents=True, exist_ok=True)
    for index, (kind, width) in enumerate(types):
        for tag in (193, 194):
            for rank, dims, count in ((0, b"", 1), (2, b"\x02\x03", 6), (2, b"\x02\x00", 0)):
                name = f"{tag}-{index}-{rank}-{count}"
                (reader / name).write_bytes(bytes([tag, kind, rank]) + dims + bytes(range(count * width)))
        for rank in range(5):
            (writer / f"{index}-{rank}").write_bytes(bytes([254, rank, index + 1, 2, 2, 2, 2, 1]))
        for zero in range(3):
            dims = [255, 255, 255, 1]
            dims[zero] = 0
            (writer / f"{index}-zero-{zero}").write_bytes(bytes([254, 3, index + 1, *dims, 1]))
    (reader / "overflow").write_bytes(bytes([194, 16, 2, 0x80, 0x80, 1, 2]))
    (reader / "bad-varint").write_bytes(bytes([194, 16, 1]) + b"\xff" * 10)
