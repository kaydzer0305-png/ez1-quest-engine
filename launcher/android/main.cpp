/*
Copyright (C) 2022 nillerusr

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of 
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL_hints.h>
#include "tier0/dbg.h"
#include "tier0/threadtools.h"

char *LauncherArgv[512];
char java_args[4096];
int iLastArgs = 0;

extern void InitCrashHandler();
DLL_EXPORT int LauncherMain( int argc, char **argv ); // from launcher.cpp

DLL_EXPORT int Java_com_valvesoftware_ValveActivity2_setenv(JNIEnv *jenv, jclass *jclass, jstring env, jstring value, jint over)
{
	Msg( "Java_com_valvesoftware_ValveActivity2_setenv %s=%s\n", jenv->GetStringUTFChars(env, NULL), jenv->GetStringUTFChars(value, NULL) );
	return setenv( jenv->GetStringUTFChars(env, NULL), jenv->GetStringUTFChars(value, NULL), over );
}

DLL_EXPORT void Java_com_valvesoftware_ValveActivity2_nativeOnActivityResult()
{
//	Msg( "Java_com_valvesoftware_ValveActivity_nativeOnActivityResult\n" );
}

DLL_EXPORT void Java_com_valvesoftware_ValveActivity2_setArgs(JNIEnv *env, jclass *clazz, jstring str)
{
	strncpy( java_args, env->GetStringUTFChars(str, NULL), sizeof java_args );
}

void SetLauncherArgs()
{
#define A(a,b) LauncherArgv[iLastArgs++] = (char*)a; \
	LauncherArgv[iLastArgs++] = (char*)b
#define D(a) LauncherArgv[iLastArgs++] = (char*)a

	static char binPath[2048];
	const char *bindir = getenv("NATIVE_LIB_DIR");
	if ( !bindir || !bindir[0] )
		bindir = getenv("APP_DATA_PATH");
	snprintf(binPath, sizeof binPath, "%s/hl2_linux", bindir );
	D(binPath);

	D("-nouserclip");

	char *pch;

	pch = strtok (java_args," ");
	while (pch != NULL)
	{
		LauncherArgv[iLastArgs++] = pch;
		pch = strtok (NULL, " ");
	}

	D("-fullscreen");
	D("-nosteam");
	D("-insecure");

#undef A
#undef D
}

float GetTotalMemory()
{
	int64_t mem = 0;

	char meminfo[8196] = { 0 };
	FILE *f = fopen("/proc/meminfo", "r");
	if( !f )
		return 0.f;

	size_t size = fread(meminfo, 1, sizeof(meminfo), f);
	if( !size )
		return 0.f;

	char *s = strstr(meminfo, "MemTotal:");

	if( !s ) return 0.f;

	sscanf(s+9, "%lld", &mem);
	fclose(f);

	return mem/1024/1024.f;
}

void android_property_print(const char *name)
{
	char prop[1024];

	char strValue[64];
	memset (strValue, 0, 64);
	snprintf(prop, sizeof(prop), "getprop %s", name);
	FILE *fp = NULL;
	fp = popen(prop, "r");
	if (!fp) return;

	fgets(strValue, sizeof(strValue), fp);
	pclose(fp);
	fp = NULL;

	Msg("prop %s=%s", name, strValue);
}

// ---------------------------------------------------------------------------
// EZQuest Java bridge (slice D, part 1).
//
// Device state pushed from com.ezquest.engine.DeviceBridge, plus the
// frame-loop heartbeat watched by the Java AnrWatchdog. Naming follows the
// existing Java_com_valvesoftware_ValveActivity2_* functions above.
//
// Integration for the engine / OpenXR tree:
//  * Call EZQuestSetEngineUp(1) once the frame loop is presenting; the Java
//    bridge then reports "engine reachable" and push calls are accepted.
//  * Call EZQuestWriteHeartbeat() once per presented frame; the Java
//    AnrWatchdog reads $APP_DATA_PATH/diagnostics/heartbeat.bin.
//  * Read g_ezBatteryPercent / g_ezBatteryCharging / g_ezThermalStatus
//    (-1 = unknown) wherever the engine wants them (perf scaling, HUD).
// ---------------------------------------------------------------------------

static volatile int g_ezEngineUp = 0;
static volatile int g_ezBatteryPercent = -1;
static volatile int g_ezBatteryCharging = 0;
static volatile int g_ezThermalStatus = -1;
static volatile uint64_t g_ezHeartbeatCounter = 0;

DLL_EXPORT void EZQuestSetEngineUp( int up )
{
	g_ezEngineUp = up ? 1 : 0;
	Msg( "EZQuest bridge: engine up = %d\n", g_ezEngineUp );
}

DLL_EXPORT jboolean Java_com_ezquest_engine_DeviceBridge_nativeEngineUp( JNIEnv *env, jclass clazz )
{
	(void)env;
	(void)clazz;
	return g_ezEngineUp ? JNI_TRUE : JNI_FALSE;
}

DLL_EXPORT jboolean Java_com_ezquest_engine_DeviceBridge_nativePushBattery( JNIEnv *env, jclass clazz, jint percent, jboolean charging )
{
	(void)env;
	(void)clazz;
	g_ezBatteryPercent = (int)percent;
	g_ezBatteryCharging = charging ? 1 : 0;
	return g_ezEngineUp ? JNI_TRUE : JNI_FALSE;
}

DLL_EXPORT void Java_com_ezquest_engine_DeviceBridge_nativePushThermal( JNIEnv *env, jclass clazz, jint status )
{
	(void)env;
	(void)clazz;
	g_ezThermalStatus = (int)status;
}

DLL_EXPORT void EZQuestWriteHeartbeat()
{
	uint64_t counter = ++g_ezHeartbeatCounter;
	const char *base = getenv( "APP_DATA_PATH" );
	if ( !base || !base[0] )
		return;
	char path[2048];
	snprintf( path, sizeof path, "%s/diagnostics/heartbeat.bin", base );
	FILE *f = fopen( path, "wb" );
	if ( !f )
		return;
	unsigned char buf[8];
	int i;
	for ( i = 0; i < 8; i++ )
		buf[i] = (unsigned char)( counter >> ( i * 8 ) );
	fwrite( buf, 1, sizeof buf, f );
	fclose( f );
}

static void EZQuestLogEnv( const char *name )
{
	const char *value = getenv( name );
	Msg( "env %s=%s\n", name, ( value && value[0] ) ? value : "(unset)" );
}

static void EZQuestLogEngineEnv()
{
	EZQuestLogEnv( "APP_DATA_PATH" );
	EZQuestLogEnv( "NATIVE_LIB_DIR" );
	EZQuestLogEnv( "APP_LIB_PATH" );
	EZQuestLogEnv( "SOURCEVR_APK_PATH" );
	EZQuestLogEnv( "SOURCEVR_GAME" );
	EZQuestLogEnv( "VALVE_GAME_PATH" );
	EZQuestLogEnv( "SOURCEVR_WRITE_GAME_PATH" );
	EZQuestLogEnv( "SOURCEVR_SHARED_CONTENT_PATH" );
	EZQuestLogEnv( "SOURCEVR_RUNTIME_GRAPH" );
	EZQuestLogEnv( "SOURCEVR_USER_LAUNCH_ARGS_PATH" );
	EZQuestLogEnv( "EXTRAS_VPK_PATH" );
	EZQuestLogEnv( "VR_CONTENT_PATH" );
	EZQuestLogEnv( "LANG" );
}


DLL_EXPORT int LauncherMainAndroid( int argc, char **argv )
{
	InitCrashHandler();

	Msg("GetTotalMemory() = %.2f \n", GetTotalMemory());

	android_property_print("ro.build.version.sdk");
	android_property_print("ro.product.device");
	android_property_print("ro.product.manufacturer");
	android_property_print("ro.product.model");
	android_property_print("ro.product.name");

	EZQuestLogEngineEnv();

	SetLauncherArgs();

	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	DeclareCurrentThreadIsMainThread(); // Init thread propertly on Android

	return LauncherMain(iLastArgs, LauncherArgv);
}
