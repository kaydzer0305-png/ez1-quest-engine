//========= Copyright Valve Corporation, All rights reserved. ============
#ifndef PARTICLES_EZ_H
#define PARTICLES_EZ_H
#ifdef _WIN32
#pragma once
#endif

#include "particles_simple.h"
#include "particle_litsmokeemitter.h"

void AddSimpleParticle( const SimpleParticle *pParticle, PMaterialHandle hMaterial, bool bInSkybox=false );
void AddEmberParticle( const SimpleParticle *pParticle, PMaterialHandle hMaterial, bool bInSkybox=false );
void AddFireSmokeParticle( const SimpleParticle *pParticle, PMaterialHandle hMaterial, bool bInSkybox=false );
void AddFireParticle( const SimpleParticle *pParticle, PMaterialHandle hMaterial, bool bInSkybox=false );
void DrawParticleSingletons( bool bInSkybox );

#endif // PARTICLES_EZ_H
