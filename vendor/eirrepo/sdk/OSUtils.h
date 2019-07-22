/*****************************************************************************
*
*  PROJECT:     Eir SDK
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        eirrepo/sdk/OSUtils.h
*  PURPOSE:     Implementation dependant routines for native features
*
*  Find the Eir SDK at: https://osdn.net/projects/eirrepo/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

// IMPORTANT: none of this code can use CRT functions or STL dynamic types!

#ifndef _COMMON_OS_UTILS_
#define _COMMON_OS_UTILS_

#include "MemoryRaw.h"
#include "MemoryUtils.h"

#include "OSUtils.vmem.h"
#include "OSUtils.arrvmem.h"
#include "OSUtils.vecvmem.h"

#include <atomic>

// Namespace of abstraction types to be used by any NativePageAllocator implementation.
namespace NativePageAllocation
{
    // Abstraction page handle information.
    struct pageHandleInfo
    {
        void *pAddress;
        size_t memSize;
    };

    // Abstraction memory region information.
    struct pageInfo
    {
        void *pAddress;
        size_t regionSize;
#if 0
        // Examples.
        bool isWritable;
        bool isReadable;
        bool isExecutable;
#endif
    };
};

// Native OS memory allocation manager that marks pages on RAM to be used by the program.
// Uses the platform-dependent native virtual memory functions.
// This implementation should be used if the API exposed by the OS is not enough for you (malloc, etc).
// Due to complicated memory saving and performance reasons we do not allow intersection of page handles (anymore).
// Version 3 RC2.
struct NativePageAllocator
{
    // WARNING: this class is NOT thread-safe!

    // There is still a lot to be done for this class!
    // For the basic purpose it always guarrantees functionality under optimal situations (enough memory, etc).
    // TODO:
    // * exception safety under usage of specialized types

    struct pageHandle;

    // Amount of cached constructs ("pages" sorta) to allocate inside cached containers.
    static constexpr size_t NUM_VECTOR_PAGES_CACHED = 1;

private:
    // Virtual memory description object.
    NativeVirtualMemoryAccessor vmemAccess;

    // Allocation systems that we need.
    struct pageAllocation;

    NativeVirtualMemoryArrayAllocator <pageHandle> _allocPageHandle;
    NativeVirtualMemoryArrayAllocator <pageAllocation> _allocPageArena;

    // It is good to keep some useful metrics.
    std::atomic <size_t> numAllocatedArenas;
    std::atomic <size_t> numAllocatedPageHandles;

public:
    inline NativePageAllocator( void )
        : _allocPageHandle( vmemAccess ), _allocPageArena( vmemAccess ), numAllocatedArenas( 0 ), numAllocatedPageHandles( 0 ),
          flowalloc_temp_alloc_cinfo( vmemAccess ),
          _cached_memReserveList( vmemAccess )
    {
        return;
    }

    inline ~NativePageAllocator( void )
    {
        // Delete all active page handles.
        while ( !LIST_EMPTY( sortedActiveHandles.root ) )
        {
            pageHandle *handle = LIST_GETITEM( pageHandle, sortedActiveHandles.root.next, managerNode );

            Free( handle );
        }

        // Now delete any active pages.
        while ( !LIST_EMPTY( activeMemoryRanges.root ) )
        {
            pageAllocation *allocation = LIST_GETITEM( pageAllocation, activeMemoryRanges.root.next, managerNode );

            DeletePageAllocation( allocation );
        }
    }

    // We are not copy-constructible because it makes no sense.
    // Instead we can be moved.
    inline NativePageAllocator( const NativePageAllocator& right ) = delete;
    inline NativePageAllocator( NativePageAllocator&& right ) noexcept
        : _allocPageHandle( std::move( right._allocPageHandle ) ),
          _allocPageArena( std::move( right._allocPageArena ) ),
          flowalloc_temp_alloc_cinfo( std::move( right.flowalloc_temp_alloc_cinfo ) ),
          _cached_memReserveList( std::move( right._cached_memReserveList ) )
    {
        // Take over all the allocations and stuff.
        this->sortedActiveHandles = std::move( right.sortedActiveHandles );
        this->activeMemoryRanges = std::move( right.activeMemoryRanges );
        this->numAllocatedArenas = right.numAllocatedArenas.load();
        this->numAllocatedPageHandles = right.numAllocatedPageHandles.load();
        this->sortedMemoryRanges = std::move( right.sortedMemoryRanges );

        // Must update the vmem accessor in the internal vectors we store.
        this->flowalloc_temp_alloc_cinfo.SetNativeVirtualMemoryAccessor( this->vmemAccess );
        this->_cached_memReserveList.SetNativeVirtualMemoryAccessor( this->vmemAccess );

        // Must update manager pointers in the allocations.
        LIST_FOREACH_BEGIN( pageAllocation, this->activeMemoryRanges.root, managerNode )

            item->manager = this;

        LIST_FOREACH_END

        // Move platform specific things.
        // TODO: moving this member away could leave the source struct invalid; will we actually use a copy here?
        this->vmemAccess = std::move( right.vmemAccess );

        // Clear the old struct somewhat.
        right.numAllocatedArenas = 0;
        right.numAllocatedPageHandles = 0;
    }

    inline NativePageAllocator& operator = ( const NativePageAllocator& right ) = delete;
    inline NativePageAllocator& operator = ( NativePageAllocator&& right ) noexcept
    {
        this->~NativePageAllocator();

        return *new (this) NativePageAllocator( std::move( right ) );
    }

    struct pageHandle;

private:
    typedef sliceOfData <size_t> memBlockSlice_t;

    struct pageAllocation;

public:
    // To associate page handles with page arenas, from version 3 on, we make use of memory address immutability guarantees.
    // This avoids having to use lists of memory handles for each arena, simplifying the memory layout A LOT.
    // It works using the following attributes:
    // * pageHandles do not intersect themselves.
    // * pageArenas do not intersect themselves.
    // * pageHandles have a valid global ordering by memory address
    // So we store the first pageHandle that is valid for each arena and list the associated handles from then on, with minimal
    // runtime overhead.
    struct pageHandle
    {
        friend struct NativePageAllocator;

        inline pageHandle( memBlockSlice_t spanSlice )
            : requestedMemory( std::move( spanSlice ) )
        {
            this->begResiding = nullptr;
        }

        inline ~pageHandle( void )
        {
            return;
        }

        inline void* GetTargetPointer( void ) const     { return (void*)requestedMemory.GetSliceStartPoint(); }
        inline size_t GetTargetSize( void ) const       { return (size_t)requestedMemory.GetSliceSize(); }

        inline memBlockSlice_t GetTargetSlice( void ) const     { return this->requestedMemory; }

    private:
        memBlockSlice_t requestedMemory;    // slice that represents memory that can be accessed by the application

        pageAllocation *begResiding;    // first memory arena that this handle is part of (must not be null)

        RwListEntry <pageHandle> managerNode;   // entry in the active page handle list, has to be sorted by address!
    };

private:
    // List of all active page handles in memory-order.
    // We need this feature to support listing for arenas without allocating additional memory.
    RwList <pageHandle> sortedActiveHandles;

    // An arena that spans multiple pages.
    struct pageAllocation
    {
        inline pageAllocation( NativePageAllocator *manager, void *arenaAddress, size_t numSlots )
        {
            size_t arenaSpanSize = (size_t)( numSlots * manager->vmemAccess.GetPlatformPageSize() );

            pageSpan = memBlockSlice_t( (size_t)arenaAddress, arenaSpanSize );

            this->manager = manager;

            this->arenaAddress = arenaAddress;
            this->allocSize = arenaSpanSize;

            this->refCount = 0;

            this->begResideHandle = nullptr;    // does not have to be not-null.

            this->slotCount = numSlots;
        }

        inline ~pageAllocation( void )
        {
            // Make sure nobody uses us anymore.
            assert( this->refCount == 0 );

            // Release the allocated arena.
            NativeVirtualMemoryAccessor::ReleaseVirtualMemory( this->arenaAddress, this->allocSize );
        }

        // Iterator across page handles in memory-order to support cancellation points at any situation.
        struct sortedPageResidentIterator
        {
            AINLINE sortedPageResidentIterator( NativePageAllocator *manager, pageAllocation *arenaHandle )
            {
                this->manager = manager;
                this->arenaHandle = arenaHandle;

                // If the arena handle has no page handles, then we just set our node to the end of list.
                pageHandle *begPageResident = arenaHandle->begResideHandle;

                if ( begPageResident == nullptr )
                {
                    this->node = &manager->sortedActiveHandles.root;
                }
                else
                {
                    this->node = &begPageResident->managerNode;
                }
            }

            AINLINE ~sortedPageResidentIterator( void )
            {
                // Nothing to do.
            }

            AINLINE bool IsEnd( void ) const
            {
                auto node = this->node;

                if ( node == &this->manager->sortedActiveHandles.root )
                {
                    return true;
                }

                // We are at the end if the current page handle floats after the given arena.
                pageHandle *curHandle = LIST_GETITEM( pageHandle, node, managerNode );
                pageAllocation *arenaHandle = this->arenaHandle;

                eir::eIntersectionResult intResult =
                    curHandle->requestedMemory.intersectWith( arenaHandle->pageSpan );

                assert( intResult != eir::INTERSECT_FLOATING_START );

                return ( intResult == eir::INTERSECT_FLOATING_END );
            }

            AINLINE void Increment( void )
            {
                this->node = this->node->next;
            }

            AINLINE pageHandle* Resolve( void ) const
            {
                return LIST_GETITEM( pageHandle, this->node, managerNode );
            }

        private:
            NativePageAllocator *manager;
            pageAllocation *arenaHandle;
            RwListEntry <pageHandle> *node;
        };

        // Get all page handles that intersect an arena in memory-order.
        template <typename callbackType, typename... Args>
        AINLINE void ForAllPageHandlesSorted( Args... theArgs )
        {
            sortedPageResidentIterator iter( this->manager, this );

            size_t sortedIndex = 0;

            while ( !iter.IsEnd() )
            {
                // Grab the handle.
                pageHandle *curHandle = iter.Resolve();

                // We have another entry in our thing.
                callbackType::ProcessEntry( curHandle, sortedIndex++, theArgs... );

                // Next one.
                iter.Increment();
            }
        }

        // DEBUG.
        inline void CheckForCollision( const RwList <pageHandle>& sortedHandles, const memBlockSlice_t& memoryRegion )
        {
#if defined(_DEBUG) && defined(_WIN32)
            // DEBUG: check for intersection, meow.
            struct checkCollHandler
            {
                static AINLINE void ProcessEntry( pageHandle *foreignHandle, size_t sortedIndex, const memBlockSlice_t& memoryRegion )
                {
                    eir::eIntersectionResult intRes = foreignHandle->requestedMemory.intersectWith( memoryRegion );

                    if ( eir::isFloatingIntersect( intRes ) == false )
                    {
                        // Oh no :(
                        __debugbreak();
                    }
                }
            };

            ForAllPageHandlesSorted <checkCollHandler> ( memoryRegion );
#endif // DEBUG CODE.
        }

        // Each page handle that resides on an arena has to reference it.
        // We used to have a list but we cannot afford the memory associated with it anymore.
        inline void RefPageHandle( void )
        {
            this->refCount++;
        }

        inline void DerefPageHandle( void )
        {
            this->refCount--;
        }

        inline bool IsBlockBeingUsed( void ) const
        {
            return ( refCount != 0 );
        }

        inline void RemovePossibleFirst( NativePageAllocator *manager, pageHandle *theHandle )
        {
            if ( theHandle != this->begResideHandle )
                return;

            sortedPageResidentIterator iter( manager, this );

            // Increment.
            iter.Increment();

            if ( iter.IsEnd() )
            {
                this->begResideHandle = nullptr;
            }
            else
            {
                this->begResideHandle = iter.Resolve();
            }
        }

        NativePageAllocator *manager;

        void *arenaAddress;         // address of the memory arena
        size_t allocSize;           // number in bytes for the allocation range

        memBlockSlice_t pageSpan;       // slice which spans the allocation range

        unsigned int refCount;          // number of handles using this page

        pageHandle *begResideHandle;    // first handle that intersects with this arena

        size_t slotCount;           // amount of slots we have space for.

        RwListEntry <pageAllocation> managerNode;   // node in the NativePageAllocator allocation list.
        RwListEntry <pageAllocation> sortedNode;
    };

    inline size_t GetAllocationArenaRange( size_t spanSize ) const
    {
        // Returns a rounded up value that determines region of RESERVE allocation.
        size_t allocGranularity = this->vmemAccess.GetPlatformAllocationGranularity();

        return ALIGN( spanSize, allocGranularity, allocGranularity );
    }

    inline size_t GetPageAllocationRange( size_t spanSize ) const
    {
        // Returns a rounded up value that determines the actual size of a page allocation.
        size_t pageSize = this->vmemAccess.GetPlatformPageSize();

        return ALIGN( spanSize, pageSize, pageSize );
    }

    RwList <pageAllocation> activeMemoryRanges;
    RwList <pageAllocation> sortedMemoryRanges;

    inline void SortedMemoryBlockInsert( pageAllocation *memBlock )
    {
        RwListEntry <pageAllocation> *insertAfter = &sortedMemoryRanges.root;

        size_t insertMemBlockAddress = (size_t)memBlock->arenaAddress;

        LIST_FOREACH_BEGIN( pageAllocation, sortedMemoryRanges.root, sortedNode )
            // Get the address of the list item as number.
            size_t memBlockAddress = (size_t)item->arenaAddress;

            if ( memBlockAddress > insertMemBlockAddress )
            {
                insertAfter = iter;
                break;
            }
        LIST_FOREACH_END

        LIST_APPEND( *insertAfter, memBlock->sortedNode );
    }

    template <typename callbackType, typename... Args>
    AINLINE static void ProcessInclinedMemoryChunk( pageAllocation *arenaHandle, const memBlockSlice_t& memoryRegion, Args... theArgs )
    {
        memBlockSlice_t sharedSlice;

        bool hasPosition = memoryRegion.getSharedRegion( arenaHandle->pageSpan, sharedSlice );

        if ( hasPosition )
        {
            // Tell the meow.
            callbackType::Process( sharedSlice, std::forward <Args> ( theArgs )... );
        }
    }

    template <typename callbackType, typename... Args>
    AINLINE void ForAllPageHandleArenasSorted( pageHandle *arenaResident, Args... theArgs )
    {
        pageAllocation *curArena = arenaResident->begResiding;

        // cannot be nullptr because page handles have to be placed on arenas.
        assert( curArena != nullptr );

        RwListEntry <pageAllocation> *node = &curArena->sortedNode;

        size_t sortedIndex = 0;

        while ( node != &this->sortedMemoryRanges.root )
        {
            // The first arena that is floating after the page handle is the end marker.
            eir::eIntersectionResult intResult = curArena->pageSpan.intersectWith( arenaResident->requestedMemory );

            assert( intResult != eir::INTERSECT_FLOATING_START );

            if ( intResult == eir::INTERSECT_FLOATING_END )
            {
                // End marker.
                break;
            }

            // Cache the next.
            RwListEntry <pageAllocation> *cachedNext = node->next;

            // Process current valid item.
            callbackType::ProcessEntry( curArena, std::forward <Args> ( theArgs )... );

            // Next.
            node = cachedNext;

            if ( node == &this->sortedMemoryRanges.root )
            {
                break;
            }

            sortedIndex++;

            curArena = LIST_GETITEM( pageAllocation, node, sortedNode );
        }
    }

    template <typename callbackType, typename... Args>
    AINLINE void SortedProcessMemoryChunks( pageHandle *arenaResident, const memBlockSlice_t& memoryRegion, Args... theArgs )
    {
        struct sortedMemoryProcessor
        {
            static AINLINE void ProcessEntry( pageAllocation *arenaHandle, const memBlockSlice_t& memoryRegion, Args... theArgs )
            {
                // Check what this allocation has to say.
                ProcessInclinedMemoryChunk <callbackType> ( arenaHandle, memoryRegion, theArgs... );
            }
        };

        ForAllPageHandleArenasSorted <sortedMemoryProcessor> ( arenaResident, memoryRegion, theArgs... );

        // Done.
    }

    struct arenaCommitOperator
    {
        static AINLINE void Process( const memBlockSlice_t& allocRegion )
        {
            void *mem_ptr = (void*)allocRegion.GetSliceStartPoint();
            size_t mem_size = allocRegion.GetSliceSize();

            bool success = NativeVirtualMemoryAccessor::CommitVirtualMemory( mem_ptr, mem_size );
            (void)success;

            assert( success == true );
        }
    };

    inline void CommitMemoryOfPageHandle( pageHandle *theHandle, const memBlockSlice_t& commitRegion )
    {
        SortedProcessMemoryChunks <arenaCommitOperator> ( theHandle, commitRegion );
    }

    struct arenaDecommitOperator
    {
        static AINLINE void Process( const memBlockSlice_t& allocRegion )
        {
            void *mem_ptr = (void*)allocRegion.GetSliceStartPoint();
            size_t mem_size = allocRegion.GetSliceSize();

            bool success = NativeVirtualMemoryAccessor::DecommitVirtualMemory( mem_ptr, mem_size );
            (void)success;

            assert( success == true );
        }
    };

    inline void DecommitMemoryOfPageHandle( pageHandle *theHandle, const memBlockSlice_t& decommitRegion )
    {
        SortedProcessMemoryChunks <arenaDecommitOperator> ( theHandle, decommitRegion );
    }

    inline pageAllocation* NewArenaAllocation( void *arenaAddress, size_t numSlots )
    {
        this->numAllocatedArenas++;

        return _allocPageArena.Allocate( this, arenaAddress, numSlots );
    }

    inline void FreeArenaAllocation( pageAllocation *arenaPtr )
    {
        this->numAllocatedArenas--;

        _allocPageArena.Deallocate( arenaPtr );
    }

    inline void DeletePageAllocation( pageAllocation *memRange )
    {
        LIST_REMOVE( memRange->sortedNode );
        LIST_REMOVE( memRange->managerNode );

        FreeArenaAllocation( memRange );
    }

    struct memReserveAllocInfo
    {
        pageAllocation *hostArena;
        bool hostArenaIsFirstSpot;
        bool isHostArenaNewlyAllocated;
    };

    // Use the virtual-memory-based vector class with caching here.
    typedef NativeVirtualMemoryVector <memReserveAllocInfo, NUM_VECTOR_PAGES_CACHED> memCachedReserveAllocList_t;

    DEF_LIST_ITER( arenaSortedIterator_t, pageAllocation, sortedNode );
    DEF_LIST_ITER( pageHandleSortedIterator_t, pageHandle, managerNode );

    // Finds the memory arena that hosts or is-prior-to a memory range.
    inline pageAllocation* FindHostingArena( const memBlockSlice_t& hostMem, bool& isHostingOut, bool& isBeforeItOut )
    {
        pageAllocation *curRegion = nullptr;

        arenaSortedIterator_t iter( this->sortedMemoryRanges );

        eir::eIntersectionResult intResult;

        while ( !iter.IsEnd() )
        {
            curRegion = iter.Resolve();

            intResult = hostMem.intersectWith( curRegion->pageSpan );

            if ( intResult == eir::INTERSECT_BORDER_START ||
                 intResult == eir::INTERSECT_INSIDE ||
                 intResult == eir::INTERSECT_EQUAL )
            {
                isHostingOut = true;
                isBeforeItOut = false;
                return curRegion;
            }
            if ( intResult == eir::INTERSECT_ENCLOSING )
            {
                // If the start points match, then we are hosting.
                // Otherwise we cannot host it.
                if ( hostMem.GetSliceStartPoint() == curRegion->pageSpan.GetSliceStartPoint() )
                {
                    isHostingOut = true;
                }
                else
                {
                    isHostingOut = false;
                }

                isBeforeItOut = false;

                return curRegion;
            }
            if ( intResult == eir::INTERSECT_FLOATING_START ||
                 intResult == eir::INTERSECT_BORDER_END )
            {
                isHostingOut = false;
                isBeforeItOut = true;
                return curRegion;
            }

            iter.Increment();
        }

        if ( curRegion && intResult == eir::INTERSECT_FLOATING_END )
        {
            isHostingOut = false;
            isBeforeItOut = false;
            return curRegion;
        }

        return nullptr;
    }

    inline pageAllocation* ReserveNewMemory( void *allocStartAddr, size_t allocSize )
    {
        assert( allocSize == GetAllocationArenaRange( allocSize ) );

        void *allocPtr = NativeVirtualMemoryAccessor::RequestVirtualMemory( (void*)allocStartAddr, allocSize );

        if ( allocPtr == nullptr )
            return nullptr;

        pageAllocation *newHostArena = NewArenaAllocation( allocPtr, allocSize / this->vmemAccess.GetPlatformPageSize() );

        if ( !newHostArena )
        {
            NativeVirtualMemoryAccessor::ReleaseVirtualMemory( allocPtr, allocSize );
            return nullptr;
        }

        return newHostArena;
    }

    // Temporary allocation data.
    struct temp_alloc_info_commit
    {
        pageAllocation *arenaToBeCommitted;
        RwListEntry <pageAllocation> *appendAfterNode;
    };

    // The real reason we put vectors into the class itself is so that we can efficiently use the caching feature.
    typedef NativeVirtualMemoryVector <temp_alloc_info_commit, NUM_VECTOR_PAGES_CACHED> flowalloc_temp_alloc_cinfo_array_t;

    flowalloc_temp_alloc_cinfo_array_t flowalloc_temp_alloc_cinfo;

    // This function is NOT RECURSIVE and NOT THREAD SAFE.
    inline bool FlowAllocateAfterRegion(
        memCachedReserveAllocList_t& areaToBeAllocatedAt_inOut, arenaSortedIterator_t& reserveArenaIter,
        const memBlockSlice_t& handleAllocRegion, const memBlockSlice_t& hostAllocRegion, pageAllocation *hostStartRegion )
    {
        struct temp_alloc_cinfo_context
        {
            AINLINE temp_alloc_cinfo_context( flowalloc_temp_alloc_cinfo_array_t& tmpArray ) : tmpArray( tmpArray )
            {
                return;
            }

            AINLINE ~temp_alloc_cinfo_context( void )
            {
                // Reset the array.
                // Screw this hyper retarded repulsion of the "finally" keyword :/
                tmpArray.Clear();
            }

        private:
            flowalloc_temp_alloc_cinfo_array_t& tmpArray;
        };

        // Remember to reset the flowalloc temp struct.
        temp_alloc_cinfo_context ctx( this->flowalloc_temp_alloc_cinfo );

        size_t reqAllocEndPoint = hostAllocRegion.GetSliceEndPoint();

        bool prepareSuccess = true;

        pageAllocation *prevArena = hostStartRegion;

        bool isInsideRegion = false;

        // Proceed to ensure there are no gaps of unallocated memory.
        pageAllocation *theEndingBit = nullptr;

        while ( reserveArenaIter.IsEnd() == false )
        {
            pageAllocation *nextArena = reserveArenaIter.Resolve();

            // Since each arena is allocated on real linear space, we can check the memory addresses.
            const memBlockSlice_t& primary_slice = prevArena->pageSpan;
            const memBlockSlice_t& secondary_slice = nextArena->pageSpan;

            // Fill any hole.
            size_t reqMemStartPos = ( primary_slice.GetSliceEndPoint() + 1 );
            size_t reqMemEndOffset = ( secondary_slice.GetSliceStartPoint() );

            // We should break if the next memory region is far-off our request.
            if ( reqMemEndOffset > reqAllocEndPoint )
            {
                theEndingBit = nextArena;
                break;
            }

            // We need to check the entire bridge part for collision, if we happen to cross an entire arena over.
            if ( isInsideRegion )
            {
                if ( prevArena->begResideHandle != nullptr )
                {
                    // There are allocations obstructing the arena we wanted to allocate at.
                    // So we cannot continue any further.
                    prepareSuccess = false;
                    break;
                }
            }

            bool isGap = ( reqMemStartPos != reqMemEndOffset );

            if ( isGap )
            {
                // We need to allocate a new arena here.
                size_t reqMemSize = ( reqMemEndOffset - reqMemStartPos );

                pageAllocation *newArena = ReserveNewMemory( (void*)reqMemStartPos, reqMemSize );

                if ( !newArena )
                {
                    // We could fail to allocate memory for meta-data, in which case we fail.
                    prepareSuccess = false;
                    break;
                }

                // Also need to allocate at the new arena.
                {
                    memReserveAllocInfo info;
                    info.hostArena = newArena;
                    info.hostArenaIsFirstSpot = true;
                    info.isHostArenaNewlyAllocated = true;

                    areaToBeAllocatedAt_inOut.AddItem( std::move( info ) );
                }

                // Remember this success and how to register it into the system.
                temp_alloc_info_commit info;
                info.arenaToBeCommitted = newArena;
                info.appendAfterNode = &prevArena->sortedNode;

                flowalloc_temp_alloc_cinfo.AddItem( std::move( info ) );
            }

            // We definitely have to allocate at this.
            {
                memReserveAllocInfo info;
                info.hostArena = nextArena;
                info.hostArenaIsFirstSpot = true;
                info.isHostArenaNewlyAllocated = false;

                areaToBeAllocatedAt_inOut.AddItem( std::move( info ) );
            }

            // Go ahead.
            reserveArenaIter.Increment();

            prevArena = nextArena;
        }

        bool allocSuccess = false;

        if ( prepareSuccess )
        {
            const size_t endAllocStartPoint = ( prevArena->pageSpan.GetSliceEndPoint() + 1 );
            const size_t reqAllocEndOffset = ( reqAllocEndPoint + 1 );

            if ( endAllocStartPoint >= reqAllocEndOffset )
            {
                allocSuccess = true;
            }
            else
            {
                // Continue with allocating the ending bit, if required.
                // First we need the relationship of the ending bit to us.
                // This is to determine if we need to allocate something on an "ending bit" that is the last thing that we could ever allocate on.
                pageAllocation *reqAllocEndBit = nullptr;

                if ( theEndingBit )
                {
                    eir::eIntersectionResult endIntResult = handleAllocRegion.intersectWith( theEndingBit->pageSpan );

                    if ( endIntResult == eir::INTERSECT_BORDER_END )
                    {
                        reqAllocEndBit = theEndingBit;
                    }
                    else if ( endIntResult == eir::INTERSECT_FLOATING_START )
                    {
                        // Nothing :)
                    }
                    else
                    {
                        // Must not happen.
                        assert( 0 );
                    }
                }

                bool hasValidEndingBitAlloc = true;

                // If we have an ending bit, we want to verify beforehand if the allocation will succeed on it.
                // This is just an optimization.
                if ( reqAllocEndBit )
                {
                    // Check for collision against already allocated things.
                    // Since we are the ending bit which is intruded from the start, we just have to check the first-in-line element.
                    bool isObstructed = false;

                    if ( prevArena->begResideHandle != nullptr )
                    {
                        pageHandle *firstAlloc = reqAllocEndBit->begResideHandle;

                        assert( firstAlloc != nullptr );

                        eir::eIntersectionResult intResult = handleAllocRegion.intersectWith( firstAlloc->requestedMemory );

                        isObstructed = ( eir::isFloatingIntersect( intResult ) == false );
                    }

                    if ( isObstructed )
                    {
                        hasValidEndingBitAlloc = false;
                    }
                }

                if ( hasValidEndingBitAlloc )
                {
                    // Here we actually have to allocate anything that might be between ending bit and previous alloc.
                    // This gap "might exist", alright?
                    bool hasValidGapAllocation = true;

                    size_t gapAllocEndOffset;

                    if ( reqAllocEndBit )
                    {
                        gapAllocEndOffset = reqAllocEndBit->pageSpan.GetSliceStartPoint();
                    }
                    else
                    {
                        gapAllocEndOffset = reqAllocEndOffset;
                    }

                    if ( endAllocStartPoint < gapAllocEndOffset )
                    {
                        hasValidGapAllocation = false;

                        size_t gapAllocReqSize = ( gapAllocEndOffset - endAllocStartPoint );

                        pageAllocation *arenaHandle = ReserveNewMemory( (void*)endAllocStartPoint, gapAllocReqSize );

                        if ( arenaHandle )
                        {
                            // Remember this region as allocate-at.
                            {
                                memReserveAllocInfo info;
                                info.hostArena = arenaHandle;
                                info.hostArenaIsFirstSpot = true;
                                info.isHostArenaNewlyAllocated = true;

                                areaToBeAllocatedAt_inOut.AddItem( std::move( info ) );
                            }

                            // Register this.
                            temp_alloc_info_commit info;
                            info.arenaToBeCommitted = arenaHandle;
                            info.appendAfterNode = &prevArena->sortedNode;

                            flowalloc_temp_alloc_cinfo.AddItem( std::move( info ) );

                            hasValidGapAllocation = true;
                        }
                    }

                    if ( hasValidGapAllocation )
                    {
                        // The only thing remaining is the allocation on the ending bit, which we have already verified to work.
                        // This operation of allocating data could also fail, but we dont have that kind of error checking currently.

                        if ( reqAllocEndBit )
                        {
                            memReserveAllocInfo allocInfo;
                            allocInfo.hostArena = reqAllocEndBit;
                            allocInfo.hostArenaIsFirstSpot = true;
                            allocInfo.isHostArenaNewlyAllocated = false;

                            areaToBeAllocatedAt_inOut.AddItem( std::move( allocInfo ) );
                        }

                        // Success!
                        allocSuccess = true;
                    }
                }
            }
        }

        if ( !allocSuccess )
        {
            // Clean up after ourselves.
            // Those arenas never accounted to anything anyway.
            flowalloc_temp_alloc_cinfo.ForAllEntries(
                [&]( const temp_alloc_info_commit& info )
            {
                pageAllocation *arena = info.arenaToBeCommitted;

                FreeArenaAllocation( arena );
            });

            // Have to reset the contents of the in-out array.
            areaToBeAllocatedAt_inOut.Clear();

            return false;
        }

        // Commit the change to the system.
        flowalloc_temp_alloc_cinfo.ForAllEntries(
            [&]( const temp_alloc_info_commit& info )
        {
            pageAllocation *arena = info.arenaToBeCommitted;

            LIST_INSERT( *info.appendAfterNode, arena->sortedNode );
            LIST_APPEND( this->activeMemoryRanges.root, arena->managerNode );
        });

        // Return the arena handles where the memory request should take place at
        // They are placed inside the in-out array.

        return true;
    }

    // This function is NOT RECURSIVE and NOT THREAD-SAFE.
    inline bool FlowAllocateRegion(
        const memBlockSlice_t& handleAllocRegion, const memBlockSlice_t& hostAllocRegion, pageAllocation *hostStartRegion, bool hostStartAllocIsFirst, bool isHostStartRegionNewlyAllocated,
        memCachedReserveAllocList_t& allocOut )
    {
        // Check for some logical things by parameters.
#ifdef _DEBUG
        {
            // Handle alloc region (page memory space) must be inside or equal to host alloc region (reserve memory space).
            eir::eIntersectionResult intResult = handleAllocRegion.intersectWith( hostAllocRegion );

            assert( intResult == eir::INTERSECT_INSIDE || intResult == eir::INTERSECT_EQUAL );
        }
#endif //_DEBUG

        // We found a valid allocation spot!
        // Thus we should allocate any non-reserved pages inbetween.
        // If even that succeeds, we are set.
        assert( allocOut.GetCount() == 0 );

        // We know that we start in a valid allocation region.
        arenaSortedIterator_t reserveArenaIter( this->sortedMemoryRanges, &hostStartRegion->sortedNode );

        // Check if the starting region is even relevant.
        eir::eIntersectionResult intResult = handleAllocRegion.intersectWith( hostStartRegion->pageSpan );

        assert( eir::isFloatingIntersect( intResult ) == false );
        {
            // Add the starting region as allocate-at.
            memReserveAllocInfo info;
            info.hostArena = hostStartRegion;
            info.hostArenaIsFirstSpot = hostStartAllocIsFirst;
            info.isHostArenaNewlyAllocated = isHostStartRegionNewlyAllocated;

            allocOut.AddItem( std::move( info ) );
        }

        reserveArenaIter.Increment();

        return FlowAllocateAfterRegion( allocOut, reserveArenaIter, handleAllocRegion, hostAllocRegion, hostStartRegion );
    }

    template <typename numberType>
    AINLINE numberType SCALE_DOWN( numberType value, numberType modval )
    {
        // This is faster than divide-and-multiply, plus it does exactly the same.
        numberType rem = ( value % modval );

        return ( value - rem );
    }

    inline bool FindSortedMemoryHandleInsertionSpot( pageAllocation *arenaHandle, const memBlockSlice_t& memRegion, bool& isFirstOut )
    {
        // In order to even allocate, the memory region must intersect with the arena's.
        // We assume that this is always the case.
#ifdef _DEBUG
        {
            eir::eIntersectionResult intResult = memRegion.intersectWith( arenaHandle->pageSpan );

            assert( eir::isFloatingIntersect( intResult ) == false );
        }
#endif //_DEBUG

        pageAllocation::sortedPageResidentIterator iter( this, arenaHandle );

        bool isFirst = true;

        while ( !iter.IsEnd() )
        {
            pageHandle *alloc = iter.Resolve();

            // Check what to make of this.
            eir::eIntersectionResult intResult = memRegion.intersectWith( alloc->requestedMemory );

            if ( intResult == eir::INTERSECT_FLOATING_START )
            {
                // Our requested memory does not conflict with anything and is prior to the current thing.
                isFirstOut = isFirst;
                return true;
            }
            if ( intResult == eir::INTERSECT_FLOATING_END )
            {
                // We can continue ahead.
            }
            else
            {
                // There was some sort of collision, which is bad.
                return false;
            }

            isFirst = false;

            // Next one.
            iter.Increment();
        }

        // We did not collide, so we are good.
        isFirstOut = isFirst;
        return true;
    }

    inline static bool IsAllocationObstructed( const memBlockSlice_t& handleAllocSlice, pageHandle *obstructAlloc )
    {
        // Check if we are obstructed by the (next) resident memory.
        // This does not guarrentee allocability on its own, but it gives us a good idea.

        eir::eIntersectionResult intResult = handleAllocSlice.intersectWith( obstructAlloc->requestedMemory );

        return ( eir::isFloatingIntersect( intResult ) == false );
    }

    // Try placement of memory allocation on a specific memory address.
    inline bool PlaceMemoryRequest( memBlockSlice_t handleMemSlice, memCachedReserveAllocList_t& allocOut )
    {
        // Search for the position of the starting address.
        size_t numMemAddr;
        size_t numMemSize;
        {
            // We have to convert this request into valid unmistakeable parameters.
            size_t allocGranularity = this->vmemAccess.GetPlatformAllocationGranularity();

            size_t realMemAddrEnd = ALIGN_SIZE( handleMemSlice.GetSliceEndPoint() + 1, allocGranularity );
            size_t realMemAddrStart = SCALE_DOWN( handleMemSlice.GetSliceStartPoint(), allocGranularity );

            numMemAddr = realMemAddrStart;
            numMemSize = ( realMemAddrEnd - realMemAddrStart );
        }

        const memBlockSlice_t searchMemoryRegion( numMemAddr, numMemSize );

        // See if we have a hosting arena.
        bool hostArenaIsHosting;
        bool hostArenaIsAfter;
        pageAllocation *hostArena = FindHostingArena( searchMemoryRegion, hostArenaIsHosting, hostArenaIsAfter );

        // If we dont, then we have to allocate one.
        bool isHostArenaAllocated = false;

        if ( !hostArena || !hostArenaIsHosting )
        {
            // Allocation has to happen until we are at the next arena.
            // If there is no next arena, we can allocate everything in one go.
            pageAllocation *firstArenaAfter = nullptr;

            if ( hostArena && hostArenaIsAfter )
            {
                firstArenaAfter = hostArena;
            }

            size_t allocStartAddr = numMemAddr;
            size_t allocEndOffset;

            if ( firstArenaAfter )
            {
                allocEndOffset = ( firstArenaAfter->pageSpan.GetSliceStartPoint() );
            }
            else
            {
                allocEndOffset = ( numMemAddr + numMemSize );
            }

            size_t allocSize = ( allocEndOffset - allocStartAddr );

            hostArena = ReserveNewMemory( (void*)allocStartAddr, allocSize );

            if ( !hostArena )
            {
                // The Operating System refused our request.
                return false;
            }

            // Register this arena.
            LIST_INSERT( this->activeMemoryRanges.root, hostArena->managerNode );

            if ( firstArenaAfter )
            {
                // Actually insert.
                LIST_APPEND( firstArenaAfter->sortedNode, hostArena->sortedNode );
            }
            else
            {
                LIST_APPEND( this->sortedMemoryRanges.root, hostArena->sortedNode );
            }

            isHostArenaAllocated = true;
        }

        // Find allocation index on the host arena.
        bool handleAllocIsFirst = false;

        bool canAllocateOnArena = FindSortedMemoryHandleInsertionSpot( hostArena, handleMemSlice, handleAllocIsFirst );

        if ( !canAllocateOnArena )
        {
            if ( isHostArenaAllocated )
            {
                DeletePageAllocation( hostArena );
            }

            return false;
        }

        // Continue allocation.
        bool wasAllocSuccess = FlowAllocateRegion( handleMemSlice, searchMemoryRegion, hostArena, handleAllocIsFirst, isHostArenaAllocated, allocOut );

        if ( !wasAllocSuccess )
        {
            if ( isHostArenaAllocated )
            {
                DeletePageAllocation( hostArena );
            }
        }

        return wasAllocSuccess;
    }

    // Find and allocate required memory, if possible.
    inline bool SearchForReservedMemory( size_t memSize, memBlockSlice_t& handleAllocSliceOut, memCachedReserveAllocList_t& allocOut )
    {
        // We have to scan all reserved and/or committed memory for space that we can use.
        // This is so that we can reuse as most memory as possible.
        // If this fails we go ahead and ask Windows itself for new memory arenas.

        memBlockSlice_t handleAllocSlice( (size_t)0, memSize );

        // TODO: check if this is still correct over here.
        //  We really need unit tests!
        //size_t alignedMemSize = GetAllocationArenaRange( memSize );

        arenaSortedIterator_t sortedIter( this->sortedMemoryRanges );

        if ( !sortedIter.IsEnd() )
        {
            pageAllocation *currentArena = sortedIter.Resolve();
            RwListEntry <pageHandle> *sortedNextHandleIter = this->sortedActiveHandles.root.next;
            bool hasNextPageHandle = ( sortedNextHandleIter != &this->sortedActiveHandles.root );

            handleAllocSlice.SetSlicePosition( (size_t)currentArena->arenaAddress );

            do
            {
                // Note that an optimization behavior in this function is that we allocate at maximum free space.
                // When we tried and failed at maximum free space, we skip the entire space!
                // This is perfectly valid under the fact that memory allocation establishes one block of contiguous memory.

                // Check if there is an obstruction in the next-in-line item.
                // Because we are address-sorted, this is a very fast operation.
                // If obstructed we can optimize the forward-movement of the iterator.
                bool isCurrentAllocationSpotObstructed = false;

                while ( hasNextPageHandle )
                {
                    pageHandle *obstructAlloc = LIST_GETITEM( pageHandle, sortedNextHandleIter, managerNode );

                    // Is the selected allocation spot available?
                    // We skip any memory blocks entirely before the handle region.
                    eir::eIntersectionResult intResult =
                        obstructAlloc->requestedMemory.intersectWith( handleAllocSlice );

                    if ( intResult != eir::INTERSECT_FLOATING_START )
                    {
                        isCurrentAllocationSpotObstructed = ( intResult != eir::INTERSECT_FLOATING_END );
                        break;
                    }

                    // Move to the next memory block.
                    sortedNextHandleIter = sortedNextHandleIter->next;

                    hasNextPageHandle = ( sortedNextHandleIter != &this->sortedActiveHandles.root );
                }

                // If we are obstructing, then we must go on.
                // Otherwise we enter this condition.
                if ( !isCurrentAllocationSpotObstructed )
                {
                    // Get the real allocation slice for the arena region.
                    size_t allocGranularity = this->vmemAccess.GetPlatformAllocationGranularity();

                    const size_t arenaAllocStart = SCALE_DOWN( handleAllocSlice.GetSliceStartPoint(), allocGranularity );
                    const size_t arenaAllocEnd = ALIGN_SIZE( handleAllocSlice.GetSliceEndPoint() + 1, allocGranularity );

                    size_t arenaAllocSize = ( arenaAllocEnd - arenaAllocStart );

                    const memBlockSlice_t arenaAllocSlice( arenaAllocStart, arenaAllocSize );

                    // Check if we would be the first on currentArena if allocation succeeded.
                    bool wouldBeFirstItem = true;

                    if ( pageHandle *begItem = currentArena->begResideHandle )
                    {
                        if ( begItem->requestedMemory.GetSliceStartPoint() < handleAllocSlice.GetSliceStartPoint() )
                        {
                            wouldBeFirstItem = false;
                        }
                    }

                    // Try performing a normal allocation on this space.
                    bool couldAllocate =
                        FlowAllocateRegion( handleAllocSlice, arenaAllocSlice, currentArena, wouldBeFirstItem, false, allocOut );

                    if ( couldAllocate )
                    {
                        // We are successful, so return the allocation place.
                        handleAllocSliceOut = std::move( handleAllocSlice );
                        return true;
                    }
                }

                // Advance the current allocation attempt.
                // For that we have to check if there is a next memory location to try.
                // IMPORTANT: the next location _must_ be valid!
                {
                    // If we have no next page handle, we just advance the arena.
                    bool doAdvanceArena = false;
                    bool doAdvancePageHandle = false;
                    bool startJustAfterPageMem = true;

                    size_t nextTryPos;

                    if ( !hasNextPageHandle )
                    {
                        doAdvanceArena = true;
                        startJustAfterPageMem = false;
                    }
                    else
                    {
                        if ( isCurrentAllocationSpotObstructed )
                        {
                            // We simply skip the current allocation block.
                            doAdvancePageHandle = true;
                        }
                        else
                        {
                            doAdvanceArena = true;
                            startJustAfterPageMem = false;
                        }
                    }

                    if ( doAdvanceArena )
                    {
                        sortedIter.Increment();

                        if ( sortedIter.IsEnd() )
                        {
                            // If there is no more location to try for allocation, we simply fail out of
                            // our search for shared memory allocations. We will directly ask the OS for
                            // memory next.
                            break;
                        }

                        currentArena = sortedIter.Resolve();
                    }

                    if ( startJustAfterPageMem )
                    {
                        assert( hasNextPageHandle == true );

                        pageHandle *nextMem = LIST_GETITEM( pageHandle, sortedNextHandleIter, managerNode );

                        nextTryPos = ( nextMem->requestedMemory.GetSliceEndPoint() + 1 );
                    }
                    else
                    {
                        nextTryPos = ( currentArena->pageSpan.GetSliceStartPoint() );
                    }

                    if ( doAdvancePageHandle )
                    {
                        assert( sortedNextHandleIter != &this->sortedActiveHandles.root );

                        sortedNextHandleIter = sortedNextHandleIter->next;

                        hasNextPageHandle = ( sortedNextHandleIter != &this->sortedActiveHandles.root );
                    }

                    // Set the next try pos.
                    handleAllocSlice.SetSlicePosition( nextTryPos );
                }

                // Normalize the arena.
                // The page handle will be normalízed on loop beginning.
                while ( true )
                {
                    eir::eIntersectionResult intResult =
                        currentArena->pageSpan.intersectWith( handleAllocSlice );

                    if ( intResult != eir::INTERSECT_FLOATING_START )
                    {
                        // There is an intersection, so we are ok.
                        break;
                    }

                    sortedIter.Increment();

                    if ( sortedIter.IsEnd() )
                    {
                        // No more arenas.
                        goto failure;
                    }

                    currentArena = sortedIter.Resolve();
                }

                // If the allocation start is before the arena start, we set it to the arena start.
                size_t arenaStartOff = currentArena->pageSpan.GetSliceStartPoint();

                if ( handleAllocSlice.GetSliceStartPoint() < arenaStartOff )
                {
                    handleAllocSlice.SetSlicePosition( arenaStartOff );
                }
            }
            while ( true );
        }

    failure:
        // We just failed.
        return false;
    }

    static inline bool IsValidAllocation( void *desiredAddress, size_t spanSize )
    {
        bool isValid = true;

        if ( desiredAddress != nullptr )
        {
            // Check that there is no number overflow.
            size_t memDesiredAddress = (size_t)desiredAddress;
            size_t memSpanSize = spanSize;

            size_t memAddressBorder = ( memDesiredAddress + memSpanSize );

            // The condition I check here is that if I add those two numbers,
            // the result must be bigger than the source operand I added to.
            isValid = ( memAddressBorder > memDesiredAddress );
        }
        return isValid;
    }

    // Algorithm that looks left and right for the best insertion spot for sorted-insertion of page handles.
    // Returns the node to insert or append to.
    inline RwListEntry <pageHandle>& FindNodeForSortedInsertion( const memBlockSlice_t& desiredMemSlice, pageAllocation *scanMiddleArena, bool& trueAppendFalseInsertOut )
    {
        // We right, left, right, left, ... and so on.
        // Until we found on the left a right-most or on the right a left-most item.

        pageAllocation *leftScan = scanMiddleArena;         // TODO: decrement this one by one (to the left).
        pageAllocation *rightScan = scanMiddleArena;

        pageHandle *prettyCloseHandle = nullptr;

        while ( true )
        {
            // Stop if left and right are none, in which case we return end of list for insertion.
            if ( leftScan == nullptr && rightScan == nullptr )
            {
                break;
            }

            // First to the right.
            if ( rightScan != nullptr )
            {
                pageHandle *begResideHandle = rightScan->begResideHandle;

                if ( begResideHandle )
                {
                    //trueAppendFalseInsertOut = false;

                    prettyCloseHandle = begResideHandle;
                    goto gotNeighborHandle;
                }

                // Increment.
                RwListEntry <pageAllocation> *nextArenaNode = rightScan->sortedNode.next;

                if ( nextArenaNode == &this->sortedMemoryRanges.root )
                {
                    rightScan = nullptr;
                }
                else
                {
                    rightScan = LIST_GETITEM( pageAllocation, nextArenaNode, sortedNode );
                }
            }

            // Next to the left.
            if ( leftScan != nullptr )
            {
                pageAllocation::sortedPageResidentIterator begResideIter( this, leftScan );

                if ( begResideIter.IsEnd() == false )
                {
                    trueAppendFalseInsertOut = true;

                    // Fetch the right-most item.
                    pageHandle *rightMostItem = begResideIter.Resolve();

                    while ( !begResideIter.IsEnd() )
                    {
                        rightMostItem = begResideIter.Resolve();

                        begResideIter.Increment();
                    }

                    // Return it.
                    prettyCloseHandle = rightMostItem;
                    goto gotNeighborHandle;
                }

                // Increment.
                RwListEntry <pageAllocation> *prevArenaNode = leftScan->sortedNode.prev;

                if ( prevArenaNode == &this->sortedMemoryRanges.root )
                {
                    leftScan = nullptr;
                }
                else
                {
                    leftScan = LIST_GETITEM( pageAllocation, prevArenaNode, sortedNode );
                }
            }
        }

        // The list is basically empty of page handles.
        // So just return the end.
        trueAppendFalseInsertOut = false;
        return this->sortedActiveHandles.root;

    gotNeighborHandle:
        assert( prettyCloseHandle != nullptr );

        // We need to sort it now.
        // Since we assume that the handle we found is pretty close to being sorted, we just go to
        // the neighbors until we found the correct spot.

        eir::eIntersectionResult beg_intResult =
            prettyCloseHandle->requestedMemory.intersectWith( desiredMemSlice );

        // Check if the close handle is left or right from our memory.
        // If it is left then we go right, if it is right then we go left -> until we found the collision-end.
        // IMPORTANT: we assume the new handle is not going to collide against any handles.

        bool leftTrueRightFalse = false;

        if ( beg_intResult == eir::INTERSECT_FLOATING_START )
        {
            // Close handle is left from the desired mem.
            leftTrueRightFalse = false;
        }
        else if ( beg_intResult == eir::INTERSECT_FLOATING_END )
        {
            // Close handle is right from the desired mem.
            leftTrueRightFalse = true;
        }
        else
        {
            // Should NEVER happen.
            assert( 0 );
        }

        while ( true )
        {
            // Get the next handle node.
            RwListEntry <pageHandle> *nextNode;

            if ( leftTrueRightFalse )
            {
                // Left.
                nextNode = prettyCloseHandle->managerNode.prev;
            }
            else
            {
                // Right.
                nextNode = prettyCloseHandle->managerNode.next;
            }

            bool reachedTheEnd = false;

            // We reached the end if we are the end node.
            if ( nextNode == &this->sortedActiveHandles.root )
            {
                reachedTheEnd = true;
            }

            // ... if we found another page handle that is floating just in front of our way.
            if ( !reachedTheEnd )
            {
                prettyCloseHandle = LIST_GETITEM( pageHandle, nextNode, managerNode );

                eir::eIntersectionResult intResult =
                    prettyCloseHandle->requestedMemory.intersectWith( desiredMemSlice );

                assert( eir::isFloatingIntersect( intResult ) == true );

                // We try to find the one that is our way.
                if ( leftTrueRightFalse )
                {
                    // Left.
                    if ( intResult == eir::INTERSECT_FLOATING_START )
                    {
                        // We append to nextCloseHandle.
                        reachedTheEnd = true;
                    }
                }
                else
                {
                    // Right.
                    if ( intResult == eir::INTERSECT_FLOATING_END )
                    {
                        // We insert to nextCloseHandle.
                        reachedTheEnd = true;
                    }
                }
            }

            if ( reachedTheEnd )
            {
                // Since we reached the end we can just perform the inclusion.
                // Remember that we always went one-too-far so we must place one-back.
                // If we went left we have to append, if we went right we have to insert.
                trueAppendFalseInsertOut = leftTrueRightFalse;
                return *nextNode;
            }
        }

        // SHOULD NEVER REACH THIS.
        trueAppendFalseInsertOut = false;
        return this->sortedActiveHandles.root;
    }

    // Cached vectors for usage by the main functions.
    // Can be used because functions are not reentrant and are not thread-safe.
    memCachedReserveAllocList_t _cached_memReserveList;

public:
    inline pageHandle* Allocate( void *desiredAddress, size_t spanSize )
    {
        pageHandle *theHandle = nullptr;

        // Only proceed if the requested allocation is valid.
        if ( IsValidAllocation( desiredAddress, spanSize ) )
        {
            // Properly align the allocation request on page boundaries.
            size_t pageSize = this->vmemAccess.GetPlatformPageSize();

            size_t pageDesiredAddressStart = SCALE_DOWN( (size_t)desiredAddress, pageSize );
            size_t pageDesiredAddressEnd = ALIGN_SIZE( (size_t)desiredAddress + spanSize, pageSize );

            size_t pageSpanSize = ( pageDesiredAddressEnd - pageDesiredAddressStart );

            // Determine the pages that should host the requested memory region
            memCachedReserveAllocList_t& hostPages = this->_cached_memReserveList;
            bool validAllocation = false;

            // The actual allocation slice.
            memBlockSlice_t pageDesiredMemSlice( pageDesiredAddressStart, pageSpanSize );

            // We first have to find pages that can host our memory.
            {
                // If we know the address we should allocate on, we attempt to find regions that have already been allocated
                // so they can host our memory.
                if ( pageDesiredAddressStart != 0 )
                {
                    validAllocation = PlaceMemoryRequest( pageDesiredMemSlice, hostPages );
                }
                else
                {
                    // Otherwise we have to search for a new spot.
                    validAllocation = SearchForReservedMemory( pageSpanSize, pageDesiredMemSlice, hostPages );

                    if ( !validAllocation )
                    {
                        // As a last resort, request memory from the OS.
                        size_t arenaSpanSize = GetAllocationArenaRange( pageSpanSize );

                        pageAllocation *newArena = ReserveNewMemory( nullptr, arenaSpanSize );

                        if ( newArena )
                        {
                            // We allocate at the start of the new arena.
                            pageDesiredMemSlice.SetSlicePosition( (size_t)newArena->arenaAddress );

                            memReserveAllocInfo allocInfo;
                            allocInfo.hostArena = newArena;
                            allocInfo.hostArenaIsFirstSpot = true;
                            allocInfo.isHostArenaNewlyAllocated = true;

                            hostPages.AddItem( std::move( allocInfo ) );

                            // Register this new reserved memory.
                            LIST_INSERT( this->activeMemoryRanges.root, newArena->managerNode );
                            SortedMemoryBlockInsert( newArena );

                            validAllocation = true;
                        }
                    }
                }
            }

            if ( validAllocation )
            {
                // Create a pageHandle to it.
                pageHandle *newHandle = _allocPageHandle.Allocate( pageDesiredMemSlice );

                if ( newHandle )
                {
                    // Register it inside the host pages.
                    size_t count = hostPages.GetCount();

                    if ( count != 0 )
                    {
                        // Has to exist.
                        const memReserveAllocInfo *firstInfo = hostPages.Get( 0 );

                        // Do registration.
                        {
                            pageAllocation *firstPageAlloc = firstInfo->hostArena;

                            // Find the handle insertion spot, fast.
                            bool trueAppendFalseInsertSpotDenom;
                            RwListEntry <pageHandle>& insertOrAppendSpot = FindNodeForSortedInsertion( pageDesiredMemSlice, firstPageAlloc, trueAppendFalseInsertSpotDenom );

                            // Register the handle.
                            if ( trueAppendFalseInsertSpotDenom )
                            {
                                // Remember that the operations are actually inverted-to-their-name
                                // if not used on the list root.

                                // APPEND.
                                LIST_INSERT( insertOrAppendSpot, newHandle->managerNode );
                            }
                            else
                            {
                                // INSERT.
                                LIST_APPEND( insertOrAppendSpot, newHandle->managerNode );
                            }

                            // hostPages has to be address-sorted, of course.
                            newHandle->begResiding = firstPageAlloc;
                        }

                        hostPages.ForAllEntries(
                            [&]( const memReserveAllocInfo& info )
                        {
                            pageAllocation *allocation = info.hostArena;

#ifdef _PARANOID_MEMTESTS_
                            // DEBUG.
                            allocation->CheckForCollision( newHandle->requestedMemory );
#endif //_PARANOID_MEMTESTS_

                            allocation->RefPageHandle();

                            // Set as first?
                            if ( info.hostArenaIsFirstSpot )
                            {
                                allocation->begResideHandle = newHandle;
                            }
                        });

                        this->numAllocatedPageHandles++;

                        // Put the memory active in the OS.
                        CommitMemoryOfPageHandle( newHandle, pageDesiredMemSlice );

                        theHandle = newHandle;

                        // Clear the hostPages list for another usage.
                        hostPages.Clear();
                    }
                }
            }

            if ( theHandle == nullptr )
            {
                // Delete all allocated pages.
                hostPages.ForAllEntries(
                    [&]( const memReserveAllocInfo& info )
                {
                    if ( info.isHostArenaNewlyAllocated )
                    {
                        pageAllocation *thePage = info.hostArena;

                        DeletePageAllocation( thePage );
                    }
                });

                // Clear it for another usage.
                hostPages.Clear();
            }
        }

        return theHandle;
    }

    inline pageHandle* FindHandleByAddress( void *pAddress )
    {
        // Just compare addresses of every alive handle and return
        // the one that matches the query.
        LIST_FOREACH_BEGIN( pageHandle, sortedActiveHandles.root, managerNode )
            if ( item->GetTargetPointer() == pAddress )
            {
                return item;
            }
        LIST_FOREACH_END

        return nullptr;
    }

private:
    // Helper function to get a signed difference between two unsigned numbers.
    template <typename numberType>
    static inline numberType GetSignedDifference( const numberType& left, const numberType& right, bool& isSigned )
    {
        bool _isSigned = ( left < right );

        numberType result;

        if ( _isSigned )
        {
            result = ( right - left );
        }
        else
        {
            result = ( left - right );
        }

        isSigned = _isSigned;

        return result;
    }

    inline void MemBlockGarbageCollection( pageAllocation *memBlock )
    {
        // If the page is not being used anymore, release it.
        if ( !memBlock->IsBlockBeingUsed() )
        {
            DeletePageAllocation( memBlock );
        }
    }

public:
    // Attempts to update the handle size so that either more or less memory
    // can be used.
    inline bool SetHandleSize( pageHandle *theHandle, size_t _unaligned_newReserveSize )
    {
        // Properly align the request size.
        // This is important because we represent real memory pages.
        size_t newReserveSize = GetPageAllocationRange( _unaligned_newReserveSize );

        // Do nothing if the handle size has not changed.
        size_t oldSize = theHandle->GetTargetSize();

        if ( newReserveSize == oldSize )
            return true;

        if ( newReserveSize == 0 )
            return false;

        bool isSigned;
        size_t memSizeDifference = GetSignedDifference( newReserveSize, oldSize, isSigned );

        bool success = false;

        if ( !isSigned )
        {
            // Make sure that this allocation is valid.
            // It can only turn invalid if the memory size is greater than before.
            if ( IsValidAllocation( theHandle->GetTargetPointer(), newReserveSize ) )
            {
                // If the new memory size is greater than the old,
                // allocate additional memory pages, on demand of course.
                memBlockSlice_t requiredRegion( (size_t)theHandle->GetTargetPointer() + (size_t)oldSize, (size_t)memSizeDifference );

                // Fetch the last arena that the page handle is part of.
                pageAllocation *lastArenaOfSpace = nullptr;
                {
                    struct fetchRightmostArenaCalloid
                    {
                        static AINLINE void ProcessEntry( pageAllocation *arenaItem, pageAllocation*& arenaOut )
                        {
                            arenaOut = arenaItem;
                        }
                    };

                    pageAllocation *rightMost = nullptr;

                    ForAllPageHandleArenasSorted <fetchRightmostArenaCalloid, pageAllocation*&> ( theHandle, rightMost );

                    assert( rightMost != nullptr );

                    lastArenaOfSpace = rightMost;
                }

                // Check if we collide against something on expansion request.
                // If we do, we basically cannot expand anyway.
                bool isCollidingAgainstStuff = false;
                {
                    // We need to have a next item.
                    RwListEntry <pageHandle> *nextPageHandleIter = ( theHandle->managerNode.next );

                    if ( nextPageHandleIter != &this->sortedActiveHandles.root )
                    {
                        pageHandle *nextHandle = LIST_GETITEM( pageHandle, nextPageHandleIter, managerNode );

                        // Check if we would obstruct.
                        isCollidingAgainstStuff = IsAllocationObstructed( requiredRegion, nextHandle );
                    }
                }

                if ( !isCollidingAgainstStuff )
                {
                    // Now we simply allocate the region(s) after the memory and
                    // merge the two (or more) allocation regions into one.

                    arenaSortedIterator_t sortedIter( this->sortedMemoryRanges, &lastArenaOfSpace->sortedNode );

                    sortedIter.Increment();

                    // Create the arena-aligned region around the required region.
                    size_t allocGranularity = this->vmemAccess.GetPlatformAllocationGranularity();

                    size_t arenaAllocStart = SCALE_DOWN( requiredRegion.GetSliceStartPoint(), allocGranularity );
                    size_t arenaAllocEnd = ALIGN_SIZE( requiredRegion.GetSliceEndPoint() + 1, allocGranularity );

                    size_t arenaAllocSize = ( arenaAllocEnd - arenaAllocStart );

                    memBlockSlice_t arenaAllocSlice( arenaAllocStart, arenaAllocSize );

                    memCachedReserveAllocList_t& expansion_inOut = this->_cached_memReserveList;

                    bool flowAllocExpandSuccess =
                        FlowAllocateAfterRegion( expansion_inOut, sortedIter, requiredRegion, arenaAllocSlice, lastArenaOfSpace );

                    // Have we succeeded in reserving the requested memory pages?
                    if ( flowAllocExpandSuccess )
                    {
                        // Add the things together, merge them.
                        expansion_inOut.ForAllEntries(
                            [&]( const memReserveAllocInfo& info )
                        {
                            pageAllocation *hostArena = info.hostArena;

#ifdef _PARANOID_MEMTESTS_
                            // DEBUG.
                            hostArena->CheckForCollision( theHandle->requestedMemory );
                            hostArena->CheckForCollision( requiredRegion );
#endif //_PARANOID_MEMTESTS_

                            hostArena->RefPageHandle();

                            // Set as first?
                            if ( info.hostArenaIsFirstSpot )
                            {
                                hostArena->begResideHandle = theHandle;
                            }
                        });

                        // Set the new handle region.
                        theHandle->requestedMemory.SetSliceEndPoint( (size_t)theHandle->GetTargetPointer() + newReserveSize - (size_t)1 );

                        // Now update the OS.
                        CommitMemoryOfPageHandle( theHandle, requiredRegion );

                        success = true;

                        // Clear the list for another usage.
                        expansion_inOut.Clear();
                    }
                }
            }
        }
        else
        {
            // Otherwise the new memory size is smaller than the old.
            // We potentially have to remove pages from the residency list.

            memBlockSlice_t requiredRegion( (size_t)theHandle->GetTargetPointer() + newReserveSize, (size_t)memSizeDifference );

            // Update the OS.
            DecommitMemoryOfPageHandle( theHandle, requiredRegion );

            // Determine the amount of arenas that should be dereferenced in the course of this influence area shrinking.
            struct derefArenasOutsidePageHandleCalloid
            {
                static AINLINE void ProcessEntry( pageAllocation *oneInSortedOrder, NativePageAllocator *manager, pageHandle *theHandle, const memBlockSlice_t& requiredRegion )
                {
                    // Since the start border of the requiredRegion is touching the end border of the new valid region of theHandle,
                    // each arena whose starting point is >= the starting point of requiredRegion is floating past the new valid region.
                    // The above statement directly matches the definition of floating past.
                    // Thus the check is valid.
                    bool isFloatingPast = ( oneInSortedOrder->pageSpan.GetSliceStartPoint() >= requiredRegion.GetSliceStartPoint() );

                    if ( isFloatingPast )
                    {
                        oneInSortedOrder->RemovePossibleFirst( manager, theHandle );

                        oneInSortedOrder->DerefPageHandle();

                        manager->MemBlockGarbageCollection( oneInSortedOrder );
                    }
                }
            };

            ForAllPageHandleArenasSorted <derefArenasOutsidePageHandleCalloid> ( theHandle, this, theHandle, requiredRegion );

            // Set the new handle region.
            theHandle->requestedMemory.SetSliceEndPoint( (size_t)theHandle->GetTargetPointer() + newReserveSize - (size_t)1 );

            success = true;
        }

        return success;
    }

    inline void Free( pageHandle *memRange )
    {
        // Release the contents of the memory to the OS.
        DecommitMemoryOfPageHandle( memRange, memRange->requestedMemory );

        // Free the link to the allocated OS memory regions.
        struct derefAllPageHandleArenasCalloid
        {
            static AINLINE void ProcessEntry( pageAllocation *memBlock, NativePageAllocator *manager, pageHandle *handleBeingRemoved )
            {
                // Make sure we are not referenced in the arena anymore.
                memBlock->RemovePossibleFirst( manager, handleBeingRemoved );

                memBlock->DerefPageHandle();

                // Clean up memory blocks that are not used anymore.
                manager->MemBlockGarbageCollection( memBlock );
            }
        };

        ForAllPageHandleArenasSorted <derefAllPageHandleArenasCalloid> ( memRange, this, memRange );

        this->numAllocatedPageHandles--;

        // Delete and unregister our pageHandle.
        LIST_REMOVE( memRange->managerNode );

        _allocPageHandle.Deallocate( memRange );
    }

    inline bool FreeByAddress( void *pAddress )
    {
        pageHandle *theHandle = FindHandleByAddress( pAddress );

        if ( !theHandle )
            return false;

        Free( theHandle );
        return true;
    }

    // Meta-data API.
    inline size_t GetPageSize( void )
    {
        return vmemAccess.GetPlatformPageSize();
    }
};

#endif //_COMMON_OS_UTILS_
