#!/usr/bin/env bash
# Build sannycore -> WebAssembly. Needs Emscripten (https://emscripten.org): `source emsdk_env.sh` first.
# Output: offline/sannycore.js + offline/sannycore.wasm (copy the whole offline/ folder next to index.html).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p ../offline
em++ -std=c++17 -O2 -Iinclude \
  src/sbcore.cpp src/mainscm.cpp src/sbapi.cpp wasm/sb_wasm.cpp \
  -o ../offline/sannycore.js \
  -sMODULARIZE=1 -sEXPORT_NAME=createSannyCore \
  -sENVIRONMENT=worker,web \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sMAXIMUM_MEMORY=1073741824 -sSTACK_SIZE=2097152 \
  -sFILESYSTEM=1 -sDISABLE_EXCEPTION_CATCHING=0 \
  -sEXPORTED_FUNCTIONS=_malloc,_free,_sb_list_modes,_sb_init,_sb_options,_sb_load_models,_sb_decompile,_sb_compile,_sb_embedded_source,_sb_text,_sb_text_len,_sb_out_ptr,_sb_out_len,_sb_error,_sb_kind \
  -sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8
ls -la ../offline/sannycore.*
