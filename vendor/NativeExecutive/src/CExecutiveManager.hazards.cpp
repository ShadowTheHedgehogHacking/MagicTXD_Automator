/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.hazards.cpp
*  PURPOSE:     Thread hazard management internals, to prevent deadlocks
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include "StdInc.h"

#include "CExecutiveManager.hazards.hxx"

BEGIN_NATIVE_EXECUTIVE

optional_struct_space <executiveHazardManagerEnvRegister_t> executiveHazardManagerEnvRegister;

// Hazard API implementation.
void PushHazard( CExecutiveManager *manager, hazardPreventionInterface *intf )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)manager;

    executiveHazardManagerEnv *hazardEnv = executiveHazardManagerEnvRegister.get().GetPluginStruct( nativeMan );

    if ( hazardEnv )
    {
        stackObjectHazardRegistry *reg = hazardEnv->GetCurrentHazardRegistry( nativeMan );

        if ( reg )
        {
            reg->PushHazard( intf );
        }
    }
}

void PopHazard( CExecutiveManager *manager )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)manager;

    executiveHazardManagerEnv *hazardEnv = executiveHazardManagerEnvRegister.get().GetPluginStruct( nativeMan );

    if ( hazardEnv )
    {
        stackObjectHazardRegistry *reg = hazardEnv->GetCurrentHazardRegistry( nativeMan );

        if ( reg )
        {
            reg->PopHazard();
        }
    }
}

void registerStackHazardManagement( void )
{
    executiveHazardManagerEnvRegister.Construct( executiveManagerFactory );
}

void unregisterStackHazardManagement( void )
{
    executiveHazardManagerEnvRegister.Destroy();
}

END_NATIVE_EXECUTIVE