# Headless Ghidra Scripts (`re/ghidra_scripts/`)

Scripts here run inside Ghidra `analyzeHeadless` against your local unaligned runtime memory dump (`re/dumps/*.exe`, gitignored):

- `ExportSymbols.java`: exports discovered functions (`name`, `rva`, `size`) relative to `ImageBase` into `re/db/` JSON for `sftool sigmaker` / signature verification.

Example workflow:

```bash
# 1. Capture a clean unaligned dump offline by enabling in soulsframe.toml:
#    [modules] probe_tracer = true
#    [probe]   dump_module = true
# 2. Copy soulsframe_mem_dump.exe into re/dumps/ and inspect sections:
./build/host-tests/tools/cpp/sftool pe-info re/dumps/ds3_dump.exe
# 3. Analyze headlessly in Ghidra and export symbol JSON:
analyzeHeadless /work/re/project SoulsDS3 \
  -import /work/re/dumps/ds3_dump.exe -overwrite \
  -scriptPath /work/re/ghidra_scripts \
  -postScript ExportSymbols.java /work/re/db/ds3_functions.json
```
