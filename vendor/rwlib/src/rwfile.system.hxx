// Basic RenderWare data repository system so subsystems can carry shaders and things with them.

namespace rw
{

#ifndef _RENDERWARE_FILE_SYSTEM_INTERNAL_
#define _RENDERWARE_FILE_SYSTEM_INTERNAL_

namespace fs
{

// RW-wide data access interface.
Stream* OpenDataStream( Interface *engineInterface, const wchar_t *filePath, eStreamMode mode );

}

#endif //_RENDERWARE_FILE_SYSTEM_INTERNAL_

}