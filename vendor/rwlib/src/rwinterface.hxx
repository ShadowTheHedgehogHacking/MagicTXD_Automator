// Interface management routines.
// Required to be included in every file which defines parts of the rw::Interface struct.
#ifndef _RENDERWARE_INTERFACE_NATIVE_INCLUDE_
#define _RENDERWARE_INTERFACE_NATIVE_INCLUDE_

#include "pluginutil.hxx"

#include "rwconf.hxx"

namespace rw
{

// Lock for multi-threaded Interface access.
// Has to be registered after the threading provider.
struct interfaceReadWriteLockProvider
{
    inline void Initialize( Interface *engineInterface )
    {
        this->theLock = CreateReadWriteLock( engineInterface );
    }

    inline void Shutdown( Interface *engineInterface )
    {
        if ( rwlock *theLock = this->theLock )
        {
            CloseReadWriteLock( engineInterface, theLock );
        }
    }

    mutable rwlock *theLock;
};

typedef PluginDependantStructRegister <interfaceReadWriteLockProvider, RwInterfaceFactory_t> rwLockProvider_t;

extern rwLockProvider_t rwlockProvider;

inline rwlock* GetReadWriteLock( const EngineInterface *engineInterface )
{
    const interfaceReadWriteLockProvider *lockEnv = rwlockProvider.GetConstPluginStruct( engineInterface );

    if ( lockEnv )
    {
        return lockEnv->theLock;
    }

    return nullptr;
}

};

#endif //_RENDERWARE_INTERFACE_NATIVE_INCLUDE_