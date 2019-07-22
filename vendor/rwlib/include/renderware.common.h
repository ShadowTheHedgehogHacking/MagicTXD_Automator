// RenderWare common structures used across this library.

#ifndef _RENDERWARE_COMMON_STRUCTURES_HEADER_
#define _RENDERWARE_COMMON_STRUCTURES_HEADER_

#include <sdk/String.h>
#include <sdk/Vector.h>
#include <sdk/Map.h>
#include <sdk/Set.h>

#include <sdk/MetaHelpers.h>

namespace rw
{

// Main memory allocator for everything:
// Use it as allocatorType for Eir SDK types.
struct RwDynMemAllocator
{
    AINLINE RwDynMemAllocator( Interface *engineInterface )
    {
        this->engineInterface = engineInterface;
    }

    AINLINE RwDynMemAllocator( RwDynMemAllocator&& ) = default;
    AINLINE RwDynMemAllocator( const RwDynMemAllocator& ) = default;    // IMPORTANT: has to be possible.

    AINLINE RwDynMemAllocator& operator = ( RwDynMemAllocator&& ) = default;
    AINLINE RwDynMemAllocator& operator = ( const RwDynMemAllocator& ) = default;

    // We implement them later in renderware.h
    AINLINE IMPL_HEAP_REDIR_METH_ALLOCATE_RETURN Allocate IMPL_HEAP_REDIR_METH_ALLOCATE_ARGS;
    AINLINE IMPL_HEAP_REDIR_METH_RESIZE_RETURN Resize IMPL_HEAP_REDIR_METH_RESIZE_ARGS;
    AINLINE IMPL_HEAP_REDIR_METH_FREE_RETURN Free IMPL_HEAP_REDIR_METH_FREE_ARGS;

    struct is_object {};

private:
    Interface *engineInterface;
};

// Static allocator that is implemented inside RenderWare for usage in static contexts.
// Should be available so that usage of strings, vectors and such can be done without
// initialized RenderWare interface.
// Implemented in "rwmem.cpp".
DEFINE_HEAP_ALLOC( RwStaticMemAllocator );

// The most used types provided using EngineInterface allocator linkage.
template <typename charType>
using rwString = eir::String <charType, RwDynMemAllocator>;

template <typename structType>
using rwVector = eir::Vector <structType, RwDynMemAllocator>;

template <typename keyType, typename valueType, typename comparatorType = eir::MapDefaultComparator>
using rwMap = eir::Map <keyType, valueType, RwDynMemAllocator, comparatorType>;

template <typename valueType, typename comparatorType = eir::SetDefaultComparator>
using rwSet = eir::Set <valueType, comparatorType, RwDynMemAllocator>;

// Used types in static contexts.
template <typename charType>
using rwStaticString = eir::String <charType, RwStaticMemAllocator>;

template <typename structType>
using rwStaticVector = eir::Vector <structType, RwStaticMemAllocator>;

template <typename keyType, typename valueType, typename comparatorType = eir::MapDefaultComparator>
using rwStaticMap = eir::Map <keyType, valueType, RwStaticMemAllocator, comparatorType>;

template <typename valueType, typename comparatorType = eir::SetDefaultComparator>
using rwStaticSet = eir::Set <valueType, RwStaticMemAllocator, comparatorType>;

} // namespace rw

#endif //_RENDERWARE_COMMON_STRUCTURES_HEADER_