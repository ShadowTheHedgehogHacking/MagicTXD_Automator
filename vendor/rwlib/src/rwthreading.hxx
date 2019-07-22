// RenderWare Threading shared include.

#include <NativeExecutive/CExecutiveManager.h>

#include "pluginutil.hxx"

namespace rw
{

struct threadingEnvironment
{
    inline void Initialize( Interface *engineInterface )
    {
        this->nativeMan = NativeExecutive::CExecutiveManager::Create();

        // Must not be optional.
        assert( this->nativeMan != nullptr );
    }

    inline void Shutdown( Interface *engineInterface )
    {
        if ( NativeExecutive::CExecutiveManager *nativeMan = this->nativeMan )
        {
            NativeExecutive::CExecutiveManager::Delete( nativeMan );

            this->nativeMan = nullptr;
        }
    }

    NativeExecutive::CExecutiveManager *nativeMan;   // NativeExecutive library handle.
};

typedef PluginDependantStructRegister <threadingEnvironment, RwInterfaceFactory_t> threadingEnvRegister_t;

extern threadingEnvRegister_t threadingEnv;

// Quick function to return the native executive.
inline NativeExecutive::CExecutiveManager* GetNativeExecutive( const EngineInterface *engineInterface )
{
    const threadingEnvironment *threadEnv = threadingEnv.GetConstPluginStruct( engineInterface );

    if ( threadEnv )
    {
        return threadEnv->nativeMan;
    }

    return nullptr;
}

// Private API.
void ThreadingMarkAsTerminating( EngineInterface *engineInterface );
void PurgeActiveThreadingObjects( EngineInterface *engineInterface );

};