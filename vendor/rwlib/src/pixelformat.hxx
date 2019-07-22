#ifndef _PIXELFORMAT_INTERNAL_INCLUDE_
#define _PIXELFORMAT_INTERNAL_INCLUDE_

namespace rw
{

AINLINE eByteAddressingMode getByteAddressingFromPaletteType( ePaletteType palType )
{
    if ( palType == PALETTE_4BIT_LSB )
    {
        return eByteAddressingMode::LEAST_SIGNIFICANT;
    }

    // Most default thing.
    return eByteAddressingMode::MOST_SIGNIFICANT;
}

AINLINE bool getpaletteindex(
    const void *texelSource, ePaletteType paletteType, uint32 maxpalette, uint32 itemDepth, uint32 colorIndex,
    uint8& paletteIndexOut
)
{
    // Get the color lookup index from the texel.
    uint8 paletteIndex;

    bool couldGetIndex = false;

    if (paletteType == PALETTE_4BIT ||
        paletteType == PALETTE_4BIT_LSB)
    {
        if (itemDepth == 4)
        {
            if (paletteType == PALETTE_4BIT_LSB)
            {
                PixelFormat::palette4bit_lsb *srcData = (PixelFormat::palette4bit_lsb*)texelSource;

                srcData->getvalue(colorIndex, paletteIndex);

                couldGetIndex = true;
            }
            else
            {
                PixelFormat::palette4bit *srcData = (PixelFormat::palette4bit*)texelSource;

                srcData->getvalue(colorIndex, paletteIndex);

                couldGetIndex = true;
            }
        }
        else if (itemDepth == 8)
        {
            PixelFormat::palette8bit *srcData = (PixelFormat::palette8bit*)texelSource;

            srcData->getvalue(colorIndex, paletteIndex);

            // Trim off unused bits.
            paletteIndex &= 0xF;

            couldGetIndex = true;
        }
    }
    else if (paletteType == PALETTE_8BIT)
    {
        if (itemDepth == 8)
        {
            PixelFormat::palette8bit *srcData = (PixelFormat::palette8bit*)texelSource;

            srcData->getvalue(colorIndex, paletteIndex);

            couldGetIndex = true;
        }
    }

    bool couldResolveSource = false;

    if (couldGetIndex && paletteIndex < maxpalette)
    {
        couldResolveSource = true;

        paletteIndexOut = paletteIndex;
    }

    return couldResolveSource;
}

AINLINE void setpaletteindex(
    void *dstTexels, uint32 itemIndex, uint32 dstDepth, ePaletteType dstPaletteType,
    uint8 palIndex
)
{
    if ( dstDepth == 4 )
    {
        if ( dstPaletteType == PALETTE_4BIT )
        {
            ( (PixelFormat::palette4bit*)dstTexels )->setvalue(itemIndex, palIndex);
        }
        else if ( dstPaletteType == PALETTE_4BIT_LSB )
        {
            ( (PixelFormat::palette4bit_lsb*)dstTexels )->setvalue(itemIndex, palIndex);
        }
        else
        {
            assert( 0 );
        }
    }
    else if ( dstDepth == 8 )
    {
        ( (PixelFormat::palette8bit*)dstTexels )->setvalue(itemIndex, palIndex);
    }
    else
    {
        assert( 0 );
    }
}

// Generic palette item copy routine.
// This is not a routine without problems; if we ever decide to support bigger palette indice than 8bit,
// we have to update this.
AINLINE void copyPaletteItemGeneric(
    const void *srcTexels, void *dstTexels,
    uint32 srcIndex, uint32 srcDepth, ePaletteType srcPaletteType,
    uint32 dstIndex, uint32 dstDepth, ePaletteType dstPaletteType,
    uint32 paletteSize
)
{
    uint8 palIndex;

    // Fetch the index
    {
        bool gotPaletteIndex = getpaletteindex(srcTexels, srcPaletteType, paletteSize, srcDepth, srcIndex, palIndex);

        if ( !gotPaletteIndex )
        {
            palIndex = 0;
        }
    }

    // Put the index.
    {
        setpaletteindex(dstTexels, dstIndex, dstDepth, dstPaletteType, palIndex);
    }
}

template <typename colorNumberType>
using decide_quot_type = typename std::conditional <std::is_floating_point <colorNumberType>::value, colorNumberType, float>::type;

template <typename srcNumberType>
struct color_transform_in_float
{
    typedef decide_quot_type <srcNumberType> quot_type;

    AINLINE static quot_type in_transform( srcNumberType value )
    {
        return (quot_type)value;
    }
};

template <typename srcNumberType>
struct color_transform_in_integer
{
    typedef decide_quot_type <srcNumberType> quot_type;

    AINLINE static quot_type in_transform( srcNumberType value )
    {
        return ( (quot_type)value / (quot_type)std::numeric_limits <srcNumberType>().max() );
    }
};

template <typename srcNumberType>
using color_transform_in = typename std::conditional <std::is_floating_point <srcNumberType>::value, color_transform_in_float <srcNumberType>, color_transform_in_integer <srcNumberType>>::type;

template <typename dstNumberType>
struct color_transform_out_float
{
    typedef decide_quot_type <dstNumberType> quot_type;

    AINLINE static dstNumberType out_transform( quot_type quotient )
    {
        return (dstNumberType)( quotient );
    }
};

template <typename dstNumberType>
struct color_transform_out_integer
{
    typedef decide_quot_type <dstNumberType> quot_type;

    AINLINE static dstNumberType out_transform( quot_type quotient )
    {
        return (dstNumberType)( std::round( quotient * (quot_type)std::numeric_limits <dstNumberType>().max() ) );
    }
};

template <typename srcNumberType>
using color_transform_out = typename std::conditional <std::is_floating_point <srcNumberType>::value, color_transform_out_float <srcNumberType>, color_transform_out_integer <srcNumberType>>::type;

template <typename numberType, typename srcNumberType, typename maxNumberType>
AINLINE void destscalecolor( srcNumberType color, const maxNumberType curMax, numberType& outVal )
{
    static_assert( std::is_integral <srcNumberType>::value == true, "the source number type must be of integral type" );

    typedef decide_quot_type <srcNumberType> quot_type;

    const quot_type colorQuotient = ( (quot_type)color / (quot_type)curMax );

    outVal = color_transform_out <numberType>::out_transform( colorQuotient );
}

template <typename numberType, typename srcNumberType>
AINLINE void destscalecolorn( srcNumberType color, numberType& outVal )
{
    typedef decide_quot_type <srcNumberType> quot_type;

    const quot_type colorQuotient = color_transform_in <srcNumberType>::in_transform( color );

    outVal = color_transform_out <numberType>::out_transform( colorQuotient );
}

template <typename dstNumberType, typename srcNumberType, typename maxNumberType>
AINLINE dstNumberType putscalecolor( srcNumberType color, const maxNumberType desiredMax )
{
    typedef decide_quot_type <srcNumberType> quot_type;

    const quot_type colorQuotient = color_transform_in <srcNumberType>::in_transform( color );

    return (dstNumberType)( colorQuotient * (quot_type)desiredMax );
}

template <typename colorNumberType>
AINLINE colorNumberType rgb2lum( colorNumberType red, colorNumberType green, colorNumberType blue )
{
    return (colorNumberType)( ( red + green + blue ) / 3 );
}

template <typename numberType>
struct color_defaults_floating
{
    static constexpr numberType zero = (numberType)0;
    static constexpr numberType one = (numberType)1.0;
};

template <typename numberType>
struct color_defaults_integer
{
    static constexpr numberType zero = (numberType)0;
    static constexpr numberType one = std::numeric_limits <numberType>().max();
};

template <typename numberType>
using color_defaults = typename std::conditional <std::is_floating_point <numberType>::value, color_defaults_floating <typename std::decay <numberType>::type>, color_defaults_integer <typename std::decay <numberType>::type>>::type;

// We want to solve the 1bit-alpha-channel problem in color samples.
// For now we use a purely default method.
template <typename colorNumberType>
AINLINE void solve1bitalpha( bool isAlpha, colorNumberType& channelOut )
{
    channelOut = ( isAlpha ? color_defaults <colorNumberType>::one : color_defaults <colorNumberType>::zero );
}

template <typename colorNumberType>
AINLINE bool resolve1bitalpha( colorNumberType channel )
{
    return ( channel != color_defaults <colorNumberType>::zero );
}

template <typename arithType>
using additive_expand = decltype( (arithType)0 + (arithType)0 );

struct abstractColorItem
{
    eColorModel model;
    union
    {
        // We use floating points because they are fast and preserve color cutting-points.
        struct
        {
            float r, g, b, a;
        } rgbaColor;
        struct
        {
            float lum;
            float alpha;
        } luminance;
    };

    AINLINE void setRGBA( uint8 red, uint8 green, uint8 blue, uint8 alpha )
    {
        this->model = COLORMODEL_RGBA;

        destscalecolorn( red, this->rgbaColor.r );
        destscalecolorn( green, this->rgbaColor.g );
        destscalecolorn( blue, this->rgbaColor.b );
        destscalecolorn( alpha, this->rgbaColor.a );
    }

    AINLINE void setLuminance( uint8 lum, uint8 alpha )
    {
        this->model = COLORMODEL_LUMINANCE;

        destscalecolorn( lum, this->luminance.lum );
        destscalecolorn( alpha, this->luminance.alpha );
    }

    AINLINE void setClearedColor( eColorModel model )
    {
        this->model = model;

        if ( model == COLORMODEL_RGBA )
        {
            this->rgbaColor.r = 0;
            this->rgbaColor.g = 0;
            this->rgbaColor.b = 0;
            this->rgbaColor.a = 0;
        }
        else if ( model == COLORMODEL_LUMINANCE )
        {
            this->luminance.lum = 0;
            this->luminance.alpha = 0;
        }
        else
        {
            // TODO.
            throw RwException( "failed to clear color data for unsupported color model" );
        }
    }
};

AINLINE void colorItem2RGBA( const abstractColorItem& colorItem, uint8& r, uint8& g, uint8& b, uint8& a )
{
    eColorModel model = colorItem.model;

    if ( model == COLORMODEL_RGBA )
    {
        destscalecolorn( colorItem.rgbaColor.r, r );
        destscalecolorn( colorItem.rgbaColor.g, g );
        destscalecolorn( colorItem.rgbaColor.b, b );
        destscalecolorn( colorItem.rgbaColor.a, a );
    }
    else if ( model == COLORMODEL_LUMINANCE )
    {
        uint8 lum;
        destscalecolorn( colorItem.luminance.lum, lum );

        r = lum;
        g = lum;
        b = lum;
        destscalecolorn( colorItem.luminance.alpha, a );
    }
    else
    {
        throw RwException( "invalid color model in colorItem2RGBA" );
    }
}

inline eColorModel getColorModelFromRasterFormat( eRasterFormat rasterFormat )
{
    eColorModel usedColorModel;

    if ( rasterFormat == RASTER_1555 ||
         rasterFormat == RASTER_565 ||
         rasterFormat == RASTER_4444 ||
         rasterFormat == RASTER_8888 ||
         rasterFormat == RASTER_888 ||
         rasterFormat == RASTER_555 )
    {
        usedColorModel = COLORMODEL_RGBA;
    }
    else if ( rasterFormat == RASTER_LUM ||
              rasterFormat == RASTER_LUM_ALPHA )
    {
        usedColorModel = COLORMODEL_LUMINANCE;
    }
    else if ( rasterFormat == RASTER_16 ||
              rasterFormat == RASTER_24 ||
              rasterFormat == RASTER_32 )
    {
        usedColorModel = COLORMODEL_DEPTH;
    }
    else
    {
        throw RwException( "unknown color model for raster format" );
    }

    return usedColorModel;
}

struct colorModelDispatcher
{
    // TODO: make every framework-color request through this struct.

    eRasterFormat rasterFormat;
    eColorOrdering colorOrder;
    uint32 depth;

    const void *paletteData;
    uint32 paletteSize;
    ePaletteType paletteType;

    eColorModel usedColorModel;

    AINLINE colorModelDispatcher( eRasterFormat rasterFormat, eColorOrdering colorOrder, uint32 depth, const void *paletteData, uint32 paletteSize, ePaletteType paletteType )
    {
        this->rasterFormat = rasterFormat;
        this->colorOrder = colorOrder;
        this->depth = depth;

        this->paletteData = paletteData;
        this->paletteSize = paletteSize;
        this->paletteType = paletteType;

        // Determine the color model of our requests.
        this->usedColorModel = getColorModelFromRasterFormat( rasterFormat );
    }

    AINLINE colorModelDispatcher( const colorModelDispatcher& right )
    {
        // Actually, it does make sense.
        // BUT ONLY FOR PERFORMANCE REASONS.
        this->rasterFormat = right.rasterFormat;
        this->colorOrder = right.colorOrder;
        this->depth = right.depth;
        this->paletteData = right.paletteData;
        this->paletteSize = right.paletteSize;
        this->paletteType = right.paletteType;
        this->usedColorModel = right.usedColorModel;
    }

public:
    AINLINE eColorModel getColorModel( void ) const
    {
        return this->usedColorModel;
    }

private:
    AINLINE static bool resolve_raster_coordinate(
        const void *texelSource, ePaletteType paletteType, const void *paletteData, uint32 maxpalette,
        uint32 colorIndex, eRasterFormat rasterFormat, uint32 itemDepth,
        const void*& realTexelSource, uint32& realColorIndex, uint32& realColorDepth
    )
    {
        bool couldResolveSource = false;

        if (paletteType != PALETTE_NONE)
        {
            uint8 paletteIndex;

            bool couldResolvePalIndex = getpaletteindex(texelSource, paletteType, maxpalette, itemDepth, colorIndex, paletteIndex);

            if (couldResolvePalIndex)
            {
                realTexelSource = paletteData;
                realColorIndex = paletteIndex;
                realColorDepth = Bitmap::getRasterFormatDepth(rasterFormat);

                couldResolveSource = true;
            }
        }
        else
        {
            realTexelSource = texelSource;
            realColorIndex = colorIndex;
            realColorDepth = itemDepth;

            couldResolveSource = true;
        }

        return couldResolveSource;
    }

    template <typename colorNumberType>
    AINLINE static bool browsetexelcolor(
        const void *texelSource, ePaletteType paletteType, const void *paletteData, uint32 maxpalette,
        uint32 colorIndex, eRasterFormat rasterFormat, eColorOrdering colorOrder, uint32 itemDepth,
        colorNumberType& red, colorNumberType& green, colorNumberType& blue, colorNumberType& alpha)
    {
        bool hasColor = false;

        const void *realTexelSource = nullptr;
        uint32 realColorIndex, realColorDepth;

        bool couldResolveSource =
            resolve_raster_coordinate(
                texelSource, paletteType, paletteData, maxpalette,
                colorIndex, rasterFormat, itemDepth,
                realTexelSource, realColorIndex, realColorDepth
            );

        if ( !couldResolveSource )
            return false;

        colorNumberType prered, pregreen, preblue, prealpha;

        if (rasterFormat == RASTER_1555)
        {
            if (realColorDepth == 16)
            {
                struct pixel_t
                {
                    uint16 red : 5;
                    uint16 green : 5;
                    uint16 blue : 5;
                    uint16 alpha : 1;
                };

                const pixel_t *colorItem = (const pixel_t*)realTexelSource + realColorIndex;

                // Scale the color values.
                destscalecolor( colorItem->red, 31, prered );
                destscalecolor( colorItem->green, 31, pregreen );
                destscalecolor( colorItem->blue, 31, preblue );
                solve1bitalpha( colorItem->alpha, prealpha );

                hasColor = true;
            }
        }
        else if (rasterFormat == RASTER_555)
        {
            if (realColorDepth == 16)
            {
                struct pixel_t
                {
                    uint16 red : 5;
                    uint16 green : 5;
                    uint16 blue : 5;
                };

                const pixel_t *srcData = ( (const pixel_t*)realTexelSource + realColorIndex );

                // Scale the color values.
                destscalecolor( srcData->red, 31, prered );
                destscalecolor( srcData->green, 31, pregreen );
                destscalecolor( srcData->blue, 31, preblue );
                prealpha = color_defaults <colorNumberType>::one;

                hasColor = true;
            }
        }
        else if (rasterFormat == RASTER_565)
        {
            if (realColorDepth == 16)
            {
                struct pixel_t
                {
                    uint16 red : 5;
                    uint16 green : 6;
                    uint16 blue : 5;
                };

                const pixel_t *srcData = ( (const pixel_t*)realTexelSource + realColorIndex );

                // Scale the color values.
                destscalecolor(srcData->red, 31, prered);
                destscalecolor(srcData->green, 63, pregreen);
                destscalecolor(srcData->blue, 31, preblue);
                prealpha = color_defaults <colorNumberType>::one;

                hasColor = true;
            }
        }
        else if (rasterFormat == RASTER_4444)
        {
            if (realColorDepth == 16)
            {
                struct pixel_t
                {
                    uint16 red : 4;
                    uint16 green : 4;
                    uint16 blue : 4;
                    uint16 alpha : 4;
                };

                const pixel_t *colorItem = (const pixel_t*)realTexelSource + realColorIndex;

                // Scale the color values.
                destscalecolor(colorItem->red, 15, prered);
                destscalecolor(colorItem->green, 15, pregreen);
                destscalecolor(colorItem->blue, 15, preblue);
                destscalecolor(colorItem->alpha, 15, prealpha);

                hasColor = true;
            }
        }
        else if (rasterFormat == RASTER_8888)
        {
            if (realColorDepth == 32)
            {
                typedef PixelFormat::pixeldata32bit pixel_t;

                const pixel_t *srcData = ( (const pixel_t*)realTexelSource + realColorIndex );

                destscalecolorn( srcData->red, prered );
                destscalecolorn( srcData->green, pregreen );
                destscalecolorn( srcData->blue, preblue );
                destscalecolorn( srcData->alpha, prealpha );

                hasColor = true;
            }
        }
        else if (rasterFormat == RASTER_888)
        {
            if (realColorDepth == 32)
            {
                struct pixel_t
                {
                    uint8 red;
                    uint8 green;
                    uint8 blue;
                    uint8 unused;
                };

                const pixel_t *srcData = ( (const pixel_t*)realTexelSource + realColorIndex );

                // Get the color values.
                destscalecolor( srcData->red, 255, prered );
                destscalecolor( srcData->green, 255, pregreen );
                destscalecolor( srcData->blue, 255, preblue );
                prealpha = color_defaults <colorNumberType>::one;

                hasColor = true;
            }
            else if (realColorDepth == 24)
            {
                struct pixel_t
                {
                    uint8 red;
                    uint8 green;
                    uint8 blue;
                };

                pixel_t *srcData = ( (pixel_t*)realTexelSource + realColorIndex );

                // Get the color values.
                destscalecolor( srcData->red, 255, prered );
                destscalecolor( srcData->green, 255, pregreen );
                destscalecolor( srcData->blue, 255, preblue );
                prealpha = color_defaults <colorNumberType>::one;

                hasColor = true;
            }
        }

        if ( hasColor )
        {
            // Respect color ordering.
            if ( colorOrder == COLOR_RGBA )
            {
                red = prered;
                green = pregreen;
                blue = preblue;
                alpha = prealpha;
            }
            else if ( colorOrder == COLOR_BGRA )
            {
                red = preblue;
                green = pregreen;
                blue = prered;
                alpha = prealpha;
            }
            else if ( colorOrder == COLOR_ABGR )
            {
                red = prealpha;
                green = preblue;
                blue = pregreen;
                alpha = prered;
            }
            else if ( colorOrder == COLOR_ARGB )
            {
                red = prealpha;
                green = prered;
                blue = pregreen;
                alpha = preblue;
            }
            else if ( colorOrder == COLOR_BARG )
            {
                red = preblue;
                green = prealpha;
                blue = prered;
                alpha = pregreen;
            }
            else
            {
                assert( 0 );
            }
        }

        return hasColor;
    }

public:
    template <typename colorNumberType>
    AINLINE bool getRGBA( const void *texelSource, unsigned int index, colorNumberType& red, colorNumberType& green, colorNumberType& blue, colorNumberType& alpha ) const
    {
        eColorModel model = this->usedColorModel;

        bool success = false;

        if ( model == COLORMODEL_RGBA )
        {
            success =
                browsetexelcolor(
                    texelSource, this->paletteType, this->paletteData, this->paletteSize,
                    index,
                    this->rasterFormat, this->colorOrder, this->depth,
                    red, green, blue, alpha
                );
        }
        else if ( model == COLORMODEL_LUMINANCE )
        {
            colorNumberType lum, a;

            success = this->getLuminance( texelSource, index, lum, a );

            if ( success )
            {
                red = lum;
                green = lum;
                blue = lum;
                alpha = a;
            }
        }
        else
        {
            throw RwException( "tried to fetch RGBA from unsupported color model" );
        }

        return success;
    }

private:
    template <typename colorNumberType>
    AINLINE static bool puttexelcolor(
        void *texelDest,
        uint32 colorIndex, eRasterFormat rasterFormat, eColorOrdering colorOrder, uint32 itemDepth,
        colorNumberType red, colorNumberType green, colorNumberType blue, colorNumberType alpha
    )
    {
        bool setColor = false;

        colorNumberType putred, putgreen, putblue, putalpha;

        // Respect color ordering.
        if ( colorOrder == COLOR_RGBA )
        {
            putred = red;
            putgreen = green;
            putblue = blue;
            putalpha = alpha;
        }
        else if ( colorOrder == COLOR_BGRA )
        {
            putred = blue;
            putgreen = green;
            putblue = red;
            putalpha = alpha;
        }
        else if ( colorOrder == COLOR_ABGR )
        {
            putred = alpha;
            putgreen = blue;
            putblue = green;
            putalpha = red;
        }
        else if ( colorOrder == COLOR_ARGB )
        {
            putred = alpha;
            putgreen = red;
            putblue = green;
            putalpha = blue;
        }
        else if ( colorOrder == COLOR_BARG )
        {
            putred = blue;
            putgreen = alpha;
            putblue = red;
            putalpha = green;
        }
        else
        {
            assert( 0 );
        }

        if (rasterFormat == RASTER_1555)
        {
            if (itemDepth == 16)
            {
                struct pixel_t
                {
                    uint16 red : 5;
                    uint16 green : 5;
                    uint16 blue : 5;
                    uint16 alpha : 1;
                };

                typedef PixelFormat::texeltemplate <pixel_t> pixel1555_t;

                pixel1555_t *dstData = (pixel1555_t*)texelDest;

                // Scale the color values.
                uint8 redScaled =       putscalecolor <uint8> (putred, 31);
                uint8 greenScaled =     putscalecolor <uint8> (putgreen, 31);
                uint8 blueScaled =      putscalecolor <uint8> (putblue, 31);
                uint8 alphaScaled =     resolve1bitalpha( putalpha );

                dstData->setcolor(colorIndex, redScaled, greenScaled, blueScaled, alphaScaled);

                setColor = true;
            }
        }
        else if (rasterFormat == RASTER_555)
        {
            if (itemDepth == 16)
            {
                struct pixel_t
                {
                    uint16 red : 5;
                    uint16 green : 5;
                    uint16 blue : 5;
                };

                pixel_t *dstData = ( (pixel_t*)texelDest + colorIndex );

                // Scale the color values.
                uint8 redScaled =       putscalecolor <uint8> (putred, 31);
                uint8 greenScaled =     putscalecolor <uint8> (putgreen, 31);
                uint8 blueScaled =      putscalecolor <uint8> (putblue, 31);

                dstData->red = redScaled;
                dstData->green = greenScaled;
                dstData->blue = blueScaled;

                setColor = true;
            }
        }
        else if (rasterFormat == RASTER_565)
        {
            if (itemDepth == 16)
            {
                struct pixel_t
                {
                    uint16 red : 5;
                    uint16 green : 6;
                    uint16 blue : 5;
                };

                pixel_t *dstData = ( (pixel_t*)texelDest + colorIndex );

                // Scale the color values.
                uint8 redScaled =       putscalecolor <uint8> (putred, 31);
                uint8 greenScaled =     putscalecolor <uint8> (putgreen, 63);
                uint8 blueScaled =      putscalecolor <uint8> (putblue, 31);

                dstData->red = redScaled;
                dstData->green = greenScaled;
                dstData->blue = blueScaled;

                setColor = true;
            }
        }
        else if (rasterFormat == RASTER_4444)
        {
            if (itemDepth == 16)
            {
                struct pixel_t
                {
                    uint16 red : 4;
                    uint16 green : 4;
                    uint16 blue : 4;
                    uint16 alpha : 4;
                };

                typedef PixelFormat::texeltemplate <pixel_t> pixel4444_t;

                pixel4444_t *dstData = (pixel4444_t*)texelDest;

                // Scale the color values.
                uint8 redScaled =       putscalecolor <uint8> (putred, 15);
                uint8 greenScaled =     putscalecolor <uint8> (putgreen, 15);
                uint8 blueScaled =      putscalecolor <uint8> (putblue, 15);
                uint8 alphaScaled =     putscalecolor <uint8> (putalpha, 15);

                dstData->setcolor(colorIndex, redScaled, greenScaled, blueScaled, alphaScaled);

                setColor = true;
            }
        }
        else if (rasterFormat == RASTER_8888)
        {
            if (itemDepth == 32)
            {
                typedef PixelFormat::pixeldata32bit pixel_t;

                pixel_t *dstData = ( (pixel_t*)texelDest + colorIndex );

                destscalecolorn( putred, dstData->red );
                destscalecolorn( putgreen, dstData->green );
                destscalecolorn( putblue, dstData->blue );
                destscalecolorn( putalpha, dstData->alpha );

                setColor = true;
            }
        }
        else if (rasterFormat == RASTER_888)
        {
            if (itemDepth == 32)
            {
                struct pixel_t
                {
                    uint8 red;
                    uint8 green;
                    uint8 blue;
                    uint8 unused;
                };

                pixel_t *dstData = ( (pixel_t*)texelDest + colorIndex );

                // Put the color values.
                destscalecolorn( putred, dstData->red );
                destscalecolorn( putgreen, dstData->green );
                destscalecolorn( putblue, dstData->blue );

                setColor = true;
            }
            else if (itemDepth == 24)
            {
                struct pixel_t
                {
                    uint8 red;
                    uint8 green;
                    uint8 blue;
                };

                pixel_t *dstData = ( (pixel_t*)texelDest + colorIndex );

                // Put the color values.
                destscalecolorn( putred, dstData->red );
                destscalecolorn( putgreen, dstData->green );
                destscalecolorn( putblue, dstData->blue );

                setColor = true;
            }
        }

        return setColor;
    }

public:
    template <typename colorNumberType>
    AINLINE bool setRGBA( void *texelSource, unsigned int index, colorNumberType red, colorNumberType green, colorNumberType blue, colorNumberType alpha ) const
    {
        eColorModel model = this->usedColorModel;

        bool success = false;

        if ( this->paletteType != PALETTE_NONE )
        {
            throw RwException( "tried to set color to palette bitmap (unsupported)" );
        }

        if ( model == COLORMODEL_RGBA )
        {
            success =
                puttexelcolor(
                    texelSource, index,
                    this->rasterFormat, this->colorOrder, this->depth,
                    red, green, blue, alpha
                );
        }
        else if ( model == COLORMODEL_LUMINANCE )
        {
            // We have to set calculate the luminance of this color.
            // Default way of converting RGB to luminance.
            // If you want a better way, write your own filter.
            colorNumberType lum = rgb2lum( red, green, blue );

            success =
                this->setLuminance(
                    texelSource, index,
                    lum, alpha
                );
        }
        else
        {
            throw RwException( "tried to set RGBA to unsupported color model" );
        }

        return success;
    }

    template <typename colorNumberType>
    AINLINE bool setLuminance( void *texelSource, unsigned int index, colorNumberType lum, colorNumberType alpha ) const
    {
        eColorModel model = this->usedColorModel;

        bool success = false;

        if ( model == COLORMODEL_RGBA )
        {
            success = this->setRGBA( texelSource, index, lum, lum, lum, alpha );
        }
        else if ( model == COLORMODEL_LUMINANCE )
        {
            eRasterFormat rasterFormat = this->rasterFormat;
            uint32 depth = this->depth;

            if ( rasterFormat == RASTER_LUM )
            {
                if ( depth == 8 )
                {
                    struct pixel_t
                    {
                        uint8 lum;
                    };

                    pixel_t *dstData = ( (pixel_t*)texelSource + index );

                    destscalecolorn( lum, dstData->lum );

                    success = true;
                }
                else if ( depth == 4 )
                {
                    PixelFormat::palette4bit *lumData = (PixelFormat::palette4bit*)texelSource;

                    uint8 scaledLum = putscalecolor <uint8> ( lum, 15 );

                    lumData->setvalue( index, scaledLum );

                    success = true;
                }
            }
            else if ( rasterFormat == RASTER_LUM_ALPHA )
            {
                if ( depth == 8 )
                {
                    struct pixel_t
                    {
                        uint8 lum : 4;
                        uint8 alpha : 4;
                    };

                    pixel_t *dstData = ( (pixel_t*)texelSource + index );

                    dstData->lum        = putscalecolor <uint8> ( lum, 15 );
                    dstData->alpha      = putscalecolor <uint8> ( alpha, 15 );

                    success = true;
                }
                else if ( depth == 16 )
                {
                    struct pixel_t
                    {
                        uint8 lum;
                        uint8 alpha;
                    };

                    pixel_t *dstData = ( (pixel_t*)texelSource + index );

                    destscalecolorn( lum, dstData->lum );
                    destscalecolorn( alpha, dstData->alpha );

                    success = true;
                }
            }
        }
        else
        {
            throw RwException( "tried to set luminance to unsupported color model" );
        }

        return success;
    }

    template <typename colorNumberType>
    AINLINE bool getLuminance( const void *texelSource, unsigned int index, colorNumberType& lum, colorNumberType& alpha ) const
    {
        eColorModel model = this->usedColorModel;

        bool success = false;

        if ( model == COLORMODEL_RGBA )
        {
            colorNumberType red, green, blue;

            success =
                this->getRGBA( texelSource, index, red, green, blue, alpha );

            if ( success )
            {
                lum = rgb2lum( red, green, blue );
            }
        }
        else if ( model == COLORMODEL_LUMINANCE )
        {
            eRasterFormat rasterFormat = this->rasterFormat;
            uint32 depth = this->depth;

            // Get the real fetch source first.
            const void *realTexelSource = nullptr;
            uint32 realColorIndex, realColorDepth;

            bool resRealSource =
                resolve_raster_coordinate(
                    texelSource, this->paletteType, this->paletteData, this->paletteSize,
                    index, rasterFormat, depth,
                    realTexelSource, realColorIndex, realColorDepth
                );

            if ( resRealSource )
            {
                if ( rasterFormat == RASTER_LUM )
                {
                    if ( realColorDepth == 8 )
                    {
                        struct pixel_t
                        {
                            uint8 lum;
                        };

                        const pixel_t *srcData = ( (const pixel_t*)realTexelSource + realColorIndex );

                        destscalecolorn( srcData->lum, lum );
                        alpha = color_defaults <colorNumberType>::one;

                        success = true;
                    }
                    else if ( realColorDepth == 4 )
                    {
                        const PixelFormat::palette4bit *lumData = (const PixelFormat::palette4bit*)realTexelSource;

                        uint8 scaledLum;

                        lumData->getvalue( realColorIndex, scaledLum );

                        destscalecolor( scaledLum, 15, lum );
                        alpha = color_defaults <colorNumberType>::one;

                        success = true;
                    }
                }
                else if ( rasterFormat == RASTER_LUM_ALPHA )
                {
                    if ( realColorDepth == 8 )
                    {
                        struct pixel_t
                        {
                            uint8 lum : 4;
                            uint8 alpha : 4;
                        };

                        const pixel_t *srcData = ( (const pixel_t*)realTexelSource + realColorIndex );

                        destscalecolor( srcData->lum, 15, lum );
                        destscalecolor( srcData->alpha, 15, alpha );

                        success = true;
                    }
                    else if ( realColorDepth == 16 )
                    {
                        struct pixel_t
                        {
                            uint8 lum;
                            uint8 alpha;
                        };

                        const pixel_t *srcData = ( (const pixel_t*)realTexelSource + realColorIndex );

                        destscalecolorn( srcData->lum, lum );
                        destscalecolorn( srcData->alpha, alpha );

                        success = true;
                    }
                }
            }
        }
        else
        {
            throw RwException( "tried to get luminance from unsupported color model" );
        }

        return success;
    }

    AINLINE void setColor( void *texelSource, unsigned int index, const abstractColorItem& colorItem ) const
    {
        eColorModel model = colorItem.model;

        bool success = false;

        if ( model == COLORMODEL_RGBA )
        {
            success = this->setRGBA( texelSource, index, colorItem.rgbaColor.r, colorItem.rgbaColor.g, colorItem.rgbaColor.b, colorItem.rgbaColor.a );
        }
        else if ( model == COLORMODEL_LUMINANCE )
        {
            success = this->setLuminance( texelSource, index, colorItem.luminance.lum, colorItem.luminance.alpha );
        }
        else
        {
            throw RwException( "invalid color model in abstract color item" );
        }

        (void)success;
    }

    AINLINE void getColor( const void *texelSource, unsigned int index, abstractColorItem& colorItem ) const
    {
        eColorModel model = this->usedColorModel;

        colorItem.model = model;

        bool success = false;

        if ( model == COLORMODEL_RGBA )
        {
            success = this->getRGBA( texelSource, index, colorItem.rgbaColor.r, colorItem.rgbaColor.g, colorItem.rgbaColor.b, colorItem.rgbaColor.a );

            if ( !success )
            {
                colorItem.rgbaColor.r = 0;
                colorItem.rgbaColor.g = 0;
                colorItem.rgbaColor.b = 0;
                colorItem.rgbaColor.a = 0;
            }
        }
        else if ( model == COLORMODEL_LUMINANCE )
        {
            success = this->getLuminance( texelSource, index, colorItem.luminance.lum, colorItem.luminance.alpha );

            if ( !success )
            {
                colorItem.luminance.lum = 0;
                colorItem.luminance.alpha = 0;
            }
        }
        else
        {
            throw RwException( "invalid color model for getting abstract color item" );
        }
    }

    AINLINE void clearColor( void *texelSource, unsigned int index )
    {
        // TODO.
        this->setLuminance( texelSource, index, 0, 0 );
    }

    AINLINE void setClearedColor( abstractColorItem& item ) const
    {
        eColorModel model = this->usedColorModel;

        item.setClearedColor( model );
    }
};

template <typename srcColorDispatcher, typename dstColorDispatcher>
inline void copyTexelDataEx(
    const void *srcTexels, void *dstTexels,
    srcColorDispatcher& fetchDispatch, dstColorDispatcher& putDispatch,
    uint32 srcWidth, uint32 srcHeight,
    uint32 srcOffX, uint32 srcOffY,
    uint32 dstOffX, uint32 dstOffY,
    uint32 srcRowSize, uint32 dstRowSize
)
{
    // If we are not a palette, then we have to process colors.
    for ( uint32 row = 0; row < srcHeight; row++ )
    {
        const void *srcRow = getConstTexelDataRow( srcTexels, srcRowSize, row + srcOffY );
        void *dstRow = getTexelDataRow( dstTexels, dstRowSize, row + dstOffY );

        for ( uint32 col = 0; col < srcWidth; col++ )
        {
            abstractColorItem colorItem;

            fetchDispatch.getColor( srcRow, col + srcOffX, colorItem );

            // Just put the color inside.
            putDispatch.setColor( dstRow, col + dstOffX, colorItem );
        }
    }
}

template <typename srcColorDispatcher, typename dstColorDispatcher>
inline void copyTexelDataBounded(
    const void *srcTexels, void *dstTexels,
    srcColorDispatcher& fetchDispatch, dstColorDispatcher& putDispatch,
    uint32 srcWidth, uint32 srcHeight,
    uint32 dstWidth, uint32 dstHeight,
    uint32 srcOffX, uint32 srcOffY,
    uint32 dstOffX, uint32 dstOffY,
    uint32 srcRowSize, uint32 dstRowSize
)
{
    // If we are not a palette, then we have to process colors.
    for ( uint32 row = 0; row < srcHeight; row++ )
    {
        const uint32 src_pos_y = ( row + srcOffY );
        const uint32 dst_pos_y = ( row + dstOffY );

        void *dstRow = nullptr;

        if ( dst_pos_y < dstHeight )
        {
            dstRow = getTexelDataRow( dstTexels, dstRowSize, dst_pos_y );

            const void *srcRow = nullptr;

            if ( src_pos_y < srcHeight )
            {
                srcRow = getConstTexelDataRow( srcTexels, srcRowSize, src_pos_y );
            }

            for ( uint32 col = 0; col < srcWidth; col++ )
            {
                const uint32 src_pos_x = ( col + srcOffX );
                const uint32 dst_pos_x = ( col + dstOffX );

                // Only proceed if we can actually write.
                if ( dst_pos_x < dstWidth )
                {
                    abstractColorItem colorItem;

                    // Attempt to get the source color.
                    bool gotColor = false;

                    if ( srcRow && src_pos_x < srcWidth )
                    {
                        fetchDispatch.getColor( srcRow, src_pos_x, colorItem );

                        gotColor = true;
                    }

                    // If we failed to get a color, we will just write a cleared one.
                    if ( !gotColor )
                    {
                        putDispatch.setClearedColor( colorItem );
                    }

                    // Just put the color inside.
                    putDispatch.setColor( dstRow, dst_pos_x, colorItem );
                }
            }
        }
    }
}

// Move color items from one array position to another array at position.
AINLINE void moveTexels(
    const void *srcTexels, void *dstTexels,
    uint32 srcTexelX, uint32 srcTexelY, uint32 dstTexelX, uint32 dstTexelY,
    uint32 texelCountX, uint32 texelCountY,
    uint32 mipWidth, uint32 mipHeight,
    eRasterFormat srcRasterFormat, uint32 srcItemDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType, uint32 srcPaletteSize,
    eRasterFormat dstRasterFormat, uint32 dstItemDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType, uint32 dstPaletteSize
)
{
    if ( srcPaletteType != PALETTE_NONE )
    {
        assert( dstPaletteType != PALETTE_NONE );

        // Move palette texels.
        ConvertPaletteDepthEx(
            srcTexels, dstTexels,
            srcTexelX, srcTexelY, dstTexelX, dstTexelY,
            mipWidth, mipHeight,
            texelCountX, texelCountY,
            srcPaletteType, dstPaletteType,
            srcPaletteSize,
            srcItemDepth, dstItemDepth,
            srcRowAlignment, dstRowAlignment
        );
    }
    else
    {
        assert( dstPaletteType == PALETTE_NONE );

        // Move color items.
        colorModelDispatcher fetchDispatch( srcRasterFormat, srcColorOrder, srcItemDepth, nullptr, 0, PALETTE_NONE );
        colorModelDispatcher putDispatch( dstRasterFormat, dstColorOrder, dstItemDepth, nullptr, 0, PALETTE_NONE );

        uint32 srcRowSize = getRasterDataRowSize( mipWidth, srcItemDepth, srcRowAlignment );
        uint32 dstRowSize = getRasterDataRowSize( mipWidth, dstItemDepth, dstRowAlignment );

        copyTexelDataEx(
            srcTexels, dstTexels,
            fetchDispatch, putDispatch,
            texelCountX, texelCountY,
            srcTexelX, srcTexelY,
            dstTexelX, dstTexelY,
            srcRowSize, dstRowSize
        );
    }
}

inline double unpackcolor( uint8 color )
{
    return ( (double)color / 255.0 );
}

inline uint8 packcolor( double color )
{
    return (uint8)( color * 255.0 );
}

inline bool doRawMipmapBuffersNeedConversion(
    eRasterFormat srcRasterFormat, uint32 srcDepth, eColorOrdering srcColorOrder, ePaletteType srcPaletteType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, eColorOrdering dstColorOrder, ePaletteType dstPaletteType
)
{
    // Returns true if the source raster format needs to be converted
    // to become the destination raster format. This is useful if you want
    // to directly acquire texels instead of passing them into a conversion
    // routine.

    // If it is a palette format, it could need conversion.
    if ( srcPaletteType != dstPaletteType )
    {
        return true;
    }
    else if ( srcPaletteType != PALETTE_NONE )
    {
        // This is a check for the palette texel buffer, whether that buffer needs a conversion.
        // The depth we get here is the actual depth of the palette indices.
        if ( srcDepth != dstDepth )
        {
            return true;
        }

        // We can early out because we are not a raw color format.
        return false;
    }

    // This is reached if we are a raw color format.
    // Check for color format change.
    if ( srcRasterFormat != dstRasterFormat || srcDepth != dstDepth || srcColorOrder != dstColorOrder )
    {
        return true;
    }

    // TODO: add optimizations to this decision making.
    // Like RGBA8888 32bit can be directly acquired to RGB8888 32bit.

    return false;
}

inline bool doesRawMipmapBufferNeedFullConversion(
    uint32 surfWidth,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType
)
{
    // We first check if this mipmap needs color conversion in general.
    // This is basically if the structure of the samples has changed.
    bool needsSampleConv =
        doRawMipmapBuffersNeedConversion(
            srcRasterFormat, srcDepth, srcColorOrder, srcPaletteType,
            dstRasterFormat, dstDepth, dstColorOrder, dstPaletteType
        );

    if ( needsSampleConv )
    {
        return true;
    }

    // Otherwise the buffer could have expanded in some way.
    // This needs conversion aswell.
    bool needsValidityConv =
        shouldAllocateNewRasterBuffer(
            surfWidth,
            srcDepth, srcRowAlignment,
            dstDepth, dstRowAlignment
        );

    if ( needsValidityConv )
    {
        return true;
    }

    // We are good to go.
    return false;
}

inline bool haveToAllocateNewPaletteBuffer(
    uint32 srcPalRasterDepth, uint32 srcPaletteSize,
    uint32 dstPalRasterDepth, uint32 dstPaletteSize
)
{
    if ( srcPalRasterDepth != dstPalRasterDepth ||
         srcPaletteSize != dstPaletteSize )
    {
        return true;
    }

    return false;
}

inline bool doPaletteBuffersNeedConversion(
    eRasterFormat srcRasterFormat, eColorOrdering srcColorOrder,
    eRasterFormat dstRasterFormat, eColorOrdering dstColorOrder
)
{
    // The palette color format is really simple.
    // Every palette raster format has only one depth.
    // So we can simple check for raster format change.
    if ( srcRasterFormat != dstRasterFormat || srcColorOrder != dstColorOrder )
    {
        return true;
    }

    return false;
}

inline bool doPaletteBuffersNeedFullConversion(
    eRasterFormat srcRasterFormat, eColorOrdering srcColorOrder, uint32 srcPaletteSize,
    eRasterFormat dstRasterFormat, eColorOrdering dstColorOrder, uint32 dstPaletteSize
)
{
    uint32 srcPalRasterDepth = Bitmap::getRasterFormatDepth( srcRasterFormat );
    uint32 dstPalRasterDepth = Bitmap::getRasterFormatDepth( dstRasterFormat );

    if ( haveToAllocateNewPaletteBuffer( srcPalRasterDepth, srcPaletteSize, dstPalRasterDepth, dstPaletteSize ) )
    {
        return true;
    }

    if ( doPaletteBuffersNeedConversion( srcRasterFormat, srcColorOrder, dstRasterFormat, dstColorOrder ) )
    {
        return true;
    }

    return false;
}

template <typename mipmapListType>
inline bool doesPixelDataNeedAddressabilityAdjustment(
    const mipmapListType& mipmaps,
    uint32 srcDepth, uint32 srcRowAlignment,
    uint32 dstDepth, uint32 dstRowAlignment
)
{
    // A change in item depth is critical.
    if ( srcDepth != dstDepth )
        return true;

    // Check if any mipmap has conflicting addressing.
    size_t numberOfMipmaps = mipmaps.GetCount();

    for ( size_t n = 0; n < numberOfMipmaps; n++ )
    {
        const auto& mipLayer = mipmaps[ n ];

        bool doesRequireNewTexelBuffer =
            shouldAllocateNewRasterBuffer(
                mipLayer.layerWidth,
                srcDepth, srcRowAlignment,
                dstDepth, dstRowAlignment
            );

        if ( doesRequireNewTexelBuffer )
        {
            // If we require a new texel buffer, we kinda have to convert stuff.
            // The conversion routine is an all-in-one fix, that should not be called too often.
            return true;
        }
    }

    // No conflict.
    return false;
}

template <typename mipmapListType>
inline bool doesPixelDataNeedConversion(
    const mipmapListType& mipmaps,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType, eCompressionType srcCompressionType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType, eCompressionType dstCompressionType
)
{
    // This function is supposed to decide whether the information stored in pixelData, which is
    // reflected by the source format, requires expensive conversion to reach the destination format.
    // pixelData is expected to be raw uncompressed texture data.

    // We kinda have to convert if the compression type changed.
    if ( srcCompressionType != dstCompressionType )
    {
        return true;
    }
    else if ( srcCompressionType != RWCOMPRESS_NONE )
    {
        // If we are already compressed, the other properties do not matter anymore.
        return false;
    }

    // This is a little different to what we do in the ConvertPixelData routine due to a different premise.
    // Here we ask if all mipmap layers need reallocation instead of a per-layer basis.

    // If the raster format has changed, there is no way around conversion.
    if ( doRawMipmapBuffersNeedConversion(
             srcRasterFormat, srcDepth, srcColorOrder, srcPaletteType,
             dstRasterFormat, dstDepth, dstColorOrder, dstPaletteType
         ) )
    {
        return true;
    }

    // Then there is the possibility that the buffer has expanded, for any mipmap inside of pixelData.
    // A conversion will properly fix that.
    if ( doesPixelDataNeedAddressabilityAdjustment(
             mipmaps,
             srcDepth, srcRowAlignment,
             dstDepth, dstRowAlignment
        ) )
    {
        return true;
    }

    // We prefer if there is no conversion required.
    return false;
}

template <typename mipmapListType>
inline bool doesPixelDataOrPaletteDataNeedConversion(
    const mipmapListType& mipmaps,
    eRasterFormat srcRasterFormat, uint32 srcDepth, uint32 srcRowAlignment, eColorOrdering srcColorOrder, ePaletteType srcPaletteType, uint32 srcPaletteSize, eCompressionType srcCompressionType,
    eRasterFormat dstRasterFormat, uint32 dstDepth, uint32 dstRowAlignment, eColorOrdering dstColorOrder, ePaletteType dstPaletteType, uint32 dstPaletteSize, eCompressionType dstCompressionType
)
{
    // We first check if the color buffer stuff needs converting.
    bool colorBufConv =
        doesPixelDataNeedConversion(
            mipmaps,
            srcRasterFormat, srcDepth, srcRowAlignment, srcColorOrder, srcPaletteType, srcCompressionType,
            dstRasterFormat, dstDepth, dstRowAlignment, dstColorOrder, dstPaletteType, dstCompressionType
        );

    if ( colorBufConv )
    {
        return true;
    }

    // Is this a palette buffer to palette buffer transformation?
    if ( srcPaletteType != PALETTE_NONE && dstPaletteType != PALETTE_NONE )
    {
        // Our palette buffer could need converting aswell!
        bool palBufConv =
            doPaletteBuffersNeedFullConversion(
                srcRasterFormat, srcColorOrder, srcPaletteSize,
                dstRasterFormat, dstColorOrder, dstPaletteSize
            );

        if ( palBufConv )
        {
            return true;
        }
    }
    // Or are we supposed to palettize something or remove its palette?
    else if ( srcPaletteType != PALETTE_NONE || dstPaletteType != PALETTE_NONE )
    {
        // We kinda need conversion here.
        // This is because we either remove the palette or palettize something.
        return true;
    }

    // We dont need to do anything.
    // This is a huge performance boost :)
    return false;
}

};

#endif //_PIXELFORMAT_INTERNAL_INCLUDE_
