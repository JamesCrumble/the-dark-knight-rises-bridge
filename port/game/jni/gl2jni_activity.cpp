/*
 * com/gameloft/glf/GL2JNIActivity and com/gameloft/glf/GL2JNILib - the Java
 * half of this game - plus the small classes under the game's own package.
 *
 * The names are the two runs of .rodata after each classpath (aa4424 and
 * aa4824 in libKRAS.so); the descriptors are the ones classes.dex declares for
 * them. On Android the methods are split between the two classes (the s*
 * ones live on the activity, the rest on GL2JNILib); which class the engine
 * uses for each lookup was not traced, so both classes carry the whole table.
 *
 * Almost everything answers with the quietest defensible constant. The ones
 * that do real work:
 *
 *   setupPaths            hands the engine its three directories (tdkr_set_paths)
 *   GetWindowWidth/Height the surface this port drives
 *   sGet/PutStoredInt     the game's small preferences, kept in a text file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <map>
#include <string>

#include "platform.h"
#include "jni.h"
#include "jni_internals.h"
#include "app_exit.h"
#include "io_paths.h"
#include "trace.h"

#include "tdkr.h"
#include "tdkr_classes.h"

#define TDKR_PACKAGE "com/gameloft/android/AMAZ/GloftKRAS"

GloftGL2JNIActivity g_activity;

/* The surface, so GetWindowWidth/Height agree with the resize main.cpp performs. */
static int g_width  = 640;
static int g_height = 480;

void tdkr_set_view_size(int w, int h)
{
    g_width  = w;
    g_height = h;
}

static const char *cstr(jstring s)
{
    return s ? ((String *)s)->str : "";
}

/* ------------------------------------------------------------ the paths */

static void Lib_setupPaths(JNIEnv *env, jclass clazz)
{
    (void)clazz;
    tdkr_set_paths(env);
}

static jbyteArray Lib_getResource(JNIEnv *env, jclass clazz, jstring name)
{
    (void)clazz;
    /* On Android this reads a file out of the APK through
     * Class.getResourceAsStream. Empty and not NULL: the caller passes what it
     * gets straight to GetArrayLength. */
    trace("getResource('%s') - answered empty", cstr(name));
    return env->NewByteArray(0);
}

static jobject Lib_sGetOBBFilename(JNIEnv *env, jclass clazz, jboolean patch)
{
    (void)clazz; (void)patch;
    /* The "ExpansionPrefs" preference; empty on a build with no expansion files. */
    return (jobject)env->NewStringUTF("");
}

/* ------------------------------------------------------------- the view */

static void Lib_createView(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    trace("createView - the loader already owns the window");
}

static void Lib_setViewSettings(JNIEnv *env, jclass clazz,
                                jint pixel, jint depth, jint stencil,
                                jint csaa, jint extra)
{
    (void)env; (void)clazz;
    trace("setViewSettings(pixel=%d depth=%d stencil=%d csaa=%d extra=%d)",
          pixel, depth, stencil, csaa, extra);
}

static jboolean Lib_setCurrentContext(JNIEnv *env, jclass clazz, jint index)
{
    (void)env; (void)clazz;
    /* One context; anything else is refused rather than silently accepted. */
    return index == 0 ? JNI_TRUE : JNI_FALSE;
}

static jint Lib_GetWindowWidth(JNIEnv *e, jclass c)  { (void)e; (void)c; return g_width;  }
static jint Lib_GetWindowHeight(JNIEnv *e, jclass c) { (void)e; (void)c; return g_height; }

/* ----------------------------------------------------------- the device */

static jobject Lib_GetManufacturer(JNIEnv *env, jclass c)   { (void)c; return (jobject)env->NewStringUTF("samsung"); }
static jobject Lib_GetDeviceName(JNIEnv *env, jclass c)     { (void)c; return (jobject)env->NewStringUTF("GT-I9300"); }
static jobject Lib_GetDeviceFirmware(JNIEnv *env, jclass c) { (void)c; return (jobject)env->NewStringUTF("4.1.2"); }

static jobject Lib_sGetPhoneLanguage(JNIEnv *env, jclass clazz)
{
    (void)clazz;
    /* <PREFIX>_LANG lets the wrapper pass the phone's language; "en" otherwise. */
    const char *lang = getenv("TDKR_LANG");
    return (jobject)env->NewStringUTF(lang && *lang ? lang : "en");
}

static jobject Lib_sGetUDID(JNIEnv *env, jclass c, jboolean b) { (void)c; (void)b; return (jobject)env->NewStringUTF("tdkr0000deadbeef"); }
static jobject Lib_sGetMAC(JNIEnv *env, jclass c, jboolean b)  { (void)c; (void)b; return (jobject)env->NewStringUTF("DE:AD:00:BE:EF:00"); }

static jint   Lib_JGetNumberOfCores(JNIEnv *e, jclass c)   { (void)e; (void)c; return 4; }
static jfloat Lib_JGetMaxCPUSpeed(JNIEnv *e, jclass c)     { (void)e; (void)c; return 1400.0f; }
static jfloat Lib_JGetCurrentCPUSpeed(JNIEnv *e, jclass c) { (void)e; (void)c; return 1400.0f; }
static jfloat Lib_JGetMaxAvailableRam(JNIEnv *e, jclass c) { (void)e; (void)c; return 1024.0f; }
static jfloat Lib_JGetFreeRam(JNIEnv *e, jclass c)         { (void)e; (void)c; return 512.0f; }
static jint   Lib_JGetFreeDiskSpace(JNIEnv *e, jclass c)   { (void)e; (void)c; return 4096; }

/* No network: the servers are gone. */
static jboolean Lib_isWifiOn(JNIEnv *e, jclass c)      { (void)e; (void)c; return JNI_FALSE; }
static jboolean Lib_IsWifiEnabled(JNIEnv *e, jclass c) { (void)e; (void)c; return JNI_FALSE; }

static jlong Lib_sGetMilliseconds(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (jlong)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------ stored settings */

/*
 * GL2JNIActivity keeps these in SharedPreferences. Here: one "key value" line
 * each in <writable>/stored_ints.txt, rewritten on every change (there are a
 * handful of keys and they change rarely).
 */
static std::map<std::string, int> g_ints;
static bool g_ints_loaded;

static std::string ints_path(void)
{
    return std::string(io_writable_dir()) + "/stored_ints.txt";
}

static void ints_load(void)
{
    if (g_ints_loaded)
        return;
    g_ints_loaded = true;
    FILE *f = fopen(ints_path().c_str(), "r");
    if (!f)
        return;
    char key[256];
    int value;
    while (fscanf(f, "%255s %d", key, &value) == 2)
        g_ints[key] = value;
    fclose(f);
}

static jint Lib_sGetStoredInt(JNIEnv *env, jclass clazz, jstring key, jint fallback)
{
    (void)env; (void)clazz;
    ints_load();
    auto it = g_ints.find(cstr(key));
    return it == g_ints.end() ? fallback : it->second;
}

static void Lib_sPutStoredInt(JNIEnv *env, jclass clazz, jstring key, jint value)
{
    (void)env; (void)clazz;
    ints_load();
    const char *k = cstr(key);
    if (!*k || strpbrk(k, " \t\r\n"))
        return;
    g_ints[k] = value;
    FILE *f = fopen(ints_path().c_str(), "w");
    if (!f)
        return;
    for (const auto &kv : g_ints)
        fprintf(f, "%s %d\n", kv.first.c_str(), kv.second);
    fclose(f);
}

/* ------------------------------------------------------- the keyboard */

static jbyteArray Lib_sGetKeyboardText(JNIEnv *env, jclass c) { (void)c; return env->NewByteArray(0); }
static jint Lib_sIsKeyboardVisible(JNIEnv *e, jclass c)       { (void)e; (void)c; return 0; }

static void Lib_sShowKeyboard(JNIEnv *env, jclass clazz, jint mode, jstring text, jint max_len)
{
    (void)env; (void)clazz; (void)text;
    static bool announced = false;
    if (!announced) {
        announced = true;
        trace("sShowKeyboard(mode=%d max=%d) - no soft keyboard in this port", mode, max_len);
    }
}

/* ------------------------------------------------------------- the rest */

static void Lib_sExitGame(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    /* Recorded and consumed by the frame loop; see portbase/android/app_exit.h. */
    android_app_request_exit("the game called GL2JNIActivity.sExitGame()");
}

static void Lib_sShowToast(JNIEnv *env, jclass clazz, jstring text)
{
    (void)env; (void)clazz;
    trace("toast: %s", cstr(text));
}

static void Lib_sShowAlert(JNIEnv *env, jclass clazz, jstring a, jstring b, jstring c)
{
    (void)env; (void)clazz;
    trace("alert: %s / %s / %s", cstr(a), cstr(b), cstr(c));
}

static jobject Lib_emptyString(JNIEnv *env, jclass c) { (void)c; return (jobject)env->NewStringUTF(""); }

static void Lib_sBrowserLaunch(JNIEnv *e, jclass c, jstring u)
{
    (void)e; (void)c;
    trace("sBrowserLaunch('%s') ignored", cstr(u));
}

static void Lib_voidNone(JNIEnv *e, jclass c)                                   { (void)e; (void)c; }
static void Lib_voidInt(JNIEnv *e, jclass c, jint i)                            { (void)e; (void)c; (void)i; }
static void Lib_voidBool(JNIEnv *e, jclass c, jboolean b)                       { (void)e; (void)c; (void)b; }
static void Lib_voidIntStr(JNIEnv *e, jclass c, jint i, jstring s)              { (void)e; (void)c; (void)i; (void)s; }
static void Lib_voidIntStrStr(JNIEnv *e, jclass c, jint i, jstring s, jstring t) { (void)e; (void)c; (void)i; (void)s; (void)t; }
static void Lib_enableAccelerometer(JNIEnv *e, jclass c, jboolean b, jfloat f)  { (void)e; (void)c; (void)b; (void)f; }

#define STR "Ljava/lang/String;"
#define M(fn, name, sig) ManagedMethod::RegisterStatic<&fn>(CLS::clazz, name, sig)
#define TDKR_GLF_METHODS                                                        \
    M(Lib_getResource,         "getResource",             "(" STR ")[B"),       \
    M(Lib_setupPaths,          "setupPaths",              "()V"),               \
    M(Lib_createView,          "createView",              "()V"),               \
    M(Lib_setViewSettings,     "setViewSettings",         "(IIIII)V"),          \
    M(Lib_setCurrentContext,   "setCurrentContext",       "(I)Z"),              \
    M(Lib_enableAccelerometer, "enableAccelerometer",     "(ZF)V"),             \
    M(Lib_GetWindowWidth,      "GetWindowWidth",          "()I"),               \
    M(Lib_GetWindowHeight,     "GetWindowHeight",         "()I"),               \
    M(Lib_JGetFreeDiskSpace,   "JGetFreeDiskSpace",       "()I"),               \
    M(Lib_voidIntStrStr,       "sGLLiveLaunch",           "(I" STR STR ")V"),   \
    M(Lib_voidInt,             "sGLLiveNotifyTrophy",     "(I)V"),              \
    M(Lib_voidNone,            "sGLLiveWelcome",          "()V"),               \
    M(Lib_voidIntStr,          "sIGPLaunch",              "(I" STR ")V"),       \
    M(Lib_voidInt,             "sLaunchWelcomeScreen",    "(I)V"),              \
    M(Lib_sBrowserLaunch,      "sBrowserLaunch",          "(" STR ")V"),        \
    M(Lib_sGetKeyboardText,    "sGetKeyboardText",        "()[B"),              \
    M(Lib_sShowKeyboard,       "sShowKeyboard",           "(I" STR "I)V"),      \
    M(Lib_sIsKeyboardVisible,  "sIsKeyboardVisible",      "()I"),               \
    M(Lib_GetManufacturer,     "GetManufacturer",         "()" STR),            \
    M(Lib_GetDeviceName,       "GetDeviceName",           "()" STR),            \
    M(Lib_GetDeviceFirmware,   "GetDeviceFirmware",       "()" STR),            \
    M(Lib_JGetNumberOfCores,   "JGetNumberOfCores",       "()I"),               \
    M(Lib_JGetMaxCPUSpeed,     "JGetMaxCPUSpeed",         "()F"),               \
    M(Lib_JGetCurrentCPUSpeed, "JGetCurrentCPUSpeed",     "()F"),               \
    M(Lib_JGetMaxAvailableRam, "JGetMaxAvailableRam",     "()F"),               \
    M(Lib_JGetFreeRam,         "JGetFreeRam",             "()F"),               \
    M(Lib_isWifiOn,            "isWifiOn",                "()Z"),               \
    M(Lib_voidNone,            "FullScreenToggleShowBar", "()V"),               \
    M(Lib_voidNone,            "FullScreenToggleHideBar", "()V"),               \
    M(Lib_sExitGame,           "sExitGame",               "()V"),               \
    M(Lib_voidBool,            "sKeepScreenOn",           "(Z)V"),              \
    M(Lib_voidNone,            "sSendToBackground",       "()V"),               \
    M(Lib_sGetMilliseconds,    "sGetMilliseconds",        "()J"),               \
    M(Lib_IsWifiEnabled,       "IsWifiEnabled",           "()Z"),               \
    M(Lib_voidBool,            "EnableOrientation",       "(Z)V"),              \
    M(Lib_voidInt,             "SetOrientation",          "(I)V"),              \
    M(Lib_sPutStoredInt,       "sPutStoredInt",           "(" STR "I)V"),       \
    M(Lib_emptyString,         "sGLLiveGetPassword",      "()" STR),            \
    M(Lib_emptyString,         "sGLLiveGetUsername",      "()" STR),            \
    M(Lib_sGetOBBFilename,     "sGetOBBFilename",         "(Z)" STR),           \
    M(Lib_sGetMAC,             "sGetMAC",                 "(Z)" STR),           \
    M(Lib_sGetUDID,            "sGetUDID",                "(Z)" STR),           \
    M(Lib_sGetPhoneLanguage,   "sGetPhoneLanguage",       "()" STR),            \
    M(Lib_sShowToast,          "sShowToast",              "(" STR ")V"),        \
    M(Lib_sGetStoredInt,       "sGetStoredInt",           "(" STR "I)I"),       \
    M(Lib_sShowAlert,          "sShowAlert",              "(" STR STR STR ")V"), \
    {NULL}

#define CLS GloftGL2JNIActivity
const ManagedMethod gl2jniActivityMethods[] = { TDKR_GLF_METHODS };
#undef CLS
#define CLS GloftGL2JNILib
const ManagedMethod gl2jniLibMethods[] = { TDKR_GLF_METHODS };
#undef CLS

Class GloftGL2JNIActivity::clazz = {
    .classpath       = "com/gameloft/glf/GL2JNIActivity",
    .classname       = "GL2JNIActivity",
    .managed_methods = gl2jniActivityMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftGL2JNIActivity),
};

Class GloftGL2JNILib::clazz = {
    .classpath       = "com/gameloft/glf/GL2JNILib",
    .classname       = "GL2JNILib",
    .managed_methods = gl2jniLibMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftGL2JNILib),
};

/* ------------------------------------------------------------- Device */

static jobject Device_getUserAgent(JNIEnv *env, jclass c) { (void)c; return (jobject)env->NewStringUTF("Mozilla/5.0 (Linux; Android 4.1.2)"); }
static jobject Device_getAndroidId(JNIEnv *env, jclass c) { (void)c; return (jobject)env->NewStringUTF("tdkr0000deadbeef"); }

#define CLS GloftDevice
const ManagedMethod gloftDeviceMethods[] = {
    M(Device_getUserAgent, "getUserAgent", "()" STR),
    M(Device_getAndroidId, "getAndroidId", "()" STR),
    {NULL},
};
#undef CLS

Class GloftDevice::clazz = {
    .classpath       = TDKR_PACKAGE "/GLUtils/Device",
    .classname       = "Device",
    .managed_methods = gloftDeviceMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftDevice),
};

/* ------------------------------------------------------------- SUtils */

static jobject SUtils_getPreferenceString(JNIEnv *env, jclass clazz, jstring key, jstring prefs)
{
    (void)clazz;
    const char *k = cstr(key);
    trace("SUtils.getPreferenceString('%s', '%s')", k, cstr(prefs));
    /* The installer stores the data directory it unpacked into under this key. */
    if (strcmp(k, "SDFolder") == 0)
        return (jobject)env->NewStringUTF(tdkr_resource_dir());
    return (jobject)env->NewStringUTF("");
}

static jobject SUtils_getPreferenceString3(JNIEnv *env, jclass clazz, jstring key, jstring fallback, jstring prefs)
{
    (void)prefs;
    if (strcmp(cstr(key), "SDFolder") == 0)
        return SUtils_getPreferenceString(env, clazz, key, prefs);
    return (jobject)env->NewStringUTF(cstr(fallback));
}

static jobject SUtils_getPackage(JNIEnv *env, jclass c)
{
    (void)c;
    return (jobject)env->NewStringUTF("com.gameloft.android.AMAZ.GloftKRAS");
}

static jobject SUtils_getSaveFolder(JNIEnv *env, jclass c)
{
    (void)c;
    /* Context.getFilesDir() on Android. */
    return (jobject)env->NewStringUTF(io_writable_dir());
}

static jobject SUtils_getSDFolder(JNIEnv *env, jclass c)
{
    (void)c;
    return (jobject)env->NewStringUTF(tdkr_resource_dir());
}

static jobject SUtils_getContext(JNIEnv *env, jclass c)
{
    (void)env; (void)c;
    static AndroidContext the_context;
    return (jobject)&the_context;
}

static void SUtils_shareInfo(JNIEnv *e, jclass c, jstring a, jstring b, jstring d)
{
    (void)e; (void)c; (void)a; (void)b; (void)d;
}

#define CLS GloftSUtils
const ManagedMethod gloftSUtilsMethods[] = {
    M(SUtils_getPreferenceString,  "getPreferenceString", "(" STR STR ")" STR),
    M(SUtils_getPreferenceString3, "getPreferenceString", "(" STR STR STR ")" STR),
    M(SUtils_getPackage,           "getPackage",          "()" STR),
    M(SUtils_getSaveFolder,        "getSaveFolder",       "()" STR),
    M(SUtils_getSDFolder,          "getSDFolder",         "()" STR),
    M(SUtils_getContext,           "getContext",          "()Landroid/content/Context;"),
    M(SUtils_shareInfo,            "shareInfo",           "(" STR STR STR ")V"),
    {NULL},
};
#undef CLS

Class GloftSUtils::clazz = {
    .classpath       = TDKR_PACKAGE "/GLUtils/SUtils",
    .classname       = "SUtils",
    .managed_methods = gloftSUtilsMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftSUtils),
};

/* ------------------------------------------------------ GameInstaller */

String GloftGameInstaller::mPreferencesName("GloftKRASPrefs");
String GloftGameInstaller::SaveFolder("");
String GloftGameInstaller::sd_folder("");
String GloftGameInstaller::DATA_PATH("");

const FieldId gloftGameInstallerFields[] = {
    REGISTER_STATIC_FIELD(GloftGameInstaller, mPreferencesName),
    REGISTER_STATIC_FIELD(GloftGameInstaller, SaveFolder),
    REGISTER_STATIC_FIELD(GloftGameInstaller, sd_folder),
    REGISTER_STATIC_FIELD(GloftGameInstaller, DATA_PATH),
    {NULL},
};

Class GloftGameInstaller::clazz = {
    .classpath       = TDKR_PACKAGE "/installer/GameInstaller",
    .classname       = "GameInstaller",
    .managed_methods = {NULL},
    .native_methods  = {NULL},
    .fields          = gloftGameInstallerFields,
    .instance_size   = sizeof(GloftGameInstaller),
};

/* ----------------------------------------------- SplashScreenActivity */

/* The "welcome screen": a web page fetched from Gameloft. Never ready, never active. */
static jint Splash_isWSReady(JNIEnv *e, jclass c, jstring s)          { (void)e; (void)c; (void)s; return 0; }
static jint Splash_isWSReady2(JNIEnv *e, jclass c, jstring s, jint i) { (void)e; (void)c; (void)s; (void)i; return 0; }
static jint Splash_isActive(JNIEnv *e, jclass c)                      { (void)e; (void)c; return 0; }
static void Splash_str(JNIEnv *e, jclass c, jstring s)                { (void)e; (void)c; (void)s; }
static void Splash_strInt(JNIEnv *e, jclass c, jstring s, jint i)     { (void)e; (void)c; (void)s; (void)i; }

#define CLS GloftSplashScreen
const ManagedMethod gloftSplashMethods[] = {
    M(Splash_isWSReady,  "isWSReady",     "(" STR ")I"),
    M(Splash_isWSReady2, "isWSReady",     "(" STR "I)I"),
    M(Lib_voidInt,       "SetWSLanguage", "(I)V"),
    M(Splash_str,        "downloadWS",    "(" STR ")V"),
    M(Splash_strInt,     "downloadWS",    "(" STR "I)V"),
    M(Splash_str,        "deleteWS",      "(" STR ")V"),
    M(Splash_str,        "showLocalWS",   "(" STR ")V"),
    M(Splash_strInt,     "showLocalWS",   "(" STR "I)V"),
    M(Splash_isActive,   "isActive",      "()I"),
    {NULL},
};
#undef CLS

Class GloftSplashScreen::clazz = {
    .classpath       = TDKR_PACKAGE "/SplashScreenActivity",
    .classname       = "SplashScreenActivity",
    .managed_methods = gloftSplashMethods,
    .native_methods  = {NULL},
    .fields          = {NULL},
    .instance_size   = sizeof(GloftSplashScreen),
};

/* ----------------------------------------------------------- registry */

static const int registered[] = {
    ClassRegistry::register_class(GloftGL2JNIActivity::clazz),
    ClassRegistry::register_class(GloftGL2JNILib::clazz),
    ClassRegistry::register_class(GloftDevice::clazz),
    ClassRegistry::register_class(GloftSUtils::clazz),
    ClassRegistry::register_class(GloftGameInstaller::clazz),
    ClassRegistry::register_class(GloftSplashScreen::clazz),
};
