// JNI bridge: Kotlin/Java  <->  sannycore.  Class: com.sannycore.SannyCore
#include <jni.h>

#include <memory>
#include <mutex>
#include <string>

#include "sbapi.h"
#include "sbcore.h"

namespace {
std::mutex g_mu;
std::unique_ptr<sb::Data> g_data;
std::string g_lastError;
std::string g_lastKind;
bool g_allowUnverified = false;
bool g_strictStrings = false;

std::string toStd(JNIEnv* env, jstring s) {
  if (!s) return "";
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string r = c ? c : "";
  if (c) env->ReleaseStringUTFChars(s, c);
  return r;
}
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
std::string bytesToStd(JNIEnv* env, jbyteArray arr) {
  jsize n = env->GetArrayLength(arr);
  std::string buf((size_t)n, '\0');
  env->GetByteArrayRegion(arr, 0, n, reinterpret_cast<jbyte*>(&buf[0]));
  return buf;
}
}  // namespace

extern "C" {

// String nativeListModes(String sannyDataDir) -> one mode per line: id|title|game|extends|sbl(0/1)   ("game select" list)
JNIEXPORT jstring JNICALL Java_com_sannycore_SannyCore_nativeListModes(JNIEnv* env, jobject, jstring dir) {
  std::string out;
  for (auto& m : sb::listModes(toStd(env, dir))) out += m.id + "|" + m.title + "|" + m.game + "|" + m.extends + "|" + (m.sbl ? "1" : "0") + "\n";
  return env->NewStringUTF(out.c_str());
}

// void nativeAllowUnverified(boolean)  -- try games whose binary encoding is not verified yet (experimental)
JNIEXPORT void JNICALL Java_com_sannycore_SannyCore_nativeAllowUnverified(JNIEnv*, jobject, jboolean v) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_allowUnverified = v == JNI_TRUE;
}

// String nativeDecompileFile(byte[] file, boolean scriptStyle, String cleoDirective)
// Auto detects the kind: main.scm (header + MAIN + missions) or a CLEO script (.csa/.cs/.csi, with or without extra info). Returns null on a fatal error.
JNIEXPORT jstring JNICALL Java_com_sannycore_SannyCore_nativeDecompileFile(JNIEnv* env, jobject, jbyteArray arr, jboolean style, jstring directive) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_data) { g_lastError = "not initialised"; return nullptr; }
  std::string buf = bytesToStd(env, arr);
  std::vector<uint8_t> file(buf.begin(), buf.end());
  sb::AnyOptions ao;
  ao.scriptStyle = style == JNI_TRUE;
  ao.allowUnverified = g_allowUnverified;
  ao.cleoDirective = toStd(env, directive);
  auto r = sb::decompileAny(file, *g_data, ao);
  g_lastError = diagText(r.diags);
  g_lastKind = r.kind == sb::FileKind::MainScm ? "main.scm" : "cleo";
  if (r.text.empty() && !r.ok) return nullptr;
  return env->NewStringUTF(r.text.c_str());
}

// byte[] nativeCompileFile(String source, boolean addExtraInfo, int flags) -> main.scm or CLEO script bytes (null on errors; see nativeLastError)
JNIEXPORT jbyteArray JNICALL Java_com_sannycore_SannyCore_nativeCompileFile(JNIEnv* env, jobject, jstring src, jboolean extra, jint flags) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_data) { g_lastError = "not initialised"; return nullptr; }
  sb::AnyOptions ao;
  ao.addExtraInfo = extra == JNI_TRUE;
  ao.extraInfoFlags = (uint32_t)flags;
  ao.allowUnverified = g_allowUnverified;
  ao.strictStringLimit = g_strictStrings;
  auto r = sb::compileAny(toStd(env, src), *g_data, ao);
  g_lastError = diagText(r.diags);
  g_lastKind = r.kind == sb::FileKind::MainScm ? "main.scm" : "cleo";
  if (!r.ok) return nullptr;
  jbyteArray arr = env->NewByteArray((jsize)r.bytes.size());
  env->SetByteArrayRegion(arr, 0, (jsize)r.bytes.size(), reinterpret_cast<const jbyte*>(r.bytes.data()));
  return arr;
}

// String nativeLastKind() -> "main.scm" | "cleo" of the last decompileFile / compileFile call
JNIEXPORT jstring JNICALL Java_com_sannycore_SannyCore_nativeLastKind(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lk(g_mu);
  return env->NewStringUTF(g_lastKind.c_str());
}

// void nativeStrictStrings(boolean) -- true: Sanny 4.2.0 text length limits (ERR_0027); false (default): longer text is accepted
JNIEXPORT void JNICALL Java_com_sannycore_SannyCore_nativeStrictStrings(JNIEnv*, jobject, jboolean v) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_strictStrings = v == JNI_TRUE;
}

// boolean nativeInit(String sannyDataDir, String modeId)   modeId: "sa_mobile", "sa", ...
JNIEXPORT jboolean JNICALL Java_com_sannycore_SannyCore_nativeInit(JNIEnv* env, jobject, jstring dir, jstring mode) {
  std::lock_guard<std::mutex> lk(g_mu);
  auto d = std::make_unique<sb::Data>();
  std::string err;
  if (!d->loadMode(toStd(env, dir), toStd(env, mode), &err)) { g_lastError = err; return JNI_FALSE; }
  g_data = std::move(d);
  g_lastError.clear();
  return JNI_TRUE;
}

// void nativeLoadModels(String text, boolean ide)   "NAME=ID" lines, or .ide file text -> #MODEL names
JNIEXPORT void JNICALL Java_com_sannycore_SannyCore_nativeLoadModels(JNIEnv* env, jobject, jstring text, jboolean ide) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_data) return;
  if (ide) g_data->loadIdeText(toStd(env, text)); else g_data->loadModelsText(toStd(env, text));
}

// String nativeDecompile(byte[] script, boolean scriptStyle, String directive)
// Script files with a Sanny "extra info" trailer are decompiled from their code part only.
JNIEXPORT jstring JNICALL Java_com_sannycore_SannyCore_nativeDecompile(JNIEnv* env, jobject, jbyteArray arr, jboolean style, jstring directive) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_data) { g_lastError = "not initialised"; return nullptr; }
  std::string buf = bytesToStd(env, arr);
  auto ex = sb::readExtraInfo(reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
  sb::Options opt;
  opt.scriptStyle = style == JNI_TRUE;
  opt.allowUnverified = g_allowUnverified;
  opt.cleoExt = toStd(env, directive);
  auto r = sb::decompile(reinterpret_cast<const uint8_t*>(buf.data()), ex.present ? ex.codeSize : buf.size(), *g_data, opt);
  g_lastError = diagText(r.diags);
  return env->NewStringUTF(r.text.c_str());
}

// String nativeEmbeddedSource(byte[] script)  -> original source stored by "add extra info", or null
JNIEXPORT jstring JNICALL Java_com_sannycore_SannyCore_nativeEmbeddedSource(JNIEnv* env, jobject, jbyteArray arr) {
  std::string buf = bytesToStd(env, arr);
  auto ex = sb::readExtraInfo(reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
  if (!ex.present || ex.source.empty()) return nullptr;
  return env->NewStringUTF(ex.source.c_str());
}

// byte[] nativeCompile(String source, boolean addExtraInfo, int flags) -> script bytes (null on errors; see nativeLastError)
JNIEXPORT jbyteArray JNICALL Java_com_sannycore_SannyCore_nativeCompile(JNIEnv* env, jobject, jstring src, jboolean extra, jint flags) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_data) { g_lastError = "not initialised"; return nullptr; }
  sb::Options opt;
  opt.allowUnverified = g_allowUnverified;
  opt.strictStringLimit = g_strictStrings;
  std::string source = toStd(env, src);
  auto r = sb::compile(source, *g_data, opt);
  g_lastError = diagText(r.diags);
  if (!r.ok) return nullptr;
  std::vector<uint8_t> out = extra ? sb::writeExtraInfo(r.bytes, (uint32_t)flags, source) : r.bytes;
  jbyteArray arr = env->NewByteArray((jsize)out.size());
  env->SetByteArrayRegion(arr, 0, (jsize)out.size(), reinterpret_cast<const jbyte*>(out.data()));
  return arr;
}

JNIEXPORT jstring JNICALL Java_com_sannycore_SannyCore_nativeLastError(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lk(g_mu);
  return env->NewStringUTF(g_lastError.c_str());
}

}  // extern "C"
