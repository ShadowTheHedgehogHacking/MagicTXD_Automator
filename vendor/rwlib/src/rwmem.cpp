#include "StdInc.h"

#include "rwthreading.hxx"

namespace rw
{

// If you want override the memory allocation then you should just override the memory callbacks
// inside of the NativeExecutive manager.

// General memory allocation routines.
// These should be used by the entire library.
void* Interface::MemAllocate( size_t memSize, size_t alignment )
{
    EngineInterface *natEngine = (EngineInterface*)this;

    threadingEnvironment *threadEnv = threadingEnv.GetPluginStruct( natEngine );

    assert( threadEnv != nullptr );

    return threadEnv->nativeMan->MemAlloc( memSize, sizeof(void*) );
}

bool Interface::MemResize( void *ptr, size_t memSize )
{
    EngineInterface *natEngine = (EngineInterface*)this;

    threadingEnvironment *threadEnv = threadingEnv.GetPluginStruct( natEngine );

    assert( threadEnv != nullptr );

    return threadEnv->nativeMan->MemResize( ptr, memSize );
}

void Interface::MemFree( void *ptr )
{
    EngineInterface *natEngine = (EngineInterface*)this;

    threadingEnvironment *threadEnv = threadingEnv.GetPluginStruct( natEngine );

    assert( threadEnv != nullptr );

    threadEnv->nativeMan->MemFree( ptr );
}

void* Interface::PixelAllocate( size_t memSize, size_t alignment )
{
    EngineInterface *natEngine = (EngineInterface*)this;

    threadingEnvironment *threadEnv = threadingEnv.GetPluginStruct( natEngine );

    assert( threadEnv != nullptr );

    return threadEnv->nativeMan->MemAlloc( memSize, sizeof(std::uint32_t) );
}

bool Interface::PixelResize( void *ptr, size_t memSize )
{
    EngineInterface *natEngine = (EngineInterface*)this;

    threadingEnvironment *threadEnv = threadingEnv.GetPluginStruct( natEngine );

    assert( threadEnv != nullptr );

    return threadEnv->nativeMan->MemResize( ptr, memSize );
}

void Interface::PixelFree( void *ptr )
{
    EngineInterface *natEngine = (EngineInterface*)this;

    threadingEnvironment *threadEnv = threadingEnv.GetPluginStruct( natEngine );

    assert( threadEnv != nullptr );

    threadEnv->nativeMan->MemFree( ptr );
}

// Implement the static API.
IMPL_HEAP_REDIR_METH_ALLOCATE_RETURN RwStaticMemAllocator::Allocate IMPL_HEAP_REDIR_METH_ALLOCATE_ARGS
{
    return NativeExecutive::NatExecGlobalStaticAlloc::Allocate( refMem, memSize, alignment );
}
IMPL_HEAP_REDIR_METH_RESIZE_RETURN RwStaticMemAllocator::Resize IMPL_HEAP_REDIR_METH_RESIZE_ARGS
{
    return NativeExecutive::NatExecGlobalStaticAlloc::Resize( refMem, objMem, reqNewSize );
}
IMPL_HEAP_REDIR_METH_FREE_RETURN RwStaticMemAllocator::Free IMPL_HEAP_REDIR_METH_FREE_ARGS
{
    NativeExecutive::NatExecGlobalStaticAlloc::Free( refMem, memPtr );
}

};
