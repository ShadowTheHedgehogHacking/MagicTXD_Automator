// RenderWare warning dispatching and reporting.
// Turns out warnings are a complicated topic that deserves its own source module.
#include "StdInc.h"

#include "rwinterface.hxx"

#include "rwthreading.hxx"

using namespace NativeExecutive;

namespace rw
{

struct warningHandlerThreadEnv
{
    // The purpose of the warning handler stack is to fetch warning output requests and to reroute them
    // so that they make more sense.
    rwStaticVector <WarningHandler*> warningHandlerStack;
};

struct warningHandlerPlugin
{
    inline void Initialize( EngineInterface *engineInterface )
    {
        // Register the per-thread warning handler environment.
        CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

        if ( nativeMan )
        {
            pluginRegister.RegisterPlugin( nativeMan );
        }
    }

    inline void Shutdown( EngineInterface *engineInterface )
    {
        // Unregister the thread env, if registered.
        CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

        if ( nativeMan )
        {
            pluginRegister.UnregisterPlugin();
        }
    }

    inline warningHandlerThreadEnv* GetWarningHandlers( CExecThread *theThread ) const
    {
        return pluginRegister.GetPluginStruct( theThread );
    }

    NativeExecutive::execThreadStructPluginRegister <warningHandlerThreadEnv> pluginRegister;
};

static PluginDependantStructRegister <warningHandlerPlugin, RwInterfaceFactory_t> warningHandlerPluginRegister;

void Interface::PushWarning( rwStaticString <char>&& message )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    scoped_rwlock_writer <rwlock> lock( GetReadWriteLock( engineInterface ) );

    const rwConfigBlock& cfgBlock = GetConstEnvironmentConfigBlock( engineInterface );

    if ( cfgBlock.GetWarningLevel() > 0 )
    {
        // If we have a warning handler, we redirect the message to it instead.
        // The warning handler is supposed to be an internal class that only the library has access to.
        WarningHandler *currentWarningHandler = nullptr;
        {
            CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

            if ( nativeMan )
            {
                CExecThread *curThread = nativeMan->GetCurrentThread();

                if ( curThread )
                {
                    warningHandlerPlugin *whandlerEnv = warningHandlerPluginRegister.GetPluginStruct( engineInterface );

                    if ( whandlerEnv )
                    {
                        warningHandlerThreadEnv *threadEnv = whandlerEnv->GetWarningHandlers( curThread );

                        if ( threadEnv )
                        {
                            if ( threadEnv->warningHandlerStack.GetCount() != 0 )
                            {
                                currentWarningHandler = threadEnv->warningHandlerStack.GetBack();
                            }
                        }
                    }
                }
            }
        }

        if ( currentWarningHandler )
        {
            // Give it the warning.
            currentWarningHandler->OnWarningMessage( std::move( message ) );
        }
        else
        {
            // Else we just post the warning to the runtime.
            if ( WarningManagerInterface *warningMan = cfgBlock.GetWarningManager() )
            {
                warningMan->OnWarning( std::move( message ) );
            }
        }
    }
}

void Interface::PushObjWarningVerb( const RwObject *theObj, const rwStaticString <char>& verbMsg )
{
    // TODO: actually make this smarter.

    EngineInterface *engineInterface = (EngineInterface*)this;

    // Print the appropriate warning depending on object type.
    // We can use the object type information for that.
    rwString <char> printMsg( eir::constr_with_alloc::DEFAULT, this );
    {
        const GenericRTTI *rtObj = engineInterface->typeSystem.GetTypeStructFromConstAbstractObject( theObj );

        if ( rtObj )
        {
            RwTypeSystem::typeInfoBase *objTypeInfo = RwTypeSystem::GetTypeInfoFromTypeStruct( rtObj );

            printMsg += objTypeInfo->name;
        }
        else
        {
            printMsg += "unknown-obj";
        }
    }

    printMsg += ' ';

    // Print some sort of name if available.
    bool hasName = false;

    if ( const TextureBase *texHandle = ToConstTexture( engineInterface, theObj ) )
    {
        const rwString <char>& texName = texHandle->GetName();

        if ( texName.IsEmpty() == false )
        {
            printMsg += "'" + texName + "'";

            hasName = true;
        }
    }

    if ( hasName )
    {
        printMsg += ' ';
    }

    // Now comes the verbual message.
    printMsg += verbMsg;

    // Give the message to the warning system.
    PushWarning( std::move( printMsg ) );
}

void GlobalPushWarningHandler( EngineInterface *engineInterface, WarningHandler *theHandler )
{
    warningHandlerPlugin *whandlerEnv = warningHandlerPluginRegister.GetPluginStruct( engineInterface );

    if ( whandlerEnv )
    {
        CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

        if ( nativeMan )
        {
            CExecThread *curThread = nativeMan->GetCurrentThread();

            if ( curThread )
            {
                warningHandlerThreadEnv *threadEnv = whandlerEnv->GetWarningHandlers( curThread );

                if ( threadEnv )
                {
                    threadEnv->warningHandlerStack.AddToBack( theHandler );
                }
            }
        }
    }
}

void GlobalPopWarningHandler( EngineInterface *engineInterface )
{
    warningHandlerPlugin *whandlerEnv = warningHandlerPluginRegister.GetPluginStruct( engineInterface );

    if ( whandlerEnv )
    {
        CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

        if ( nativeMan )
        {
            CExecThread *curThread = nativeMan->GetCurrentThread();

            if ( curThread )
            {
                warningHandlerThreadEnv *threadEnv = whandlerEnv->GetWarningHandlers( curThread );

                if ( threadEnv )
                {
                    assert( threadEnv->warningHandlerStack.GetCount() != 0 );

                    threadEnv->warningHandlerStack.RemoveFromBack();
                }
            }
        }
    }
}

void registerWarningHandlerEnvironment( void )
{
    warningHandlerPluginRegister.RegisterPlugin( engineFactory );
}

};