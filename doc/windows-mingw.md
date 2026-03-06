# Windows MinGW Build Test

Run from repository root:

```bash
scripts/test_windows_mingw.sh
```

Optional environment overrides:

- `TOOLCHAIN_PREFIX` (default: `x86_64-w64-mingw32`)
- `CC`
- `AR`
- `BUILD_DIR`

This script cross-compiles standard and embedded Mirage libraries (`.a` + `.dll`) and Windows test executables.
