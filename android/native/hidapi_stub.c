/*
 * Flat-baseline stub for SDL's HID (USB/Bluetooth controller) backend.
 *
 * SDLActivity always constructs HIDDeviceManager, whose constructor calls
 * SDL.loadLibrary("hidapi") and -- when the library is absent -- shows a
 * blocking "SDL HIDAPI Error" dialog that ends the app. CI does not (yet)
 * build the real SDL hidapi backend, so this stub provides the eight JNI
 * entry points HIDDeviceManager declares (see
 * thirdparty/.../org/libsdl/app/HIDDeviceManager.java) as no-ops. With
 * zero devices attached the manager then initializes harmlessly.
 *
 * Quest controllers arrive via OpenXR, not HID, so nothing is lost on the
 * flat baseline. Replace with the real backend (SDL-src
 * src/hidapi/android) when USB/BT controllers matter.
 *
 * Built in CI (.github/workflows/build-android-arm64.yml) with the NDK
 * clang into libhidapi.so; never compiled on host.
 */
#include <jni.h>

JNIEXPORT void JNICALL
Java_org_libsdl_app_HIDDeviceManager_HIDDeviceRegisterCallback(JNIEnv *env, jobject thiz)
{
	(void)env;
	(void)thiz;
}

JNIEXPORT void JNICALL
Java_org_libsdl_app_HIDDeviceManager_HIDDeviceReleaseCallback(JNIEnv *env, jobject thiz)
{
	(void)env;
	(void)thiz;
}

JNIEXPORT void JNICALL
Java_org_libsdl_app_HIDDeviceManager_HIDDeviceConnected(JNIEnv *env, jobject thiz,
	jint deviceID, jstring identifier, jint vendorId, jint productId,
	jstring serial_number, jint release_number, jstring manufacturer_string,
	jstring product_string, jint interface_number, jint interface_class,
	jint interface_subclass, jint interface_protocol)
{
	(void)env;
	(void)thiz;
	(void)deviceID;
	(void)identifier;
	(void)vendorId;
	(void)productId;
	(void)serial_number;
	(void)release_number;
	(void)manufacturer_string;
	(void)product_string;
	(void)interface_number;
	(void)interface_class;
	(void)interface_subclass;
	(void)interface_protocol;
}

JNIEXPORT void JNICALL
Java_org_libsdl_app_HIDDeviceManager_HIDDeviceOpenPending(JNIEnv *env, jobject thiz, jint deviceID)
{
	(void)env;
	(void)thiz;
	(void)deviceID;
}

JNIEXPORT void JNICALL
Java_org_libsdl_app_HIDDeviceManager_HIDDeviceOpenResult(JNIEnv *env, jobject thiz,
	jint deviceID, jboolean opened)
{
	(void)env;
	(void)thiz;
	(void)deviceID;
	(void)opened;
}

JNIEXPORT void JNICALL
Java_org_libsdl_app_HIDDeviceManager_HIDDeviceDisconnected(JNIEnv *env, jobject thiz, jint deviceID)
{
	(void)env;
	(void)thiz;
	(void)deviceID;
}

JNIEXPORT void JNICALL
Java_org_libsdl_app_HIDDeviceManager_HIDDeviceInputReport(JNIEnv *env, jobject thiz,
	jint deviceID, jbyteArray report)
{
	(void)env;
	(void)thiz;
	(void)deviceID;
	(void)report;
}

JNIEXPORT void JNICALL
Java_org_libsdl_app_HIDDeviceManager_HIDDeviceFeatureReport(JNIEnv *env, jobject thiz,
	jint deviceID, jbyteArray report)
{
	(void)env;
	(void)thiz;
	(void)deviceID;
	(void)report;
}
