/*
Copyright (C) 2026 kaydzer0305-png
*/

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "ezquest_vr_tracking.h"

#include <string.h>
#include <pthread.h>

namespace {
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
EzVrTrackingSnapshot g_snap;
}

void EZQuestVrPublishEyes( const EzVrEyeFrame *eyes, int eyeCount, const XrView *views )
{
        if ( !eyes || eyeCount < 1 )
                return;

        EzVrTrackingSnapshot local;
        memset( &local, 0, sizeof local );
        local.valid = 1;
        local.eyeCount = eyeCount > 2 ? 2 : eyeCount;
        local.width = eyes[0].width;
        local.height = eyes[0].height;
        for ( int i = 0; i < local.eyeCount; i++ )
        {
                if ( views )
                        local.views[i] = views[i];
                memcpy( local.view[i], eyes[i].view, sizeof( float ) * 16 );
                memcpy( local.viewInv[i], eyes[i].viewInv, sizeof( float ) * 16 );
                memcpy( local.proj[i], eyes[i].proj, sizeof( float ) * 16 );
        }

        if ( local.eyeCount == 2 && views )
        {
                local.midPos[0] = 0.5f * ( views[0].pose.position.x + views[1].pose.position.x );
                local.midPos[1] = 0.5f * ( views[0].pose.position.y + views[1].pose.position.y );
                local.midPos[2] = 0.5f * ( views[0].pose.position.z + views[1].pose.position.z );
                local.midQuat[0] = views[0].pose.orientation.x;
                local.midQuat[1] = views[0].pose.orientation.y;
                local.midQuat[2] = views[0].pose.orientation.z;
                local.midQuat[3] = views[0].pose.orientation.w;
        }
        else if ( views )
        {
                local.midPos[0] = views[0].pose.position.x;
                local.midPos[1] = views[0].pose.position.y;
                local.midPos[2] = views[0].pose.position.z;
                local.midQuat[0] = views[0].pose.orientation.x;
                local.midQuat[1] = views[0].pose.orientation.y;
                local.midQuat[2] = views[0].pose.orientation.z;
                local.midQuat[3] = views[0].pose.orientation.w;
        }

        pthread_mutex_lock( &g_lock );
        g_snap = local;
        pthread_mutex_unlock( &g_lock );
}

int EZQuestVrCopyTracking( EzVrTrackingSnapshot *out )
{
        if ( !out )
                return 0;
        pthread_mutex_lock( &g_lock );
        *out = g_snap;
        pthread_mutex_unlock( &g_lock );
        return out->valid;
}

int EZQuestVrTrackingValid( void )
{
        pthread_mutex_lock( &g_lock );
        const int v = g_snap.valid;
        pthread_mutex_unlock( &g_lock );
        return v;
}

#endif
