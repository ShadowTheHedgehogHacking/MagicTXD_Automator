#include "StdInc.h"

#ifndef _COMPILE_FOR_LEGACY

#ifdef _WIN32

#include "rwdriver.d3d12.hxx"

namespace rw
{

void d3d12DriverInterface::MaterialInstance( Interface *engineInterface, void *driverObjMem, void *objMem, Material *sysMat )
{

}

void d3d12DriverInterface::MaterialUninstance( Interface *engineInterface, void *driverObjMem, void *objMem )
{

}

};

#endif //_WIN32

#endif //_COMPILE_FOR_LEGACY
