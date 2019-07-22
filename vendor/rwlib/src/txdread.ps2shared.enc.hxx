// Shared, encoding-based routines based on Sony PS2 architecture.
// This header was made to keep heavy routines local to the code that needs them.

namespace rw
{

// Color routines.
inline double clampcolor( double theColor )
{
    return std::min( 1.0, std::max( 0.0, theColor ) );
}

inline uint8 convertPCAlpha2PS2Alpha( uint8 pcAlpha )
{
    // TODO: generate a table that maps PC alpha to PS2 alpha.

    double pcAlphaDouble = clampcolor( (double)pcAlpha / 255.0 );

    double ps2AlphaDouble = pcAlphaDouble * 128.0;

    ps2AlphaDouble = floor( ps2AlphaDouble + 0.5 );

    uint8 ps2Alpha = (uint8)( ps2AlphaDouble );

    return ps2Alpha;
}

inline uint8 convertPS2Alpha2PCAlpha( uint8 ps2Alpha )
{
    // TODO: generate a table that maps PS2 alpha to PC alpha
    // it has to conform with the table in convertPCAlpha2PS2Alpha

    double ps2AlphaDouble = clampcolor( (double)ps2Alpha / 128.0 );

    double pcAlphaDouble = ps2AlphaDouble * 255.0;

    pcAlphaDouble = floor( pcAlphaDouble + 0.495 );

    uint8 pcAlpha = (uint8)( pcAlphaDouble );

    return pcAlpha;
}

static inline bool doesRequirePlatformDestinationConversion(
    eColorOrdering srcColorOrder, eColorOrdering dstColorOrder,
    eRasterFormat srcRasterFormat, eRasterFormat dstRasterFormat,
    uint32 mipWidth,
    uint32 srcItemDepth, uint32 srcRowAlignment,
    uint32 dstItemDepth, uint32 dstRowAlignment,
    bool fixAlpha
)
{
    return (
        fixAlpha ||
        doesRawMipmapBufferNeedFullConversion(
            mipWidth,
            srcRasterFormat, srcItemDepth, srcRowAlignment, srcColorOrder, PALETTE_NONE,
            dstRasterFormat, dstItemDepth, dstRowAlignment, dstColorOrder, PALETTE_NONE
        )
    );
}

inline void convertTexelsFromPS2(
    const void *texelSource, void *dstTexels, uint32 mipWidth, uint32 mipHeight, uint32 srcDataSize,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder,
    bool fixAlpha
)
{
    if (doesRequirePlatformDestinationConversion(
            srcColorOrder, dstColorOrder,
            srcRasterFormat, dstRasterFormat,
            mipWidth,
            srcDepth, srcRowAlignment,
            dstDepth, dstRowAlignment,
            fixAlpha
        )
    )
    {
        colorModelDispatcher fetchDispatch( srcRasterFormat, srcColorOrder, srcDepth, nullptr, 0, PALETTE_NONE );
        colorModelDispatcher putDispatch( dstRasterFormat, dstColorOrder, dstDepth, nullptr, 0, PALETTE_NONE );

        uint32 srcRowSize = getRasterDataRowSize( mipWidth, srcDepth, srcRowAlignment );
        uint32 dstRowSize = getRasterDataRowSize( mipWidth, dstDepth, dstRowAlignment );

        for (uint32 row = 0; row < mipHeight; row++)
        {
            const void *srcRow = getConstTexelDataRow( texelSource, srcRowSize, row );
            void *dstRow = getTexelDataRow( dstTexels, dstRowSize, row );

            for (uint32 col = 0; col < mipWidth; col++)
            {
                uint8 red, green, blue, alpha;
                fetchDispatch.getRGBA( srcRow, col, red, green, blue, alpha );

	            // fix alpha
                if (fixAlpha)
                {
                    uint8 newAlpha = convertPS2Alpha2PCAlpha(alpha);

#ifdef DEBUG_ALPHA_LEVELS
                    assert(convertPCAlpha2PS2Alpha(newAlpha) == alpha);
#endif //DEBUG_ALPHA_LEVELS

                    alpha = newAlpha;
                }

                putDispatch.setRGBA( dstRow, col, red, green, blue, alpha );
            }
	    }
    }
    else
    {
        memcpy( dstTexels, texelSource, srcDataSize );
    }
}

inline void convertTexelsToPS2(
    const void *srcTexelData, void *dstTexelData, uint32 mipWidth, uint32 mipHeight, uint32 srcDataSize,
    eRasterFormat srcRasterFormat, eRasterFormat dstRasterFormat,
    uint32 srcItemDepth, uint32 srcRowAlignment, uint32 dstItemDepth, uint32 dstRowAlignment,
    eColorOrdering srcColorOrder, eColorOrdering ps2ColorOrder,
    bool fixAlpha
)
{
    if (doesRequirePlatformDestinationConversion(
            srcColorOrder, ps2ColorOrder,
            srcRasterFormat, dstRasterFormat,
            mipWidth,
            srcItemDepth, srcRowAlignment,
            dstItemDepth, dstRowAlignment,
            fixAlpha
        )
    )
    {
        colorModelDispatcher fetchDispatch( srcRasterFormat, srcColorOrder, srcItemDepth, nullptr, 0, PALETTE_NONE );
        colorModelDispatcher putDispatch( dstRasterFormat, ps2ColorOrder, dstItemDepth, nullptr, 0, PALETTE_NONE );

        uint32 srcRowSize = getRasterDataRowSize( mipWidth, srcItemDepth, srcRowAlignment );
        uint32 dstRowSize = getRasterDataRowSize( mipWidth, dstItemDepth, dstRowAlignment );

		for ( uint32 row = 0; row < mipHeight; row++ )
        {
            const void *srcRow = getConstTexelDataRow( srcTexelData, srcRowSize, row );
            void *dstRow = getTexelDataRow( dstTexelData, dstRowSize, row );

            for ( uint32 col = 0; col < mipWidth; col++ )
            {
                uint8 red, green, blue, alpha;
                fetchDispatch.getRGBA( srcRow, col, red, green, blue, alpha );

		        // fix alpha
                if (fixAlpha)
                {
                    uint8 newAlpha = convertPCAlpha2PS2Alpha(alpha);

#ifdef DEBUG_ALPHA_LEVELS
                    assert(convertPS2Alpha2PCAlpha(newAlpha) == alpha);
#endif //DEBUG_ALPHA_LEVELS

                    alpha = newAlpha;
                }

                putDispatch.setRGBA( dstRow, col, red, green, blue, alpha );
            }
		}
    }
    else
    {
        memcpy( dstTexelData, srcTexelData, srcDataSize );
    }
}

/* convert from CLUT format used by the ps2 */
const static uint32 _clut_permute_psmct32[] =
{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static bool clut(
    Interface *engineInterface,
    ePaletteType paletteType, void *srcTexels,
    uint32 clutWidth, uint32 clutHeight, uint32 clutDataSize, eFormatEncodingType swizzleEncodingType,
    void*& newTexels
)
{
    const uint32 *permuteData = nullptr;
    uint32 permuteWidth, permuteHeight;
    uint32 itemDepth;

    bool requiresSwizzling = true;

    if (paletteType == PALETTE_8BIT)
    {
        permuteData = _clut_permute_psmct32;

        permuteWidth = 16;
        permuteHeight = 2;

        itemDepth = getFormatEncodingDepth(swizzleEncodingType);
    }
    else if (paletteType == PALETTE_4BIT)
    {
        requiresSwizzling = false;
    }
    else
    {
        assert( 0 );
    }

    bool success = false;

    if ( requiresSwizzling )
    {
        if (permuteData != nullptr)
        {
            // Create a new permutation destination.
            void *dstTexels = engineInterface->PixelAllocate( clutDataSize );

            uint32 alignedClutWidth = ALIGN_SIZE( clutWidth, permuteWidth );
            uint32 alignedClutHeight = ALIGN_SIZE( clutHeight, permuteHeight );

            uint32 colsWidth = ( alignedClutWidth / permuteWidth );
            uint32 colsHeight = ( alignedClutHeight / permuteHeight );

            const uint32 clutRequiredRowAlignment = 1;

            // Perform the permutation.
            memcodec::permutationUtilities::permuteArray(
                srcTexels, clutWidth, clutHeight, itemDepth, permuteWidth, permuteHeight,
                dstTexels, clutWidth, clutHeight, itemDepth, permuteWidth, permuteHeight,
                colsWidth, colsHeight,
                permuteData, permuteData, permuteWidth, permuteHeight,
                1, 1,
                clutRequiredRowAlignment, clutRequiredRowAlignment,
                false
            );

            // Return the new texels.
            newTexels = dstTexels;

            success = true;
        }
    }
    else
    {
        newTexels = srcTexels;

        success = true;
    }

    return success;
}

inline void getEffectivePaletteTextureDimensions(ePaletteType paletteType, uint32& palWidth_out, uint32& palHeight_out)
{
    uint32 palWidth = 0;
    uint32 palHeight = 0;

    if (paletteType == PALETTE_4BIT)
    {
        palWidth = 8;
        palHeight = 2;
    }
    else if (paletteType == PALETTE_8BIT)
    {
        palWidth = 16;
        palHeight = 16;
    }
    else
    {
        assert( 0 );
    }

    palWidth_out = palWidth;
    palHeight_out = palHeight;
}

inline void GetPS2TexturePalette(
    Interface *engineInterface,
    uint32 clutWidth, uint32 clutHeight, eFormatEncodingType clutEncodingType, void *clutTexels,
    eRasterFormat srcRasterFormat, eColorOrdering srcColorOrder,
    eRasterFormat dstRasterFormat, eColorOrdering dstColorOrder,
    ePaletteType paletteType,
    void*& dstPalTexels, uint32& dstPalSize
)
{
    const uint32 paletteRowAlignment = 1;

    // Prepare the palette colors.
    void *paletteTexelSource = clutTexels;

    // Prepare the unclut operation.
    uint32 palSize = ( clutWidth * clutHeight );

    uint32 srcPalFormatDepth = Bitmap::getRasterFormatDepth(srcRasterFormat);

    uint32 srcPalTexDataSize = getPaletteDataSize( palSize, srcPalFormatDepth );

    uint32 dstPalFormatDepth = Bitmap::getRasterFormatDepth(dstRasterFormat);

    assert( srcPalFormatDepth == dstPalFormatDepth );

    // Unswizzle the palette, now.
    void *clutPalTexels = nullptr;
    {
        bool unclutSuccess = clut(engineInterface, paletteType, paletteTexelSource, clutWidth, clutHeight, srcPalTexDataSize, clutEncodingType, clutPalTexels);

        if ( unclutSuccess == false )
        {
            throw RwException( "failed to unswizzle the CLUT" );
        }
    }

    // If we have not swizzled, then we need to allocate a new array.
    const void *srcTexels = clutPalTexels;

    if ( clutPalTexels == paletteTexelSource )
    {
        clutPalTexels = engineInterface->PixelAllocate( srcPalTexDataSize );
    }

    // Repair the colors.
    {
        uint32 realSwizzleWidth, realSwizzleHeight;

        getEffectivePaletteTextureDimensions(paletteType, realSwizzleWidth, realSwizzleHeight);

        convertTexelsFromPS2(
            srcTexels, clutPalTexels, realSwizzleWidth, realSwizzleHeight, srcPalTexDataSize,
            srcRasterFormat, srcPalFormatDepth, paletteRowAlignment, srcColorOrder,
            dstRasterFormat, dstPalFormatDepth, paletteRowAlignment, dstColorOrder,
            true
        );
    }

    // Give stuff to the runtime.
    dstPalTexels = clutPalTexels;
    dstPalSize = palSize;
}

inline void GeneratePS2CLUT(
    Interface *engineInterface,
    uint32 dstCLUTWidth, uint32 dstCLUTHeight,
    const void *srcPalTexelData, ePaletteType paletteType, uint32 paletteSize,
    eFormatEncodingType clutRequiredEncoding,
    eRasterFormat srcRasterFormat, uint32 srcPalFormatDepth, eColorOrdering srcColorOrder,
    eRasterFormat dstRasterFormat, uint32 dstPalFormatDepth, eColorOrdering dstColorOrder,
    void*& dstCLUTTexelData, uint32& dstCLUTDataSize
)
{
    const uint32 paletteRowAlignment = 1;

    // Allocate a new destination texel array.
    void *dstPalTexelData = nullptr;
    {
        uint32 palDataSize = getPaletteDataSize(paletteSize, dstPalFormatDepth);

        dstPalTexelData = engineInterface->PixelAllocate( palDataSize );

        if ( !dstPalTexelData )
        {
            throw RwException( "failed to allocate palette texel destination buffer" );
        }

        convertTexelsToPS2(
            srcPalTexelData, dstPalTexelData, paletteSize, 1, palDataSize,
            srcRasterFormat, dstRasterFormat,
            srcPalFormatDepth, paletteRowAlignment, dstPalFormatDepth, paletteRowAlignment,
            srcColorOrder, dstColorOrder,
            true
        );
    }

    // Generate a palette texture.
    void *newPalTexelData;
    uint32 newPalDataSize;

    genpalettetexeldata(
        engineInterface,
        dstCLUTWidth, dstCLUTHeight,
        dstPalTexelData, dstRasterFormat, paletteType, paletteSize,
        newPalTexelData, newPalDataSize
    );

    // If we allocated a new palette array, we need to free the source one.
    if ( newPalTexelData != dstPalTexelData )
    {
        engineInterface->PixelFree( dstPalTexelData );
    }

    // Now CLUT the palette.
    void *clutSwizzledTexels = nullptr;

    bool clutSuccess =
        clut(
            engineInterface, paletteType, newPalTexelData,
            dstCLUTWidth, dstCLUTHeight, newPalDataSize,
            clutRequiredEncoding, clutSwizzledTexels
        );

    if ( clutSuccess == false )
    {
        // The probability of this failing is slim like a straw of hair.
        throw RwException( "failed to swizzle the CLUT" );
    }

    // If we allocated new texels for the CLUT, delete the old ones.
    if ( clutSwizzledTexels != newPalTexelData )
    {
        engineInterface->PixelFree( newPalTexelData );
    }

    // Return values to the runtime.
    dstCLUTTexelData = clutSwizzledTexels;
    dstCLUTDataSize = newPalDataSize;
}

};