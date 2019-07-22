/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.hazards.hxx
*  PURPOSE:     Thread hazard management internals, to prevent deadlocks
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _STACK_HAZARD_MANAGEMENT_INTERNALS_
#define _STACK_HAZARD_MANAGEMENT_INTERNALS_

#include "CExecutiveManager.fiber.hxx"

#include <sdk/Vector.h>

BEGIN_NATIVE_EXECUTIVE

// Struct that is registered at hazardous objects, basically anything that hosts CPU time.
// This cannot be a dependant struct.
struct stackObjectHazardRegistry abstract
{
    inline stackObjectHazardRegistry( CExecutiveManagerNative *manager ) : hazardStack( nullptr, 0, manager )
    {
        return;
    }

    inline void Initialize( CExecutiveManager *manager )
    {
        this->rwlockHazards = manager->CreateReadWriteLock();
    }

    inline void Shutdown( CExecutiveManager *manager )
    {
        manager->CloseReadWriteLock( this->rwlockHazards );
    }

private:
    struct hazardStackEntry
    {
        hazardPreventionInterface *intf;
    };

    eir::Vector <hazardStackEntry, NatExecStandardObjectAllocator> hazardStack;

    // Lock that is used to safely manage the hazard stack.
    CReadWriteLock *rwlockHazards;

public:
    inline void PushHazard( hazardPreventionInterface *intf )
    {
        hazardStackEntry entry;
        entry.intf = intf;

        CReadWriteWriteContextSafe <CReadWriteLock> hazardCtx( this->rwlockHazards );

        this->hazardStack.AddToBack( entry );
    }

    inline void PopHazard( void )
    {
        CReadWriteWriteContextSafe <CReadWriteLock> hazardCtx( this->rwlockHazards );

        this->hazardStack.RemoveFromBack();
    }

    inline void PurgeHazards( CExecutiveManager *manager )
    {
        CReadWriteLock *rwlock = this->rwlockHazards;

        while ( true )
        {
            hazardStackEntry entry;

            bool gotEntry = false;
            {
                CReadWriteWriteContextSafe <CReadWriteLock> hazardCtx( rwlock );

                if ( hazardStack.GetCount() > 0 )
                {
                    entry = std::move( hazardStack.GetBack() );

                    gotEntry = true;
                }
            }

            if ( gotEntry )
            {
                // Process the hazard.
                entry.intf->TerminateHazard();
            }
            else
            {
                break;
            }
        }
    }
};

// Then we need an environment that takes care of all hazardous objects.
struct executiveHazardManagerEnv
{
private:
    struct stackObjectHazardRegistry_fiber : public stackObjectHazardRegistry
    {
        inline stackObjectHazardRegistry_fiber( CFiberImpl *fiber ) : stackObjectHazardRegistry( fiber->manager )
        {
            return;
        }

        inline void Initialize( CFiberImpl *fiber )
        {
            stackObjectHazardRegistry::Initialize( fiber->manager );
        }

        inline void Shutdown( CFiberImpl *fiber )
        {
            stackObjectHazardRegistry::Shutdown( fiber->manager );
        }
    };

    struct stackObjectHazardRegistry_thread : public stackObjectHazardRegistry
    {
        inline stackObjectHazardRegistry_thread( CExecThreadImpl *thread ) : stackObjectHazardRegistry( thread->manager )
        {
            return;
        }

        inline void Initialize( CExecThreadImpl *thread )
        {
            stackObjectHazardRegistry::Initialize( thread->manager );
        }

        inline void Shutdown( CExecThreadImpl *thread )
        {
            stackObjectHazardRegistry::Shutdown( thread->manager );
        }
    };

public:
    // We want to register the hazard object struct in threads and fibers.
    privateFiberEnvironment::fiberFactory_t::pluginOffset_t _fiberHazardOffset;
    privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t _threadHazardOffset;

    inline void Initialize( CExecutiveManagerNative *manager )
    {
        // Register the fiber plugin.
        privateFiberEnvironment::fiberFactory_t::pluginOffset_t fiberPluginOff = privateFiberEnvironment::fiberFactory_t::INVALID_PLUGIN_OFFSET;

        privateFiberEnvironment *fiberEnv = privateFiberEnvironmentRegister.get().GetPluginStruct( manager );

        if ( fiberEnv )
        {
            fiberPluginOff = 
                fiberEnv->fiberFact.RegisterDependantStructPlugin <stackObjectHazardRegistry_fiber> ( privateFiberEnvironment::fiberFactory_t::ANONYMOUS_PLUGIN_ID );
        }

        this->_fiberHazardOffset = fiberPluginOff;

        // Register the thread plugin.
        privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t threadPluginOff = privateThreadEnvironment::threadPluginContainer_t::INVALID_PLUGIN_OFFSET;

        privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( manager );

        if ( threadEnv )
        {
            threadPluginOff =
                threadEnv->threadPlugins.RegisterDependantStructPlugin <stackObjectHazardRegistry_thread> ( privateThreadEnvironment::threadPluginContainer_t::ANONYMOUS_PLUGIN_ID );
        }

        this->_threadHazardOffset = threadPluginOff;
    }

    inline void Shutdown( CExecutiveManagerNative *manager )
    {
        if ( privateThreadEnvironment::threadPluginContainer_t::IsOffsetValid( this->_threadHazardOffset ) )
        {
            privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( manager );

            if ( threadEnv )
            {
                threadEnv->threadPlugins.UnregisterPlugin( this->_threadHazardOffset );
            }
        }

        if ( privateFiberEnvironment::fiberFactory_t::IsOffsetValid( this->_fiberHazardOffset ) )
        {
            privateFiberEnvironment *fiberEnv = privateFiberEnvironmentRegister.get().GetPluginStruct( manager );

            if ( fiberEnv )
            {
                fiberEnv->fiberFact.UnregisterPlugin( this->_fiberHazardOffset );
            }
        }
    }

private:
    inline stackObjectHazardRegistry* GetFiberHazardRegistry( CFiberImpl *fiber )
    {
        stackObjectHazardRegistry *reg = nullptr;

        if ( privateFiberEnvironment::fiberFactory_t::IsOffsetValid( this->_fiberHazardOffset ) )
        {
            reg = privateFiberEnvironment::fiberFactory_t::RESOLVE_STRUCT <stackObjectHazardRegistry_fiber> ( fiber, this->_fiberHazardOffset );
        }

        return reg;
    }

    inline stackObjectHazardRegistry* GetThreadHazardRegistry( CExecThreadImpl *thread )
    {
        stackObjectHazardRegistry *reg = nullptr;

        if ( privateThreadEnvironment::threadPluginContainer_t::IsOffsetValid( this->_threadHazardOffset ) )
        {
            reg = privateThreadEnvironment::threadPluginContainer_t::RESOLVE_STRUCT <stackObjectHazardRegistry_thread> ( thread, this->_threadHazardOffset );
        }

        return reg;
    }

public:
    inline void PurgeThreadHazards( CExecThreadImpl *theThread )
    {
        CExecutiveManager *execManager = theThread->manager;

        // First the thread stack.
        {
            stackObjectHazardRegistry *reg = this->GetThreadHazardRegistry( theThread );

            if ( reg )
            {
                reg->PurgeHazards( execManager );
            }
        }

        // Now the fiber stack.
        {
            threadFiberStackIterator fiberIter( theThread );

            while ( fiberIter.IsEnd() == false )
            {
                CFiberImpl *curFiber = fiberIter.Resolve();

                if ( curFiber )
                {
                    stackObjectHazardRegistry *reg = this->GetFiberHazardRegistry( curFiber );

                    if ( reg )
                    {
                        reg->PurgeHazards( execManager );
                    }
                }

                fiberIter.Increment();
            }
        }
    }

    inline stackObjectHazardRegistry* GetThreadCurrentHazardRegistry( CExecThreadImpl *theThread )
    {
        // First we try any active fiber.
        CFiberImpl *currentFiber = (CFiberImpl*)theThread->GetCurrentFiber();

        if ( currentFiber )
        {
            return this->GetFiberHazardRegistry( currentFiber );
        }

        return this->GetThreadHazardRegistry( theThread );
    }

    inline stackObjectHazardRegistry* GetCurrentHazardRegistry( CExecutiveManagerNative *manager )
    {
        CExecThreadImpl *nativeThread = (CExecThreadImpl*)manager->GetCurrentThread();

        if ( nativeThread == nullptr )
        {
            return nullptr;
        }

        return this->GetThreadHazardRegistry( nativeThread );
    }
};

typedef PluginDependantStructRegister <executiveHazardManagerEnv, executiveManagerFactory_t> executiveHazardManagerEnvRegister_t;

extern optional_struct_space <executiveHazardManagerEnvRegister_t> executiveHazardManagerEnvRegister;

END_NATIVE_EXECUTIVE

#endif //_STACK_HAZARD_MANAGEMENT_INTERNALS_