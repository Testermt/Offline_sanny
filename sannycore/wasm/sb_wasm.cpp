// WebAssembly bridge for sannycore (Emscripten). Same logic as src/sb_jni.cpp, but with a plain C API
// that offline-engine.js calls. Results are kept in module-owned buffers (valid until the next call).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "sbapi.h"
#include "sbcore.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define SB_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define SB_EXPORT extern "C"
#endif

namespace {
std::unique_ptr<sb::Data> g_data;
std::string g_error, g_kind, g_text, g_modes;
std::vector<uint8_t> g_bytes;
bool g_allowUnverified = false, g_strict = false;

std::string diagText(const std::vector<sb::Diag>& ds) {
  std::string r;
  for (auto& d : ds) {
    char b[16];
    snprintf(b, sizeof b, "ERR_%04d: ", d.code);
    r += b + d.msg;
    if (d.line >= 0) r += " (line " + std::to_string(d.line) + ")";
    if (d.offset >= 0) r += " (offset " + std::to_string(d.offset) + ")";
    r += "\n";
  }
  return r;
}
const char* kindName(sb::FileKind k) { return k == sb::FileKind::MainScm ? "main.scm" : "cleo"; }
}  // namespace

// "id|title|game|extends|sbl" per line
SB_EXPORT const char* sb_list_modes(const char* dataDir) {
  g_modes.clear();
  for (auto& m : sb::listModes(dataDir ? dataDir : ""))
    g_modes += m.id + "|" + m.title + "|" + m.game + "|" + m.extends + "|" + (m.sbl ? "1" : "0") + "\n";
  return g_modes.c_str();
}

SB_EXPORT int sb_init(const char* dataDir, const char* mode) {
  auto d = std::make_unique<sb::Data>();
  std::string err;
  if (!d->loadMode(dataDir ? dataDir : "", mode ? mode : "sa_mobile", &err)) { g_error = err; return 0; }
  g_data = std::move(d);
  g_error.clear();
  return 1;
}

SB_EXPORT void sb_options(int allowUnverified, int strictStrings) {
  g_allowUnverified = allowUnverified != 0;
  g_strict = strictStrings != 0;
}

SB_EXPORT void sb_load_models(const char* text, int ide) {
  if (!g_data || !text) return;
  if (ide) g_data->loadIdeText(text); else g_data->loadModelsText(text);
}

// 1 = ok (text in sb_text), 0 = failed (sb_error)
SB_EXPORT int sb_decompile(const uint8_t* p, int n, int scriptStyle, const char* directive) {
  g_text.clear();
  if (!g_data) { g_error = "not initialised"; return 0; }
  std::vector<uint8_t> file(p, p + n);
  sb::AnyOptions ao;
  ao.scriptStyle = scriptStyle != 0;
  ao.allowUnverified = g_allowUnverified;
  ao.cleoDirective = directive ? directive : "";
  auto r = sb::decompileAny(file, *g_data, ao);
  g_error = diagText(r.diags);
  g_kind = kindName(r.kind);
  g_text = r.text;
  return (r.text.empty() && !r.ok) ? 0 : 1;
}

// 1 = ok (bytes in sb_out_ptr / sb_out_len), 0 = failed (sb_error)
SB_EXPORT int sb_compile(const char* src, int addExtraInfo, int flags) {
  g_bytes.clear();
  if (!g_data) { g_error = "not initialised"; return 0; }
  sb::AnyOptions ao;
  ao.addExtraInfo = addExtraInfo != 0;
  ao.extraInfoFlags = (uint32_t)flags;
  ao.allowUnverified = g_allowUnverified;
  ao.strictStringLimit = g_strict;
  auto r = sb::compileAny(src ? src : "", *g_data, ao);
  g_error = diagText(r.diags);
  g_kind = kindName(r.kind);
  if (!r.ok) return 0;
  g_bytes = std::move(r.bytes);
  return 1;
}

// Source text embedded by "add extra info" (empty string when none)
SB_EXPORT const char* sb_embedded_source(const uint8_t* p, int n) {
  g_text.clear();
  auto ex = sb::readExtraInfo(p, (size_t)n);
  if (ex.present) g_text = ex.source;
  return g_text.c_str();
}

SB_EXPORT const char* sb_text() { return g_text.c_str(); }
SB_EXPORT int sb_text_len() { return (int)g_text.size(); }
SB_EXPORT const uint8_t* sb_out_ptr() { return g_bytes.data(); }
SB_EXPORT int sb_out_len() { return (int)g_bytes.size(); }
SB_EXPORT const char* sb_error() { return g_error.c_str(); }
SB_EXPORT const char* sb_kind() { return g_kind.c_str(); }
