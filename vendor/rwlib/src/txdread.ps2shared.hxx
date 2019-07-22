// Shared definitions between Sony platforms that originate from the design of the PlayStation 2.

#ifndef RW_SHARED_PS2_DEFINITIONS
#define RW_SHARED_PS2_DEFINITIONS

#include "pixelformat.hxx"

namespace rw
{

enum eMemoryLayoutType
{
    PSMCT32 = 0,
    PSMCT24,
    PSMCT16,

    PSMCT16S = 10,

    PSMT8 = 19,
    PSMT4,

    PSMT8H = 27,
    PSMT4HL = 36,
    PSMT4HH = 44,

    PSMZ32 = 48,
    PSMZ24,
    PSMZ16,

    PSMZ16S = 58
};

enum eFormatEncodingType
{
    FORMAT_UNKNOWN,
    FORMAT_IDTEX4,
    FORMAT_IDTEX8,
    FORMAT_IDTEX8_COMPRESSED,
    FORMAT_TEX16,
    FORMAT_TEX32
};

inline static bool getMemoryLayoutFromTexelFormat(eFormatEncodingType encodingType, eMemoryLayoutType& memLayout)
{
    eMemoryLayoutType theLayout;

    if ( encodingType == FORMAT_IDTEX4 || encodingType == FORMAT_IDTEX8_COMPRESSED )
    {
        theLayout = PSMT4;
    }
    else if ( encodingType == FORMAT_IDTEX8 )
    {
        theLayout = PSMT8;
    }
    else if ( encodingType == FORMAT_TEX16 )
    {
        theLayout = PSMCT16S;
    }
    else if ( encodingType == FORMAT_TEX32 )
    {
        theLayout = PSMCT32;
    }
    else
    {
        return false;
    }

    memLayout = theLayout;

    return true;
}

static inline eFormatEncodingType getFormatEncodingFromRasterFormat(eRasterFormat rasterFormat, ePaletteType paletteType)
{
    eFormatEncodingType encodingFormat = FORMAT_UNKNOWN;

    if ( paletteType != PALETTE_NONE )
    {
        if (paletteType == PALETTE_4BIT)
        {
            encodingFormat = FORMAT_IDTEX8_COMPRESSED;
        }
        else if (paletteType == PALETTE_8BIT)
        {
            encodingFormat = FORMAT_IDTEX8;
        }
        else
        {
            throw RwException( "invalid palette type in PS2 swizzle format detection" );
        }
    }
    else
    {
        if (rasterFormat == RASTER_LUM)
        {
            // We assume that we are 8bit LUM here.
            encodingFormat = FORMAT_IDTEX8;
        }
        else if (rasterFormat == RASTER_1555 || rasterFormat == RASTER_565 || rasterFormat == RASTER_4444 ||
                 rasterFormat == RASTER_16 || rasterFormat == RASTER_555)
        {
            encodingFormat = FORMAT_TEX16;
        }
        else if (rasterFormat == RASTER_8888 || rasterFormat == RASTER_888 || rasterFormat == RASTER_32)
        {
            encodingFormat = FORMAT_TEX32;
        }
    }

    return encodingFormat;
}

static inline eFormatEncodingType getFormatEncodingFromMemoryLayout(eMemoryLayoutType memLayout)
{
    eFormatEncodingType encodingFormat = FORMAT_UNKNOWN;

    if (memLayout == PSMT4)
    {
        encodingFormat = FORMAT_IDTEX8_COMPRESSED;
    }
    else if (memLayout == PSMT8)
    {
        encodingFormat = FORMAT_IDTEX8;
    }
    else if (memLayout == PSMCT16 || memLayout == PSMCT16S)
    {
        encodingFormat = FORMAT_TEX16;
    }
    else if (memLayout == PSMCT32)
    {
        encodingFormat = FORMAT_TEX32;
    }

    return encodingFormat;
}

static inline uint32 getFormatEncodingDepth(eFormatEncodingType encodingType)
{
    uint32 depth = 0;

    if (encodingType == FORMAT_IDTEX4 || encodingType == FORMAT_IDTEX8_COMPRESSED)
    {
        depth = 4;
    }
    else if (encodingType == FORMAT_IDTEX8)
    {
        depth = 8;
    }
    else if (encodingType == FORMAT_TEX16)
    {
        depth = 16;
    }
    else if (encodingType == FORMAT_TEX32)
    {
        depth = 32;
    }

    return depth;
}

static inline void genpalettetexeldata(
    Interface *engineInterface,
    uint32 texelWidth, uint32 texelHeight,
    void *paletteData, eRasterFormat rasterFormat, ePaletteType paletteType, uint32 itemCount,
    void*& texelData, uint32& texelDataSize
)
{
    // Allocate texture memory.
    uint32 texelItemCount = ( texelWidth * texelHeight );

    // Calculate the data size.
    uint32 palDepth = Bitmap::getRasterFormatDepth(rasterFormat);

    assert( itemCount != 0 );
    assert( texelItemCount != 0 );

    uint32 srcDataSize = getPaletteDataSize( itemCount, palDepth );
    uint32 dstDataSize = getPaletteDataSize( texelItemCount, palDepth );

    assert( srcDataSize != 0 );
    assert( dstDataSize != 0 );

    void *newTexelData = nullptr;

    if ( srcDataSize != dstDataSize )
    {
        newTexelData = engineInterface->PixelAllocate( dstDataSize );

        // Write the new memory.
        memcpy(newTexelData, paletteData, std::min(srcDataSize, dstDataSize));
        
        if (dstDataSize > srcDataSize)
        {
            // Zero out the rest.
            memset((char*)newTexelData + srcDataSize, 0, (dstDataSize - srcDataSize));
        }
    }
    else
    {
        newTexelData = paletteData;
    }

    // Give parameters to the runtime.
    texelData = newTexelData;
    texelDataSize = dstDataSize;
}

}

#endif //RW_SHARED_PS2_DEFINITIONS