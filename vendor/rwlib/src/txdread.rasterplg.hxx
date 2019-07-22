#ifndef _RENDERWARE_RASTER_INTERNALS_
#define _RENDERWARE_RASTER_INTERNALS_

#include "pluginutil.hxx"

namespace rw
{

// Internal raster plugins for consistency management.
struct _getRasterPluginFactStructoid
{
    AINLINE static rwMainRasterEnv_t::rasterFactory_t* getFactory( EngineInterface *engineInterface )
    {
        rwMainRasterEnv_t *rasterEnv = rwMainRasterEnv_t::pluginRegister.GetPluginStruct( engineInterface );

        if ( rasterEnv )
        {
            return &rasterEnv->rasterFactory;
        }

        return nullptr;
    }
};

typedef factLockProviderEnv <rwMainRasterEnv_t::rasterFactory_t, _getRasterPluginFactStructoid> rasterConsistencyEnv;

typedef PluginDependantStructRegister <rasterConsistencyEnv, RwInterfaceFactory_t> rasterConsistencyRegister_t;

extern rasterConsistencyRegister_t rasterConsistencyRegister;

inline rwlock* GetRasterLock( const rw::Raster *ras )
{
    rasterConsistencyEnv *consisEnv = rasterConsistencyRegister.GetPluginStruct( (EngineInterface*)ras->engineInterface );

    if ( consisEnv )
    {
        return consisEnv->GetLock( ras );
    }

    return nullptr;
}

};

#endif //_RENDERWARE_RASTER_INTERNALS_