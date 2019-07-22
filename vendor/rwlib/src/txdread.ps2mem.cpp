#include "StdInc.h"

#ifdef RWLIB_INCLUDE_NATIVETEX_PLAYSTATION2

#include "txdread.ps2.hxx"

#include "txdread.ps2gsman.hxx"

namespace rw
{

uint32 NativeTexturePS2::calculateGPUDataSize(
    const uint32 mipmapBasePointer[], const uint32 mipmapMemorySize[], uint32 mipmapMax,
    eMemoryLayoutType memLayoutType,
    uint32 clutBasePointer, uint32 clutMemSize
) const
{
    size_t numMipMaps = this->mipmaps.GetCount();

    if ( numMipMaps == 0 )
        return 0;

    // Calculate the maximum memory offset required.
    uint32 maxMemOffset = 0;

    for ( size_t n = 0; n < numMipMaps; n++ )
    {
        uint32 thisOffset = ( mipmapBasePointer[n] + mipmapMemorySize[n] );

        if ( maxMemOffset < thisOffset )
        {
            maxMemOffset = thisOffset;
        }
    }

    // Include CLUT.
    {
        uint32 thisOffset = ( clutBasePointer + clutMemSize );

        if ( maxMemOffset < thisOffset )
        {
            maxMemOffset = thisOffset;
        }
    }

    uint32 textureMemoryDataSize = ( maxMemOffset * 64 );

    return ALIGN_SIZE( textureMemoryDataSize, 2048u );
}

eFormatEncodingType NativeTexturePS2::getHardwareRequiredEncoding(LibraryVersion version) const
{
    eFormatEncodingType imageEncodingType = FORMAT_UNKNOWN;

    eRasterFormat rasterFormat = this->rasterFormat;
    ePaletteType paletteType = this->paletteType;

    if ( paletteType != PALETTE_NONE )
    {
        if (paletteType == PALETTE_4BIT)
        {
            if (version.rwLibMinor < 3)
            {
                imageEncodingType = FORMAT_IDTEX8_COMPRESSED;
            }
            else
            {
                if (this->requiresHeaders || this->hasSwizzle)
                {
                    imageEncodingType = FORMAT_TEX32;
                }
                else
                {
                    imageEncodingType = FORMAT_IDTEX8_COMPRESSED;
                }
            }
        }
        else if (paletteType == PALETTE_8BIT)
        {
            if (this->requiresHeaders || this->hasSwizzle)
            {
                imageEncodingType = FORMAT_TEX32;
            }
            else
            {
                imageEncodingType = FORMAT_IDTEX8;
            }
        }
        else
        {
            throw RwException( "invalid palette type in PS2 hardware swizzle detection" );
        }
    }
    else
    {
        if (rasterFormat == RASTER_LUM)
        {
            // We assume we are 8bit LUM here.
            imageEncodingType = FORMAT_IDTEX8;
        }
        else if (rasterFormat == RASTER_1555 || rasterFormat == RASTER_555 || rasterFormat == RASTER_565 ||
                 rasterFormat == RASTER_4444 || rasterFormat == RASTER_16)
        {
            imageEncodingType = FORMAT_TEX16;
        }
        else if (rasterFormat == RASTER_8888 || rasterFormat == RASTER_888 || rasterFormat == RASTER_32)
        {
            imageEncodingType = FORMAT_TEX32;
        }
    }

    return imageEncodingType;
}

struct ps2GSMemoryLayoutManager
{
    typedef sliceOfData <uint32> memUnitSlice_t;

    struct MemoryRectBase
    {
        typedef memUnitSlice_t side_t;

        side_t x_slice, y_slice;

        inline MemoryRectBase( uint32 blockX, uint32 blockY, uint32 blockWidth, uint32 blockHeight )
            : x_slice( blockX, blockWidth ), y_slice( blockY, blockHeight )
        {
            return;
        }

        inline bool IsColliding( const MemoryRectBase *right ) const
        {
            eir::eIntersectionResult x_result =
                this->x_slice.intersectWith( right->x_slice );

            eir::eIntersectionResult y_result =
                this->y_slice.intersectWith( right->y_slice );

            return ( !eir::isFloatingIntersect( x_result ) && !eir::isFloatingIntersect( y_result ) );
        }

        inline MemoryRectBase SubRect( const MemoryRectBase *right ) const
        {
            uint32 maxStartX =
                std::max( this->x_slice.GetSliceStartPoint(), right->x_slice.GetSliceStartPoint() );
            uint32 maxStartY =
                std::max( this->y_slice.GetSliceStartPoint(), right->y_slice.GetSliceStartPoint() );

            uint32 minEndX =
                std::min( this->x_slice.GetSliceEndPoint(), right->x_slice.GetSliceEndPoint() );
            uint32 minEndY =
                std::min( this->y_slice.GetSliceEndPoint(), right->y_slice.GetSliceEndPoint() );

            MemoryRectBase subRect(
                maxStartX,
                maxStartY,
                minEndX - maxStartX + 1,
                minEndY - maxStartY + 1
            );

            return subRect;
        }

        inline bool HasSpace( void ) const
        {
            return
                this->x_slice.GetSliceSize() > 0 &&
                this->y_slice.GetSliceSize() > 0;
        }
    };

    struct MemoryRectangle : public MemoryRectBase
    {
        inline MemoryRectangle( uint32 blockX, uint32 blockY, uint32 blockWidth, uint32 blockHeight )
            : MemoryRectBase( blockX, blockY, blockWidth, blockHeight )
        {
            return;
        }

        RwListEntry <MemoryRectangle> node;
    };

    struct VirtualMemoryPage
    {
        inline VirtualMemoryPage( Interface *engineInterface )
        {
            this->engineInterface = engineInterface;
        }

        inline ~VirtualMemoryPage( void )
        {
            Interface *engineInterface = this->engineInterface;

            RwDynMemAllocator memAlloc( engineInterface );

            LIST_FOREACH_BEGIN( MemoryRectangle, allocatedRects.root, node )

                eir::dyn_del_struct( memAlloc, nullptr, item );

            LIST_FOREACH_END

            LIST_CLEAR( allocatedRects.root );
        }

        inline bool IsColliding( const MemoryRectBase *theRect ) const
        {
            LIST_FOREACH_BEGIN( MemoryRectangle, this->allocatedRects.root, node )

                if ( item->IsColliding( theRect ) == true )
                {
                    // There is a collision, so this rectangle is invalid.
                    // Try another position.
                    return true;
                }

            LIST_FOREACH_END

            return false;
        }

        Interface *engineInterface;

        // has a constant blockWidth and blockHeight same for every virtual page with same memLayout.
        // has a constant blocksPerWidth and blocksPerHeight same for every virtual page with same memLayout.
        eMemoryLayoutType memLayout;

        RwList <MemoryRectangle> allocatedRects;

        RwListEntry <VirtualMemoryPage> node;
    };

    struct MemoryPage
    {
        inline MemoryPage( Interface *engineInterface )
        {
            this->engineInterface = engineInterface;
        }

        inline ~MemoryPage( void )
        {
            Interface *engineInterface = this->engineInterface;

            RwDynMemAllocator memAlloc( engineInterface );

            LIST_FOREACH_BEGIN( VirtualMemoryPage, vmemList.root, node )

                eir::dyn_del_struct <VirtualMemoryPage> ( memAlloc, nullptr, item );

            LIST_FOREACH_END

            LIST_CLEAR( vmemList.root );
        }

        Interface *engineInterface;

        RwList <VirtualMemoryPage> vmemList;

        RwListEntry <MemoryPage> node;

        inline VirtualMemoryPage* GetVirtualMemoryLayout( eMemoryLayoutType layoutType )
        {
            LIST_FOREACH_BEGIN( VirtualMemoryPage, vmemList.root, node )

                if ( item->memLayout == layoutType )
                {
                    return item;
                }

            LIST_FOREACH_END

            return nullptr;
        }

        inline VirtualMemoryPage* AllocateVirtualMemoryLayout( eMemoryLayoutType layoutType )
        {
            Interface *engineInterface = this->engineInterface;

            RwDynMemAllocator memAlloc( engineInterface );

            VirtualMemoryPage *newPage = eir::dyn_new_struct <VirtualMemoryPage> ( memAlloc, nullptr, engineInterface );

            newPage->memLayout = layoutType;

            LIST_APPEND( this->vmemList.root, newPage->node );

            return newPage;
        }
    };

    Interface *engineInterface;

    RwList <MemoryPage> pages;

    inline ps2GSMemoryLayoutManager( Interface *engineInterface )
    {
        this->engineInterface = engineInterface;
        this->bufferAllocationPageWidth = 0;
    }

    inline ~ps2GSMemoryLayoutManager( void )
    {
        RwDynMemAllocator memAlloc( this->engineInterface );

        LIST_FOREACH_BEGIN( MemoryPage, pages.root, node )

            eir::dyn_del_struct <MemoryPage> ( memAlloc, nullptr, item );

        LIST_FOREACH_END

        LIST_CLEAR( pages.root );
    }

    // Memory management constants of the PS2 Graphics Synthesizer.
    static const uint32 gsColumnSize = 16 * sizeof(uint32);
    static const uint32 gsBlockSize = gsColumnSize * 4;
    static const uint32 gsPageSize = gsBlockSize * 32;

    struct memoryLayoutProperties_t
    {
        uint32 pixelWidthPerBlock, pixelHeightPerBlock;
        uint32 widthBlocksPerPage, heightBlocksPerPage;

        const uint32 *const* blockArrangement;

        memUnitSlice_t pageDimX, pageDimY;
    };

    uint32 bufferAllocationPageWidth;

    inline void SetBufferPageWidth( uint32 width )
    {
        this->bufferAllocationPageWidth = width;
    }

    inline static void getMemoryLayoutProperties(eMemoryLayoutType memLayout, eFormatEncodingType encodingType, memoryLayoutProperties_t& layoutProps)
    {
        uint32 pixelWidthPerColumn = 0;
        uint32 pixelHeightPerColumn = 0;

        // For safety.
        layoutProps.blockArrangement = nullptr;

        if ( memLayout == PSMT4 && encodingType == FORMAT_IDTEX4 )
        {
            pixelWidthPerColumn = 32;
            pixelHeightPerColumn = 4;

            layoutProps.widthBlocksPerPage = 4;
            layoutProps.heightBlocksPerPage = 8;

            layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmt4;
        }
        else if ( memLayout == PSMT4 && encodingType == FORMAT_IDTEX8_COMPRESSED )
        {
            // TODO: fix this.
            pixelWidthPerColumn = 32;
            pixelHeightPerColumn = 4;

            layoutProps.widthBlocksPerPage = 4;
            layoutProps.heightBlocksPerPage = 8;

            layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmt4;
        }
        else if ( memLayout == PSMT8 )
        {
            pixelWidthPerColumn = 16;
            pixelHeightPerColumn = 4;

            layoutProps.widthBlocksPerPage = 8;
            layoutProps.heightBlocksPerPage = 4;

            layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmt8;
        }
        else if ( memLayout == PSMCT32 || memLayout == PSMCT24 ||
                  memLayout == PSMZ32 || memLayout == PSMZ24 )
        {
            pixelWidthPerColumn = 8;
            pixelHeightPerColumn = 2;

            layoutProps.widthBlocksPerPage = 8;
            layoutProps.heightBlocksPerPage = 4;

            if ( memLayout == PSMCT32 || memLayout == PSMCT24 )
            {
                layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmct32;
            }
            else if ( memLayout == PSMZ32 || memLayout == PSMZ24 )
            {
                layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmz32;
            }
        }
        else if ( memLayout == PSMCT16 || memLayout == PSMCT16S ||
                  memLayout == PSMZ16 || memLayout == PSMZ16S )
        {
            pixelWidthPerColumn = 16;
            pixelHeightPerColumn = 2;

            layoutProps.widthBlocksPerPage = 4;
            layoutProps.heightBlocksPerPage = 8;

            if ( memLayout == PSMCT16 )
            {
                layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmct16;
            }
            else if ( memLayout == PSMCT16S )
            {
                layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmct16s;
            }
            else if ( memLayout == PSMZ16 )
            {
                layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmz16;
            }
            else if ( memLayout == PSMZ16S )
            {
                layoutProps.blockArrangement = (const uint32*const*)ps2GSMemoryLayoutArrangements::psmz16s;
            }
        }
        else
        {
            // TODO.
            assert( 0 );
        }

        // Expand to block dimensions.
        layoutProps.pixelWidthPerBlock = pixelWidthPerColumn;
        layoutProps.pixelHeightPerBlock = pixelHeightPerColumn * 4;

        // Set up the page dimensions.
        layoutProps.pageDimX = memUnitSlice_t( 0, layoutProps.widthBlocksPerPage );
        layoutProps.pageDimY = memUnitSlice_t( 0, layoutProps.heightBlocksPerPage );
    }

    inline MemoryPage* GetPage( uint32 pageIndex )
    {
        uint32 n = 0;

        // Try to fetch an existing page.
        LIST_FOREACH_BEGIN( MemoryPage, pages.root, node )

            if ( n++ == pageIndex )
            {
                return item;
            }

        LIST_FOREACH_END

        // Allocate missing pages.
        MemoryPage *allocPage = nullptr;

        Interface *engineInterface = this->engineInterface;

        RwDynMemAllocator memAlloc( engineInterface );

        while ( n++ <= pageIndex )
        {
            allocPage = eir::dyn_new_struct <MemoryPage> ( memAlloc, nullptr, engineInterface );

            LIST_APPEND( pages.root, allocPage->node );
        }

        return allocPage;
    }

    inline static uint32 getTextureBasePointer(const memoryLayoutProperties_t& layoutProps, uint32 pageX, uint32 pageY, uint32 bufferWidth, uint32 blockOffsetX, uint32 blockOffsetY)
    {
        // Get block index from the dimensional coordinates.
        // This requires a dispatch according to the memory layout.
        uint32 blockIndex = 0;
        {
            const uint32 *const *blockArrangement = layoutProps.blockArrangement;

            const uint32 *row = (const uint32*)( (const uint32*)blockArrangement + blockOffsetY * layoutProps.widthBlocksPerPage );

            blockIndex = row[ blockOffsetX ];
        }

        // Allocate the texture at the current position in the buffer.
        uint32 pageIndex = ( pageY * bufferWidth + pageX );

        return ( pageIndex * 32 + blockIndex );
    }

    struct memoryCollider
    {
        ps2GSMemoryLayoutManager *manager;

        const memoryLayoutProperties_t& layoutProps;
        eMemoryLayoutType memLayoutType;
        uint32 blockWidth, blockHeight;
        uint32 texelPageWidth, texelPageHeight;
        uint32 pageMaxBlockWidth, pageMaxBlockHeight;
        uint32 allocPageWidth;

        inline memoryCollider(
            ps2GSMemoryLayoutManager *manager,
            eMemoryLayoutType memLayoutType,
            const memoryLayoutProperties_t& layoutProps,
            uint32 blockWidth, uint32 blockHeight,
            uint32 allocPageWidth
        ) : layoutProps( layoutProps )
        {
            this->manager = manager;

            this->memLayoutType = memLayoutType;

            this->blockWidth = blockWidth;
            this->blockHeight = blockHeight;

            this->allocPageWidth = allocPageWidth;

            // Get the width in pages.
            this->pageMaxBlockWidth = ALIGN_SIZE( blockWidth, layoutProps.widthBlocksPerPage );

            this->texelPageWidth = this->pageMaxBlockWidth / layoutProps.widthBlocksPerPage;

            // Get the height in pages.
            this->pageMaxBlockHeight = ALIGN_SIZE( blockHeight, layoutProps.heightBlocksPerPage );

            this->texelPageHeight = this->pageMaxBlockHeight / layoutProps.heightBlocksPerPage;
        }

        inline bool testCollision(uint32 pageX, uint32 pageY, uint32 blockOffX, uint32 blockOffY)
        {
            // Construct a rectangle that matches our request.
            MemoryRectBase actualRect(
                pageX * layoutProps.widthBlocksPerPage + pageY * ( this->allocPageWidth * layoutProps.widthBlocksPerPage ) + blockOffX,
                blockOffY,
                this->blockWidth,
                this->blockHeight
            );

            bool hasFoundCollision = false;

            for ( uint32 y = 0; y < this->texelPageHeight; y++ )
            {
                for ( uint32 x = 0; x < this->texelPageWidth; x++ )
                {
                    uint32 real_x = ( x + pageX );
                    uint32 real_y = ( y + pageY );

                    // Calculate the real index of this page.
                    uint32 pageIndex = ( this->allocPageWidth * real_y + real_x );

                    MemoryPage *thePage = manager->GetPage( pageIndex );

                    // Collide our page rect with the contents of this page.
                    VirtualMemoryPage *vmemLayout = thePage->GetVirtualMemoryLayout( memLayoutType );

                    if ( vmemLayout )
                    {
                        bool isCollided = vmemLayout->IsColliding( &actualRect );

                        if ( isCollided )
                        {
                            hasFoundCollision = true;
                            break;
                        }
                    }
                }

                if ( hasFoundCollision )
                {
                    break;
                }
            }

            return hasFoundCollision;
        }
    };

    static const bool _allocateAwayFromBaseline = false;

    inline bool findAllocationRegion(
        eMemoryLayoutType memLayoutType,
        uint32 texelBlockWidth, uint32 texelBlockHeight,
        uint32 bufferPageWidth, const memoryLayoutProperties_t& layoutProps,
        uint32& pageX_out, uint32& pageY_out, uint32& blockX_out, uint32& blockY_out
    )
    {
        // Loop through all pages and try to find the correct placement for the new texture.
        uint32 pageX = 0;
        uint32 pageY = 0;
        uint32 blockOffsetX = 0;
        uint32 blockOffsetY = 0;

        bool validAllocation = false;

        memoryCollider memCollide(
            this, memLayoutType,
            layoutProps,
            texelBlockWidth, texelBlockHeight,
            bufferPageWidth
        );

        uint32 layoutStartX = layoutProps.pageDimX.GetSliceStartPoint();
        uint32 layoutStartY = layoutProps.pageDimY.GetSliceStartPoint();

        while ( true )
        {
            bool allocationSuccessful = false;

            // Try to allocate on the memory plane.
            {
                bool performBlockMovement = ( memCollide.texelPageWidth == 1 && memCollide.texelPageHeight == 1 );

                bool canAllocateOnPage = true;

                MemoryRectBase thisRect(
                    layoutStartX,
                    layoutStartY,
                    texelBlockWidth,
                    texelBlockHeight
                );

                // We have to assume that we cannot allocate on this page.
                canAllocateOnPage = false;

                while ( true )
                {
                    // Make sure we are not outside of the page dimensions.
                    if ( performBlockMovement )
                    {
                        eir::eIntersectionResult x_result =
                            thisRect.x_slice.intersectWith( layoutProps.pageDimX );

                        if ( x_result != eir::INTERSECT_INSIDE && x_result != eir::INTERSECT_EQUAL )
                        {
                            // Advance to next line.
                            thisRect.x_slice.SetSlicePosition( layoutStartX );
                            thisRect.y_slice.OffsetSliceBy( 1 );
                        }

                        eir::eIntersectionResult y_result =
                            thisRect.y_slice.intersectWith( layoutProps.pageDimY );

                        if ( y_result != eir::INTERSECT_INSIDE && y_result != eir::INTERSECT_EQUAL )
                        {
                            // This page is not it.
                            break;
                        }
                    }

                    bool foundFreeSpot =
                        ( memCollide.testCollision(pageX, pageY, thisRect.x_slice.GetSliceStartPoint(), thisRect.y_slice.GetSliceStartPoint()) == false );

                    // If there are no conflicts on our page, we can allocate on it.
                    if ( foundFreeSpot == true )
                    {
                        blockOffsetX = thisRect.x_slice.GetSliceStartPoint();
                        blockOffsetY = thisRect.y_slice.GetSliceStartPoint();

                        canAllocateOnPage = true;
                        break;
                    }

                    if ( performBlockMovement )
                    {
                        // We need to advance our position.
                        thisRect.x_slice.OffsetSliceBy( 1 );
                    }
                    else
                    {
                        break;
                    }
                }

                // If we can allocate on this page, then we succeeded!
                if ( canAllocateOnPage == true )
                {
                    allocationSuccessful = true;
                }
            }

            // If the allocation has been successful, break.
            if ( allocationSuccessful )
            {
                validAllocation = true;
                break;
            }

            if ( _allocateAwayFromBaseline )
            {
                // We need to try from the next page.
                pageX++;

                // If the page is the limit, then restart and go to next line.
                if ( pageX == bufferPageWidth )
                {
                    pageX = 0;

                    pageY++;
                }
            }
            else
            {
                // We only allocate on the baseline.
                pageY++;
            }
        }

        if ( validAllocation )
        {
            pageX_out = pageX;
            pageY_out = pageY;
            blockX_out = blockOffsetX;
            blockY_out = blockOffsetY;
        }

        return validAllocation;
    }

    inline static uint32 calculateTextureMemSize(
        const memoryLayoutProperties_t& layoutProps,
        uint32 texBasePointer,
        uint32 pageX, uint32 pageY, uint32 bufferPageWidth,
        uint32 blockOffsetX, uint32 blockOffsetY, uint32 blockWidth, uint32 blockHeight
    )
    {
        uint32 texelBlockWidthOffset = ( blockWidth - 1 ) + blockOffsetX;
        uint32 texelBlockHeightOffset = ( blockHeight - 1 ) + blockOffsetY;

        uint32 finalPageX = pageX + texelBlockWidthOffset / layoutProps.widthBlocksPerPage;
        uint32 finalPageY = pageY + texelBlockHeightOffset / layoutProps.heightBlocksPerPage;

        uint32 finalBlockOffsetX = texelBlockWidthOffset % layoutProps.widthBlocksPerPage;
        uint32 finalBlockOffsetY = texelBlockHeightOffset % layoutProps.heightBlocksPerPage;

        uint32 texEndOffset =
            getTextureBasePointer(layoutProps, finalPageX, finalPageY, bufferPageWidth, finalBlockOffsetX, finalBlockOffsetY);

        return ( texEndOffset - texBasePointer ) + 1; //+1 because its a size
    }

    inline void addAllocationPresence(
        const memoryLayoutProperties_t& layoutProps, eMemoryLayoutType memLayoutType,
        uint32 bufferPageWidth,
        uint32 pageX, uint32 pageY, uint32 pageWidth, uint32 pageHeight,
        uint32 totalBlockOffX, uint32 totalBlockOffY, uint32 blockWidth, uint32 blockHeight
    )
    {
        //uint32 pageMaxBlockWidth = ( pageWidth * layoutProps.widthBlocksPerPage );

        // Add our collision rectangles onto the pages we allocated.
        MemoryRectBase pageAllocArea(
            totalBlockOffX,
            totalBlockOffY,
            blockWidth,
            blockHeight
        );

        for ( uint32 allocPageY = 0; allocPageY < pageHeight; allocPageY++ )
        {
            for ( uint32 allocPageX = 0; allocPageX < pageWidth; allocPageX++ )
            {
                uint32 realPageX = ( allocPageX + pageX );
                uint32 realPageY = ( allocPageY + pageY );

                uint32 pageBlockOffX =
                    layoutProps.pageDimX.GetSliceStartPoint() + realPageX * layoutProps.widthBlocksPerPage;
                uint32 pageBlockOffY =
                    layoutProps.pageDimY.GetSliceStartPoint() + realPageY * layoutProps.heightBlocksPerPage;

                MemoryRectBase pageZone(
                    pageBlockOffX,
                    pageBlockOffY,
                    layoutProps.widthBlocksPerPage,
                    layoutProps.heightBlocksPerPage
                );

                MemoryRectBase subRectAllocZone = pageZone.SubRect( &pageAllocArea );

                // If there is a zone to include, we do that.
                if ( subRectAllocZone.HasSpace() )
                {
                    // Transform the subrect onto a linear zone.
                    uint32 blockLocalX =
                        subRectAllocZone.x_slice.GetSliceStartPoint() - pageBlockOffX;
                    uint32 blockLocalY =
                        subRectAllocZone.y_slice.GetSliceStartPoint() - pageBlockOffY;

                    uint32 pageIndex = ( realPageY * bufferPageWidth + realPageX );

                    MemoryPage *thePage = this->GetPage( pageIndex );

                    VirtualMemoryPage *vmemLayout = thePage->GetVirtualMemoryLayout( memLayoutType );

                    if ( !vmemLayout )
                    {
                        vmemLayout = thePage->AllocateVirtualMemoryLayout( memLayoutType );
                    }

                    if ( vmemLayout )
                    {
                        RwDynMemAllocator memAlloc( this->engineInterface );

                        MemoryRectangle *memRect =
                            eir::dyn_new_struct <MemoryRectangle >(
                                memAlloc, nullptr,
                                blockLocalX + realPageX * layoutProps.widthBlocksPerPage + realPageY * ( bufferPageWidth * layoutProps.widthBlocksPerPage ),
                                blockLocalY,
                                subRectAllocZone.x_slice.GetSliceSize(),
                                subRectAllocZone.y_slice.GetSliceSize()
                            );

                        if ( memRect )
                        {
                            LIST_INSERT( vmemLayout->allocatedRects.root, memRect->node );
                        }
                    }
                }
            }
        }
    }

    inline static uint32 calculateTextureBufferPageWidth(
        const memoryLayoutProperties_t& layoutProps,
        uint32 texelWidth, uint32 texelHeight
    )
    {
        // Scale up texel dimensions.
        uint32 alignedTexelWidth = ALIGN_SIZE( texelWidth, layoutProps.pixelWidthPerBlock );

        // Get block dimensions.
        uint32 texelBlockWidth = ( alignedTexelWidth / layoutProps.pixelWidthPerBlock );

        // Get the width in pages.
        uint32 pageMaxBlockWidth = ALIGN_SIZE( texelBlockWidth, layoutProps.widthBlocksPerPage );

        // Return the width in amount of pages.
        uint32 texBufferPageWidth = ( pageMaxBlockWidth / layoutProps.widthBlocksPerPage );

        return texBufferPageWidth;
    }

    inline bool allocateTexture(
        eMemoryLayoutType memLayoutType, const memoryLayoutProperties_t& layoutProps,
        uint32 texelWidth, uint32 texelHeight,
        uint32& texBasePointerOut, uint32& texMemSize, uint32& texOffX, uint32& texOffY, uint32& texBufferWidthOut
    )
    {
        // Scale up texel dimensions.
        uint32 alignedTexelWidth = ALIGN_SIZE( texelWidth, layoutProps.pixelWidthPerBlock );
        uint32 alignedTexelHeight = ALIGN_SIZE( texelHeight, layoutProps.pixelHeightPerBlock );

        // Get block dimensions.
        uint32 texelBlockWidth = ( alignedTexelWidth / layoutProps.pixelWidthPerBlock );
        uint32 texelBlockHeight = ( alignedTexelHeight / layoutProps.pixelHeightPerBlock );

        // Get the minimum required texture buffer width.
        // It must be aligned to the page dimensions.
        uint32 texBufferWidth = ( ALIGN_SIZE( texelBlockWidth, layoutProps.widthBlocksPerPage ) * layoutProps.pixelWidthPerBlock ) / 64;

        // Do some hacks.
        if ( memLayoutType == PSMT8 )
        {
            if ( texelBlockWidth > layoutProps.widthBlocksPerPage )
            {
                if ( texelBlockHeight == layoutProps.heightBlocksPerPage / 2 )
                {
                    texelBlockWidth /= 2;
                    texelBlockHeight *= 2;
                }
            }
        }

        // Get the width in pages.
        uint32 pageMaxBlockWidth = ALIGN_SIZE( texelBlockWidth, layoutProps.widthBlocksPerPage );

        uint32 texelPageWidth = pageMaxBlockWidth / layoutProps.widthBlocksPerPage;

        // Get the height in pages.
        uint32 pageMaxBlockHeight = ALIGN_SIZE( texelBlockHeight, layoutProps.heightBlocksPerPage );

        uint32 texelPageHeight = pageMaxBlockHeight / layoutProps.heightBlocksPerPage;

        // TODO: this is not the real buffer width yet.

        // Loop through all pages and try to find the correct placement for the new texture.
        uint32 pageX = 0;
        uint32 pageY = 0;
        uint32 blockOffsetX = 0;
        uint32 blockOffsetY = 0;

        bool validAllocation =
            findAllocationRegion(
                memLayoutType, texelBlockWidth, texelBlockHeight,
                texelPageWidth, layoutProps,
                pageX, pageY, blockOffsetX, blockOffsetY
            );

        // This may trigger if we overshot memory capacity.
        if ( validAllocation == false )
            return false;

        // Calculate the texture base pointer.
        uint32 texBasePointer = getTextureBasePointer(layoutProps, pageX, pageY, texelPageWidth, blockOffsetX, blockOffsetY);

        texBasePointerOut = texBasePointer;

        // Calculate the required memory size.
        texMemSize = calculateTextureMemSize(layoutProps, texBasePointer, pageX, pageY, texelPageWidth, blockOffsetX, blockOffsetY, texelBlockWidth, texelBlockHeight);

        // Give the target coordinates to the runtime.
        // They are passed as block coordinates.
        uint32 totalBlockOffX = pageX * layoutProps.widthBlocksPerPage + blockOffsetX;
        uint32 totalBlockOffY = pageY * layoutProps.heightBlocksPerPage + blockOffsetY;
        {
            texOffX = totalBlockOffX;
            texOffY = totalBlockOffY;
        }

        // Give the texture buffer width to the runtime.
        texBufferWidthOut = texBufferWidth;

        // Make sure we cannot allocate on the regions that were allocated on.
        addAllocationPresence(
            layoutProps, memLayoutType,
            texelPageWidth,
            pageX, pageY, texelPageWidth, texelPageHeight,
            totalBlockOffX, totalBlockOffY, texelBlockWidth, texelBlockHeight
        );

        return true;
    }

    inline bool allocateCLUT(
        eMemoryLayoutType memLayoutType, const memoryLayoutProperties_t& layoutProps,
        uint32 clutWidth, uint32 clutHeight,
        uint32& clutBasePointerOut, uint32& clutMemSize, uint32& clutOffX, uint32& clutOffY, uint32& clutBufferWidthOut,
        size_t mipmapCount
    )
    {
        // Get the allocation width of this buffer.
        uint32 bufferAllocPageWidth = this->bufferAllocationPageWidth;

        assert( bufferAllocPageWidth != 0 );

        // Scale up texel dimensions.
        uint32 alignedTexelWidth = ALIGN_SIZE( clutWidth, layoutProps.pixelWidthPerBlock );
        uint32 alignedTexelHeight = ALIGN_SIZE( clutHeight, layoutProps.pixelHeightPerBlock );

        // Get block dimensions.
        uint32 texelBlockWidth = ( alignedTexelWidth / layoutProps.pixelWidthPerBlock );
        uint32 texelBlockHeight = ( alignedTexelHeight / layoutProps.pixelHeightPerBlock );

        // Get the width in pages.
        uint32 pageMaxBlockWidth = ALIGN_SIZE( texelBlockWidth, layoutProps.widthBlocksPerPage );

        uint32 texelPageWidth = pageMaxBlockWidth / layoutProps.widthBlocksPerPage;

        // Get the height in pages.
        uint32 pageMaxBlockHeight = ALIGN_SIZE( texelBlockHeight, layoutProps.heightBlocksPerPage );

        uint32 texelPageHeight = pageMaxBlockHeight / layoutProps.heightBlocksPerPage;

        // Get the minimum required texture buffer width.
        // It must be aligned to the page dimensions.
        // This value should be atleast 2.
        uint32 texBufferWidth = ( texelPageWidth * layoutProps.widthBlocksPerPage * layoutProps.pixelWidthPerBlock ) / 64;

        // TODO: this is not the real buffer width yet.

        // Try to allocate the CLUT at the bottom right of the last page on the first column.
        uint32 pageX = 0;
        uint32 pageY = 0;
        uint32 blockOffsetX = 0;
        uint32 blockOffsetY = 0;

        bool validAllocation = false;
        {
            uint32 pageStride = texelPageWidth;

            uint32 localPageX = 0;
            uint32 localPageY = 0;
            uint32 localBlockOffX = 0;
            uint32 localBlockOffY = 0;

            // We always allocate on the bottom right corner.
            //if ( mipmapCount > 1 )
            {
                localBlockOffX = ( layoutProps.widthBlocksPerPage - texelBlockWidth );
                localBlockOffY = ( layoutProps.heightBlocksPerPage - texelBlockHeight );
            }

            // Try to find the last free page.
            memoryCollider memCollideFullPage(
                this, memLayoutType, layoutProps,
                layoutProps.widthBlocksPerPage, layoutProps.heightBlocksPerPage,
                pageStride
            );
            {
                while ( true )
                {
                    bool isPageFree = ( memCollideFullPage.testCollision( localPageX, localPageY, 0, 0 ) == false );

                    if ( isPageFree )
                    {
                        break;
                    }

                    localPageY++;
                }
            }

            if ( localPageY != 0 )
            {
                // Try to allocate on the occupied space.
                memoryCollider clutCollider( this, memLayoutType, layoutProps, texelBlockWidth, texelBlockHeight, pageStride );

                bool hasSpotOnOccupiedSpace =
                    ( clutCollider.testCollision(localPageX, localPageY - 1, localBlockOffX, localBlockOffY) == false );

                bool needsReset = true;

                if ( hasSpotOnOccupiedSpace )
                {
                    // Check that there is nothing on the right.
                    bool canLocatePrevPage = true;

                    if ( bufferAllocPageWidth > 1 )
                    {
                        bool isOnRight = memCollideFullPage.testCollision(localPageX + 1, localPageY - 1, 0, 0);

                        if (isOnRight)
                        {
                            canLocatePrevPage = false;

                            localPageY--;
                        }
                    }

                    if ( canLocatePrevPage )
                    {
                        needsReset = false;

                        localPageY--;
                    }
                }

                if ( needsReset )
                {
                    localBlockOffX = 0;
                    localBlockOffY = 0;
                }
            }

            // Linearize the page coords.

            pageX = localPageX;
            pageY = localPageY;
            blockOffsetX = localBlockOffX;
            blockOffsetY = localBlockOffY;

            validAllocation = true;
        }

        // This may trigger if we overshot memory capacity.
        if ( validAllocation == false )
            return false;

        // Calculate the texture base pointer.
        uint32 texBasePointer = getTextureBasePointer(layoutProps, pageX, pageY, texelPageWidth, blockOffsetX, blockOffsetY);

        clutBasePointerOut = texBasePointer;

        // Calculate the required memory size.
        clutMemSize = calculateTextureMemSize(layoutProps, texBasePointer, pageX, pageY, texelPageWidth, blockOffsetX, blockOffsetY, texelBlockWidth, texelBlockHeight);

        // Give the target coordinates to the runtime.
        // They are passed as block coordinates.
        uint32 totalBlockOffX = pageX * layoutProps.widthBlocksPerPage + blockOffsetX;
        uint32 totalBlockOffY = pageY * layoutProps.heightBlocksPerPage + blockOffsetY;
        {
            clutOffX = totalBlockOffX;
            clutOffY = totalBlockOffY;
        }

        // Give the texture buffer width to the runtime.
        clutBufferWidthOut = texBufferWidth;

        // Make sure we cannot allocate on the regions that were allocated on.
        addAllocationPresence(
            layoutProps, memLayoutType,
            texelPageWidth,
            pageX, pageY, texelPageWidth, texelPageHeight,
            totalBlockOffX, totalBlockOffY, texelBlockWidth, texelBlockHeight
        );

        return true;
    }
};

struct singleMemLayoutGSAllocator
{
    ps2GSMemoryLayoutManager gsMem;

    ps2GSMemoryLayoutManager::memoryLayoutProperties_t layoutProps;

    eMemoryLayoutType pixelMemLayoutType;
    eFormatEncodingType encodingMemLayout;
    eFormatEncodingType encodingPixelMemLayoutType;

    uint32 maxBuffHeight;

    inline singleMemLayoutGSAllocator(
        Interface *engineInterface,
        eFormatEncodingType encodingMemLayout, eFormatEncodingType encodingPixelMemLayoutType,
        eMemoryLayoutType pixelMemLayoutType
    ) : gsMem( engineInterface )
    {
        this->pixelMemLayoutType = pixelMemLayoutType;
        this->encodingMemLayout = encodingMemLayout;
        this->encodingPixelMemLayoutType = encodingPixelMemLayoutType;
        this->maxBuffHeight = 0;

        // Get format properties.
        ps2GSMemoryLayoutManager::getMemoryLayoutProperties( pixelMemLayoutType, encodingPixelMemLayoutType, this->layoutProps );
    }

    inline ~singleMemLayoutGSAllocator( void )
    {
        return;
    }

    inline void getDecodedDimensions(uint32 encodedWidth, uint32 encodedHeight, uint32& realWidth, uint32& realHeight) const
    {
        bool gotDecodedDimms =
            ps2GSPixelEncodingFormats::getPackedFormatDimensions(
                encodingMemLayout, encodingPixelMemLayoutType, encodedWidth, encodedHeight,
                realWidth,realHeight
            );

        assert( gotDecodedDimms == true );
    }

    inline bool allocateTexture(uint32 encodedWidth, uint32 encodedHeight, uint32& texBasePointerOut, uint32& texBufferWidthOut, uint32& texMemSizeOut, uint32& texOffXOut, uint32& texOffYOut)
    {
        uint32 texelWidth, texelHeight;

        getDecodedDimensions(encodedWidth, encodedHeight, texelWidth, texelHeight);

        uint32 texBasePointer = 0;
        uint32 texBufferWidth = 0;
        uint32 texMemSize = 0;
        uint32 texOffX = 0;
        uint32 texOffY = 0;

        if ( maxBuffHeight < texelHeight )
        {
            maxBuffHeight = texelHeight;
        }

        bool allocationSuccess = gsMem.allocateTexture( this->pixelMemLayoutType, this->layoutProps, texelWidth, texelHeight, texBasePointer, texMemSize, texOffX, texOffY, texBufferWidth );

        // If we fail to allocate any texture, we must terminate here.
        if ( allocationSuccess )
        {
            // Write the results to the runtime.
            texBasePointerOut = texBasePointer;
            texBufferWidthOut = texBufferWidth;
            texMemSizeOut = texMemSize;
            texOffXOut = texOffX;
            texOffYOut = texOffY;
        }

        return allocationSuccess;
    }

    inline bool allocateCLUT(
        uint32 encodedWidth, uint32 encodedHeight, uint32& clutBasePointerOut, uint32& clutBufferWidthOut, uint32& clutMemSizeOut, uint32& clutOffXOut, uint32& clutOffYOut,
        size_t mipmapCount
    )
    {
        uint32 texelWidth, texelHeight;

        getDecodedDimensions(encodedWidth, encodedHeight, texelWidth, texelHeight);

        uint32 clutBasePointer = 0;
        uint32 clutBufferWidth = 0;
        uint32 clutMemSize = 0;
        uint32 clutOffX = 0;
        uint32 clutOffY = 0;

        if ( maxBuffHeight < texelHeight )
        {
            maxBuffHeight = texelHeight;
        }

        bool allocationSuccess = gsMem.allocateCLUT(
            this->pixelMemLayoutType, this->layoutProps, texelWidth, texelHeight, clutBasePointer, clutMemSize, clutOffX, clutOffY, clutBufferWidth,
            mipmapCount
        );

        // If we fail to allocate any texture, we must terminate here.
        if ( allocationSuccess )
        {
            // Write the results to the runtime.
            clutBasePointerOut = clutBasePointer;
            clutBufferWidthOut = clutBufferWidth;
            clutMemSizeOut = clutMemSize;
            clutOffXOut = clutOffX;
            clutOffYOut = clutOffY;
        }

        return allocationSuccess;
    }
};

bool NativeTexturePS2::allocateTextureMemoryNative(
    uint32 mipmapBasePointer[], uint32 mipmapBufferWidth[], uint32 mipmapMemorySize[], ps2MipmapTransmissionData mipmapTransData[], uint32 maxMipmaps,
    eMemoryLayoutType& pixelMemLayoutTypeOut,
    uint32& clutBasePointerOut, uint32& clutMemSizeOut, ps2MipmapTransmissionData& clutTransDataOut,
    uint32& maxBuffHeightOut
) const
{
    // Get the memory layout of the encoded texture.
    eFormatEncodingType encodingMemLayout = this->swizzleEncodingType;

    if ( encodingMemLayout == FORMAT_UNKNOWN )
        return false;

    eFormatEncodingType encodingPixelMemLayoutType = getFormatEncodingFromRasterFormat( this->rasterFormat, this->paletteType );

    if ( encodingPixelMemLayoutType == FORMAT_UNKNOWN )
        return false;

    // Get the memory layout type of our decoded texture data.
    // This is used to fetch texel data from the permuted block correctly.
    eMemoryLayoutType pixelMemLayoutType;

    bool gotDecodedMemLayout = getMemoryLayoutFromTexelFormat(encodingPixelMemLayoutType, pixelMemLayoutType);

    if ( gotDecodedMemLayout == false )
        return false;

    // Get the encoding memory layout.
    eMemoryLayoutType encodedMemLayoutType;

    bool gotEncodedMemLayout = getMemoryLayoutFromTexelFormat(encodingMemLayout, encodedMemLayoutType);

    if ( gotEncodedMemLayout == false )
        return false;

    // Get the properties of the encoded mem layout.
    ps2GSMemoryLayoutManager::memoryLayoutProperties_t encodedLayoutProps;

    ps2GSMemoryLayoutManager::getMemoryLayoutProperties(encodedMemLayoutType, encodingMemLayout, encodedLayoutProps);

    size_t mipmapCount = this->mipmaps.GetCount();

    // Perform the allocation.
    {
        Interface *engineInterface = this->engineInterface;

        singleMemLayoutGSAllocator gsAlloc( engineInterface, encodingMemLayout, encodingPixelMemLayoutType, pixelMemLayoutType );

        // Calculate the required buffer width.
        uint32 maxBufferPageWidth = 0;
        uint32 mainTexPageWidth = 0;

        for ( size_t n = 0; n < mipmapCount; n++ )
        {
            const NativeTexturePS2::GSTexture& gsTex = this->mipmaps[n];

            uint32 realWidth, realHeight;

            gsAlloc.getDecodedDimensions(gsTex.swizzleWidth, gsTex.swizzleHeight, realWidth, realHeight);

            uint32 thisPageWidth = gsAlloc.gsMem.calculateTextureBufferPageWidth(gsAlloc.layoutProps, realWidth, realHeight);

            if ( maxBufferPageWidth < thisPageWidth )
            {
                maxBufferPageWidth = thisPageWidth;
            }

            if ( n == 0 )
            {
                mainTexPageWidth = thisPageWidth;
            }
        }

        // The CLUT must fit too.
        // (this may be left out since the clut cannot ever be wider than one page)
        if ( false )
        {
            uint32 realWidth, realHeight;

            gsAlloc.getDecodedDimensions(this->paletteTex.swizzleWidth, this->paletteTex.swizzleHeight, realWidth, realHeight);

            uint32 thisPageWidth = gsAlloc.gsMem.calculateTextureBufferPageWidth(gsAlloc.layoutProps, realWidth, realHeight);

            if ( maxBufferPageWidth < thisPageWidth )
            {
                maxBufferPageWidth = thisPageWidth;
            }
        }

        // Make sure that the main texture is the largest texture.
        assert( mainTexPageWidth == maxBufferPageWidth );

        // Set the buffer width.
        gsAlloc.gsMem.SetBufferPageWidth( maxBufferPageWidth );

        for ( uint32 n = 0; n < mipmapCount; n++ )
        {
            const NativeTexturePS2::GSTexture& gsTex = this->mipmaps[n];

            // Get the texel dimensions of this texture.
            uint32 encodedWidth = gsTex.swizzleWidth;
            uint32 encodedHeight = gsTex.swizzleHeight;

            uint32 texBasePointer;
            uint32 texMemSize;
            uint32 texBufferWidth;
            uint32 texOffX;
            uint32 texOffY;

            bool hasAllocated = gsAlloc.allocateTexture(encodedWidth, encodedHeight, texBasePointer, texBufferWidth, texMemSize, texOffX, texOffY);

            if ( !hasAllocated )
            {
                return false;
            }

            if ( n >= maxMipmaps )
            {
                // We do not know how to handle more mipmaps than the hardware allows.
                // For safety reasons terminate.
                return false;
            }

            // Store the results.
            mipmapBasePointer[ n ] = texBasePointer;

            // Store the size of the texture in memory.
            mipmapMemorySize[ n ] = texMemSize;

            // Also store our texture buffer width.
            mipmapBufferWidth[ n ] = texBufferWidth;

            // Store the target coordinates.
            ps2MipmapTransmissionData& transData = mipmapTransData[ n ];

            // Get the offset in pixels.
            uint32 pixelTexOffX = ( texOffX * encodedLayoutProps.pixelWidthPerBlock );
            uint32 pixelTexOffY = ( texOffY * encodedLayoutProps.pixelHeightPerBlock );

            if (encodingMemLayout == FORMAT_TEX32 && encodingPixelMemLayoutType == FORMAT_IDTEX8_COMPRESSED)
            {
                pixelTexOffX *= 2;
            }

            transData.destX = pixelTexOffX;
            transData.destY = pixelTexOffY;
        }

        // Normalize all the remaining fields.
        for ( size_t n = mipmapCount; n < maxMipmaps; n++ )
        {
            mipmapBasePointer[ n ] = 0;
            mipmapMemorySize[ n ] = 0;
            mipmapBufferWidth[ n ] = 1;

            ps2MipmapTransmissionData& transData = mipmapTransData[ n ];

            transData.destX = 0;
            transData.destY = 0;
        }

        // Allocate the palette data at the end.
        uint32 clutBasePointer = 0;
        uint32 clutMemSize = 0;
        ps2MipmapTransmissionData clutTransData;

        clutTransData.destX = 0;
        clutTransData.destY = 0;

        if (this->paletteType != PALETTE_NONE)
        {
            const NativeTexturePS2::GSTexture& palTex = this->paletteTex;

            uint32 psmct32_width = palTex.swizzleWidth;
            uint32 psmct32_height = palTex.swizzleHeight;

            // Allocate it.
            uint32 _clutBasePointer;
            uint32 _clutMemSize;
            uint32 clutBufferWidth;
            uint32 clutOffX;
            uint32 clutOffY;

            bool hasAllocatedCLUT = false;

            if (this->paletteType == PALETTE_4BIT)
            {
                // Just allocate it at the end of the buffer.
                uint32 buffEnd = 0;

                for ( uint32 n = 0; n < mipmapCount; n++ )
                {
                    uint32 thisMaxOff = ( mipmapBasePointer[n] + mipmapMemorySize[n] );

                    if ( buffEnd < thisMaxOff )
                    {
                        buffEnd = thisMaxOff;
                    }
                }

                _clutBasePointer = buffEnd;
                _clutMemSize = 1;
                clutBufferWidth = 1;
                clutOffX = 0;
                clutOffY = 0;

                hasAllocatedCLUT = true;
            }
            else if (this->paletteType == PALETTE_8BIT)
            {
                hasAllocatedCLUT = gsAlloc.allocateCLUT(
                    psmct32_width, psmct32_height, _clutBasePointer, clutBufferWidth, _clutMemSize, clutOffX, clutOffY,
                    mipmapCount
                );
            }

            if ( hasAllocatedCLUT == false )
            {
                return false;
            }

            // Transform to final CLUT coords.
            uint32 clutPixelOffX = ( clutOffX * encodedLayoutProps.pixelWidthPerBlock );
            uint32 clutPixelOffY = ( clutOffY * encodedLayoutProps.pixelHeightPerBlock );

            if (encodingMemLayout == FORMAT_TEX32 && encodingPixelMemLayoutType == FORMAT_IDTEX8_COMPRESSED)
            {
                clutPixelOffX *= 2;
            }

            uint32 clutFinalOffX = clutPixelOffX;
            uint32 clutFinalOffY = clutPixelOffY;

            // Write to the runtime.
            clutBasePointer = _clutBasePointer;
            clutMemSize = _clutMemSize;

            clutTransData.destX = clutFinalOffX;
            clutTransData.destY = clutFinalOffY;
        }

        maxBuffHeightOut = gsAlloc.maxBuffHeight;

        clutBasePointerOut = clutBasePointer;
        clutMemSizeOut = clutMemSize;
        clutTransDataOut = clutTransData;
    }

    // Give the pixel mem layout type to the runtime.
    pixelMemLayoutTypeOut = pixelMemLayoutType;

    return true;
}

bool NativeTexturePS2::allocateTextureMemory(
    uint32 mipmapBasePointer[], uint32 mipmapBufferWidth[], uint32 mipmapMemorySize[], ps2MipmapTransmissionData mipmapTransData[], uint32 maxMipmaps,
    eMemoryLayoutType& pixelMemLayoutTypeOut,
    uint32& clutBasePointerOut, uint32& clutMemSizeOut, ps2MipmapTransmissionData& clutTransDataOut
) const
{
    // Allocate the memory.
    uint32 maxBuffHeight;

    bool success = allocateTextureMemoryNative(
        mipmapBasePointer, mipmapBufferWidth, mipmapMemorySize, mipmapTransData, maxMipmaps,
        pixelMemLayoutTypeOut,
        clutBasePointerOut, clutMemSizeOut, clutTransDataOut,
        maxBuffHeight
    );

    if ( success )
    {
        // Convert the transition datas to pixel offsets in the encoded format.
        size_t mipmapCount = this->mipmaps.GetCount();
        {
            // First mipmaps.
            for ( size_t n = 0; n < mipmapCount; n++ )
            {
                ps2MipmapTransmissionData& transData = mipmapTransData[ n ];

                uint32 texOffY = transData.destY;

                // Clamp it.
                texOffY = ( texOffY % maxBuffHeight );

                transData.destY = texOffY;
            }

            // Now the CLUT.
            {
                ps2MipmapTransmissionData& clutTransData = clutTransDataOut;

                uint32 clutOffY = clutTransData.destY;

                // Clamp it.
                clutOffY = ( clutOffY % maxBuffHeight );

                clutTransData.destY = clutOffY;
            }
        }

        // Make sure buffer sizes are in their limits.
        for ( uint32 n = 0; n < mipmapCount; n++ )
        {
            uint32 thisBase = mipmapBasePointer[n];

            if ( thisBase >= 0x4000 )
            {
                return false;
            }

            uint32 thisBufferWidth = mipmapBufferWidth[n];

            if ( thisBufferWidth >= 64 )
            {
                return false;
            }
        }
    }

    return success;
}

bool NativeTexturePS2::getDebugBitmap( Bitmap& bmpOut ) const
{
    // Setup colors to use for the rectangles.
    struct singleColorSourcePipeline : public Bitmap::sourceColorPipeline
    {
        double red, green, blue, alpha;

        inline singleColorSourcePipeline( void )
        {
            this->red = 0;
            this->green = 0;
            this->blue = 0;
            this->alpha = 1.0;
        }

        uint32 getWidth( void ) const
        {
            return 1;
        }

        uint32 getHeight( void ) const
        {
            return 1;
        }

        void fetchcolor( uint32 x, uint32 y, double& red, double& green, double& blue, double& alpha )
        {
            red = this->red;
            green = this->green;
            blue = this->blue;
            alpha = this->alpha;
        }
    };

    singleColorSourcePipeline colorSrcPipe;

    // Get the memory layout of the encoded texture.
    eFormatEncodingType encodingMemLayout = this->swizzleEncodingType;

    if ( encodingMemLayout == FORMAT_UNKNOWN )
        return false;

    eFormatEncodingType encodingPixelMemLayoutType = getFormatEncodingFromRasterFormat( this->rasterFormat, this->paletteType );

    if ( encodingPixelMemLayoutType == FORMAT_UNKNOWN )
        return false;

    size_t mipmapCount = this->mipmaps.GetCount();

    // Perform the allocation.
    const uint32 maxMipmaps = 7;

    eMemoryLayoutType pixelMemLayoutType;
    uint32 maxBuffHeight;

    uint32 mipmapBasePointer[ maxMipmaps ];
    uint32 mipmapBufferWidth[ maxMipmaps ];
    uint32 mipmapMemorySize[ maxMipmaps ];

    ps2MipmapTransmissionData mipmapTransData[ maxMipmaps ];

    uint32 clutBasePointer;
    uint32 clutMemorySize;

    ps2MipmapTransmissionData clutTransData;

    bool hasAllocated = this->allocateTextureMemoryNative(
        mipmapBasePointer, mipmapBufferWidth, mipmapMemorySize, mipmapTransData, maxMipmaps,
        pixelMemLayoutType,
        clutBasePointer, clutMemorySize, clutTransData,
        maxBuffHeight
    );

    if ( !hasAllocated )
        return false;

    for ( size_t n = 0; n < mipmapCount; n++ )
    {
        const NativeTexturePS2::GSTexture& gsTex = this->mipmaps[n];

        // Get the texel dimensions of this texture.
        uint32 encodedWidth = gsTex.swizzleWidth;
        uint32 encodedHeight = gsTex.swizzleHeight;

        const ps2MipmapTransmissionData& mipTransData = mipmapTransData[ n ];

        uint32 pixelOffX = mipTransData.destX;
        uint32 pixelOffY = mipTransData.destY;

        // Get the real width and height.
        uint32 texelWidth = encodedWidth;
        uint32 texelHeight = encodedHeight;

        // Make sure the bitmap is large enough for our drawing.
        if (encodingMemLayout == FORMAT_TEX32 && encodingPixelMemLayoutType == FORMAT_IDTEX8_COMPRESSED)
        {
            texelWidth *= 2;
        }

        uint32 reqWidth = pixelOffX + texelWidth;
        uint32 reqHeight = pixelOffY + texelHeight;

        bmpOut.enlargePlane( reqWidth, reqHeight );

        // Set special color depending on mipmap count.
        if ( n == 0 )
        {
            colorSrcPipe.red = 0.5666;
            colorSrcPipe.green = 0;
            colorSrcPipe.blue = 0;
        }
        else if ( n == 1 )
        {
            colorSrcPipe.red = 0;
            colorSrcPipe.green = 0.5666;
            colorSrcPipe.blue = 0;
        }
        else if ( n == 2 )
        {
            colorSrcPipe.red = 0;
            colorSrcPipe.green = 0;
            colorSrcPipe.blue = 1.0;
        }
        else if ( n == 3 )
        {
            colorSrcPipe.red = 1.0;
            colorSrcPipe.green = 1.0;
            colorSrcPipe.blue = 0;
        }
        else if ( n == 4 )
        {
            colorSrcPipe.red = 0;
            colorSrcPipe.green = 1.0;
            colorSrcPipe.blue = 1.0;
        }
        else if ( n == 5 )
        {
            colorSrcPipe.red = 1.0;
            colorSrcPipe.green = 1.0;
            colorSrcPipe.blue = 1.0;
        }
        else if ( n == 6 )
        {
            colorSrcPipe.red = 0.5;
            colorSrcPipe.green = 0.5;
            colorSrcPipe.blue = 0.5;
        }

        // Draw the rectangle.
        bmpOut.draw(
            colorSrcPipe, pixelOffX, pixelOffY, texelWidth, texelHeight,
            Bitmap::SHADE_SRCALPHA, Bitmap::SHADE_ONE, Bitmap::BLEND_ADDITIVE
        );
    }

    // Also render the palette if its there.
    if (this->paletteType == PALETTE_8BIT)
    {
        uint32 clutOffX = clutTransData.destX;
        uint32 clutOffY = clutTransData.destY;

        colorSrcPipe.red = 1;
        colorSrcPipe.green = 0.75;
        colorSrcPipe.blue = 0;

        uint32 palWidth = 0;
        uint32 palHeight = 0;

        if (this->paletteType == PALETTE_4BIT)
        {
            palWidth = 8;
            palHeight = 3;
        }
        else if (this->paletteType == PALETTE_8BIT)
        {
            palWidth = 16;
            palHeight = 16;
        }

        bmpOut.enlargePlane( palWidth + clutOffX, palHeight + clutOffY );

        bmpOut.draw(
            colorSrcPipe, clutOffX, clutOffY,
            palWidth, palHeight,
            Bitmap::SHADE_SRCALPHA, Bitmap::SHADE_ONE, Bitmap::BLEND_ADDITIVE
        );
    }

    return true;
}

}

#endif //RWLIB_INCLUDE_NATIVETEX_PLAYSTATION2
