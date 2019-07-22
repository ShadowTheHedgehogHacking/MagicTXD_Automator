/*****************************************************************************
*
*  PROJECT:     Eir SDK
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        eirrepo/sdk/OSUtils.memheap.h
*  PURPOSE:     Virtual-memory-based memory heap
*
*  Find the Eir SDK at: https://osdn.net/projects/eirrepo/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _NATIVE_VIRTUAL_MEMORY_HEAP_ALLOCATOR_
#define _NATIVE_VIRTUAL_MEMORY_HEAP_ALLOCATOR_

#include "OSUtils.h"
#include "MacroUtils.h"
#include "rwlist.hpp"
#include "AVLTree.h"

// For std::max_align_t.
#include <cstddef>

// Helper.
template <typename numberType>
AINLINE numberType UINT_DOWNPUSH( numberType value, numberType alignment )
{
    return ( value - value % alignment );
}

// Heap allocator class that provides sized memory chunks from OS-provided virtual memory.
// Version 2.
// * now using AVL trees in free-bytes lists to optimize allocation performance
struct NativeHeapAllocator
{
    // Allocations are made on virtual memory islands that bundle many together. Each vmem island
    // has a list of allocations residing on it. It can potentially grow infinitely but if it
    // cannot then another island is created. Each island dies if there are no more allocations
    // on it.
    // Advantage of using this class is that you have FULL CONTROL over memory allocation. You
    // can even design the features that your memory allocator should have ;)

    static constexpr size_t MIN_PAGES_FOR_ISLAND = 4;   // minimum amount of pages to reserve for an island.
    static constexpr size_t DEFAULT_ALIGNMENT = sizeof(std::max_align_t);

    inline NativeHeapAllocator( void )
    {
        return;
    }

    inline NativeHeapAllocator( const NativeHeapAllocator& right ) = delete;
    inline NativeHeapAllocator( NativeHeapAllocator&& right ) noexcept
    {
        // Move fields over, simply.
        this->nativeMemProv = std::move( right.nativeMemProv );
        this->listIslands = std::move( right.listIslands );

        // The items on the right have automatically been cleared.
    }

    inline ~NativeHeapAllocator( void )
    {
        // Release all memory.
        LIST_FOREACH_BEGIN( VMemIsland, this->listIslands.root, managerNode )

            NativePageAllocator::pageHandle *islandHandle = item->allocHandle;

            item->~VMemIsland();

            this->nativeMemProv.Free( islandHandle );

        LIST_FOREACH_END
    }

    // Assignments.
    inline NativeHeapAllocator& operator = ( const NativeHeapAllocator& right ) = delete;
    inline NativeHeapAllocator& operator = ( NativeHeapAllocator&& right ) noexcept
    {
        this->~NativeHeapAllocator();

        return *new (this) NativeHeapAllocator( std::move( right ) );
    }

    inline void* Allocate( size_t memSize, size_t alignedBy = 0 )
    {
        // This is not an easy system, lol.

        if ( memSize == 0 )
        {
            // Cannot allocate something that has no size.
            return nullptr;
        }

        if ( alignedBy == 0 )
        {
            // I guess the user wants the best-default.
            alignedBy = DEFAULT_ALIGNMENT;
        }

        // If the allocation succeeded we have this data.
        VMemAllocation *allocObj;

        // Try one of the existing islands for a memory allocation first.
        LIST_FOREACH_BEGIN( VMemIsland, this->listIslands.root, managerNode )

            VMemAllocation *tryAllocObj = item->Allocate( this, memSize, alignedBy );

            if ( tryAllocObj != nullptr )
            {
                allocObj = tryAllocObj;
                goto gotToAllocate;
            }

        LIST_FOREACH_END

        // If all islands refused to provide memory then we have to provide an entirely new island.
        // At least we try.
        {
            // Determine the minimum memory size that we should reserve for the island.
            size_t pageSize = this->nativeMemProv.GetPageSize();

            size_t minSizeByMinPages = ( pageSize * MIN_PAGES_FOR_ISLAND );

            // Since ALIGNment is always >= than the input and offsets are equal-synonyms to sizes,
            // we can use this to have the first position of a header.
            size_t offsetToFirstHeaderTryPos = ALIGN( sizeof(VMemIsland), VMemIsland::HEADER_ALIGNMENT, VMemIsland::HEADER_ALIGNMENT );

            // It is most important that we at least can allocate one object on the new allocation.
            // Since we cannot know the virtual memory address of allocation in advance we
            // actually have to do some good estimate on the maximum required memory size.
            // But since the virtual memory pages are allocated at power-of-two offsets the
            // estimate should be very good for power-of-two alignments.
            size_t minSizeByObject = ( offsetToFirstHeaderTryPos + alignedBy + memSize + sizeof(VMemAllocation) );

            size_t actualMinSize = std::max( minSizeByMinPages, minSizeByObject );

            NativePageAllocator::pageHandle *newPageHandle = this->nativeMemProv.Allocate( nullptr, actualMinSize );

            if ( newPageHandle )
            {
                // Create the new island.
                void *memPtr = newPageHandle->GetTargetPointer();

                VMemIsland *newIsland = new (memPtr) VMemIsland( newPageHandle );

                // Allocate the memory on it.
                VMemAllocation *newAllocObj = newIsland->Allocate( this, memSize, alignedBy );

                assert( newAllocObj != nullptr );

                if ( newAllocObj )
                {
                    // We can register the island too.
                    LIST_APPEND( this->listIslands.root, newIsland->managerNode );

                    // Just return it.
                    allocObj = newAllocObj;
                    goto gotToAllocate;
                }
                else
                {
                    // Release stuff because something funky failed...
                    newIsland->~VMemIsland();

                    this->nativeMemProv.Free( newPageHandle );
                }
            }
        }

        // Could not allocate anything.
        // The most probable reason is that there is no more system RAM available.
        return nullptr;

    gotToAllocate:
        // Return the data portion of our allocation.
        void *dataPtr = ( (char*)allocObj + allocObj->dataOff );

        return dataPtr;
    }

private:
    // Forward declaration for the methods here.
    struct VMemAllocation;

    AINLINE VMemAllocation* get_mem_block_from_ptr( void *memPtr )
    {
        // TODO: add debug to this code, so that memory corruption can be combatted.
        // it could iterate over all memory pointers to verify that memPtr really belongs to us.

        size_t header_size = sizeof(VMemAllocation);

        size_t memOff = (size_t)memPtr;

        size_t memHandleOff = UINT_DOWNPUSH( memOff - header_size, VMemIsland::HEADER_ALIGNMENT );

        return (VMemAllocation*)memHandleOff;
    }

    AINLINE const VMemAllocation* get_mem_block_from_ptr( const void *memPtr ) const
    {
        size_t header_size = sizeof(VMemAllocation);

        size_t memOff = (size_t)memPtr;

        size_t memHandleOff = UINT_DOWNPUSH( memOff - header_size, VMemIsland::HEADER_ALIGNMENT );

        return (const VMemAllocation*)memHandleOff;
    }

public:
    inline void Free( void *memPtr )
    {
        // We guarrantee that this operation is O(1) in Release mode with all optimizations.

        VMemAllocation *memHandle = get_mem_block_from_ptr( memPtr );

        // Release the memory.
        VMemIsland *manager = memHandle->manager;

        manager->Free( this, memHandle );

        // If the memory island is empty, we can garbage collect it.
        if ( manager->HasNoAllocations() )
        {
            NativePageAllocator::pageHandle *islandHandle = manager->allocHandle;

            LIST_REMOVE( manager->managerNode );

            manager->~VMemIsland();

            this->nativeMemProv.Free( islandHandle );
        }
    }

    // Attempts to change the size of an allocation.
    inline bool SetAllocationSize( void *memPtr, size_t newSize )
    {
        // We can only fail if the allocation does not fit with regards to the remaining free space.
        // Or the required data size is zero (makes no sense!)

        VMemAllocation *memHandle = get_mem_block_from_ptr( memPtr );

        VMemIsland *manager = memHandle->manager;

        return manager->ResizeAllocation( this, memHandle, memPtr, newSize );
    }

    // Returns the data size of an allocation.
    inline size_t GetAllocationSize( const void *memPtr ) const
    {
        const VMemAllocation *memHandle = get_mem_block_from_ptr( memPtr );

        return memHandle->dataSize;
    }

    // Returns the whole size of this allocation.
    // This includes the meta-data header as well as the alignment.
    inline size_t GetAllocationMetaSize( const void *memPtr ) const
    {
        const VMemAllocation *memHandle = get_mem_block_from_ptr( memPtr );

        return ( memHandle->dataOff + memHandle->dataSize );
    }

    struct heapStats
    {
        size_t usedBytes = 0;
        size_t usedMetaBytes = 0;
        size_t freeBytes = 0;
        size_t countOfAllocations = 0;
        size_t countOfIslands = 0;
    };

    // Returns statistics about this memory allocator.
    inline heapStats GetStatistics( void ) const
    {
        heapStats stats;

        LIST_FOREACH_BEGIN( VMemIsland, this->listIslands.root, managerNode )

            VMemIsland::usageStats islandStats = item->GetUsageStatistics();

            stats.usedBytes += islandStats.usedBytes;
            stats.usedMetaBytes += islandStats.usedMetaBytes;
            stats.freeBytes += islandStats.freeBytes;
            stats.countOfAllocations += islandStats.countOfAllocations;

            // One more island.
            stats.countOfIslands++;

        LIST_FOREACH_END

        return stats;
    }

    // Walks all allocations of this heap allocator.
    template <typename callbackType>
    AINLINE void WalkAllocations( const callbackType& cb )
    {
        LIST_FOREACH_BEGIN( VMemIsland, this->listIslands.root, managerNode )

            // Even if we walk allocations in memory-order for single islands, we have not ordered the islands (no point),
            // so there is no order-guarantee for this function.
            item->WalkAllocations(
                [&]( VMemAllocation *allocObj )
            {
                void *memPtr = (char*)allocObj + allocObj->dataOff;

                cb( memPtr );
            });

        LIST_FOREACH_END
    }

    // Quick helper to check if an allocation is inside this heap allocator.
    AINLINE bool DoesOwnAllocation( const void *memptr )
    {
        bool foundPtr = false;

        this->WalkAllocations(
            [&]( void *ptr )
        {
            if ( ptr == memptr )
            {
                foundPtr = true;
            }
        });

        return foundPtr;
    }

    // Simple realloc helper just because it is being exposed in the CRT aswell.
    inline void* Realloc( void *memPtr, size_t newSize, size_t alignment = DEFAULT_ALIGNMENT )
    {
        if ( memPtr == nullptr )
        {
            return Allocate( newSize, alignment );
        }

        if ( newSize == 0 )
        {
            Free( memPtr );
            return nullptr;
        }

        // Now do the tricky part.
        // If we suceeded in resizing, we leave it at that.
        // Otherwise we must allocate a new bit of memory, copy all old bytes over, free the old and return the new.
        bool couldResize = SetAllocationSize( memPtr, newSize );

        if ( couldResize )
        {
            return memPtr;
        }

        // Now we just trash the old block.
        // Did the CRT state anything about alignment tho?
        void *newMemPtr = Allocate( newSize, alignment );

        if ( newMemPtr == nullptr )
        {
            // We follow the guide as to what happens when "realloc fails"...
            // https://linux.die.net/man/3/realloc
            // You can detect this case when you passed in a positive value
            // for request size but this function returns nullptr.
            return nullptr;
        }

        // Memory copy.
        {
            char *srcPtr = (char*)memPtr;
            char *dstPtr = (char*)newMemPtr;
            size_t srcSize = GetAllocationSize( memPtr );

            for ( size_t n = 0; n < newSize; n++ )
            {
                char putByte;

                if ( n < srcSize )
                {
                    putByte = *( srcPtr + n );
                }
                else
                {
                    putByte = 0;
                }

                *( dstPtr + n ) = putByte;
            }
        }

        // Free the old.
        Free( memPtr );

        return newMemPtr;
    }

private:
    // Virtual memory manager object.
    NativePageAllocator nativeMemProv;

    typedef sliceOfData <size_t> memBlockSlice_t;

    // To increase allocation performance we remember all free memory regions and sort
    // this list by size of free blocks. So when we process an allocation request we
    // very quickly know where to put it into for best-fit.
    struct VMemAllocation;

    struct VMemFreeBlock
    {
        inline VMemFreeBlock( void ) = default;
        inline VMemFreeBlock( memBlockSlice_t slice )
            : freeRegion( std::move( slice ) )
        {
            return;
        }

        memBlockSlice_t freeRegion;                 // can be empty to display no space (0, -1).
        AVLNode sortedBySizeNode;
        RwListEntry <VMemFreeBlock> sortedByAddrNode;
    };

    // Allocation object on a VMemIsland object.
    struct VMemIsland;

    struct VMemAllocation
    {
        inline VMemAllocation( VMemIsland *allocHost, size_t dataSize, size_t dataOff )
        {
            this->dataSize = dataSize;
            this->dataOff = dataOff;
            this->manager = allocHost;
        }

        inline VMemAllocation( const VMemAllocation& ) = delete;
        inline VMemAllocation( VMemAllocation&& ) = delete;

        inline ~VMemAllocation( void )
        {
            // Anything to do?
            return;
        }

        inline VMemAllocation& operator = ( const VMemAllocation& ) = delete;
        inline VMemAllocation& operator = ( VMemAllocation&& ) = delete;

        // Returns the region that this allocation has to occupy.
        inline memBlockSlice_t GetRegion( void ) const
        {
            size_t dataStart = (size_t)this;
            size_t dataSize = ( this->dataOff + this->dataSize );

            return memBlockSlice_t( dataStart, dataSize );
        }

        // We need certain meta-data per-allocation to maintain stuff.

        // Statistic fields.
        size_t dataSize;        // size in bytes of the region after this header reserved for the user application.
        size_t dataOff;         // offset after this header to the data for alignment purposes.

        // Manager meta-data.
        VMemIsland *manager;    // need this field because freeing memory

        VMemFreeBlock freeSpaceAfterThis;   // designates any free space that could be after this allocation.

        // THERE ALWAYS IS DATA PAST THIS STRUCT, DETERMINED BY THE dataSize FIELD.
        // But it is offset by dataOff from the start of this struct.
    };

    // Sorted-by-size AVLTree dispatcher.
    struct avlAllocSortedBySizeDispatcher
    {
        static eir::eCompResult CompareNodes( const AVLNode *left, const AVLNode *right )
        {
            const VMemFreeBlock *leftBlock = AVL_GETITEM( VMemFreeBlock, left, sortedBySizeNode );
            const VMemFreeBlock *rightBlock = AVL_GETITEM( VMemFreeBlock, right, sortedBySizeNode );

            return eir::DefaultValueCompare(
                leftBlock->freeRegion.GetSliceSize(),
                rightBlock->freeRegion.GetSliceSize()
            );
        }

        static eir::eCompResult CompareNodeWithValue( const AVLNode *left, size_t right )
        {
            const VMemFreeBlock *leftBlock = AVL_GETITEM( VMemFreeBlock, left, sortedBySizeNode );

            return eir::DefaultValueCompare(
                leftBlock->freeRegion.GetSliceSize(),
                right
            );
        }
    };
    typedef AVLTree <avlAllocSortedBySizeDispatcher> VMemAllocAVLTree;

    // Container of many allocation objects, growing infinitely.
    struct VMemIsland
    {
        // This class is placed on top of every vmem page allocation.

        inline VMemIsland( NativePageAllocator::pageHandle *allocHandle )
        {
            this->allocHandle = allocHandle;

            // Initialize the free space at its entirety.
            size_t realMemStartOffset = ( (size_t)this + sizeof(VMemIsland) );

            this->firstFreeSpaceBlock.freeRegion =
                memBlockSlice_t::fromOffsets( realMemStartOffset, allocHandle->GetTargetSlice().GetSliceEndPoint() );

            // List it into the manager.
            LIST_APPEND( this->sortedByAddrFreeBlocks.root, this->firstFreeSpaceBlock.sortedByAddrNode );

            if ( this->firstFreeSpaceBlock.freeRegion.IsEmpty() == false )
            {
                this->avlSortedBySize.Insert( &this->firstFreeSpaceBlock.sortedBySizeNode );
            }
        }

        inline ~VMemIsland( void )
        {
            // Cleaning up of memory is done by the implementation that destroys this instance.
            return;
        }

        // The alignment that is required for the header struct (VMemAllocation).
        static constexpr size_t HEADER_ALIGNMENT = sizeof(void*);

        // Used by object allocation to determine the correct bounds.
        struct alignedObjSizeByOffset
        {
            AINLINE alignedObjSizeByOffset( size_t dataSize, size_t dataAlignment )
            {
                this->dataSize = dataSize;
                this->dataAlignment = dataAlignment;
            }

            // Since this function is called every time until we found the last spot, we can
            // save state during the call that we fetch when we are done.
            AINLINE void ScanNextBlock( size_t& offsetInOut, size_t& sizeOut )
            {
                size_t offsetIn = offsetInOut;

                // We have to at least start allocation from this offset.
                size_t minStartPosForHeader = ALIGN( offsetIn, HEADER_ALIGNMENT, HEADER_ALIGNMENT );

                size_t minEndOffsetAfterHeader = ( minStartPosForHeader + sizeof(VMemAllocation) );

                // Calculate the position of our data that we should use.
                size_t dataAlignment = this->dataAlignment;

                size_t goodStartPosForData = ALIGN( minEndOffsetAfterHeader, dataAlignment, dataAlignment );

                // Calculate the real header position now.
                size_t goodStartPosForHeader = UINT_DOWNPUSH( goodStartPosForData - sizeof(VMemAllocation), HEADER_ALIGNMENT );

                // Determine the real memory size we have to allocate.
                size_t endOffsetAfterData = ( goodStartPosForData + this->dataSize );

                size_t realAllocSize = ( endOffsetAfterData - goodStartPosForHeader );

                // Return good stuff.
                offsetInOut = goodStartPosForHeader;
                sizeOut = realAllocSize;

                // Remember good meta-data.
                this->allocDataOff = ( goodStartPosForData - goodStartPosForHeader );
            }

            AINLINE size_t GetAlignment( void ) const
            {
                // Cannot really say.
                return 1;
            }

            // Meta-data for ourselves.
            size_t dataSize;
            size_t dataAlignment;

            // Data that we can fetch after successful allocation.
            size_t allocDataOff;
        };

        // Returns the size of memory actually taken by data for this island allocation.
        // This is defined by the offset of the first byte in the last free space block.
        inline size_t GetIslandUsedBytesSize( void ) const
        {
            assert( LIST_EMPTY( this->sortedByAddrFreeBlocks.root ) == false );

            VMemFreeBlock *lastFreeBlock = LIST_GETITEM( VMemFreeBlock, this->sortedByAddrFreeBlocks.root.prev, sortedByAddrNode );

            return ( lastFreeBlock->freeRegion.GetSliceStartPoint() - (size_t)this );
        }

    private:
        AINLINE bool grow_validity_region( NativeHeapAllocator *manager, VMemFreeBlock *lastFreeBlock, size_t newReqSize )
        {
            bool growSuccess = manager->nativeMemProv.SetHandleSize( this->allocHandle, newReqSize );

            if ( growSuccess )
            {
                if ( lastFreeBlock->freeRegion.IsEmpty() == false )
                {
                    this->avlSortedBySize.RemoveByNodeFast( &lastFreeBlock->sortedBySizeNode );
                }

                // Grow the available free space.
                lastFreeBlock->freeRegion.SetSliceEndPoint( this->allocHandle->GetTargetSlice().GetSliceEndPoint() );

                // Since we have grown we must have some space now.
                this->avlSortedBySize.Insert( &lastFreeBlock->sortedBySizeNode );
            }

            return growSuccess;
        }

    public:
        // Each memory island can maybe allocate new data.
        // If an island cannot allocate anymore, maybe it can later, but we
        // (almost) always can create another island!
        inline VMemAllocation* Allocate( NativeHeapAllocator *manager, size_t dataSize, size_t alignedBy )
        {
            void *vmemPtr = (void*)this;
            size_t vmemOffset = (size_t)vmemPtr;

            // It only makes sense to pick alignedBy as power-of-two value.
            // But we allow other values too I guess? Maybe we will do unit tests for those aswell to stress test things?

            void *allocPtr;
            memBlockSlice_t allocSlice;
            VMemFreeBlock *freeBlockToAllocateInto;

            alignedObjSizeByOffset posDispatch( dataSize, alignedBy );

            // Try to find a spot between existing data.
            {
                // Scan for the free block whose size is equal or just above the data size with the meta-data block.
                // This is the best-estimate beginning of the allocatable free regions.
                // The scan is logarithmic time so a really great idea.
                AVLNode *firstAllocatable = this->avlSortedBySize.GetJustAboveOrEqualNode( dataSize + sizeof(VMemAllocation) );

                VMemAllocAVLTree::diff_node_iterator iter( firstAllocatable );

                while ( !iter.IsEnd() )
                {
                    // We have to check each member of the nodestack of the current best-fit node because allocation could
                    // fail due to misalignment. But since we have the best-fit node going for good alignment usage
                    // is something the user wants: do not worry!

                    VMemAllocAVLTree::nodestack_iterator nodestack_iter( iter.Resolve() );

                    while ( !nodestack_iter.IsEnd() )
                    {
                        VMemFreeBlock *smallFreeBlock = AVL_GETITEM( VMemFreeBlock, nodestack_iter.Resolve(), sortedBySizeNode );

                        // Try to allocate into it.
                        // It succeeds if the allocation does fit into the free region.
                        size_t reqSize;
                        size_t allocOff = smallFreeBlock->freeRegion.GetSliceStartPoint();

                        posDispatch.ScanNextBlock( allocOff, reqSize );

                        memBlockSlice_t requiredMemRegion( allocOff, reqSize );

                        eir::eIntersectionResult intResult = requiredMemRegion.intersectWith( smallFreeBlock->freeRegion );

                        if ( intResult == eir::INTERSECT_INSIDE ||
                             intResult == eir::INTERSECT_EQUAL )
                        {
                            // We found a valid allocation slot!
                            // So return it.
                            allocPtr = (void*)allocOff;
                            allocSlice = requiredMemRegion;
                            freeBlockToAllocateInto = smallFreeBlock;
                            goto foundAllocationSpot;
                        }

                        // Try the next same-size freeblock.
                        nodestack_iter.Increment();
                    }

                    // Try the next bigger block.
                    iter.Increment();
                }
            }

            // Try to make space for a new thing by growing the validity region.
            {
                // We have to calculate the end offset of the data that we need.
                // The next position to allocate at is after all valid data.
                assert( LIST_EMPTY( this->sortedByAddrFreeBlocks.root ) == false );

                VMemFreeBlock *lastFreeBlock = LIST_GETITEM( VMemFreeBlock, this->sortedByAddrFreeBlocks.root.prev, sortedByAddrNode );
                size_t tryNewMemOffset = lastFreeBlock->freeRegion.GetSliceStartPoint();
                size_t realAllocSize;

                posDispatch.ScanNextBlock( tryNewMemOffset, realAllocSize );

                size_t finalMemEndOffset = ( tryNewMemOffset + realAllocSize );

                // Calculate the required new virtual memory size.
                size_t newReqSize = ( finalMemEndOffset - vmemOffset );

                bool growSuccess = grow_validity_region( manager, lastFreeBlock, newReqSize );

                if ( growSuccess )
                {
                    // Just return the new spot.
                    // We will insert to the end node.
                    allocPtr = (void*)tryNewMemOffset;
                    allocSlice = memBlockSlice_t( tryNewMemOffset, realAllocSize );
                    freeBlockToAllocateInto = lastFreeBlock;
                    goto foundAllocationSpot;
                }
            }

            // Could not allocate on this island, at least.
            // Maybe try another island or a new one.
            return nullptr;

        foundAllocationSpot:
            // Since allocation succeeded we can fetch meta-data from the position dispatcher.
            size_t allocDataOff = posDispatch.allocDataOff;

            VMemAllocation *newAlloc = new (allocPtr) VMemAllocation( this, dataSize, allocDataOff );

            // Subtract our allocation from the free region we have found and newly manage the things.
            bool hadSomethingStartFromLeft = false;
            bool hadFreeSpaceAfterNewAlloc = false;

            // Update the pointery things.
            LIST_INSERT( freeBlockToAllocateInto->sortedByAddrNode, newAlloc->freeSpaceAfterThis.sortedByAddrNode );

            // Update the region sizes.

            // It cannot be empty because something just got allocated into it.
            assert( freeBlockToAllocateInto->freeRegion.IsEmpty() == false );

            // When updating AVLTree node values we must remove the nodes (temporarily).
            avlSortedBySize.RemoveByNodeFast( &freeBlockToAllocateInto->sortedBySizeNode );

            freeBlockToAllocateInto->freeRegion.subtractRegion( allocSlice,
                [&]( const memBlockSlice_t& slicedRegion, bool isStartingFromLeft )
            {
                if ( isStartingFromLeft )
                {
                    hadSomethingStartFromLeft = true;

                    // Update the new free region.
                    freeBlockToAllocateInto->freeRegion = slicedRegion;
                    avlSortedBySize.Insert( &freeBlockToAllocateInto->sortedBySizeNode );
                }
                else
                {
                    // It is important that we keep the pointers inside of free region intact,
                    // so even if it is empty we know where it is supposed to start.
                    hadFreeSpaceAfterNewAlloc = true;

                    // This has to be the memory that is available just after our allocation.
                    newAlloc->freeSpaceAfterThis.freeRegion = slicedRegion;
                    avlSortedBySize.Insert( &newAlloc->freeSpaceAfterThis.sortedBySizeNode );
                }
            });

            if ( !hadSomethingStartFromLeft )
            {
                // We have subtracted the left free block entirely, so keep it removed.
                freeBlockToAllocateInto->freeRegion.collapse();
            }

            if ( !hadFreeSpaceAfterNewAlloc )
            {
                // Make proper empty space.
                newAlloc->freeSpaceAfterThis.freeRegion = memBlockSlice_t( allocSlice.GetSliceEndPoint() + 1, 0 );
            }

            return newAlloc;
        }

    private:
        AINLINE bool is_last_node( VMemAllocation *allocObj )
        {
            return ( this->sortedByAddrFreeBlocks.root.prev == &allocObj->freeSpaceAfterThis.sortedByAddrNode );
        }

        AINLINE void truncate_to_minimum_space( NativeHeapAllocator *manager, VMemFreeBlock *lastFreeBlock )
        {
            // WARNING: we assume that lastFreeBlock IS NOT INSIDE THE AVL TREE.

            // Make sure we at least have the minimum size.
            size_t pageSize = manager->nativeMemProv.GetPageSize();

            size_t minSizeByPage = ( pageSize * MIN_PAGES_FOR_ISLAND );

            size_t actualReqSize = minSizeByPage;

            // Minimum size by span.
            {
                size_t vmemOff = (size_t)this;

                size_t newReqSize_local = ( lastFreeBlock->freeRegion.GetSliceStartPoint() - vmemOff );

                if ( newReqSize_local > actualReqSize )
                {
                    actualReqSize = newReqSize_local;
                }
            }

            bool gotToShrink = manager->nativeMemProv.SetHandleSize( this->allocHandle, actualReqSize );

            assert( gotToShrink == true );

            // Update the region of free space for the last block.
            lastFreeBlock->freeRegion.SetSliceEndPoint( this->allocHandle->GetTargetSlice().GetSliceEndPoint() );
        }

    public:
        inline void Free( NativeHeapAllocator *manager, VMemAllocation *allocObj )
        {
            bool isLastNode = is_last_node( allocObj );

            // We simply release out the memory that we are asked to free.
            VMemFreeBlock *potLastFreeBlock = nullptr;
            {
                size_t newFreeEndOffset = allocObj->freeSpaceAfterThis.freeRegion.GetSliceEndPoint();

                RwListEntry <VMemFreeBlock> *nodePrevFreeBlock = allocObj->freeSpaceAfterThis.sortedByAddrNode.prev;

                // Has to be because there is a first free block, always.
                assert( nodePrevFreeBlock != &this->sortedByAddrFreeBlocks.root );

                VMemFreeBlock *prevFreeBlock = LIST_GETITEM( VMemFreeBlock, nodePrevFreeBlock, sortedByAddrNode );

                // When updating the size we must remove from the tree.
                if ( prevFreeBlock->freeRegion.IsEmpty() == false )
                {
                    this->avlSortedBySize.RemoveByNodeFast( &prevFreeBlock->sortedBySizeNode );
                }

                prevFreeBlock->freeRegion.SetSliceEndPoint( newFreeEndOffset );

                // If we deleted the last block, then the previous one becomes the new last.
                potLastFreeBlock = prevFreeBlock;
            }

            // Kill the current last node, with the free block.
            if ( allocObj->freeSpaceAfterThis.freeRegion.IsEmpty() == false )
            {
                this->avlSortedBySize.RemoveByNodeFast( &allocObj->freeSpaceAfterThis.sortedBySizeNode );
            }

            LIST_REMOVE( allocObj->freeSpaceAfterThis.sortedByAddrNode );

            allocObj->~VMemAllocation();

            // If we got rid of the last allocation, then we should attempt to shrink
            // the required memory region to best-fit.
            if ( isLastNode )
            {
                VMemFreeBlock *lastFreeBlock = potLastFreeBlock;

                truncate_to_minimum_space( manager, lastFreeBlock );
            }

            // Kinda has to have a size now (?).
            if ( potLastFreeBlock->freeRegion.IsEmpty() == false )
            {
                this->avlSortedBySize.Insert( &potLastFreeBlock->sortedBySizeNode );
            }
        }

        inline bool ResizeAllocation( NativeHeapAllocator *manager, VMemAllocation *memHandle, void *memPtr, size_t newSize )
        {
            if ( newSize == 0 )
                return false;

            // We do not have to update anything, so bail.
            size_t oldDataSize = memHandle->dataSize;

            if ( oldDataSize == newSize )
                return true;

            bool isGrowingAlloc = ( oldDataSize < newSize );

            // If we are the last allocation we can either shrink or grow the allocation depending on the
            // requested size.
            bool isLastNode = is_last_node( memHandle );

            // Since we know the free space after the memory handle, we can simply grow or shrink without issue.
            // The operation takes logarithmic time though, because we update the AVL tree.

            size_t startOfDataOffset = (size_t)memPtr;

            size_t newRequestedStartOfFreeBytes = ( startOfDataOffset + newSize );

            // Get the offset to the byte that is last of the available (possible) free space.
            size_t endOfFreeSpaceOffset = memHandle->freeSpaceAfterThis.freeRegion.GetSliceEndPoint();

            // If this is not a valid offset for the free bytes, we bail.
            // We add 1 because it could become empty aswell.
            // (I guess this could only be triggered if we grow memory?)
            if ( endOfFreeSpaceOffset + 1 < newRequestedStartOfFreeBytes )
            {
                // If we are the last node we could actually try to grow the island.
                if ( !isLastNode )
                {
                    return false;
                }

                assert( isGrowingAlloc );

                size_t requiredMemSize = ( newRequestedStartOfFreeBytes - (size_t)this );

                bool couldGrow = grow_validity_region( manager, &memHandle->freeSpaceAfterThis, requiredMemSize );

                if ( !couldGrow )
                {
                    // We absolutely fail.
                    return false;
                }

                // Second wind! We got more space.
            }

            // Update the meta-data.
            if ( memHandle->freeSpaceAfterThis.freeRegion.IsEmpty() == false )
            {
                this->avlSortedBySize.RemoveByNodeFast( &memHandle->freeSpaceAfterThis.sortedBySizeNode );
            }

            memHandle->freeSpaceAfterThis.freeRegion.SetSliceStartPoint( newRequestedStartOfFreeBytes );
            memHandle->dataSize = newSize;

            // If we are actually shrinking the allocation, we should try to truncate the virtual memory to the minimum required.
            if ( isLastNode && !isGrowingAlloc )
            {
                truncate_to_minimum_space( manager, &memHandle->freeSpaceAfterThis );
            }

            // Insert the new thing again.
            if ( memHandle->freeSpaceAfterThis.freeRegion.IsEmpty() == false )
            {
                this->avlSortedBySize.Insert( &memHandle->freeSpaceAfterThis.sortedBySizeNode );
            }

            return true;
        }

        inline bool HasNoAllocations( void ) const
        {
            // If there is just the first free space block, then there cannot be any allocation either.
            return ( this->firstFreeSpaceBlock.sortedByAddrNode.next == &this->sortedByAddrFreeBlocks.root );
        }

        // Returns statistics about usage of this memory island.
        struct usageStats
        {
            size_t usedBytes = 0;
            size_t usedMetaBytes = 0;
            size_t freeBytes = 0;
            size_t countOfAllocations = 0;
        };

        inline usageStats GetUsageStatistics( void ) const
        {
            usageStats stats;

            // Have to take the header bytes of each island as meta-bytes into account.
            // Just saying that having too many islands is not the best idea.
            stats.usedMetaBytes += sizeof( VMemIsland );

            LIST_FOREACH_BEGIN( VMemFreeBlock, this->sortedByAddrFreeBlocks.root, sortedByAddrNode )

                // If we have an allocation associated with this free block, add up the data bytes.
                if ( item != &this->firstFreeSpaceBlock )
                {
                    VMemAllocation *allocObj = LIST_GETITEM( VMemAllocation, item, freeSpaceAfterThis );

                    size_t dataSize = allocObj->dataSize;

                    stats.usedBytes += dataSize;
                    stats.usedMetaBytes += ( dataSize + allocObj->dataOff );

                    // We have one more allocation.
                    stats.countOfAllocations++;
                }

                // Count the free bytes aswell.
                stats.freeBytes += item->freeRegion.GetSliceSize();

            LIST_FOREACH_END

            return stats;
        }

        // Walks all memory allocations of this island in memory-address order.
        template <typename callbackType>
        AINLINE void WalkAllocations( const callbackType& cb )
        {
            LIST_FOREACH_BEGIN( VMemFreeBlock, this->sortedByAddrFreeBlocks.root, sortedByAddrNode )

                if ( item != &this->firstFreeSpaceBlock )
                {
                    VMemAllocation *allocObj = LIST_GETITEM( VMemAllocation, item, freeSpaceAfterThis );

                    cb( allocObj );
                }

            LIST_FOREACH_END
        }

        RwListEntry <VMemIsland> managerNode;

        NativePageAllocator::pageHandle *allocHandle;   // handle into the NativePageAllocator for meta-info

        VMemFreeBlock firstFreeSpaceBlock;      // describes the amount of memory free before any allocation.
        RwList <VMemFreeBlock> sortedByAddrFreeBlocks;
        VMemAllocAVLTree avlSortedBySize;
    };

    RwList <VMemIsland> listIslands;
};

#endif //_NATIVE_VIRTUAL_MEMORY_HEAP_ALLOCATOR_
