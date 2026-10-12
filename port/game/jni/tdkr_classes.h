#pragma once

#include "jni.h"
#include "jni_internals.h"

/*
 * The Java classes this game asks for.
 *
 * Classpath-shaped strings in the library (strings -a libKRAS.so):
 *
 *   com/gameloft/glf/GL2JNIActivity, com/gameloft/glf/GL2JNILib,
 *   android/os/Bundle, android/os/Process, android/media/AudioTrack
 *
 * plus the ones assembled at runtime from the package prefix
 * "com/gameloft/android/AMAZ/GloftKRAS" and a tail: /GLUtils/SUtils,
 * /GLUtils/Device, /installer/GameInstaller, /SplashScreenActivity.
 *
 * Grouped by the file that implements them:
 *
 *   gl2jni_activity.cpp  GL2JNIActivity, GL2JNILib, SUtils, Device,
 *                        GameInstaller, SplashScreenActivity
 *   audio_track.cpp      android/media/AudioTrack (the game's only audio output)
 *   android_app.cpp      Context, Bundle, Process and the device-id helpers
 *
 * Two rules run through all of them:
 *
 *   - Never return NULL to the engine. It stores what it gets and dereferences
 *     it later.
 *   - Method lookup is an exact strcmp on name AND descriptor. The descriptors
 *     here are the ones the game's own classes.dex declares.
 */

#define TDKR_CLASS(name)                       \
    class name : public Object {                \
    public:                                     \
        static Class clazz;                     \
        Class *_getClass() { return &clazz; }   \
    }

/* com/gameloft/glf/GL2JNIActivity and com/gameloft/glf/GL2JNILib - the two
 * classes the engine calls back into. Both carry the same method table. */
TDKR_CLASS(GloftGL2JNIActivity);
TDKR_CLASS(GloftGL2JNILib);

/* <package>/GLUtils/Device, /GLUtils/SUtils, /SplashScreenActivity */
TDKR_CLASS(GloftDevice);
TDKR_CLASS(GloftSUtils);
TDKR_CLASS(GloftSplashScreen);

TDKR_CLASS(AndroidBundle);
TDKR_CLASS(AndroidProcess);
TDKR_CLASS(AndroidSystemProperties);
TDKR_CLASS(AndroidSettingsSecure);
TDKR_CLASS(AndroidTelephonyManager);
TDKR_CLASS(AndroidContentResolver);
TDKR_CLASS(AndroidViewRoot);
TDKR_CLASS(JavaUuid);
TDKR_CLASS(JavaObject);

#undef TDKR_CLASS

/*
 * The classes the game reads a *field* off, rather than calling.
 *
 * These cannot use the macro above because a field has to exist as a real
 * member to have an address: REGISTER_STATIC_FIELD records &Class::member, and
 * the fake JVM hands the engine that address directly.
 */
class AndroidContext : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }
};

class AndroidBuild : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    /* Read by the device-id fallback chain (be9ca0/be9cb4). */
    static String SERIAL;
};

class AndroidBuildVersion : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    /* Named at be9c30/be9c4c. Declared, deliberately not registered - see the
     * comment on its Class in android_app.cpp. */
    static int SDK_INT;
};

/*
 * <package>/installer/GameInstaller. The engine reads the name of the
 * preferences file off it (mPreferencesName, a static String) and then asks
 * SUtils.getPreferenceString("SDFolder", <that name>) for the data directory.
 */
class GloftGameInstaller : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    static String mPreferencesName;
    static String SaveFolder;
    static String sd_folder;
    static String DATA_PATH;
};

/*
 * The singletons the engine is handed.
 *
 * On Android these are distinct objects with state; here they exist so that a
 * method looked up on what getContentResolver() returned resolves to
 * something. They are deliberately global rather than allocated per call: the
 * engine keeps the pointers it is given for the life of the process and
 * compares some of them, and a fresh object per call would break that
 * silently.
 */
extern GloftGL2JNIActivity     g_activity;
extern AndroidTelephonyManager g_telephony_manager;
extern AndroidContentResolver  g_content_resolver;
extern AndroidViewRoot         g_view_root;
