#ifndef _RENDERWARE_TEXDICT_OBJECT_UTILS_
#define _RENDERWARE_TEXDICT_OBJECT_UTILS_

#include "txdread.common.hxx"

namespace rw
{

// We need a TXD consistency lock.
struct _fetchTXDTypeStructoid
{
    AINLINE static RwTypeSystem::typeInfoBase* resolveType( EngineInterface *engineInterface )
    {
        texDictionaryStreamPlugin *txdEnv = texDictionaryStreamStore.GetPluginStruct( engineInterface );

        if ( txdEnv )
        {
            return txdEnv->txdTypeInfo;
        }

        return nullptr;
    }
};

typedef rwobjLockTypeRegister <_fetchTXDTypeStructoid> txdConsistencyLockEnv;

static PluginDependantStructRegister <txdConsistencyLockEnv, RwInterfaceFactory_t> txdConsistencyLockRegister;


inline rwlock* GetTXDLock( const rw::TexDictionary *txdHandle )
{
    EngineInterface *engineInterface = (EngineInterface*)txdHandle->engineInterface;

    txdConsistencyLockEnv *txdEnv = txdConsistencyLockRegister.GetPluginStruct( engineInterface );

    if ( txdEnv )
    {
        return txdEnv->GetLock( engineInterface, txdHandle );
    }

    return nullptr;
}

};

#endif //_RENDERWARE_TEXDICT_OBJECT_UTILS_