#include "StdInc.h"

#ifndef _COMPILE_FOR_LEGACY

#ifdef _WIN32

#include "rwdriver.d3d12.hxx"

namespace rw
{

void d3d12DriverInterface::RasterInstance( Interface *engineInterface, void *driverObjMem, void *objMem, Raster *sysRaster )
{
    if ( sysRaster->hasNativeDataOfType( "Direct3D9" ) == false )
    {
        throw RwException( "unsupported raster type for Direct3D 12 native raster instancing" );
    }

    // TODO.
}

void d3d12DriverInterface::RasterUninstance( Interface *engineInterface, void *driverObjMem, void *objMem )
{
    // TODO.
}

}

#endif //_WIN32

#endif //_COMPILE_FOR_LEGACY
