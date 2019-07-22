// This file contains PlayStation 2 specific memory permutation structures.

// Include the main swizzling helpers.
#include "txdread.memcodec.hxx"

namespace rw
{

// The PS2 memory is a rectangular device. Basically its a set of pages that can be used for allocating image chunks.
// This class is supposed to emulate the texture allocation behavior.
namespace ps2GSMemoryLayoutArrangements
{
    // Layout arrangements.
    // These tables define the linear arrangement of block coordinates in a page.
    // Pages are the ultimate unit of linear arrangement on PS2 GS memory.
    const static uint32 psmct32[4][8] =
    {
        { 0u, 1u, 4u, 5u, 16u, 17u, 20u, 21u },
        { 2u, 3u, 6u, 7u, 18u, 19u, 22u, 23u },
        { 8u, 9u, 12u, 13u, 24u, 25u, 28u, 29u },
        { 10u, 11u, 14u, 15u, 26u, 27u, 30u, 31u }
    };
    const static uint32 psmz32[4][8] =
    {
        { 24u, 25u, 28u, 29u, 8u, 9u, 12u, 13u },
        { 26u, 27u, 30u, 31u, 10u, 11u, 14u, 15u },
        { 16u, 17u, 20u, 21u, 0u, 1u, 4u, 5u },
        { 18u, 19u, 22u, 23u, 2u, 3u, 6u, 7u }
    };
    const static uint32 psmct16[8][4] =
    {
        { 0u, 2u, 8u, 10u },
        { 1u, 3u, 9u, 11u },
        { 4u, 6u, 12u, 14u },
        { 5u, 7u, 13u, 15u },
        { 16u, 18u, 24u, 26u },
        { 17u, 19u, 25u, 27u },
        { 20u, 22u, 28u, 30u },
        { 21u, 23u, 29u, 31u }
    };
    const static uint32 psmz16[8][4] =
    {
        { 24u, 26u, 16u, 18u },
        { 25u, 27u, 17u, 19u },
        { 28u, 30u, 20u, 22u },
        { 29u, 31u, 21u, 23u },
        { 8u, 10u, 0u, 2u },
        { 9u, 11u, 1u, 3u },
        { 12u, 14u, 4u, 6u },
        { 13u, 15u, 5u, 7u }
    };
    const static uint32 psmct16s[8][4] =
    {
        { 0u, 2u, 16u, 18u },
        { 1u, 3u, 17u, 19u },
        { 8u, 10u, 24u, 26u },
        { 9u, 11u, 25u, 27u },
        { 4u, 6u, 20u, 22u },
        { 5u, 7u, 21u, 23u },
        { 12u, 14u, 28u, 30u },
        { 13u, 15u, 29u, 31u }
    };
    const static uint32 psmz16s[8][4] =
    {
        { 24u, 26u, 8u, 10u },
        { 25u, 27u, 9u, 11u },
        { 16u, 18u, 0u, 2u },
        { 17u, 19u, 1u, 3u },
        { 28u, 30u, 12u, 14u },
        { 29u, 31u, 13u, 15u },
        { 20u, 22u, 4u, 6u },
        { 21u, 23u, 5u, 7u }
    };
    const static uint32 psmt8[4][8] =
    {
        { 0u, 1u, 4u, 5u, 16u, 17u, 20u, 21u },
        { 2u, 3u, 6u, 7u, 18u, 19u, 22u, 23u },
        { 8u, 9u, 12u, 13u, 24u, 25u, 28u, 29u },
        { 10u, 11u, 14u, 15u, 26u, 27u, 30u, 31u }
    };
    const static uint32 psmt4[8][4] =
    {
        { 0u, 2u, 8u, 10u },
        { 1u, 3u, 9u, 11u },
        { 4u, 6u, 12u, 14u },
        { 5u, 7u, 13u, 15u },
        { 16u, 18u, 24u, 26u },
        { 17u, 19u, 25u, 27u },
        { 20u, 22u, 28u, 30u },
        { 21u, 23u, 29u, 31u }
    };
};

// There structs define how blocks of pixels of smaller size get packed into blocks of pixels of bigger size.
// They are essentially what you call "swizzling", just without confusing shit.
namespace ps2GSPixelEncodingFormatsData
{
    // width: 32px, height: 4px
    const static uint32 psmt4_to_psmct32_prim[] =
    {
        0, 68, 8, 76, 16, 84, 24, 92,
        1, 69, 9, 77, 17, 85, 25, 93,
        2, 70, 10, 78, 18, 86, 26, 94,
        3, 71, 11, 79, 19, 87, 27, 95,
        4, 64, 12, 72, 20, 80, 28, 88,
        5, 65, 13, 73, 21, 81, 29, 89,
        6, 66, 14, 74, 22, 82, 30, 90,
        7, 67, 15, 75, 23, 83, 31, 91,
        32, 100, 40, 108, 48, 116, 56, 124,
        33, 101, 41, 109, 49, 117, 57, 125,
        34, 102, 42, 110, 50, 118, 58, 126,
        35, 103, 43, 111, 51, 119, 59, 127,
        36, 96, 44, 104, 52, 112, 60, 120,
        37, 97, 45, 105, 53, 113, 61, 121,
        38, 98, 46, 106, 54, 114, 62, 122,
        39, 99, 47, 107, 55, 115, 63, 123
    };

    const static uint32 psmt4_to_psmct32_sec[] =
    {
        4, 64, 12, 72, 20, 80, 28, 88,
        5, 65, 13, 73, 21, 81, 29, 89,
        6, 66, 14, 74, 22, 82, 30, 90,
        7, 67, 15, 75, 23, 83, 31, 91,
        0, 68, 8, 76, 16, 84, 24, 92,
        1, 69, 9, 77, 17, 85, 25, 93,
        2, 70, 10, 78, 18, 86, 26, 94,
        3, 71, 11, 79, 19, 87, 27, 95,
        36, 96, 44, 104, 52, 112, 60, 120,
        37, 97, 45, 105, 53, 113, 61, 121,
        38, 98, 46, 106, 54, 114, 62, 122,
        39, 99, 47, 107, 55, 115, 63, 123,
        32, 100, 40, 108, 48, 116, 56, 124,
        33, 101, 41, 109, 49, 117, 57, 125,
        34, 102, 42, 110, 50, 118, 58, 126,
        35, 103, 43, 111, 51, 119, 59, 127
    };

    // width: 16px, height: 4px
    const static uint32 psmt8_to_psmct32_prim[] =
    {
        0, 36, 8, 44,
        1, 37, 9, 45,
        2, 38, 10, 46,
        3, 39, 11, 47,
        4, 32, 12, 40,
        5, 33, 13, 41,
        6, 34, 14, 42,
        7, 35, 15, 43,
        16, 52, 24, 60,
        17, 53, 25, 61,
        18, 54, 26, 62,
        19, 55, 27, 63,
        20, 48, 28, 56,
        21, 49, 29, 57,
        22, 50, 30, 58,
        23, 51, 31, 59
    };

    const static uint32 psmt8_to_psmct32_sec[] =
    {
        4, 32, 12, 40,
        5, 33, 13, 41,
        6, 34, 14, 42,
        7, 35, 15, 43,
        0, 36, 8, 44,
        1, 37, 9, 45,
        2, 38, 10, 46,
        3, 39, 11, 47,
        20, 48, 28, 56,
        21, 49, 29, 57,
        22, 50, 30, 58,
        23, 51, 31, 59,
        16, 52, 24, 60,
        17, 53, 25, 61,
        18, 54, 26, 62,
        19, 55, 27, 63
    };
};

struct ps2GSPixelEncodingGeneric
{
    typedef eFormatEncodingType encodingFormatType;

    inline static uint32 getFormatEncodingDepth( eFormatEncodingType format )
    {
        return rw::getFormatEncodingDepth( format );
    }

    inline static bool isPackOperation( eFormatEncodingType srcFormat, eFormatEncodingType dstFormat )
    {
        if ( srcFormat == dstFormat )
            return false;

        if ( srcFormat == FORMAT_IDTEX4 )
        {
            if ( dstFormat == FORMAT_IDTEX8 ||
                 dstFormat == FORMAT_IDTEX8_COMPRESSED ||
                 dstFormat == FORMAT_TEX16 ||
                 dstFormat == FORMAT_TEX32 )
            {
                return true;
            }
        }
        else if ( srcFormat == FORMAT_IDTEX8 )
        {
            if ( dstFormat == FORMAT_TEX16 ||
                 dstFormat == FORMAT_TEX32 )
            {
                return true;
            }
            else if ( dstFormat == FORMAT_IDTEX4 ||
                      dstFormat == FORMAT_IDTEX8_COMPRESSED )
            {
                return false;
            }
        }
        else if ( srcFormat == FORMAT_IDTEX8_COMPRESSED )
        {
            if ( dstFormat == FORMAT_TEX16 ||
                 dstFormat == FORMAT_TEX32 )
            {
                return true;
            }
            else if ( dstFormat == FORMAT_IDTEX4 ||
                      dstFormat == FORMAT_IDTEX8_COMPRESSED )
            {
                return false;
            }
        }
        else if ( srcFormat == FORMAT_TEX16 )
        {
            if ( dstFormat == FORMAT_IDTEX4 ||
                 dstFormat == FORMAT_IDTEX8 ||
                 dstFormat == FORMAT_IDTEX8_COMPRESSED )
            {
                return false;
            }
            else if ( dstFormat == FORMAT_TEX32 )
            {
                return true;
            }
        }
        else if ( srcFormat == FORMAT_TEX32 )
        {
            if ( dstFormat == FORMAT_IDTEX4 ||
                 dstFormat == FORMAT_IDTEX8 ||
                 dstFormat == FORMAT_IDTEX8_COMPRESSED ||
                 dstFormat == FORMAT_TEX16 )
            {
                return false;
            }
        }

        // If anything reaches this, then we have an unhandled situation.
        return false;
    }

    inline static bool getEncodingFormatDimensions(
        eFormatEncodingType encodingType,
        uint32& pixelColumnWidth, uint32& pixelColumnHeight
    )
    {
        uint32 rawColumnWidth = 0;
        uint32 rawColumnHeight = 0;

        if (encodingType == FORMAT_IDTEX4)
        {
            // PSMT4
            rawColumnWidth = 32;
            rawColumnHeight = 4;
        }
        else if (encodingType == FORMAT_IDTEX8)
        {
            // PSMT8
            rawColumnWidth = 16;
            rawColumnHeight = 4;
        }
        else if (encodingType == FORMAT_IDTEX8_COMPRESSED)
        {
            // special format used by RenderWare (undocumented)
            rawColumnWidth = 16;
            rawColumnHeight = 4;
        }
        else if (encodingType == FORMAT_TEX16)
        {
            // PSMCT16
            rawColumnWidth = 16;
            rawColumnHeight = 2;
        }
        else if (encodingType == FORMAT_TEX32)
        {
            // PSMCT32
            rawColumnWidth = 8;
            rawColumnHeight = 2;
        }
        else
        {
            return false;
        }
        
        pixelColumnWidth = rawColumnWidth;
        pixelColumnHeight = rawColumnHeight;
        return true;
    }

    inline static bool getPermutationDimensions(eFormatEncodingType permFormat, uint32& permWidth, uint32& permHeight)
    {
        if (permFormat == FORMAT_IDTEX4)
        {
            permWidth = 8;
            permHeight = 16;
        }
        else if (permFormat == FORMAT_IDTEX8 || permFormat == FORMAT_IDTEX8_COMPRESSED)
        {
            permWidth = 4;
            permHeight = 16;
        }
        else
        {
            return false;
        }

        return true;
    }

    inline static bool detect_packing_routine(
        eFormatEncodingType rawFormat, eFormatEncodingType packedFormat,
        const uint32*& permutationData_primCol,
        const uint32*& permutationData_secCol
    )
    {
        if (packedFormat == FORMAT_TEX32)
        {
            if (rawFormat == FORMAT_IDTEX4)
            {
                permutationData_primCol = ps2GSPixelEncodingFormatsData::psmt4_to_psmct32_prim;
                permutationData_secCol = ps2GSPixelEncodingFormatsData::psmt4_to_psmct32_sec;
                return true;
            }
            else if (rawFormat == FORMAT_IDTEX8 || rawFormat == FORMAT_IDTEX8_COMPRESSED)
            {
                permutationData_primCol = ps2GSPixelEncodingFormatsData::psmt8_to_psmct32_prim;
                permutationData_secCol = ps2GSPixelEncodingFormatsData::psmt8_to_psmct32_sec;
                return true;
            }
        }
        return false;
    }
};

typedef memcodec::genericMemoryEncoder <ps2GSPixelEncodingGeneric> ps2GSPixelEncodingFormats;

};