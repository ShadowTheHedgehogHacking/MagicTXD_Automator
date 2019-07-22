#include "StdInc.h"

#include "rwimaging.hxx"

#include "pixelformat.hxx"

#include <sdk/PluginHelpers.h>

#include "streamutil.hxx"

namespace rw
{

#ifdef RWLIB_INCLUDE_BMP_IMAGING
// .bmp file imaging extension.

typedef uint8 BYTE;
typedef uint16 WORD;
typedef uint32 DWORD;
typedef int32 LONG;

#pragma pack(push,1)
struct bmpFileHeader
{
    char                            bfType_magic[2];
    endian::little_endian <DWORD>   bfSize;
    endian::little_endian <WORD>    bfReserved1;
    endian::little_endian <WORD>    bfReserved2;
    endian::little_endian <DWORD>   bfOffBits;
};

struct bmpInfoHeader
{
    endian::little_endian <DWORD>      biSize;
    endian::little_endian <LONG>       biWidth;
    endian::little_endian <LONG>       biHeight;
    endian::little_endian <WORD>       biPlanes;
    endian::little_endian <WORD>       biBitCount;
    endian::little_endian <DWORD>      biCompression;
    endian::little_endian <DWORD>      biSizeImage;
    endian::little_endian <LONG>       biXPelsPerMeter;
    endian::little_endian <LONG>       biYPelsPerMeter;
    endian::little_endian <DWORD>      biClrUsed;
    endian::little_endian <DWORD>      biClrImportant;
};
#pragma pack(pop)

inline uint32 getBMPTexelDataRowAlignment( void )
{
    return sizeof( DWORD );
}

static const imaging_filename_ext bmp_ext[] =
{
    { "BMP", true }
};

struct bmpImagingEnv : public imagingFormatExtension
{
    inline void Initialize( Interface *engineInterface )
    {
        // Register ourselves.
        RegisterImagingFormat( engineInterface, "Raw Bitmap", IMAGING_COUNT_EXT(bmp_ext), bmp_ext, this );
    }

    inline void Shutdown( Interface *engineInterface )
    {
        UnregisterImagingFormat( engineInterface, this );
    }

    bool IsStreamCompatible( Interface *engineInterface, Stream *inputStream ) const override
    {
        int64 bmpStartOffset = inputStream->tell();

        // This is a pretty straight-forward system.
        bmpFileHeader header;

        size_t headerReadCount = inputStream->read( &header, sizeof( header ) );

        if ( headerReadCount != sizeof( header ) )
        {
            return false;
        }

        if ( header.bfType_magic[0] != 'B' || header.bfType_magic[1] != 'M' )
        {
            return false;
        }

        if ( header.bfReserved1 != 0 || header.bfReserved2 != 0 )
        {
            return false;
        }

        bmpInfoHeader infoHeader;

        size_t infoReadCount = inputStream->read( &infoHeader, sizeof( infoHeader ) );

        if ( infoReadCount != sizeof( infoHeader ) )
        {
            return false;
        }

        // Alright, now just the raster data has to be valid.
        inputStream->seek( bmpStartOffset + header.bfOffBits, eSeekMode::RWSEEK_BEG );

        uint32 imageRowSize = getRasterDataRowSize( abs( infoHeader.biWidth ), infoHeader.biBitCount, getBMPTexelDataRowAlignment() );

        uint32 imageDataSize = getRasterDataSizeByRowSize( imageRowSize, abs( infoHeader.biHeight ) );

        skipAvailable( inputStream, imageDataSize );

        // I guess we should be a BMP file.
        return true;
    }

    void GetStorageCapabilities( pixelCapabilities& capsOut ) const override
    {
        capsOut.supportsDXT1 = false;
        capsOut.supportsDXT2 = false;
        capsOut.supportsDXT3 = false;
        capsOut.supportsDXT4 = false;
        capsOut.supportsDXT5 = false;
        capsOut.supportsPalette = true;
    }

    void DeserializeImage( Interface *engineInterface, Stream *inputStream, imagingLayerTraversal& outputPixels ) const override
    {
        int64 bitmapStartOffset = inputStream->tell();

        // BMP files are a pretty simple format.
        bmpFileHeader header;

        size_t headerReadCount = inputStream->read( &header, sizeof( header ) );

        if ( headerReadCount != sizeof( header ) )
        {
            throw RwException( "could not read .bmp header" );
        }

        // We only know one bitmap type.
        if ( header.bfType_magic[0] != 'B' || header.bfType_magic[1] != 'M' )
        {
            throw RwException( "invalid checksum for .bmp" );
        }

        if ( header.bfReserved1 != 0 || header.bfReserved2 != 0 )
        {
            throw RwException( "unknown bitmap extension; reserved not zero" );
        }

        // Now we read the info header.
        bmpInfoHeader infoHeader;

        size_t infoHeaderReadCount = inputStream->read( &infoHeader, sizeof( infoHeader ) );

        if ( infoHeaderReadCount != sizeof( infoHeader ) )
        {
            throw RwException( "could not read .bmp info header" );
        }

        if ( infoHeader.biSize != sizeof( infoHeader ) )
        {
            throw RwException( "unknown .bmp revision: invalid info header size" );
        }

        if ( infoHeader.biPlanes != 1 )
        {
            throw RwException( "invalid amount of .bmp planes (must be 1)" );
        }

        if ( infoHeader.biCompression != 0 )
        {
            throw RwException( "no support for compressed .bmp files (sorry)" );
        }

        // We ignore the size field.

        // Decide about the raster format we decode to.
        eRasterFormat rasterFormat;
        uint32 depth;
        eColorOrdering colorOrder;

        ePaletteType paletteType = PALETTE_NONE;

        bool hasRasterFormat = false;

        uint32 itemDepth;

        if ( infoHeader.biBitCount == 1 )
        {
            throw RwException( "cannot read monochrome .bmp files" );

#if 0
            rasterFormat = RASTER_LUM;
            depth = 8;

            itemDepth = 8;

            hasRasterFormat = true;
#endif
        }
        else if ( infoHeader.biBitCount == 4 || infoHeader.biBitCount == 8 )
        {
            rasterFormat = RASTER_888;
            depth = 32;

            if ( infoHeader.biBitCount == 4 )
            {
                // I actually added support especially for palette data that has a different byte ordering.
                // This feature is important. All native textures must be aware of it.
                paletteType = PALETTE_4BIT_LSB;

                itemDepth = 4;
            }
            else if ( infoHeader.biBitCount == 8 )
            {
                paletteType = PALETTE_8BIT;

                itemDepth = 8;
            }

            colorOrder = COLOR_BGRA;

            hasRasterFormat = true;
        }
        else if ( infoHeader.biBitCount == 16 )
        {
            rasterFormat = RASTER_565;
            depth = 16;

            itemDepth = depth;

            colorOrder = COLOR_BGRA;

            hasRasterFormat = true;
        }
        else if ( infoHeader.biBitCount == 24 )
        {
            rasterFormat = RASTER_888;
            depth = 24;

            itemDepth = depth;

            colorOrder = COLOR_BGRA;

            hasRasterFormat = true;
        }
        else if ( infoHeader.biBitCount == 32 )
        {
            rasterFormat = RASTER_888;
            depth = 32;

            itemDepth = depth;

            colorOrder = COLOR_BGRA;

            hasRasterFormat = true;
        }

        if ( hasRasterFormat == false )
        {
            throw RwException( "unsupported .bmp raster format" );
        }

        // If we have a palette, read it.
        void *paletteData = nullptr;
        uint32 paletteSize = 0;

        if ( paletteType != PALETTE_NONE )
        {
            paletteSize = infoHeader.biClrUsed;

            if ( paletteSize == 0 )
            {
                // In this case we assume the maximum color count.
                paletteSize = getPaletteItemCount( paletteType );
            }

            uint32 paletteDataSize = getPaletteDataSize( paletteSize, depth );

            checkAhead( inputStream, paletteDataSize );

            paletteData = engineInterface->PixelAllocate( paletteDataSize );

            if ( paletteData == nullptr )
            {
                throw RwException( "failed to allocate palette data for BMP deserialization" );
            }

            try
            {
                size_t palReadCount = inputStream->read( paletteData, paletteDataSize );

                if ( palReadCount != paletteDataSize )
                {
                    throw RwException( "failed to read .bmp palette data" );
                }
            }
            catch( ... )
            {
                engineInterface->PixelFree( paletteData );

                throw;
            }
        }

        try
        {
            // We skip to the raster data to read it.
            inputStream->seek( bitmapStartOffset + header.bfOffBits, eSeekMode::RWSEEK_BEG );

            // Now read the actual image data.
            LONG bmpWidth = infoHeader.biWidth;
            LONG bmpHeight = infoHeader.biHeight;

            bool isUpsideDown = ( bmpHeight > 0 );

            uint32 width = abs( bmpWidth );
            uint32 height = abs( bmpHeight );

            // Calculate the image data size, which includes the padding bytes for each row.
            const uint32 rowPadding = getBMPTexelDataRowAlignment();

            const uint32 rowSize = getRasterDataRowSize( width, itemDepth, rowPadding );

            const uint32 imageDataSize = ( rowSize * height );

            checkAhead( inputStream, imageDataSize );

            void *texelData = engineInterface->PixelAllocate( imageDataSize );

            int64 imageDataStartOffset = inputStream->tell();

            try
            {
                // Read all the rows.
                for ( uint32 y = 0; y < height; y++ )
                {
                    // Seek to the row.
                    int64 seekPos;

                    if ( !isUpsideDown )
                    {
                        seekPos = rowSize * y;
                    }
                    else
                    {
                        seekPos = rowSize * ( height - y - 1 );
                    }

                    inputStream->seek( imageDataStartOffset + seekPos, RWSEEK_BEG );

                    // Read only the part we are interested in, at the appropriate position.
                    void *texelPosition = getTexelDataRow( texelData, rowSize, y );

                    size_t readCountRow = inputStream->read( texelPosition, rowSize );

                    if ( readCountRow != rowSize )
                    {
                        throw RwException( "failed to read .bmp image data rows" );
                    }

                    // Alright, we got the row. Lets continue.
                }
            }
            catch( ... )
            {
                engineInterface->PixelFree( texelData );

                throw;
            }

            // Alright. We are successful.
            outputPixels.layerWidth = width;
            outputPixels.layerHeight = height;
            outputPixels.mipWidth = width;
            outputPixels.mipHeight = height;
            outputPixels.texelSource = texelData;
            outputPixels.dataSize = imageDataSize;

            outputPixels.rasterFormat = rasterFormat;
            outputPixels.depth = itemDepth;
            outputPixels.rowAlignment = rowPadding;
            outputPixels.colorOrder = colorOrder;
            outputPixels.paletteType = paletteType;
            outputPixels.paletteData = paletteData;
            outputPixels.paletteSize = paletteSize;
            outputPixels.compressionType = RWCOMPRESS_NONE;

            outputPixels.hasAlpha = false;  // BMP never has alpha.
        }
        catch( ... )
        {
            if ( paletteData != nullptr )
            {
                engineInterface->PixelFree( paletteData );

                paletteData = nullptr;
            }

            throw;
        }
    }

    void SerializeImage( Interface *engineInterface, Stream *outputStream, const imagingLayerTraversal& inputPixels ) const override
    {
        uint32 mipWidth = inputPixels.mipWidth;
        uint32 mipHeight = inputPixels.mipHeight;

        uint32 layerWidth = inputPixels.layerWidth;
        uint32 layerHeight = inputPixels.layerHeight;

        // Determine the image format that we should output as.
        eRasterFormat rasterFormat = inputPixels.rasterFormat;
        uint32 depth = inputPixels.depth;
        uint32 rowAlignment = inputPixels.rowAlignment;
        eColorOrdering colorOrder = inputPixels.colorOrder;
        ePaletteType paletteType = inputPixels.paletteType;
        void *paletteData = inputPixels.paletteData;
        uint32 paletteSize = inputPixels.paletteSize;

        if ( inputPixels.compressionType != RWCOMPRESS_NONE )
        {
            throw RwException( "cannot serialize compressed pixel data in .bmp" );
        }

        eRasterFormat dstRasterFormat = rasterFormat;
        uint32 dstDepth = depth;
        eColorOrdering dstColorOrder = COLOR_BGRA;  // always.
        ePaletteType dstPaletteType = paletteType;
        void *dstPaletteData = paletteData;
        uint32 dstPaletteSize = paletteSize;

        uint32 colorUseCount = 0;

        uint32 dstItemDepth = dstDepth;

        if ( dstPaletteType != PALETTE_NONE )
        {
            if ( dstPaletteType == PALETTE_4BIT )
            {
                dstPaletteType = PALETTE_4BIT_LSB;
            }
            else if ( dstPaletteType != PALETTE_4BIT_LSB )
            {
                dstPaletteType = PALETTE_8BIT;
            }

            dstRasterFormat = RASTER_888;
            dstDepth = 32;

            colorUseCount = dstPaletteSize;

            if ( dstPaletteType == PALETTE_4BIT_LSB )
            {
                dstItemDepth = 4;
            }
            else if ( dstPaletteType == PALETTE_8BIT )
            {
                dstItemDepth = 8;
            }
            else
            {
                assert( 0 );
            }

            dstPaletteSize = getPaletteItemCount( dstPaletteType );
        }
        else
        {
            if ( dstRasterFormat == RASTER_565 )
            {
                dstDepth = 16;
            }
            else if ( dstRasterFormat == RASTER_888 )
            {
                if ( dstDepth != 24 )
                {
                    dstDepth = 24;
                }
            }
            else
            {
                dstRasterFormat = RASTER_888;
                dstDepth = 24;
            }

            colorUseCount = 0;

            dstItemDepth = dstDepth;
        }

        // Convert the imaging layer to a proper format.
        void *srcTexels = inputPixels.texelSource;

        const uint32 rowPadding = getBMPTexelDataRowAlignment();

        const uint32 dstRowSize = getRasterDataRowSize( mipWidth, dstItemDepth, rowPadding );

        uint32 dstDataSize = getRasterDataSizeByRowSize( dstRowSize, mipHeight );

        try
        {
            // Also re-encode the palette, if there is one.
            uint32 palDataSize = 0;

            if ( dstPaletteType != PALETTE_NONE )
            {
                assert( paletteType != PALETTE_NONE );

                palDataSize = getPaletteDataSize( dstPaletteSize, dstDepth );

                uint32 srcPalRasterDepth = Bitmap::getRasterFormatDepth( rasterFormat );

                TransformPaletteDataEx(
                    engineInterface,
                    paletteData,
                    paletteSize, dstPaletteSize,
                    rasterFormat, colorOrder, srcPalRasterDepth,
                    dstRasterFormat, dstColorOrder, dstDepth,
                    false,
                    dstPaletteData
                );
            }

            // Calculate the file size.
            DWORD actualFileSize = sizeof( bmpFileHeader );

            // After the file header is an info header.
            actualFileSize += sizeof( bmpInfoHeader );

            // Now we should have a palette, if we are palettized.
            if ( dstPaletteType != PALETTE_NONE )
            {
                actualFileSize += palDataSize;
            }

            DWORD rasterOffBits = actualFileSize;

            // The last part is our image data.
            actualFileSize += dstDataSize;

            // Lets push some data onto disk.
            bmpFileHeader header;
            header.bfType_magic[0] = 'B';
            header.bfType_magic[1] = 'M';
            header.bfSize = actualFileSize;
            header.bfReserved1 = 0;
            header.bfReserved2 = 0;
            header.bfOffBits = rasterOffBits;

            outputStream->write( &header, sizeof( header ) );

            const bool isUpsideDown = false;

            // Now write the info header.
            bmpInfoHeader infoHeader;
            infoHeader.biSize = sizeof( infoHeader );
            infoHeader.biWidth = layerWidth;
            infoHeader.biHeight = layerHeight;      // we say it is an upside down BMP and write our data upside down.
            infoHeader.biPlanes = 1;
            infoHeader.biBitCount = dstItemDepth;
            infoHeader.biCompression = 0;           // no compression.
            infoHeader.biSizeImage = 0;             // not determined, can be produced automatically.
            infoHeader.biXPelsPerMeter = 3780;      // ???
            infoHeader.biYPelsPerMeter = 3780;
            infoHeader.biClrUsed = colorUseCount;
            infoHeader.biClrImportant = 0;          // all.

            outputStream->write( &infoHeader, sizeof( infoHeader ) );

            // If we are a palette .bmp, write the palette.
            if ( dstPaletteType != PALETTE_NONE )
            {
                outputStream->write( dstPaletteData, palDataSize );
            }

            // Write the friggin raster data!
            // We need to write in padded rows.
            {
                // Use a temporary buffer to transform to, as we write on a row-by-row basis.
                void *tmpRow = engineInterface->PixelAllocate( dstRowSize );

                if ( tmpRow == nullptr )
                {
                    throw RwException( "failed to allocate transformation row for BMP serialization" );
                }

                try
                {
                    // Write the rows.
                    for ( uint32 y = 0; y < mipHeight; y++ )
                    {
                        // Get the real row we should write.
                        uint32 real_row = 0;

                        if ( isUpsideDown )
                        {
                            real_row = y;
                        }
                        else
                        {
                            real_row = ( mipHeight - y - 1 );
                        }

                        // Write a row buffer.
                        // We do not care about clearing the end of the buffer.
                        moveTexels(
                            srcTexels, tmpRow,
                            0, real_row,
                            0, 0,
                            mipWidth, 1,
                            mipWidth, mipHeight,
                            rasterFormat, depth, rowAlignment, colorOrder, paletteType, paletteSize,
                            dstRasterFormat, dstItemDepth, rowPadding, dstColorOrder, dstPaletteType, dstPaletteSize
                        );

                        outputStream->write( tmpRow, dstRowSize );

                        // We are done.
                    }
                }
                catch( ... )
                {
                    engineInterface->PixelFree( tmpRow );

                    throw;
                }

                engineInterface->PixelFree( tmpRow );
            }
        }
        catch( ... )
        {
            // Since there was an error during writing the bitmap, we want to free
            // resources and quit.
            if ( paletteData != dstPaletteData )
            {
                engineInterface->PixelFree( dstPaletteData );
            }

            throw;
        }

        // Clear runtime information.
        if ( paletteData != dstPaletteData )
        {
            engineInterface->PixelFree( dstPaletteData );
        }
    }
};

static PluginDependantStructRegister <bmpImagingEnv, RwInterfaceFactory_t> bmpEnvRegister;

#endif //RWLIB_INCLUDE_BMP_IMAGING

void registerBMPImagingExtension( void )
{
#ifdef RWLIB_INCLUDE_BMP_IMAGING
    bmpEnvRegister.RegisterPlugin( engineFactory );
#endif //RWLIB_INCLUDE_BMP_IMAGING
}

};
