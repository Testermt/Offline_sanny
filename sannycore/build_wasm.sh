#!/usr/bin/env bash
# Build sannycore -> WebAssembly (needs Emscripten on PATH). Finds the sources wherever they are in the repo.
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
SBCORE="$(find "$ROOT" -type f -name sbcore.cpp -path '*/src/*' | head -n1)"
WRAP="$(find "$ROOT" -type f -name sb_wasm.cpp | head -n1)"
[ -n "$SBCORE" ] || { echo "ERROR: src/sbcore.cpp not found in repo ($ROOT). Upload the sannycore source (src/ + include/) too."; exit 1; }
[ -n "$WRAP" ]   || { echo "ERROR: wasm/sb_wasm.cpp not found in repo."; exit 1; }
SRC="$(dirname "$SBCORE")"; CORE="$(dirname "$SRC")"
for f in sbcore.cpp mainscm.cpp sbapi.cpp; do [ -f "$SRC/$f" ] || { echo "ERROR: $SRC/$f missing"; exit 1; }; done
[ -f "$CORE/include/sbcore.h" ] || { echo "ERROR: $CORE/include/sbcore.h missing"; exit 1; }
echo "sources: $SRC  wrapper: $WRAP"
mkdir -p "$ROOT/offline"
em++ -std=c++17 -O2 -I"$CORE/include" \
  "$SRC/sbcore.cpp" "$SRC/mainscm.cpp" "$SRC/sbapi.cpp" "$WRAP" \
  -o "$ROOT/offline/sannycore.js" \
  -sMODULARIZE=1 -sEXPORT_NAME=createSannyCore \
  -sENVIRONMENT=worker,web \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sMAXIMUM_MEMORY=1073741824 -sSTACK_SIZE=2097152 \
  -sFILESYSTEM=1 -sDISABLE_EXCEPTION_CATCHING=0 \
  -sEXPORTED_FUNCTIONS=_malloc,_free,_sb_list_modes,_sb_init,_sb_options,_sb_load_models,_sb_decompile,_sb_compile,_sb_embedded_source,_sb_text,_sb_text_len,_sb_out_ptr,_sb_out_len,_sb_error,_sb_kind \
  -sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8
ls -la "$ROOT"/offline/sannycore.*
