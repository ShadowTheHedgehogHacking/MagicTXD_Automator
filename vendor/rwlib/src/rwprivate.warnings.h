// RenderWare private global include file about the warning management.

#ifndef _RENDERWARE_PRIVATE_WARNINGSYS_
#define _RENDERWARE_PRIVATE_WARNINGSYS_

namespace rw
{

// Internal warning dispatcher class.
struct WarningHandler abstract
{
    virtual void OnWarningMessage( rwStaticString <char>&& theMessage ) = 0;
};

void GlobalPushWarningHandler( EngineInterface *engineInterface, WarningHandler *theHandler );
void GlobalPopWarningHandler( EngineInterface *engineInterface );

};

#endif //_RENDERWARE_PRIVATE_WARNINGSYS_