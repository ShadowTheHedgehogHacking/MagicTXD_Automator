// Include this file if you want framework specific features from RenderWare.
// It is not recommended to globally include this file.

#ifndef _RENDERWARE_FRAMEWORK_FEATURES_
#define _RENDERWARE_FRAMEWORK_FEATURES_

#ifdef _WIN32
#include <windows.h>
#endif

namespace rw
{

// Framework entry points.
#ifdef _WIN32
BOOL WINAPI frameworkEntryPoint_win32( HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow );
#endif

};

#endif //_RENDERWARE_FRAMEWORK_FEATURES_