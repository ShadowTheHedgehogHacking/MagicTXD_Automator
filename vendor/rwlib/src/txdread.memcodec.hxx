// This file contains general memory encoding routines (so called swizzling).
// It is recommended to use this file if you need stable, proofed algorithms.
#ifndef RW_TEXTURE_MEMORY_ENCODING
#define RW_TEXTURE_MEMORY_ENCODING

// Optimized algorithms are frowned upon, because in general it is hard to proove their correctness.

namespace rw
{

namespace memcodec
{

// Common utilities for permutation providers.
namespace permutationUtilities
{
    inline static void permuteArray(
        const void *srcToBePermuted, uint32 rawWidth, uint32 rawHeight, uint32 rawDepth, uint32 rawColumnWidth, uint32 rawColumnHeight,
        void *dstTexels, uint32 packedWidth, uint32 packedHeight, uint32 packedDepth, uint32 packedColumnWidth, uint32 packedColumnHeight,
        uint32 colsWidth, uint32 colsHeight,
        const uint32 *permutationData_primCol, const uint32 *permutationData_secCol, uint32 permWidth, uint32 permHeight,
        uint32 permutationStride, uint32 permHoriSplit,
        uint32 srcRowAlignment, uint32 dstRowAlignment,
        bool revert, bool isPackingConvention = true
        )
    {
        // Get the dimensions of a column as expressed in units of the permutation format.
        uint32 permProcessColumnWidth = packedColumnWidth;
        uint32 permProcessColumnHeight = packedColumnHeight;

        uint32 permIterWidth = rawColumnWidth;
        uint32 permIterHeight = rawColumnHeight;

        uint32 permSourceWidth = rawWidth;
        uint32 permSourceHeight = rawHeight;

        uint32 packedTargetWidth = packedWidth;
        uint32 packedTargetHeight = packedHeight;

        uint32 permItemDepth = rawDepth;

        uint32 packedTransformedColumnWidth = ( permProcessColumnWidth * permutationStride ) / permHoriSplit;
        uint32 packedTransformedColumnHeight = ( permProcessColumnHeight );

        colsWidth *= permHoriSplit;

        // Get the stride through the packed data in raw format.
        uint32 packedTransformedStride = ( packedTargetWidth * permutationStride );

        // Determine the strides for both arrays.
        uint32 srcStride, targetStride;

        if ( !revert )
        {
            srcStride = permSourceWidth;
            targetStride = packedTransformedStride;
        }
        else
        {
            srcStride = packedTransformedStride;
            targetStride = permSourceWidth;
        }

        // Calculate the row sizes.
        uint32 srcRowSize = getRasterDataRowSize( srcStride, permItemDepth, srcRowAlignment );
        uint32 dstRowSize = getRasterDataRowSize( targetStride, permItemDepth, dstRowAlignment );

        // Permute the pixels.
        for ( uint32 colY = 0; colY < colsHeight; colY++ )
        {
            // Get the data to permute with.
            bool isPrimaryCol = ( colY % 2 == 0 );

            const uint32 *permuteData =
                ( isPrimaryCol ? permutationData_primCol : permutationData_secCol );

            // Get the 2D array offset of colY (source array).
            uint32 source_colY_pixeloff = ( colY * permIterHeight );

            // Get the 2D array offset of colY (target array).
            uint32 target_colY_pixeloff = ( colY * packedTransformedColumnHeight );

            for ( uint32 colX = 0; colX < colsWidth; colX++ )
            {
                // Get the 2D array offset of colX (source array).
                uint32 source_colX_pixeloff = ( colX * permIterWidth );

                // Get the 2D array offset of colX (target array).
                uint32 target_colX_pixeloff = ( colX * packedTransformedColumnWidth );

                // Loop through all pixels of this column and permute them.
                for ( uint32 permY = 0; permY < packedTransformedColumnHeight; permY++ )
                {
                    for ( uint32 permX = 0; permX < packedTransformedColumnWidth; permX++ )
                    {
                        // Get the index of this pixel.
                        uint32 localPixelIndex = ( permY * packedTransformedColumnWidth + permX );

                        // Get the new location to put the pixel at.
                        uint32 newPixelLoc = permuteData[ localPixelIndex ];

                        // Transform this coordinate into a 2D array position.
                        uint32 local_pixel_xOff = ( newPixelLoc % permIterWidth );
                        uint32 local_pixel_yOff = ( newPixelLoc / permIterWidth );

                        // Get the combined pixel position.
                        uint32 source_pixel_xOff = ( source_colX_pixeloff );
                        uint32 source_pixel_yOff = ( source_colY_pixeloff );

                        uint32 target_pixel_xOff = ( target_colX_pixeloff );
                        uint32 target_pixel_yOff = ( target_colY_pixeloff );

                        if ( isPackingConvention )
                        {
                            source_pixel_xOff += local_pixel_xOff;
                            source_pixel_yOff += local_pixel_yOff;

                            target_pixel_xOff += permX;
                            target_pixel_yOff += permY;
                        }
                        else
                        {
                            source_pixel_xOff += permX;
                            source_pixel_yOff += permY;

                            target_pixel_xOff += local_pixel_xOff;
                            target_pixel_yOff += local_pixel_yOff;
                        }

                        if ( source_pixel_xOff < permSourceWidth && source_pixel_yOff < permSourceHeight &&
                             target_pixel_xOff < packedTransformedStride &&
                             target_pixel_yOff < packedTargetHeight )
                        {
                            // Determine the 2D array coordinates for source and destination arrays.
                            uint32 source_xOff, source_yOff;
                            uint32 target_xOff, target_yOff;

                            if ( !revert )
                            {
                                source_xOff = source_pixel_xOff;
                                source_yOff = source_pixel_yOff;

                                target_xOff = target_pixel_xOff;
                                target_yOff = target_pixel_yOff;
                            }
                            else
                            {
                                source_xOff = target_pixel_xOff;
                                source_yOff = target_pixel_yOff;

                                target_xOff = source_pixel_xOff;
                                target_yOff = source_pixel_yOff;
                            }

                            // Get the rows.
                            const void *srcRow = getConstTexelDataRow( srcToBePermuted, srcRowSize, source_yOff );
                            void *dstRow = getTexelDataRow( dstTexels, dstRowSize, target_yOff );

                            // Move the data over.
                            moveDataByDepth(
                                dstRow, srcRow,
                                permItemDepth,
                                eByteAddressingMode::MOST_SIGNIFICANT,
                                target_xOff, source_xOff
                            );
                        }
                    }
                }
            }
        }
    }

    template <typename processorType, typename callbackType>
    AINLINE void GenericProcessTiledCoordsFromLinear(
        uint32 linearX, uint32 linearY, uint32 surfWidth, uint32 surfHeight,
        uint32 clusterWidth, uint32 clusterHeight, uint32 clusterCount,
        const callbackType& cb
    )
    {
        uint32 cluster_inside_x = ( linearX % clusterWidth );
        uint32 cluster_inside_y = ( linearY % clusterHeight );

        uint32 cluster_col = ( linearX / clusterWidth );
        uint32 cluster_row = ( linearY / clusterHeight );

        uint32 alignedSurfWidth = ALIGN_SIZE( surfWidth, clusterWidth );
        uint32 alignedSurfHeight = ALIGN_SIZE( surfHeight, clusterHeight );

        processorType proc( alignedSurfWidth, alignedSurfHeight, clusterWidth, clusterHeight, clusterCount );

        for ( uint32 cluster_index = 0; cluster_index < clusterCount; cluster_index++ )
        {
            uint32 tiled_x, tiled_y;
            proc.Get(
                cluster_col, cluster_row,
                cluster_inside_x, cluster_inside_y, cluster_index,
                tiled_x, tiled_y
            );

            cb( tiled_x, tiled_y, cluster_index );
        }
    }

    // Unoptimized packed tile processor for placing 2D tiles linearly into a buffer, basically
    // improving lookup performance through cache-friendlyness.
    // Makes sense when sending 2D color data to a highly-parallel simple device like a GPU.
    struct packedTileProcessor
    {
        AINLINE packedTileProcessor( uint32 alignedSurfWidth, uint32 alignedSurfHeight, uint32 clusterWidth, uint32 clusterHeight, uint32 clusterCount )
        {
            this->clusterWidth = clusterWidth;

            this->globalClustersPerWidth = ( alignedSurfWidth / clusterWidth );
            this->globalClustersPerHeight = ( alignedSurfHeight / clusterHeight );

            uint32 globalClusterWidth = ( clusterWidth * clusterCount );
            uint32 globalClusterHeight = ( clusterHeight );

            this->globalClusterWidth = globalClusterWidth;
            this->globalClusterHeight = globalClusterHeight;

            this->clusteredSurfWidth = ( alignedSurfWidth * clusterCount );
            this->clusteredSurfHeight = ( alignedSurfHeight );

            // This is the size in indices of one tile.
            this->local_clusterIndexSize = ( clusterWidth * clusterHeight );
            this->globalClusterIndexSize = ( globalClusterWidth * globalClusterHeight );
        }

        AINLINE void Get(
            uint32 globalClusterX, uint32 globalClusterY,
            uint32 localClusterX, uint32 localClusterY, uint32 cluster_index,
            uint32& tiled_x, uint32& tiled_y
        )
        {
            // TODO: optimize this, so that it can be put into a one-time initializator.
            uint32 local_cluster_advance_index = ( localClusterX + localClusterY * this->clusterWidth );

            uint32 global_cluster_advance_index = ( globalClusterX + globalClusterY * this->globalClustersPerWidth ) * this->globalClusterIndexSize;

            uint32 cluster_advance_index = ( local_cluster_advance_index + global_cluster_advance_index );

            // Now the actual mutating part.
            uint32 per_cluster_advance_index = ( cluster_advance_index + this->local_clusterIndexSize * cluster_index );

            tiled_x = ( per_cluster_advance_index % this->clusteredSurfWidth );
            tiled_y = ( per_cluster_advance_index / this->clusteredSurfWidth );
        }

    private:
        uint32 clusterWidth;
        uint32 globalClusterWidth, globalClusterHeight;
        uint32 clusteredSurfWidth, clusteredSurfHeight;

        uint32 local_clusterIndexSize;
        uint32 globalClusterIndexSize;

        uint32 globalClustersPerWidth, globalClustersPerHeight;
    };

    // A way to get clustered tiled coordinates for formats.
    template <typename callbackType>
    AINLINE void ProcessPackedTiledCoordsFromLinear(
        uint32 linearX, uint32 linearY, uint32 surfWidth, uint32 surfHeight,
        uint32 clusterWidth, uint32 clusterHeight, uint32 clusterCount,
        const callbackType& cb
    )
    {
        GenericProcessTiledCoordsFromLinear <packedTileProcessor> (
            linearX, linearY, surfWidth, surfHeight,
            clusterWidth, clusterHeight, clusterCount,
            cb
        );
    }

    template <typename processorType>
    AINLINE void GenericGetTiledCoordFromLinear(
        uint32 linearX, uint32 linearY, uint32 surfWidth, uint32 surfHeight,
        uint32 clusterWidth, uint32 clusterHeight,  // clusterCount == 1
        uint32& dst_tiled_x, uint32& dst_tiled_y
    )
    {
        // By passing 1 as clusterCount we are sure that we get one coordinate.
        // The lambda has to be triggered, exactly one time.
        GenericProcessTiledCoordsFromLinear <processorType> (
            linearX, linearY, surfWidth, surfHeight,
            clusterWidth, clusterHeight, 1,
            [&]( uint32 tiled_x, uint32 tiled_y, uint32 cluster_index )
        {
            dst_tiled_x = tiled_x;
            dst_tiled_y = tiled_y;
        });
    }

    AINLINE void GetPackedTiledCoordFromLinear(
        uint32 linearX, uint32 linearY, uint32 surfWidth, uint32 surfHeight,
        uint32 clusterWidth, uint32 clusterHeight,  // clusterCount == 1
        uint32& dst_tiled_x, uint32& dst_tiled_y
    )
    {
        GenericGetTiledCoordFromLinear <packedTileProcessor> (
            linearX, linearY,
            surfWidth, surfHeight,
            clusterWidth, clusterHeight,
            dst_tiled_x, dst_tiled_y
        );
    }

    // Optimized implementation of a packed tile processor designed for the default function.
    struct optimizedPackedTileProcessor
    {
        AINLINE optimizedPackedTileProcessor( uint32 alignedSurfWidth, uint32 alignedSurfHeight, uint32 clusterWidth, uint32 clusterHeight, uint32 clusterCount )
        {
            this->packedData_xOff = 0;
            this->packedData_yOff = 0;

            this->packed_surfWidth = ( alignedSurfWidth * clusterCount );
        }

        AINLINE void Get(
            uint32 colX, uint32 colY,
            uint32 cluster_x, uint32 cluster_y, uint32 cluster_index,
            uint32& tile_x, uint32& tile_y
        )
        {
            // Return the current stuff.
            tile_x = packedData_xOff;
            tile_y = packedData_yOff;

            // Advance the packed pointer.
            // We depend on the way the function iterates over the texels.
            packedData_xOff++;

            if ( packedData_xOff >= packed_surfWidth )
            {
                packedData_xOff = 0;
                packedData_yOff++;
            }
        }

    private:
        uint32 packedData_xOff, packedData_yOff;
        uint32 packed_surfWidth;
    };

    // Very simple processor that just returns the linear coordinate, effectively not swizzling.
    struct linearTileProcessor
    {
        AINLINE linearTileProcessor( uint32 alignedSurfWidth, uint32 alignedSurfHeight, uint32 clusterWidth, uint32 clusterHeight, uint32 clusterCount )
        {
            this->localClusterWidth = clusterWidth;

            this->globalClusterWidth = ( clusterWidth * clusterCount );
            this->globalClusterHeight = ( clusterHeight );
        }

        AINLINE void Get(
            uint32 colX, uint32 colY,
            uint32 cluster_x, uint32 cluster_y, uint32 cluster_index,
            uint32& tiled_x, uint32& tiled_y
        )
        {
            tiled_x = ( colX * this->globalClusterWidth + cluster_x + this->localClusterWidth * cluster_index );
            tiled_y = ( colY * this->globalClusterHeight + cluster_y );
        }

    private:
        uint32 localClusterWidth;

        uint32 globalClusterWidth;
        uint32 globalClusterHeight;
    };

    // Main texture layer tile processing algorithm.
    template <typename processorType, typename callbackType>
    AINLINE void GenericProcessTileLayerPerCluster(
        uint32 surfWidth, uint32 surfHeight,
        uint32 clusterWidth, uint32 clusterHeight,
        uint32 clusterCount,
        const callbackType& cb
    )
    {
        uint32 alignedSurfWidth = ALIGN_SIZE( surfWidth, clusterWidth );
        uint32 alignedSurfHeight = ALIGN_SIZE( surfHeight, clusterHeight );

        uint32 cols_width = ( alignedSurfWidth / clusterWidth );
        uint32 cols_height = ( alignedSurfHeight / clusterHeight );

        processorType proc( alignedSurfWidth, alignedSurfHeight, clusterWidth, clusterHeight, clusterCount );

        for ( uint32 colY = 0; colY < cols_height; colY++ )
        {
            uint32 col_y_pixelOff = ( colY * clusterHeight );

            for ( uint32 colX = 0; colX < cols_width; colX++ )
            {
                uint32 col_x_pixelOff = ( colX * clusterWidth );

                for ( uint32 cluster_index = 0; cluster_index < clusterCount; cluster_index++ )
                {
                    for ( uint32 cluster_y = 0; cluster_y < clusterHeight; cluster_y++ )
                    {
                        uint32 perm_y_off = ( col_y_pixelOff + cluster_y );

                        for ( uint32 cluster_x = 0; cluster_x < clusterWidth; cluster_x++ )
                        {
                            uint32 perm_x_off = ( col_x_pixelOff + cluster_x );

                            uint32 tiled_x, tiled_y;
                            proc.Get(
                                colX, colY,
                                cluster_x, cluster_y, cluster_index,
                                tiled_x, tiled_y
                            );

                            // Notify the runtime.
                            cb( perm_x_off, perm_y_off, tiled_x, tiled_y, cluster_index );
                        }
                    }
                }
            }
        }
    }

    template <typename callbackType>
    AINLINE void ProcessTextureLayerPackedTiles(
        uint32 surfWidth, uint32 surfHeight,
        uint32 clusterWidth, uint32 clusterHeight,
        uint32 clusterCount,
        const callbackType& cb
    )
    {
        GenericProcessTileLayerPerCluster <optimizedPackedTileProcessor> (
            surfWidth, surfHeight,
            clusterWidth, clusterHeight,
            clusterCount,
            cb
        );
    }

    inline void TestTileEncoding(
        uint32 surfWidth, uint32 surfHeight,
        uint32 clusterWidth, uint32 clusterHeight, uint32 clusterCount
    )
    {
        memcodec::permutationUtilities::ProcessTextureLayerPackedTiles(
            surfWidth, surfHeight,
            clusterWidth, clusterHeight, clusterCount,
            [&]( uint32 layerX, uint32 layerY, uint32 tiled_x, uint32 tiled_y, uint32 cluster_index )
        {
            memcodec::permutationUtilities::ProcessPackedTiledCoordsFromLinear(
                layerX, layerY,
                surfWidth, surfHeight,
                clusterWidth, clusterHeight, clusterCount,
                [&]( uint32 try_tiled_x, uint32 try_tiled_y, uint32 try_cluster_index )
            {
                if ( try_cluster_index == cluster_index )
                {
                    assert( try_tiled_x == tiled_x );
                    assert( try_tiled_y == tiled_y );
                }
            });
        });
    }

    inline bool TranscodeTextureLayerTiles(
        Interface *engineInterface,
        uint32 surfWidth, uint32 surfHeight, const void *srcTexels,
        uint32 permDepth,
        uint32 srcRowAlignment, uint32 dstRowAlignment,
        uint32 clusterWidth, uint32 clusterHeight,
        bool doSwizzleOrUnswizzle,
        void*& dstTexelsOut, uint32& dstDataSizeOut
    )
    {
        // Allocate the destination array.
        uint32 dstRowSize = getRasterDataRowSize( surfWidth, permDepth, dstRowAlignment );

        uint32 dstDataSize = getRasterDataSizeByRowSize( dstRowSize, surfHeight );

        void *dstTexels = engineInterface->PixelAllocate( dstDataSize );

        if ( !dstTexels )
        {
            return false;
        }

        uint32 srcRowSize = getRasterDataRowSize( surfWidth, permDepth, srcRowAlignment );

        try
        {
            ProcessTextureLayerPackedTiles(
                surfWidth, surfHeight,
                clusterWidth, clusterHeight, 1,
                [&]( uint32 perm_x_off, uint32 perm_y_off, uint32 packedData_xOff, uint32 packedData_yOff, uint32 cluster_index )
            {
                // Determine the pointer configuration.
                uint32 src_pos_x = 0;
                uint32 src_pos_y = 0;

                uint32 dst_pos_x = 0;
                uint32 dst_pos_y = 0;

                if ( doSwizzleOrUnswizzle )
                {
                    // We want to swizzle.
                    // This means the source is raw 2D plane, the destination is linear aligned binary buffer.
                    src_pos_x = perm_x_off;
                    src_pos_y = perm_y_off;

                    dst_pos_x = packedData_xOff;
                    dst_pos_y = packedData_yOff;
                }
                else
                {
                    // We want to unswizzle.
                    // This means the source is linear aligned binary buffer, dest is raw 2D plane.
                    src_pos_x = packedData_xOff;
                    src_pos_y = packedData_yOff;

                    dst_pos_x = perm_x_off;
                    dst_pos_y = perm_y_off;
                }

                if ( src_pos_x <= surfWidth && src_pos_y <= surfHeight &&
                     dst_pos_x <= surfWidth && dst_pos_y <= surfHeight )
                {
                    // Move data if in valid bounds.
                    const void *srcRow = getConstTexelDataRow( srcTexels, srcRowSize, src_pos_y );
                    void *dstRow = getTexelDataRow( dstTexels, dstRowSize, dst_pos_y );

                    moveDataByDepth(
                        dstRow, srcRow,
                        permDepth,
                        eByteAddressingMode::MOST_SIGNIFICANT,
                        dst_pos_x, src_pos_x
                    );
                }
            });
        }
        catch( ... )
        {
            engineInterface->PixelFree( dstTexels );

            throw;
        }

        // Return the data to the runtime.
        dstTexelsOut = dstTexels;
        dstDataSizeOut = dstDataSize;
        return true;
    }
};

// Class factory for creating a memory permutation engine.
template <typename baseSystem>
struct genericMemoryEncoder
{
    typedef typename baseSystem::encodingFormatType encodingFormatType;

    // Expects the following methods in baseSystem:
/*
    inline static uint32 getFormatEncodingDepth( encodingFormatType format );

    inline static bool isPackOperation( encodingFormatType srcFormat, encodingFormatType dstFormat );

    inline static bool getEncodingFormatDimensions(
        encodingFormatType encodingType,
        uint32& pixelColumnWidth, uint32& pixelColumnHeight
    );

    inline static bool getPermutationDimensions( encodingFormatType permFormat, uint32& permWidth, uint32& permHeight );

    inline static bool detect_packing_routine(
        encodingFormatType rawFormat, encodingFormatType packedFormat,
        const uint32*& permutationData_primCol,
        const uint32*& permutationData_secCol
    )
*/

    // Purpose of this routine is to pack smaller memory data units into bigger
    // memory data units in a way, so that unpacking is easier for the hardware than
    // it would be in its raw permutation.
    // It is pure optimization. A great hardware usage example is the PlayStation 2.
    inline static void* transformImageData(
        Interface *engineInterface,
        encodingFormatType srcFormat, encodingFormatType dstFormat,
        const void *srcToBeTransformed,
        uint32 srcMipWidth, uint32 srcMipHeight,
        uint32 srcRowAlignment, uint32 dstRowAlignment,
        uint32& dstMipWidthInOut, uint32& dstMipHeightInOut,
        uint32& dstDataSizeOut,
        bool hasDestinationDimms = false,
        bool lenientPacked = false
    )
    {
        assert(srcFormat != encodingFormatType::FORMAT_UNKNOWN);
        assert(dstFormat != encodingFormatType::FORMAT_UNKNOWN);

        if ( srcFormat == dstFormat )
        {
            return nullptr;
        }

        // Decide whether its unpacking or packing.
        bool isPack = baseSystem::isPackOperation( srcFormat, dstFormat );

        // Packing is the operation of putting smaller data types into bigger data types.
        // Since we define data structures to pack things, we use those both ways.

        encodingFormatType rawFormat, packedFormat;

        if ( isPack )
        {
            rawFormat = srcFormat;
            packedFormat = dstFormat;
        }
        else
        {

            rawFormat = dstFormat;
            packedFormat = srcFormat;
        }

        // We need to get the dimensions of the permutation.
        // This is from the view of packing, so we use the format 'to be packed'.
        uint32 permWidth, permHeight;

        bool gotPermDimms = baseSystem::getPermutationDimensions(rawFormat, permWidth, permHeight);

        assert( gotPermDimms == true );

        // Calculate the permutation stride and the hori split.
        uint32 rawDepth = baseSystem::getFormatEncodingDepth( rawFormat );
        uint32 packedDepth = baseSystem::getFormatEncodingDepth( packedFormat );

        uint32 permutationStride = ( packedDepth / rawDepth );

        uint32 permHoriSplit = ( permutationStride / permWidth );

        // Get the dimensions of the permutation area.
        uint32 rawColumnWidth, rawColumnHeight;
        uint32 packedColumnWidth, packedColumnHeight;

        bool gotRawDimms = baseSystem::getEncodingFormatDimensions( rawFormat, rawColumnWidth, rawColumnHeight );

        assert( gotRawDimms == true );

        bool gotPackedDimms = baseSystem::getEncodingFormatDimensions( packedFormat, packedColumnWidth, packedColumnHeight );

        assert( gotPackedDimms == true );

        // Calculate the dimensions.
        uint32 rawWidth, rawHeight;
        uint32 packedWidth, packedHeight;

        uint32 columnWidthCount;
        uint32 columnHeightCount;

        if ( isPack )
        {
            rawWidth = srcMipWidth;
            rawHeight = srcMipHeight;

            // The raw image does not have to be big enough to fill the entire packed
            // encoding.
            uint32 expRawWidth = ALIGN_SIZE( rawWidth, rawColumnWidth );
            uint32 expRawHeight = ALIGN_SIZE( rawHeight, rawColumnHeight );

            columnWidthCount = ( expRawWidth / rawColumnWidth );
            columnHeightCount = ( expRawHeight / rawColumnHeight );

            if ( hasDestinationDimms )
            {
                packedWidth = dstMipWidthInOut;
                packedHeight = dstMipHeightInOut;
            }
            else
            {
                packedWidth = ALIGN_SIZE( ( packedColumnWidth * columnWidthCount ) / permHoriSplit, packedColumnWidth );
                packedHeight = ( packedColumnHeight * columnHeightCount );
            }
        }
        else
        {
            packedWidth = srcMipWidth;
            packedHeight = srcMipHeight;

            if ( lenientPacked == true )
            {
                // This is a special mode where we allow partial packed transformation.
                // Essentially, we allow broken behavior, because somebody else thought it was a good idea.
                uint32 expPackedWidth = ALIGN_SIZE( packedWidth, packedColumnWidth );
                uint32 expPackedHeight = ALIGN_SIZE( packedHeight, packedColumnHeight );

                columnWidthCount = ( expPackedWidth / packedColumnWidth );
                columnHeightCount = ( expPackedHeight / packedColumnHeight );
            }
            else
            {
                // If texels are packed, they have to be properly formatted.
                // Else there is a real problem.
                assert((packedWidth % packedColumnWidth) == 0);
                assert((packedHeight % packedColumnHeight) == 0);

                columnWidthCount = ( packedWidth / packedColumnWidth );
                columnHeightCount = ( packedHeight / packedColumnHeight );
            }

            if ( hasDestinationDimms )
            {
                rawWidth = dstMipWidthInOut;
                rawHeight = dstMipHeightInOut;
            }
            else
            {
                rawWidth = ( ( rawColumnWidth * columnWidthCount ) * permHoriSplit );
                rawHeight = ( rawColumnHeight * columnHeightCount );
            }
        }

        // Determine the dimensions of the destination data.
        // We could have them already.
        uint32 dstMipWidth, dstMipHeight;

        if ( isPack )
        {
            dstMipWidth = packedWidth;
            dstMipHeight = packedHeight;
        }
        else
        {
            dstMipWidth = rawWidth;
            dstMipHeight = rawHeight;
        }

        // Allocate the container for the destination tranformation.
        uint32 dstFormatDepth;

        if ( isPack )
        {
            dstFormatDepth = packedDepth;
        }
        else
        {
            dstFormatDepth = rawDepth;
        }

        uint32 dstRowSize = getRasterDataRowSize( dstMipWidth, dstFormatDepth, dstRowAlignment );

        uint32 dstDataSize = getRasterDataSizeByRowSize( dstRowSize, dstMipHeight );

        void *newtexels = engineInterface->PixelAllocate( dstDataSize );

        if ( newtexels )
        {
            // Determine the encoding permutation.
            const uint32 *permutationData_primCol = nullptr;
            const uint32 *permutationData_secCol = nullptr;

            baseSystem::detect_packing_routine(
                rawFormat, packedFormat,
                permutationData_primCol,
                permutationData_secCol
            );

            if (permutationData_primCol != nullptr && permutationData_secCol != nullptr)
            {
                // Permute!
                permutationUtilities::permuteArray(
                    srcToBeTransformed, rawWidth, rawHeight, rawDepth, rawColumnWidth, rawColumnHeight,
                    newtexels, packedWidth, packedHeight, packedDepth, packedColumnWidth, packedColumnHeight,
                    columnWidthCount, columnHeightCount,
                    permutationData_primCol, permutationData_secCol,
                    permWidth, permHeight,
                    permutationStride, permHoriSplit,
                    srcRowAlignment, dstRowAlignment,
                    !isPack
                );
            }
            else
            {
                engineInterface->PixelFree( newtexels );

                newtexels = nullptr;
            }
        }

        if ( newtexels )
        {
            // Return the destination dimms.
            if ( !hasDestinationDimms )
            {
                dstMipWidthInOut = dstMipWidth;
                dstMipHeightInOut = dstMipHeight;
            }

            dstDataSizeOut = dstDataSize;
        }

        return newtexels;
    }

    inline static bool getPackedFormatDimensions(
        encodingFormatType rawFormat, encodingFormatType packedFormat,
        uint32 rawWidth, uint32 rawHeight,
        uint32& packedWidthOut, uint32& packedHeightOut
    )
    {
        // Get the encoding properties of the source data.
        uint32 rawColumnWidth, rawColumnHeight;

        bool gotRawDimms = baseSystem::getEncodingFormatDimensions(rawFormat, rawColumnWidth, rawColumnHeight);

        if ( gotRawDimms == false )
        {
            return false;
        }

        uint32 rawDepth = baseSystem::getFormatEncodingDepth(rawFormat);

        // Make sure the raw texture is a multiple of the column dimensions.
        uint32 expRawWidth = ALIGN_SIZE( rawWidth, rawColumnWidth );
        uint32 expRawHeight = ALIGN_SIZE( rawHeight, rawColumnHeight );

        // Get the amount of columns from our source image.
        // The encoded image must have the same amount of columns, after all.
        uint32 rawWidthColumnCount = ( expRawWidth / rawColumnWidth );
        uint32 rawHeightColumnCount = ( expRawHeight / rawColumnHeight );

        // Get the encoding properties of the target data.
        uint32 packedColumnWidth, packedColumnHeight;

        bool gotPackedDimms = baseSystem::getEncodingFormatDimensions(packedFormat, packedColumnWidth, packedColumnHeight);

        if ( gotPackedDimms == false )
        {
            return false;
        }

        uint32 packedDepth = baseSystem::getFormatEncodingDepth(packedFormat);

        // Get the dimensions of the packed format.
        // Since it has to have the same amount of columns, we multiply those column
        // dimensions with the column counts.
        uint32 packedWidth = ( rawWidthColumnCount * packedColumnWidth );
        uint32 packedHeight = ( rawHeightColumnCount * packedColumnHeight );

        if ( rawFormat != packedFormat )
        {
            // Get permutation dimensions.
            uint32 permWidth, permHeight;

            encodingFormatType permFormat = encodingFormatType::FORMAT_UNKNOWN;

            if ( rawDepth < packedDepth )
            {
                permFormat = rawFormat;
            }
            else
            {
                permFormat = packedFormat;
            }

            bool gotPermDimms = baseSystem::getPermutationDimensions(permFormat, permWidth, permHeight);

            if ( gotPermDimms == false )
            {
                return false;
            }

            // Get the amount of raw texels that can be put into one packed texel.
            uint32 permutationStride;

            bool isPack = baseSystem::isPackOperation( rawFormat, packedFormat );

            if ( isPack )
            {
                permutationStride = ( packedDepth / rawDepth );
            }
            else
            {
                permutationStride = ( rawDepth / packedDepth );
            }

            // Get the horizontal split count.
            uint32 permHoriSplit = ( permutationStride / permWidth );

            // Give the values to the runtime.
            if ( isPack )
            {
                packedWidthOut = ( packedWidth / permHoriSplit );
            }
            else
            {
                packedWidthOut = ( packedWidth * permHoriSplit );
            }
            packedHeightOut = packedHeight;
        }
        else
        {
            // Just return what we have.
            packedWidthOut = packedWidth;
            packedHeightOut = packedHeight;
        }

        // Make sure we align the packed coordinates.
        packedWidthOut = ALIGN_SIZE( packedWidthOut, packedColumnWidth );
        packedHeightOut = ALIGN_SIZE( packedHeightOut, packedColumnHeight );

        return true;
    }
};

}

}

#endif //RW_TEXTURE_MEMORY_ENCODING
