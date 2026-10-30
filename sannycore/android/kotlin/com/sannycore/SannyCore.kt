package com.sannycore

/**
 * Kotlin face of libsannycore.so (JNI).
 * Copy Sanny's `data/` folder (plus optypes.txt from tools/gen_optypes.py) into app storage and pass that path to [init].
 */
object SannyCore {
    init { System.loadLibrary("sannycore") }

    data class Mode(val id: String, val title: String, val game: String, val extends: String, val sbl: Boolean)

    /** Game select: every edit mode found in the Sanny `data` folder (SBL modes are listed but not supported yet). */
    fun listModes(dataDir: String): List<Mode> = nativeListModes(dataDir).lines().filter { it.isNotBlank() }.map {
        val p = it.split("|")
        Mode(p[0], p[1], p[2], p[3], p[4] == "1")
    }

    /** @param dataDir Sanny `data` folder, @param mode edit mode id: "sa_mobile", "sa", ... */
    fun init(dataDir: String, mode: String = "sa_mobile"): Boolean = nativeInit(dataDir, mode)

    /** Optional #MODEL names: pass .ide file text (ide=true) or "NAME=ID" lines (ide=false). */
    fun loadModels(text: String, ide: Boolean = true) = nativeLoadModels(text, ide)

    /** Binary script -> Sanny-style source. Handles files with an "extra info" trailer. Warnings: [lastError]. */
    fun decompile(script: ByteArray, scriptStyle: Boolean = true, directive: String = ""): String? =
        nativeDecompile(script, scriptStyle, directive)

    /**
     * Any script file of GTA San Andreas (Android): the kind is detected from the content.
     *  - main.scm / mainV1.scm -> text with DEFINE header, "//-------------MAIN---------------" and "//-------------Mission N---------------"
     *  - CLEO scripts (.csa .cs .csi), with or without Sanny's extra info trailer -> Sanny text (hex..end data blocks included)
     */
    fun decompileFile(file: ByteArray, scriptStyle: Boolean = true, cleoDirective: String = ""): String? =
        nativeDecompileFile(file, scriptStyle, cleoDirective)

    /** Text -> main.scm or CLEO script bytes (main.scm when the text has a DEFINE OBJECTS header and a MAIN section). */
    fun compileFile(source: String, addExtraInfo: Boolean = false, flags: Int = 0x1420): ByteArray? =
        nativeCompileFile(source, addExtraInfo, flags)

    /** "main.scm" or "cleo": what the last decompileFile / compileFile call handled. */
    fun lastKind(): String = nativeLastKind()

    /** Original source embedded by Sanny's "add extra info" option, or null. */
    fun embeddedSource(script: ByteArray): String? = nativeEmbeddedSource(script)

    /** Source -> script bytes (null on errors, see [lastError]). addExtraInfo embeds the source like Sanny does. */
    fun compile(source: String, addExtraInfo: Boolean = false, flags: Int = 0x1420): ByteArray? =
        nativeCompile(source, addExtraInfo, flags)

    /** Only SA / SA Mobile encodings are verified. Enable to try the other games anyway (experimental, may produce wrong bytes). */
    fun allowUnverified(enabled: Boolean) = nativeAllowUnverified(enabled)

    /** true: Sanny 4.2.0 text-length limits (ERR_0027: 7 chars, 15 on SA PC). Default false: longer 'text' parameters compile (8..15 chars as a 16-byte field, longer as a length-prefixed string). */
    fun strictStrings(enabled: Boolean) = nativeStrictStrings(enabled)

    fun lastError(): String = nativeLastError()

    private external fun nativeListModes(dataDir: String): String
    private external fun nativeInit(dataDir: String, mode: String): Boolean
    private external fun nativeLoadModels(text: String, ide: Boolean)
    private external fun nativeDecompile(script: ByteArray, scriptStyle: Boolean, directive: String): String?
    private external fun nativeEmbeddedSource(script: ByteArray): String?
    private external fun nativeCompile(source: String, addExtraInfo: Boolean, flags: Int): ByteArray?
    private external fun nativeAllowUnverified(enabled: Boolean)
    private external fun nativeStrictStrings(enabled: Boolean)
    private external fun nativeDecompileFile(file: ByteArray, scriptStyle: Boolean, cleoDirective: String): String?
    private external fun nativeCompileFile(source: String, addExtraInfo: Boolean, flags: Int): ByteArray?
    private external fun nativeLastKind(): String
    private external fun nativeLastError(): String
}
