#include "StdInc.h"

#ifdef RWLIB_INCLUDE_NATIVETEX_PSP

#include <type_traits>

#include "txdread.psp.hxx"

#include "txdread.psp.mem.hxx"

#include "txdread.miputil.hxx"

#include "streamutil.hxx"

#include "pixelutil.hxx"

#include "txdread.ps2shared.enc.hxx"

namespace rw
{

void pspNativeTextureTypeProvider::SerializeTexture( TextureBase *theTexture, PlatformTexture *nativeTex, BlockProvider& inputProvider ) const
{
    // We write a very similar structure to the PS2 native texture.
    Interface *engineInterface = theTexture->engineInterface;

    const NativeTexturePSP *pspTex = (NativeTexturePSP*)nativeTex;

    uint32 mipmapCount = (uint32)pspTex->mipmaps.GetCount();

    if ( mipmapCount == 0 )
    {
        throw RwException( "attempt to write an empty PSP native texture" );
    }

    {
        BlockProvider metaBlock( &inputProvider );

        metaBlock.EnterContext();

        try
        {
            // Basically the checksum and the filtering flags.
            metaBlock.writeUInt32( PSP_FOURCC );

            // Thankfully Rockstar Leeds was much more traditional than War Drum.
            // They actually planned to use the Criterion filtering flags struct!
            // No need to reinvent the wheel, amirite?
            texFormatInfo formatInfo;
            formatInfo.set( *theTexture );

            formatInfo.writeToBlock( metaBlock );
        }
        catch( ... )
        {
            metaBlock.LeaveContext();

            throw;
        }
        
        metaBlock.LeaveContext();
    }

    // Write the texture names.
    {
        const rwString <char>& texName = theTexture->GetName();

        utils::writeStringChunkANSI( engineInterface, inputProvider, texName.GetConstString(), texName.GetLength() );
    }
    {
        const rwString <char>& maskName = theTexture->GetMaskName();

        utils::writeStringChunkANSI( engineInterface, inputProvider, maskName.GetConstString(), maskName.GetLength() );
    }

    // Now write GPU data.
    {
        BlockProvider colorMainBlock( &inputProvider );

        colorMainBlock.EnterContext();

        try
        {
            // Write a header block which contains important info about the texture format.
            uint32 depth = pspTex->depth;
            {
                BlockProvider formatMetaBlock( &colorMainBlock );

                formatMetaBlock.EnterContext();

                try
                {
                    const NativeTexturePSP::GETexture& baseLayer = pspTex->mipmaps[ 0 ];

                    psp::textureMetaDataHeader header;
                    header.width = baseLayer.width;
                    header.height = baseLayer.height;
                    header.mipmapCount = mipmapCount;
                    header.depth = depth;
                    header.unknown = pspTex->unk;

                    formatMetaBlock.writeStruct( header );
                }
                catch( ... )
                {
                    formatMetaBlock.LeaveContext();

                    throw;
                }

                formatMetaBlock.LeaveContext();
            }

            // Write the color buffers now.
            {
                BlockProvider gpuDataBlock( &colorMainBlock );

                gpuDataBlock.EnterContext();

                try
                {
                    // First write all mipmaps.
                    for ( uint32 n = 0; n < mipmapCount; n++ )
                    {
                        const NativeTexturePSP::GETexture& mipLayer = pspTex->mipmaps[ n ];

                        gpuDataBlock.write( mipLayer.texels, mipLayer.dataSize );
                    }

                    ePaletteType paletteType;
                    eColorOrdering colorOrder;

                    eRasterFormat rasterFormat = decodeDepthRasterFormat( depth, colorOrder, paletteType );

                    if ( paletteType != PALETTE_NONE )
                    {
                        // Write palette data aswell.
                        uint32 palRasterDepth = Bitmap::getRasterFormatDepth( rasterFormat );

                        uint32 paletteSize = getPaletteItemCount( paletteType );

                        uint32 palDataSize = getPaletteDataSize( paletteSize, palRasterDepth );

                        // Contrary to other platforms, we always have a properly sized palette buffer.
                        gpuDataBlock.write( pspTex->palette, palDataSize );
                    }
                }
                catch( ... )
                {
                    gpuDataBlock.LeaveContext();

                    throw;
                }

                gpuDataBlock.LeaveContext();
            }
        }
        catch( ... )
        {
            colorMainBlock.LeaveContext();

            throw;
        }

        colorMainBlock.LeaveContext();
    }

    // Finally, write the extensions.
    engineInterface->SerializeExtensions( theTexture, inputProvider );
}

inline bool TranscodePermutePSPMipmapLayer_native(
    Interface *engineInterface,
    uint32 layerWidth, uint32 layerHeight, const void *srcTexels,
    uint32 itemDepth,
    uint32 srcRowAlignment, uint32 dstRowAlignment,
    eFormatEncodingType swizzlePermutationEncoding,
    bool doSwizzleOrUnswizzle,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    void *dstTexels = nullptr;
    uint32 dstDataSize = 0;

    bool success = false;

    if ( swizzlePermutationEncoding == eFormatEncodingType::FORMAT_TEX32 )
    {
        assert( itemDepth == 32 );

        // TODO: we can generalize this routine into a pixel-block-unpacker algorithm based on little-data.

        // In contrast to the Graphics Synthesizer memory encoding, the PSP appears to have a mixture
        // between column-based encoding and binary buffer linear placement encoding.
        // The strength of the latter is that ít permutes things differently based on the image dimensions.
        // It is what we want to implement here.
        const uint32 permDepth = 128;

        const uint32 permItemCount = ( permDepth / itemDepth );

        uint32 permutePane_width = ( layerWidth / permItemCount );
        uint32 permutePane_height = ( layerHeight );

        const uint32 psmct32_permCluster_width = 1;
        const uint32 psmct32_permCluster_height = 8;

        success =
            memcodec::permutationUtilities::TranscodeTextureLayerTiles(
                engineInterface, permutePane_width, permutePane_height, srcTexels,
                permDepth,
                srcRowAlignment, dstRowAlignment,
                psmct32_permCluster_width, psmct32_permCluster_height,
                doSwizzleOrUnswizzle,
                dstTexels, dstDataSize
            );
    }
    // Otherwise we have encountered an unknown permutation strategy.
    // Should not happen, but we handle it safely in case.

    if ( success )
    {
        dstTexelsOut = dstTexels;
        dstDataSizeOut = dstDataSize;
    }

    return success;
}

inline bool DecodePSPMipmapLayer(
    Interface *engineInterface,
    uint32 layerWidth, uint32 layerHeight, const void *srcTexels, uint32 srcDataSize,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType,
    uint32 paletteSize,
    bool isSwizzled,
    eFormatEncodingType swizzleRawFormatEncoding, eFormatEncodingType swizzleColorBufferFormat,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    void *dstTexels = nullptr;
    uint32 dstDataSize = 0;

    // If the texture is swizzled, we want to unswizzle it.
    // If requiresTexelCopy == true, then color data has already been copied in destination
    // row format into dstTexels.
    bool requiresTexelCopy = false;

    if ( isSwizzled )
    {
        // Unswizzling depends on whether we are handling a packing-conversion or a simple permutation-conversion.
        if ( swizzleRawFormatEncoding == swizzleColorBufferFormat )
        {
            // We must have the same depth in raw and packed format.
            if ( srcDepth != dstDepth )
            {
                throw RwException( "invalid srcDepth and dstDepth in PSP native texture mipmap encoding" );
            }

            // This is the permutation convention. Texture data is not packed into smaller units but permuted on a large panel.
            bool couldTranscode =
                TranscodePermutePSPMipmapLayer_native(
                    engineInterface, layerWidth, layerHeight, srcTexels,
                    srcDepth,
                    srcRowAlignment, dstRowAlignment,
                    swizzleRawFormatEncoding,
                    false,
                    dstTexels, dstDataSize
                );

            if ( !couldTranscode )
            {
                // Something went wrong, so bail.
                return false;
            }

            requiresTexelCopy = false;
        }
        else
        {
            // We want to get pseudo-packed dimensions.
            // Unfortunately we cannot use the correct method of obtaining the packed dimensions,
            // because the lowest mipmap levels must be handled broken.
            uint32 packedWidth, packedHeight;

            // Get the broken ones.
            bool gotBrokenDimms = getPSPBrokenPackedFormatDimensions(
                swizzleRawFormatEncoding, swizzleColorBufferFormat,
                layerWidth, layerHeight,
                packedWidth, packedHeight
            );

            if ( !gotBrokenDimms )
            {
                // We can abort fetching mipmaps at any time.
                // It goes well with direct acquisition too, because not getting one layer
                // does not change the end result, as long as the result is not empty.
                return false;
            }

            dstTexels = pspMemoryEncoding::transformImageData(
                engineInterface,
                swizzleColorBufferFormat, swizzleRawFormatEncoding,
                srcTexels, packedWidth, packedHeight,
                srcRowAlignment, dstRowAlignment,
                layerWidth, layerHeight,
                dstDataSize, true,
                true
            );

            if ( !dstTexels )
            {
                throw RwException( "failed to unswizzle PSP native texture color buffer" );
            }

            // Since we already have the texels in the destination buffer, we do not require a copy anymore.
            requiresTexelCopy = false;
        }
    }
    else
    {
        // The conversion to the framework-friendly format has the same layer dimensions as supplied with this native texture.
        // Also the depth stays the same. The only changing factor is the export row alignment, hence we cannot say that the datasize stays the same.
        uint32 dstRowSize = getRasterDataRowSize( layerWidth, dstDepth, dstRowAlignment );

        dstDataSize = getRasterDataSizeByRowSize( dstRowSize, layerHeight );

        dstTexels = engineInterface->PixelAllocate( dstDataSize );

        if ( !dstTexels )
        {
            throw RwException( "failed to allocate texture buffer in PSP native texture pixel fetch" );
        }

        // We just allocated a buffer, so we definately require a texel copy.
        requiresTexelCopy = true;
    }

    assert( dstTexels != nullptr );

    try
    {
        // If we are not a palette texture, we must convert the colors from PSP format to framework format.
        if ( srcPaletteType == PALETTE_NONE )
        {
            assert( dstPaletteType == PALETTE_NONE );

            // Determine where to fetch colors from.
            const void *colorSourceTexels = nullptr;
            uint32 colorSourceRowAlignment = 0;
            uint32 colorSourceDataSize = 0;

            if ( requiresTexelCopy )
            {
                colorSourceTexels = srcTexels;
                colorSourceRowAlignment = srcRowAlignment;
                colorSourceDataSize = srcDataSize;
            }
            else
            {
                colorSourceTexels = dstTexels;
                colorSourceRowAlignment = dstRowAlignment;
                colorSourceDataSize = dstDataSize;
            }

            convertTexelsFromPS2(
                colorSourceTexels, dstTexels,
                layerWidth, layerHeight, colorSourceDataSize,
                srcRasterFormat, srcDepth, colorSourceRowAlignment, srcColorOrder,
                dstRasterFormat, dstDepth, dstRowAlignment, dstColorOrder,
                true
            );

            // After this operation we can say for sure that colors are in the destination color buffer.
            requiresTexelCopy = false;
        }
        else
        {
            assert( dstPaletteType != PALETTE_NONE );

            // If we still require a texel copy, do it here.
            if ( requiresTexelCopy )
            {
                ConvertPaletteDepth(
                    srcTexels, dstTexels,
                    layerWidth, layerHeight,
                    srcPaletteType, dstPaletteType,
                    paletteSize,
                    srcDepth, dstDepth,
                    srcRowAlignment, dstRowAlignment
                );

                requiresTexelCopy = false;
            }
        }
    }
    catch( ... )
    {
        engineInterface->PixelFree( dstTexels );

        throw;
    }

    dstTexelsOut = dstTexels;
    dstDataSizeOut = dstDataSize;
    return true;
}

void pspNativeTextureTypeProvider::GetPixelDataFromTexture( Interface *engineInterface, void *objMem, pixelDataTraversal& pixelsOut )
{
    // Alright, time to allow everyone to edit the contents of PSP native textures!
    // Even though it is a dead format, I think there can be valuable stuff to get here.

    NativeTexturePSP *nativeTex = (NativeTexturePSP*)objMem;

    uint32 srcDepth = nativeTex->depth;

    ePaletteType srcPaletteType;
    eColorOrdering srcColorOrder;

    eRasterFormat srcRasterFormat = decodeDepthRasterFormat( srcDepth, srcColorOrder, srcPaletteType );

    if ( srcRasterFormat == RASTER_DEFAULT )
    {
        throw RwException( "fatal error: attempt to fetch pixel data from unknown PSP native texture" );
    }

    void *srcPaletteData = nativeTex->palette;
    uint32 srcPaletteSize = getPaletteItemCount( srcPaletteType );

    uint32 srcRowAlignment = getPSPTextureDataRowAlignment();

    size_t mipmapCount = nativeTex->mipmaps.GetCount();

    // If data is swizzled, we need to know about the color buffer format conversions.
    eFormatEncodingType swizzleColorBufferFormat = FORMAT_UNKNOWN;
    eFormatEncodingType swizzleRawFormatEncoding = FORMAT_UNKNOWN;
    {
        swizzleColorBufferFormat = nativeTex->colorBufferFormat;
        swizzleRawFormatEncoding = getFormatEncodingFromRasterFormat( srcRasterFormat, srcPaletteType );
    }

    // Determine the target raster format to decode to.
    eRasterFormat dstRasterFormat = srcRasterFormat;
    eColorOrdering dstColorOrder = srcColorOrder;
    uint32 dstDepth = srcDepth;
    uint32 dstRowAlignment = getPSPExportTextureDataRowAlignment();

    ePaletteType dstPaletteType = srcPaletteType;

    // Convert the mipmap layers.
    try
    {
        size_t mip_index = 0;

        while ( mip_index < mipmapCount )
        {
            const NativeTexturePSP::GETexture& srcLayer = nativeTex->mipmaps[ mip_index ];

            uint32 layerWidth = srcLayer.width;
            uint32 layerHeight = srcLayer.height;

            void *srcTexels = srcLayer.texels;
            uint32 srcDataSize = srcLayer.dataSize;

            void *dstTexels = nullptr;
            uint32 dstDataSize = 0;

            // Check if this layer is swizzled.
            bool isLayerSwizzled = srcLayer.isSwizzled;

            bool couldDecodeLayer = 
                DecodePSPMipmapLayer(
                    engineInterface,
                    layerWidth, layerHeight, srcTexels, srcDataSize,
                    srcRasterFormat, srcDepth, srcRowAlignment, srcColorOrder, srcPaletteType,
                    dstRasterFormat, dstDepth, dstRowAlignment, dstColorOrder, dstPaletteType,
                    srcPaletteSize,
                    isLayerSwizzled,
                    swizzleRawFormatEncoding, swizzleColorBufferFormat,
                    dstTexels, dstDataSize
                );

            if ( !couldDecodeLayer )
            {
                // We can safely quit decoding.
                break;
            }

            // Store the decoded mipmap layer.
            pixelsOut.mipmaps.Resize( mip_index + 1 );

            pixelDataTraversal::mipmapResource& dstLayer = pixelsOut.mipmaps[ mip_index ];
            dstLayer.layerWidth = layerWidth;
            dstLayer.layerHeight = layerHeight;
            dstLayer.width = layerWidth;
            dstLayer.height = layerHeight;
            dstLayer.texels = dstTexels;
            dstLayer.dataSize = dstDataSize;
            
            mip_index++;
        }

        if ( mip_index == 0 )
        {
            throw RwException( "failed to fetch any color data from PSP native texture" );
        }

        // Also export the palette, if we are a palettized texture.
        // Luckily, the palette is never turned into a native CLUT.
        uint32 dstPaletteSize = 0;
        void *dstPaletteData = nullptr;

        if ( srcPaletteType != PALETTE_NONE )
        {
            assert( dstPaletteType != PALETTE_NONE );

            // Since we can acquire all formats that the PSP native texture could ever export,
            // we always keep the same raster format. In that case, the palette size stays the same
            // for the export, which means that we can do some optimizations!

            assert( srcRasterFormat == dstRasterFormat );
            assert( srcPaletteType == dstPaletteType );

            uint32 clutWidth, clutHeight;
            getEffectivePaletteTextureDimensions( dstPaletteType, clutWidth, clutHeight );

            eFormatEncodingType clutEncodingType = getFormatEncodingFromRasterFormat( dstRasterFormat, PALETTE_NONE );

            GetPS2TexturePalette(
                engineInterface,
                clutWidth, clutHeight,
                clutEncodingType, srcPaletteData,
                srcRasterFormat, srcColorOrder,
                dstRasterFormat, dstColorOrder,
                dstPaletteType,
                dstPaletteData, dstPaletteSize
            );
        }

        // Give information about the format to the runtime.
        pixelsOut.rasterFormat = dstRasterFormat;
        pixelsOut.depth = dstDepth;
        pixelsOut.rowAlignment = dstRowAlignment;
        pixelsOut.colorOrder = dstColorOrder;
        pixelsOut.paletteType = dstPaletteType;
        pixelsOut.paletteData = dstPaletteData;
        pixelsOut.paletteSize = dstPaletteSize;

        // There is no compression support for the PSP native texture.
        // Thank you, Rockstar Leeds!
        pixelsOut.compressionType = RWCOMPRESS_NONE;

        // Since there is no alpha flag in this native texture, we need to calculate it.
        pixelsOut.hasAlpha = calculateHasAlpha( engineInterface, pixelsOut );
        
        pixelsOut.autoMipmaps = false;
        pixelsOut.cubeTexture = false;
        pixelsOut.rasterType = 4;

        // We always have to convert between PS2/PSP and PC colors, so it does not make sense to directly
        // acquire pixel data from PSP native textures.
        pixelsOut.isNewlyAllocated = true;
    }
    catch( ... )
    {
        // If there was any exception when exporting the texel data, the texels
        // will for sure not be used anymore. We must release memory here.
        pixelsOut.FreePixels( engineInterface );

        throw;
    }
}

inline bool TranscodeMipmapToPSPFormat(
    Interface *engineInterface,
    uint32 layerWidth, uint32 layerHeight, const void *srcTexels, uint32 srcDataSize,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType,
    uint32 paletteSize,
    bool requiresSwizzle, bool requiresMipmapDestinationConversion,
    eFormatEncodingType swizzleRawFormatEncoding, eFormatEncodingType swizzleColorBufferFormat,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    void *allocatedTexels = nullptr;
    uint32 allocatedTexelsDataSize = 0;

    uint32 transRowAlignment = 0;

    const void *linearTransColors = nullptr;

    if ( !requiresSwizzle || requiresMipmapDestinationConversion )
    {
        {
            uint32 dstRowSize = getRasterDataRowSize( layerWidth, dstDepth, dstRowAlignment );

            allocatedTexelsDataSize = getRasterDataSizeByRowSize( dstRowSize, layerHeight );

            allocatedTexels = engineInterface->PixelAllocate( allocatedTexelsDataSize );
        }

        if ( allocatedTexels == nullptr )
        {
            throw RwException( "failed to allocate PSP color transformation buffer in texel acquisition routine" );
        }

        try
        {
            if ( dstPaletteType == PALETTE_NONE )
            {
                // If we are not a palette texture, we have color samples in the main buffers.
                // We have to convert the color samples to PSP color samples!

                // Do the color conversion.
                convertTexelsToPS2(
                    srcTexels, allocatedTexels,
                    layerWidth, layerHeight, srcDataSize,
                    srcRasterFormat, dstRasterFormat,
                    srcDepth, srcRowAlignment,
                    dstDepth, dstRowAlignment,
                    srcColorOrder, dstColorOrder,
                    true
                );
            }
            else
            {
                // If we are a palette texture, we possibly have to convert indices to proper depth.

                ConvertPaletteDepth(
                    srcTexels, allocatedTexels,
                    layerWidth, layerHeight,
                    srcPaletteType, dstPaletteType,
                    paletteSize,
                    srcDepth, dstDepth,
                    srcRowAlignment, dstRowAlignment
                );
            }
        }
        catch( ... )
        {
            engineInterface->PixelFree( allocatedTexels );

            throw;
        }

        transRowAlignment = dstRowAlignment;

        linearTransColors = allocatedTexels;
    }
    else
    {
        // We can just take the source texels, because we assume their format is good enough.

        if ( !requiresSwizzle )
        {
            // Need to allocate a new layer because it will be directly given to the runtime.
            allocatedTexels = engineInterface->PixelAllocate( srcDataSize );

            if ( !allocatedTexels )
            {
                throw RwException( "failed to allocate PSP native texture internal color buffer" );
            }

            allocatedTexelsDataSize = srcDataSize;

            memcpy( allocatedTexels, srcTexels, srcDataSize );

            linearTransColors = allocatedTexels;
        }
        else
        {
            // We can go out optimized, because we will allocate a new layer in the swizzling logic.
            linearTransColors = srcTexels;
        }
 
        transRowAlignment = srcRowAlignment;
    }

    // Determine the destination texels.
    void *dstTexels = nullptr;
    uint32 dstDataSize = 0;

    bool couldTranscodeLayer = false;

    try
    {
        bool preprocessSuccess = false;

        if ( requiresSwizzle )
        {
            void *swizzleTexels = nullptr;
            uint32 swizzleDataSize = 0;

            bool swizzleSuccess = false;

            // Decide whether we need a permutation or packing conversion.
            if ( swizzleRawFormatEncoding == swizzleColorBufferFormat )
            {
                // Do the permutation, eh.
                TranscodePermutePSPMipmapLayer_native(
                    engineInterface, layerWidth, layerHeight, linearTransColors,
                    dstDepth, 
                    transRowAlignment, dstRowAlignment,
                    swizzleRawFormatEncoding,
                    true,
                    swizzleTexels, swizzleDataSize
                );
                
                swizzleSuccess = true;
            }
            else
            {
                // We simply swizzle the data.
                // For that we need the packed dimensions.
                uint32 packedWidth, packedHeight;

                bool gotBrokenPackedDimms = getPSPBrokenPackedFormatDimensions(
                    swizzleRawFormatEncoding, swizzleColorBufferFormat,
                    layerWidth, layerHeight,
                    packedWidth, packedHeight
                );

                if ( gotBrokenPackedDimms )
                {
                    // Do it!
                    swizzleTexels = pspMemoryEncoding::transformImageData(
                        engineInterface,
                        swizzleRawFormatEncoding, swizzleColorBufferFormat,
                        linearTransColors, layerWidth, layerHeight,
                        transRowAlignment, dstRowAlignment,
                        packedWidth, packedHeight,
                        swizzleDataSize,
                        true, true
                    );

                    if ( !swizzleTexels )
                    {
                        throw RwException( "failed to swizzle mipmap data for PSP native texture" );
                    }

                    swizzleSuccess = true;
                }
            }

            if ( swizzleSuccess )
            {
                if ( linearTransColors == allocatedTexels )
                {
                    // Free the old texels.
                    engineInterface->PixelFree( allocatedTexels );
                }

                dstTexels = swizzleTexels;
                dstDataSize = swizzleDataSize;

                preprocessSuccess = true;
            }
        }
        else
        {
            assert( linearTransColors != nullptr );
            assert( linearTransColors == allocatedTexels );

            // We return the texels in linear form.
            dstTexels = allocatedTexels;
            dstDataSize = allocatedTexelsDataSize;

            // If we have nothing to swizzle, preprocess is successful by default.
            preprocessSuccess = true;
        }

        if ( preprocessSuccess )
        {
            couldTranscodeLayer = true;
        }
    }
    catch( ... )
    {
        if ( allocatedTexels )
        {
            engineInterface->PixelFree( allocatedTexels );
        }

        throw;
    }

    if ( couldTranscodeLayer )
    {
        dstTexelsOut = dstTexels;
        dstDataSizeOut = dstDataSize;
    }

    return couldTranscodeLayer;
}

static inline bool doesColorDataNeedPSPDestinationConversion(
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType
)
{
    bool requiresMipmapDestinationConversion = false;

    if ( dstPaletteType == PALETTE_NONE )
    {
        assert( srcPaletteType == PALETTE_NONE );

        // We always have to fix colors, kind of, so we want to convert.
        requiresMipmapDestinationConversion = true;
    }
    else
    {
        assert( srcPaletteType != PALETTE_NONE );

        // If the depth or the actual sample type changed, do things.
        if ( srcDepth != dstDepth || srcPaletteType != dstPaletteType )
        {
            requiresMipmapDestinationConversion = true;
        }
    }

    return requiresMipmapDestinationConversion;
}

void pspNativeTextureTypeProvider::SetPixelDataToTexture( Interface *engineInterface, void *objMem, const pixelDataTraversal& pixelsIn, acquireFeedback_t& feedbackOut )
{
    // In this function we receive general color data from the framework.
    // We want to pick the closest approximate raster format that is supported by this native texture
    // and convert the pixel data to it.

    // We cannot handle compressed texel data.
    // Unless somebody finds a way to store DXTn texels in the PSP native texture!
    if ( pixelsIn.compressionType != RWCOMPRESS_NONE )
    {
        throw RwException( "cannot process compressed texel data in PSP native texture color data acquisition" );
    }

    // Verify that the pixel data is following native size rules.
    {
        nativeTextureSizeRules sizeRules;

        getPSPNativeTextureSizeRules( sizeRules );

        bool isValidPixelData = sizeRules.verifyPixelData( pixelsIn );

        if ( !isValidPixelData )
        {
            throw RwException( "received texel data that violates native size rules in PSP native texture color data acquisition" );
        }
    }

    size_t mipmapCount = pixelsIn.mipmaps.GetCount();

    if ( mipmapCount == 0 )
    {
        throw RwException( "attempt to acquire empty texel data in PSP native texture color data acquisition" );
    }

    NativeTexturePSP *nativeTex = (NativeTexturePSP*)objMem;

    eRasterFormat srcRasterFormat = pixelsIn.rasterFormat;
    uint32 srcDepth = pixelsIn.depth;
    uint32 srcRowAlignment = pixelsIn.rowAlignment;
    eColorOrdering srcColorOrder = pixelsIn.colorOrder;
    ePaletteType srcPaletteType = pixelsIn.paletteType;
    const void *srcPaletteData = pixelsIn.paletteData;
    uint32 srcPaletteSize = pixelsIn.paletteSize;

    // Decide what format we want to encode to.
    eRasterFormat dstRasterFormat;
    uint32 dstDepth;
    uint32 dstRowAlignment = getPSPTextureDataRowAlignment();
    eColorOrdering dstColorOrder;
    ePaletteType dstPaletteType;
    
    if ( srcPaletteType != PALETTE_NONE )
    {
        if ( srcPaletteType == PALETTE_4BIT || srcPaletteType == PALETTE_4BIT_LSB )
        {
            dstPaletteType = PALETTE_4BIT;
            dstDepth = 4;
        }
        else
        {
            // We want to output anything else, that we may not know for entirely sure,
            // as palette format that is maximally supported, which is PAL8.
            dstPaletteType = PALETTE_8BIT;
            dstDepth = 8;
        }

        // The only really supported palette raster format is RASTER_8888.
        dstRasterFormat = RASTER_8888;
        dstColorOrder = COLOR_RGBA;
    }
    else
    {
        // I suspect that the PSP native texture supports RASTER_1555 and RASTER_8888 only.
        // This is the featureset of the PS2 native texture aswell.

        // TODO: if there is ever gonna be a 16bit texture, then add support for it.
#if 0
        if ( srcRasterFormat == RASTER_555 || srcRasterFormat == RASTER_1555 )
        {
            dstDepth = 16;
        }
        else
#endif
        {
            dstDepth = 32;
        }

        dstRasterFormat = decodeDepthRasterFormat( dstDepth, dstColorOrder, dstPaletteType );
    }

    // If we want to swizzle, we also want to know the color buffer format conversion parameters.
    eFormatEncodingType swizzleColorBufferFormat = FORMAT_UNKNOWN;
    eFormatEncodingType swizzleRawFormatEncoding = FORMAT_UNKNOWN;
    {
        swizzleColorBufferFormat = getPSPHardwareColorBufferFormat( dstDepth );

        if ( swizzleColorBufferFormat == FORMAT_UNKNOWN )
        {
            throw RwException( "failed to determine PSP native texture hardware color buffer format in texel acquisition routine" );
        }

        swizzleRawFormatEncoding = getFormatEncodingFromRasterFormat( dstRasterFormat, dstPaletteType );
    }

    // Determine whether we even need a destination conversion.
    // This conversion should prepare the color data for swizzling.
    bool requiresMipmapDestinationConversion = 
        doesColorDataNeedPSPDestinationConversion(
            srcRasterFormat, srcDepth, srcRowAlignment, srcColorOrder, srcPaletteType,
            dstRasterFormat, dstDepth, dstRowAlignment, dstColorOrder, dstPaletteType
        );

    // Convert the mipmap layers.
    size_t mip_index = 0;

    while ( mip_index < mipmapCount )
    {
        const pixelDataTraversal::mipmapResource& mipLayer = pixelsIn.mipmaps[ mip_index ];

        uint32 layerWidth = mipLayer.width;
        uint32 layerHeight = mipLayer.height;
        void *srcTexels = mipLayer.texels;
        uint32 srcDataSize = mipLayer.dataSize;

        // Create the destination data.
        void *dstTexels = nullptr;
        uint32 dstDataSize = 0;

        // Does this layer need swizzling?
        bool doesLayerNeedSwizzling = isPSPSwizzlingRequired( layerWidth, layerHeight, dstDepth );

        bool couldTranscodeLayer =
            TranscodeMipmapToPSPFormat(
                engineInterface,
                layerWidth, layerHeight, srcTexels, srcDataSize,
                srcRasterFormat, srcDepth, srcRowAlignment, srcColorOrder, srcPaletteType,
                dstRasterFormat, dstDepth, dstRowAlignment, dstColorOrder, dstPaletteType,
                srcPaletteSize, // == dstPaletteSize
                doesLayerNeedSwizzling, requiresMipmapDestinationConversion,
                swizzleRawFormatEncoding, swizzleColorBufferFormat,
                dstTexels, dstDataSize
            );

        if ( couldTranscodeLayer )
        {
            // We can store the successfully decoded texels as a new layer.
            NativeTexturePSP::GETexture newLayer;
            newLayer.width = layerWidth;
            newLayer.height = layerHeight;
            newLayer.texels = dstTexels;
            newLayer.dataSize = dstDataSize;

            newLayer.isSwizzled = doesLayerNeedSwizzling;

            nativeTex->mipmaps.AddToBack( std::move( newLayer ) );
        }
        else
        {
            // If we failed to apply any texels, we must free the buffer.
            if ( srcTexels != dstTexels )
            {
                engineInterface->PixelFree( dstTexels );
            }

            // Also do not attempt to process further mipmaps.
            break;
        }

        // Increase the mipmap index.
        mip_index++;
    }

    if ( mip_index == 0 )
    {
        throw RwException( "failed to set any color data to PSP native texture" );
    }

    // Store the palette into our texture aswell.
    void *dstPaletteData = nullptr;
    uint32 dstPaletteSize = 0;

    if ( srcPaletteType != PALETTE_NONE )
    {
        assert( dstPaletteType != PALETTE_NONE );

        dstPaletteSize = srcPaletteSize;

        uint32 srcPalRasterDepth = Bitmap::getRasterFormatDepth( srcRasterFormat );

        uint32 dstPalRasterDepth = Bitmap::getRasterFormatDepth( dstRasterFormat );

        uint32 clutWidth, clutHeight;
        getEffectivePaletteTextureDimensions( dstPaletteType, clutWidth, clutHeight );

        eFormatEncodingType clutEncodingType = getFormatEncodingFromRasterFormat( dstRasterFormat, PALETTE_NONE );

        uint32 dstCLUTDataSize;

        GeneratePS2CLUT(
            engineInterface,
            clutWidth, clutHeight,
            srcPaletteData, dstPaletteType, dstPaletteSize,
            clutEncodingType,
            srcRasterFormat, srcPalRasterDepth, srcColorOrder,
            dstRasterFormat, dstPalRasterDepth, dstColorOrder,
            dstPaletteData, dstCLUTDataSize
        );
    }

    nativeTex->depth = dstDepth;
    nativeTex->colorBufferFormat = swizzleColorBufferFormat;
    nativeTex->palette = dstPaletteData;
    nativeTex->unk = 0;

    // Due to color format differences we cannot ever directly acquire the data.
    feedbackOut.hasDirectlyAcquired = false;
}

void pspNativeTextureTypeProvider::UnsetPixelDataFromTexture( Interface *engineInterface, void *objMem, bool deallocate )
{
    // Clear all pixel data from this native texture.

    NativeTexturePSP *nativeTex = (NativeTexturePSP*)objMem;

    if ( deallocate )
    {
        // Request to delete all color memory from this native texture.
        size_t mipmapCount = nativeTex->mipmaps.GetCount();

        for ( size_t n = 0; n < mipmapCount; n++ )
        {
            NativeTexturePSP::GETexture& mipLayer = nativeTex->mipmaps[ n ];

            mipLayer.Deallocate( engineInterface );
        }

        if ( void *paletteData = nativeTex->palette )
        {
            engineInterface->PixelFree( paletteData );
        }
    }

    // Clear the raster status.
    nativeTex->mipmaps.Clear();

    nativeTex->depth = 0;
    nativeTex->colorBufferFormat = FORMAT_UNKNOWN;
    nativeTex->palette = nullptr;
    nativeTex->unk = 0;
}

template <bool isConst>
struct pspMipmapManager
{
    typedef typename std::conditional <isConst, const NativeTexturePSP, NativeTexturePSP>::type nativeType;

    nativeType *nativeTex;

    inline pspMipmapManager( nativeType *nativeTex )
    {
        this->nativeTex = nativeTex;
    }

    inline void GetLayerDimensions(
        const NativeTexturePSP::GETexture& mipLayer,
        uint32& layerWidth, uint32& layerHeight
    )
    {
        layerWidth = mipLayer.width;
        layerHeight = mipLayer.height;
    }

    inline void GetSizeRules( nativeTextureSizeRules& rulesOut ) const
    {
        // This function is not used, anyway.
        // If it would come to use, TODO: maybe dimensions depend on the actual texture!
        getPSPNativeTextureSizeRules( rulesOut );
    }

    inline void Deinternalize(
        Interface *engineInterface,
        const NativeTexturePSP::GETexture& mipLayer,
        uint32& widthOut, uint32& heightOut, uint32& layerWidthOut, uint32& layerHeightOut,
        eRasterFormat& dstRasterFormatOut, eColorOrdering& dstColorOrderOut, uint32& dstDepthOut,
        uint32& dstRowAlignmentOut,
        ePaletteType& dstPaletteTypeOut, void*& dstPaletteDataOut, uint32& dstPaletteSizeOut,
        eCompressionType& dstCompressionTypeOut, bool& hasAlphaOut,
        void*& dstTexelsOut, uint32& dstDataSizeOut,
        bool& isNewlyAllocatedOut, bool& isPaletteNewlyAllocatedOut
    )
    {
        // Give the requested mipmap layer to the runtime.
        uint32 layerWidth = mipLayer.width;
        uint32 layerHeight = mipLayer.height;

        const void *srcTexels = mipLayer.texels;
        uint32 dataSize = mipLayer.dataSize;

        // Create a minimal destination array.
        uint32 depth = nativeTex->depth;
        uint32 dstRowAlignment = getPSPExportTextureDataRowAlignment();

        ePaletteType paletteType;
        eColorOrdering colorOrder;

        eRasterFormat rasterFormat = decodeDepthRasterFormat( depth, colorOrder, paletteType );

        uint32 paletteSize = getPaletteItemCount( paletteType );

        bool isSwizzled = mipLayer.isSwizzled;

        eFormatEncodingType swizzleColorBufferFormat = FORMAT_UNKNOWN;
        eFormatEncodingType swizzleRawFormatEncoding = FORMAT_UNKNOWN;

        if ( isSwizzled )
        {
            swizzleColorBufferFormat = nativeTex->colorBufferFormat;
            swizzleRawFormatEncoding = getFormatEncodingFromRasterFormat( rasterFormat, paletteType );
        }

        void *dstTexels = nullptr;
        uint32 dstDataSize = 0;

        bool couldDecodeLayer = DecodePSPMipmapLayer(
            engineInterface,
            layerWidth, layerHeight, srcTexels, dataSize,
            rasterFormat, depth, getPSPTextureDataRowAlignment(), colorOrder, paletteType,
            rasterFormat, depth, dstRowAlignment, colorOrder, paletteType,
            paletteSize,
            isSwizzled,
            swizzleRawFormatEncoding, swizzleColorBufferFormat,
            dstTexels, dstDataSize
        );

        if ( !couldDecodeLayer )
        {
            throw RwException( "failed to decode PSP mipmap layer" );
        }

        void *dstPaletteData = nullptr;

        if ( void *srcPaletteData = nativeTex->palette )
        {
            uint32 clutWidth, clutHeight;
            getEffectivePaletteTextureDimensions( paletteType, clutWidth, clutHeight );

            eFormatEncodingType clutEncodingType = getFormatEncodingFromRasterFormat( rasterFormat, PALETTE_NONE );
            
            GetPS2TexturePalette(
                engineInterface,
                clutWidth, clutHeight,
                clutEncodingType,
                srcPaletteData,
                rasterFormat, colorOrder,
                rasterFormat, colorOrder,
                paletteType,
                dstPaletteData,
                paletteSize
            );
        }

        widthOut = layerWidth;
        heightOut = layerHeight;

        layerWidthOut = layerWidth;
        layerHeightOut = layerHeight;

        dstRasterFormatOut = rasterFormat;
        dstColorOrderOut = colorOrder;
        dstDepthOut = depth;
        dstRowAlignmentOut = dstRowAlignment;
        dstPaletteTypeOut = paletteType;
        dstPaletteDataOut = dstPaletteData;
        dstPaletteSizeOut = paletteSize;
        
        dstCompressionTypeOut = RWCOMPRESS_NONE;
        hasAlphaOut = false;

        dstTexelsOut = dstTexels;
        dstDataSizeOut = dstDataSize;

        // Just like the PS2 native texture, we must calculate the alpha flag ourselves.
        hasAlphaOut =
            rawMipmapCalculateHasAlpha(
                engineInterface,
                layerWidth, layerHeight, dstTexels, dstDataSize,
                rasterFormat, depth, dstRowAlignment, colorOrder,
                paletteType, dstPaletteData, paletteSize
            );

        isNewlyAllocatedOut = true; // we have to newly allocate the texels because they come "swizzled"
        isPaletteNewlyAllocatedOut = true;  // we always newly allocate the palette; because it comes "swizzled"
    }

    inline void Internalize(
        Interface *engineInterface,
        NativeTexturePSP::GETexture& mipLayer,
        uint32 width, uint32 height, uint32 layerWidth, uint32 layerHeight, void *srcTexels, uint32 dataSize,
        eRasterFormat rasterFormat, eColorOrdering colorOrder, uint32 depth,
        uint32 rowAlignment,
        ePaletteType paletteType, void *paletteData, uint32 paletteSize,
        eCompressionType compressionType, bool hasAlpha,
        bool& hasDirectlyAcquiredOut
    )
    {
        // If we do not receive any raw mipmap layers, we bail out.
        // It is very tedious if you'd have to convert the mipmap layer yourself on each function call.
        // So we should handle those pretty rare cases inside the framework itself.
        if ( compressionType != RWCOMPRESS_NONE )
        {
            throw RwException( "cannot receive mipmap layer in compressed format for PSP native texture" );
        }

        // We want to encode the mipmap layer into our format.

        uint32 dstDepth = nativeTex->depth;

        ePaletteType dstPaletteType;
        eColorOrdering dstColorOrder;

        eRasterFormat dstRasterFormat = decodeDepthRasterFormat( dstDepth, dstColorOrder, dstPaletteType );

        // Calculate whether this layer needs swizzling.
        bool isSwizzled = isPSPSwizzlingRequired( layerWidth, layerHeight, dstDepth );

        eFormatEncodingType swizzleColorBufferFormat = FORMAT_UNKNOWN;
        eFormatEncodingType swizzleRawFormatEncoding = FORMAT_UNKNOWN;

        if ( isSwizzled )
        {
            swizzleColorBufferFormat = nativeTex->colorBufferFormat;
            swizzleRawFormatEncoding = getFormatEncodingFromRasterFormat( dstRasterFormat, dstPaletteType );
        }

        // If the texture is palettized, then we have to have the palette for encoding.
        void *dstPaletteData = nullptr;
        uint32 dstPaletteSize = 0;

        if ( dstPaletteType != PALETTE_NONE )
        {
            void *clutTexels = nativeTex->palette;

            uint32 clutWidth, clutHeight;
            getEffectivePaletteTextureDimensions( dstPaletteType, clutWidth, clutHeight );

            eFormatEncodingType clutEncodingType = getFormatEncodingFromRasterFormat( dstRasterFormat, PALETTE_NONE );

            GetPS2TexturePalette(
                engineInterface,
                clutWidth, clutHeight,
                clutEncodingType,
                clutTexels,
                dstRasterFormat, dstColorOrder,
                dstRasterFormat, dstColorOrder,
                dstPaletteType,
                dstPaletteData,
                dstPaletteSize
            );
        }

        try
        {
            // We need to turn the source mipmap into the exact same format that the native texture has.
            void *dstTexels = nullptr;
            uint32 dstDataSize = 0;

            uint32 surfWidth, surfHeight;

            ConvertMipmapLayerNative(
                engineInterface,
                width, height, layerWidth, layerHeight, srcTexels, dataSize,
                rasterFormat, depth, rowAlignment, colorOrder, paletteType, paletteData, paletteSize, compressionType,
                dstRasterFormat, dstDepth, getPSPTextureDataRowAlignment(), dstColorOrder, dstPaletteType, dstPaletteData, dstPaletteSize, RWCOMPRESS_NONE,
                true,
                surfWidth, surfHeight,
                dstTexels, dstDataSize
            );

            assert( surfWidth == layerWidth );
            assert( surfHeight == layerHeight );

            try
            {
                // We must encode the mipmap layer properly.
                void *encodedTexels = nullptr;
                uint32 encodedDataSize = 0;

                bool transcodeSuccess = TranscodeMipmapToPSPFormat(
                    engineInterface,
                    layerWidth, layerHeight, dstTexels, dstDataSize,
                    dstRasterFormat, dstDepth, getPSPTextureDataRowAlignment(), dstColorOrder, dstPaletteType,
                    dstRasterFormat, dstDepth, getPSPTextureDataRowAlignment(), dstColorOrder, dstPaletteType,
                    dstPaletteSize,
                    isSwizzled, false,
                    swizzleRawFormatEncoding, swizzleColorBufferFormat,
                    encodedTexels, encodedDataSize
                );

                if ( !transcodeSuccess )
                {
                    throw RwException( "failed to encode PSP mipmap layer" );
                }

                // Give the encoded result to the runtime.
                mipLayer.width = surfWidth;
                mipLayer.height = surfHeight;
                mipLayer.texels = encodedTexels;
                mipLayer.dataSize = encodedDataSize;

                mipLayer.isSwizzled = isSwizzled;
            }
            catch( ... )
            {
                if ( dstTexels != srcTexels )
                {
                    engineInterface->PixelFree( dstTexels );
                }

                throw;
            }

            if ( dstTexels != srcTexels )
            {
                engineInterface->PixelFree( dstTexels );
            }
        }
        catch( ... )
        {
            // On error, remember to clean up things we allocated for ourselves.
            if ( dstPaletteData )
            {
                engineInterface->PixelFree( dstPaletteData );
            }

            throw;
        }

        // Make sure to release the palette after encoding.
        if ( dstPaletteData )
        {
            engineInterface->PixelFree( dstPaletteData );
        }

        // We never directly acquire, because we have to swizzle the data.
        hasDirectlyAcquiredOut = false;
    }
};

bool pspNativeTextureTypeProvider::GetMipmapLayer( Interface *engineInterface, void *objMem, uint32 mipIndex, rawMipmapLayer& layerOut )
{
    NativeTexturePSP *nativeTex = (NativeTexturePSP*)objMem;

    pspMipmapManager <false> mipMan( nativeTex );

    return virtualGetMipmapLayer
        <NativeTexturePSP::GETexture>
    (
        engineInterface, mipMan,
        mipIndex, nativeTex->mipmaps,
        layerOut
    );
}

bool pspNativeTextureTypeProvider::AddMipmapLayer( Interface *engineInterface, void *objMem, const rawMipmapLayer& layerIn, acquireFeedback_t& feedbackOut )
{
    NativeTexturePSP *nativeTex = (NativeTexturePSP*)objMem;

    pspMipmapManager <false> mipMan( nativeTex );

    return virtualAddMipmapLayer
        <NativeTexturePSP::GETexture>
    (
        engineInterface, mipMan,
        nativeTex->mipmaps,
        layerIn, feedbackOut
    );
}

void pspNativeTextureTypeProvider::ClearMipmaps( Interface *engineInterface, void *objMem )
{
    NativeTexturePSP *nativeTex = (NativeTexturePSP*)objMem;

    size_t mipmapCount = nativeTex->mipmaps.GetCount();

    if ( mipmapCount > 1 )
    {
        for ( size_t n = 1; n < mipmapCount; n++ )
        {
            NativeTexturePSP::GETexture& mipLayer = nativeTex->mipmaps[ n ];

            mipLayer.Deallocate( engineInterface );
        }

        nativeTex->mipmaps.Resize( 1 );
    }
}

bool pspNativeTextureTypeProvider::DoesTextureHaveAlpha( const void *objMem )
{
    // Just like in the PS2 native texture, this operation is expensive.
    // No alpha flag is being stored in the native texture, after all.

    const NativeTexturePSP *nativeTex = (const NativeTexturePSP*)objMem;

    Interface *engineInterface = nativeTex->engineInterface;

    pspMipmapManager <true> mipMan( nativeTex );

    rawMipmapLayer rawLayer;

    bool gotLayer = virtualGetMipmapLayer <NativeTexturePSP::GETexture> (
        engineInterface, mipMan,
        0,      // we just check the first layer, should be enough.
        nativeTex->mipmaps,
        rawLayer
    );

    if ( !gotLayer )
        return false;

    bool hasAlpha = false;

    try
    {
        // Just a security measure.
        assert( rawLayer.compressionType == RWCOMPRESS_NONE );

        hasAlpha =
            rawMipmapCalculateHasAlpha(
                engineInterface,
                rawLayer.mipData.layerWidth, rawLayer.mipData.layerHeight, rawLayer.mipData.texels, rawLayer.mipData.dataSize,
                rawLayer.rasterFormat, rawLayer.depth, rawLayer.rowAlignment, rawLayer.colorOrder,
                rawLayer.paletteType, rawLayer.paletteData, rawLayer.paletteSize
            );
    }
    catch( ... )
    {
        if ( rawLayer.isNewlyAllocated )
        {
            engineInterface->PixelFree( rawLayer.mipData.texels );
        }

        throw;
    }

    // Free memory.
    if ( rawLayer.isNewlyAllocated )
    {
        engineInterface->PixelFree( rawLayer.mipData.texels );
    }

    return hasAlpha;
}

};

#endif //RWLIB_INCLUDE_NATIVETEX_PSP