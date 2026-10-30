# Offline Mode — setup (3 steps)

Folder layout (next to index.html):
```
index.html  sw.js  manifest.json
offline/offline-engine.js  offline/offline-mode.js         <- included
offline/sannycore.js  offline/sannycore.wasm               <- step 1
offline/sannydata.pack                                     <- step 2
sannycore/ (your C++ source + wasm/sb_wasm.cpp + build_wasm.sh + tools/pack_data.py)
```
1. **Build the WASM** (needs Emscripten; easiest = GitHub Actions): put `sannycore/` + `.github/workflows/build-wasm.yml`
   in a repo -> Actions -> "Build sannycore WASM" -> Run -> download artifact `sannycore-wasm`
   (`sannycore.js` + `sannycore.wasm`) into `offline/`. Or locally: `source emsdk_env.sh && bash sannycore/build_wasm.sh`.
2. **Pack Sanny's data**: `python3 sannycore/tools/pack_data.py <Sanny Builder>/data offline/sannydata.pack`
   (add `--modes sa,sa_mobile,gta3` to make it smaller).
3. Deploy. In the page: *Download offline engine* -> switch on *Use Offline Mode*.

Notes
- sannydata.pack contains Sanny Builder's own data files (opcode lists etc.). Check Sanny's licence before hosting it publicly.
- Offline works for the games sannycore loads (sa, sa_mobile, gta3 verified; others experimental). Other games, and
  custom SDK / custom DB uploads, still use the server (when online).
- The service worker (v2) also caches Tailwind/Monaco so the editor itself opens with no internet.
