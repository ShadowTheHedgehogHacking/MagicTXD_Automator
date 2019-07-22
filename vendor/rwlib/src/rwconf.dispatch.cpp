// We want to register configuration blocks into the interface and into each thread.
#include "StdInc.h"

#include "rwconf.hxx"

#include "rwthreading.hxx"

using namespace NativeExecutive;

namespace rw
{

struct rwConfigDispatchEnv
{ 
    struct perThreadConfigBlock : public threadPluginInterface
    {
        bool OnPluginConstruct( CExecThread *theThread, threadPluginOffset pluginOffset, threadPluginDescriptor pluginId ) override
        {
            void *objMem = theThread->ResolvePluginMemory( pluginOffset );

            if ( !objMem )
                return false;

            cfg_block_constructor constr( this->engineInterface );

            rwConfigBlock *cfgBlock = cfgEnv->configFactory.ConstructPlacementEx( objMem, constr );

            return ( cfgBlock != nullptr );
        }

        void OnPluginDestruct( CExecThread *theThread, threadPluginOffset pluginOffset, threadPluginDescriptor pluginId ) override
        {
            rwConfigBlock *cfgBlock = (rwConfigBlock*)theThread->ResolvePluginMemory( pluginOffset );
            
            if ( !cfgBlock )
                return;

            cfgEnv->configFactory.DestroyPlacement( cfgBlock );
        }

        bool OnPluginAssign( CExecThread *dstThread, const CExecThread *srcThread, threadPluginOffset pluginOffset, threadPluginDescriptor pluginId ) override
        {
            rwConfigBlock *dstBlock = (rwConfigBlock*)dstThread->ResolvePluginMemory( pluginOffset );
            const rwConfigBlock *srcBlock = (const rwConfigBlock*)srcThread->ResolvePluginMemory( pluginOffset );

            return cfgEnv->configFactory.Assign( dstBlock, srcBlock );
        }

        EngineInterface *engineInterface;
        rwConfigEnv *cfgEnv;
    };
    perThreadConfigBlock _perThreadPluginInterface;

    threadPluginOffset _perThreadPluginOffset;

    inline void Initialize( EngineInterface *engineInterface )
    {
        rwConfigEnv *cfgEnv = rwConfigEnvRegister.GetPluginStruct( engineInterface );

        this->globalCfg = nullptr;

        if ( cfgEnv )
        {
            cfg_block_constructor constr( engineInterface );

            RwDynMemAllocator memAlloc( engineInterface );

            this->globalCfg = cfgEnv->configFactory.ConstructTemplate( memAlloc, constr );
        }

        // We want per-thread configuration states, too!
        _perThreadPluginInterface.engineInterface = engineInterface;
        _perThreadPluginInterface.cfgEnv = cfgEnv;

        if ( cfgEnv )
        {
            CExecutiveManager *manager = GetNativeExecutive( engineInterface );

            if ( manager )
            {
                size_t cfgBlockSize = cfgEnv->configFactory.GetClassSize();

                _perThreadPluginOffset =
                    manager->RegisterThreadPlugin( cfgBlockSize, &_perThreadPluginInterface );
            }
        }
    }

    inline void Shutdown( EngineInterface *engineInterface )
    {
        rwConfigEnv *cfgEnv = rwConfigEnvRegister.GetPluginStruct( engineInterface );

        // Unregister the per thread environment plugin.
        if ( CExecThread::IsPluginOffsetValid( _perThreadPluginOffset ) )
        {
            if ( cfgEnv )
            {
                CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

                if ( nativeMan )
                {
                    nativeMan->UnregisterThreadPlugin( _perThreadPluginOffset );
                }
            }
        }

        // Destroy the global configuration.
        if ( cfgEnv )
        {
            if ( this->globalCfg )
            {
                RwDynMemAllocator memAlloc( engineInterface );

                cfgEnv->configFactory.Destroy( memAlloc, this->globalCfg );
            }
        }
    }

    inline rwConfigBlock* GetThreadConfig( CExecThread *theThread ) const
    {
        return (rwConfigBlock*)theThread->ResolvePluginMemory( this->_perThreadPluginOffset );
    }

    inline const rwConfigBlock* GetConstThreadConfig( const CExecThread *theThread ) const
    {
        return (const rwConfigBlock*)theThread->ResolvePluginMemory( this->_perThreadPluginOffset );
    }

    rwConfigBlock *globalCfg;
};

static PluginDependantStructRegister <rwConfigDispatchEnv, RwInterfaceFactory_t> rwConfigDispatchEnvRegister;

rwConfigBlock& GetEnvironmentConfigBlock( EngineInterface *engineInterface )
{
    rwConfigDispatchEnv *cfgEnv = rwConfigDispatchEnvRegister.GetPluginStruct( engineInterface );

    if ( !cfgEnv )
    {
        throw RwException( "failed to get configuration block environment" );
    }
    
    // Decide whether to return the per-thread state.
    CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

    if ( nativeMan )
    {
        CExecThread *curThread = nativeMan->GetCurrentThread();

        if ( curThread )
        {
            rwConfigBlock *cfgBlock = cfgEnv->GetThreadConfig( curThread );

            if ( cfgBlock && cfgBlock->enableThreadedConfig )
            {
                return *cfgBlock;
            }
        }
    }

    return *cfgEnv->globalCfg;
}

const rwConfigBlock& GetConstEnvironmentConfigBlock( const EngineInterface *engineInterface )
{
    const rwConfigDispatchEnv *cfgEnv = rwConfigDispatchEnvRegister.GetConstPluginStruct( engineInterface );

    if ( !cfgEnv )
    {
        throw RwException( "failed to get configuration block environment" );
    }

    // Decide whether to return the per-thread state.
    CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

    if ( nativeMan )
    {
        CExecThread *curThread = nativeMan->GetCurrentThread();

        if ( curThread )
        {
            const rwConfigBlock *cfgBlock = cfgEnv->GetConstThreadConfig( curThread );

            if ( cfgBlock && cfgBlock->enableThreadedConfig )
            {
                return *cfgBlock;
            }
        }
    }

    return *cfgEnv->globalCfg;
}

// Public API for setting states.
void AssignThreadedRuntimeConfig( Interface *intf )
{
    EngineInterface *engineInterface = (EngineInterface*)intf;

    rwConfigEnv *cfgEnv = rwConfigEnvRegister.GetPluginStruct( engineInterface );

    if ( !cfgEnv )
        return;

    rwConfigDispatchEnv *cfgDispatch = rwConfigDispatchEnvRegister.GetPluginStruct( engineInterface );

    if ( !cfgDispatch )
        return;

    // We want to create a private copy of the global configuration and enable the per-thread state block.
    CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

    if ( !nativeMan )
        return;

    CExecThread *curThread = nativeMan->GetCurrentThread();
    
    if ( !curThread )
        return;
    
    rwConfigBlock *threadedCfg = cfgDispatch->GetThreadConfig( curThread );

    if ( !threadedCfg )
        return;

    if ( threadedCfg->enableThreadedConfig == false )
    {
        // First get us a private copy of the global configuration.
        bool couldSet = cfgEnv->configFactory.Assign( threadedCfg, cfgDispatch->globalCfg );

        if ( !couldSet )
        {
            throw RwException( "failed to assign threaded configuration from global configuration" );
        }

        // Enable our config.
        threadedCfg->enableThreadedConfig = true;
    }

    // Success!
}

void ReleaseThreadedRuntimeConfig( Interface *intf )
{
    EngineInterface *engineInterface = (EngineInterface*)intf;

    rwConfigDispatchEnv *cfgDispatch = rwConfigDispatchEnvRegister.GetPluginStruct( engineInterface );

    if ( !cfgDispatch )
        return;

    // We simply want to disable our copy of the threaded configuration.
    CExecutiveManager *nativeMan = GetNativeExecutive( engineInterface );

    if ( !nativeMan )
        return;

    CExecThread *curThread = nativeMan->GetCurrentThread();
    
    if ( !curThread )
        return;
    
    rwConfigBlock *threadedCfg = cfgDispatch->GetThreadConfig( curThread );

    if ( !threadedCfg )
        return;

    // Simply disable us.
    threadedCfg->enableThreadedConfig = false;

    // Success!
}

void registerConfigurationBlockDispatching( void )
{
    rwConfigDispatchEnvRegister.RegisterPlugin( engineFactory );
}

};