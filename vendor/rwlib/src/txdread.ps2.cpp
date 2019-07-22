#include "StdInc.h"

#ifdef RWLIB_INCLUDE_NATIVETEX_PLAYSTATION2

#include <bitset>
#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>
#include <cmath>

#include <type_traits>

#include "txdread.ps2.hxx"
#include "txdread.d3d.hxx"

#include "pixelformat.hxx"

#include "pixelutil.hxx"

#include "txdread.ps2gsman.hxx"

#include "pluginutil.hxx"

#include "txdread.miputil.hxx"

#include "txdread.ps2shared.enc.hxx"

namespace rw
{

inline void verifyTexture( Interface *engineInterface, const NativeTexturePS2::GSTexture& gsTex, bool hasHeaders, eFormatEncodingType currentEncodingType, eFormatEncodingType imageDecodeFormatType, ps2MipmapTransmissionData& transmissionOffset )
{
    // If the texture had the headers, then it should have come with the required registers.
    if ( hasHeaders )
    {
        // Debug register contents (only the important ones).
        size_t regCount = gsTex.storedRegs.GetCount();

        bool hasTRXPOS = false;
        bool hasTRXREG = false;
        bool hasTRXDIR = false;

        for ( size_t n = 0; n < regCount; n++ )
        {
            const NativeTexturePS2::GSTexture::GSRegInfo& regInfo = gsTex.storedRegs[ n ];

            if ( regInfo.regID == GIF_REG_TRXPOS )
            {
                // TRXPOS
                const ps2GSRegisters::TRXPOS_REG& trxpos = (const ps2GSRegisters::TRXPOS_REG&)regInfo.content;

                if ( trxpos.ssax != 0 )
                {
                    engineInterface->PushWarning( "PS2 native texture TRXPOS register: invalid value for ssax" );
                }
                if ( trxpos.ssay != 0 )
                {
                    engineInterface->PushWarning( "PS2 native texture TRXPOS register: invalid value for ssay" );
                }
                if ( trxpos.dir != 0 )
                {
                    engineInterface->PushWarning( "PS2 native texture TRXPOS register: invalid value for dir" );
                }

                // Give the transmission settings to the runtime.
                transmissionOffset.destX = trxpos.dsax;
                transmissionOffset.destY = trxpos.dsay;

                hasTRXPOS = true;
            }
            else if ( regInfo.regID == GIF_REG_TRXREG )
            {
                // TRXREG
                const ps2GSRegisters::TRXREG_REG& trxreg = (const ps2GSRegisters::TRXREG_REG&)regInfo.content;

                // Convert this to swizzle width and height.
                uint32 storedSwizzleWidth = trxreg.transmissionAreaWidth;
                uint32 storedSwizzleHeight = trxreg.transmissionAreaHeight;

                if (currentEncodingType == FORMAT_TEX32 && imageDecodeFormatType == FORMAT_IDTEX8_COMPRESSED)
                {
                    storedSwizzleWidth /= 2;
                }

                if ( storedSwizzleWidth != gsTex.swizzleWidth )
                {
                    engineInterface->PushWarning( "PS2 native texture TRXREG register: invalid transmission area width" );
                }
                if ( storedSwizzleHeight != gsTex.swizzleHeight)
                {
                    engineInterface->PushWarning( "PS2 native texture TRXREG register: invalid transmission area height" );
                }

                hasTRXREG = true;
            }
            else if ( regInfo.regID == GIF_REG_TRXDIR )
            {
                // TRXDIR
                const ps2GSRegisters::TRXDIR_REG& trxdir = (const ps2GSRegisters::TRXDIR_REG&)regInfo.content;

                // Textures have to be transferred to the GS memory.
                if ( trxdir.xdir != 0 )
                {
                    engineInterface->PushWarning( "PS2 native texture TRXDIR register: invalid xdir value" );
                }

                hasTRXDIR = true;
            }
        }

        // We kinda require all registers.
        if ( hasTRXPOS == false )
        {
            engineInterface->PushWarning( "PS2 native texture is missing TRXPOS register" );
        }
        if ( hasTRXREG == false )
        {
            engineInterface->PushWarning( "PS2 native texture is missing TRXREG register" );
        }
        if ( hasTRXDIR == false )
        {
            engineInterface->PushWarning( "PS2 native texture is missing TRXDIR register" );
        }
    }
}

uint32 NativeTexturePS2::GSTexture::readGIFPacket(Interface *engineInterface, BlockProvider& inputProvider, bool hasHeaders, bool& corruptedHeaders_out)
{
    // See https://www.dropbox.com/s/onjaprt82y81sj7/EE_Users_Manual.pdf page 151

    uint32 readCount = 0;

	if (hasHeaders)
    {
        // A GSTexture always consists of a register list and the image data.
        struct invalid_gif_exception
        {
        };

        bool corruptedHeaders = false;

        int64 streamOff_safe = inputProvider.tell();

        uint32 gif_readCount = 0;

        try
        {
            {
                GIFtag_serialized regListTag_ser;
                inputProvider.read( &regListTag_ser, sizeof(regListTag_ser) );

                gif_readCount += sizeof(regListTag_ser);

                GIFtag regListTag = regListTag_ser;

                // If we have a register list, parse it.
                if (regListTag.flg == 0)
                {
                    if (regListTag.eop != false ||
                        regListTag.pre != false ||
                        regListTag.prim != 0)
                    {
                        throw invalid_gif_exception();
                    }

                    // Only allow the register list descriptor.
                    if (regListTag.nreg != 1 ||
                        regListTag.getRegisterID(0) != 0xE)
                    {
                        throw invalid_gif_exception();
                    }

                    uint32 numRegs = regListTag.nloop;

                    // Preallocate the register space.
                    this->storedRegs.Resize( numRegs );

                    for ( uint32 n = 0; n < numRegs; n++ )
                    {
                        // Read the register content.
                        uint64 regContent = inputProvider.readUInt64();

                        // Read the register ID.
                        regID_struct regID( inputProvider.readUInt64() );

                        // Put the register into the register storage.
                        GSRegInfo& regInfo = this->storedRegs[ n ];

                        regInfo.regID = (eGSRegister)regID.regID;
                        regInfo.content = regContent;
                    }

                    gif_readCount += numRegs * ( sizeof(unsigned long long) * 2 );
                }
                else
                {
                    throw invalid_gif_exception();
                }
            }

            // Read the image data GIFtag.
            {
                GIFtag_serialized imgDataTag_ser;
                inputProvider.read( &imgDataTag_ser, sizeof(imgDataTag_ser) );

                gif_readCount += sizeof(imgDataTag_ser);

                GIFtag imgDataTag = imgDataTag_ser;

                // Verify that this is an image data tag.
                if (imgDataTag.eop != false ||
                    imgDataTag.pre != false ||
                    imgDataTag.prim != 0 ||
                    imgDataTag.flg != 2 ||
                    imgDataTag.nreg != 0)
                {
                    throw invalid_gif_exception();
                }

                // Verify the image data size.
                if (imgDataTag.nloop != (this->dataSize / (sizeof(unsigned long long) * 2)))
                {
                    throw invalid_gif_exception();
                }
            }
        }
        catch( invalid_gif_exception& )
        {
            // We ignore the headers and try to read the image data.
            inputProvider.seek( streamOff_safe + 0x50, RWSEEK_BEG );

            gif_readCount = 0x50;

            corruptedHeaders = true;
        }

        readCount += gif_readCount;

        corruptedHeaders_out = corruptedHeaders;
	}

    uint32 texDataSize = this->dataSize;

    void *texelData = nullptr;

    if ( texDataSize != 0 )
    {
        // Check that we even have that much data in the stream.
        inputProvider.check_read_ahead( texDataSize );

        texelData = engineInterface->PixelAllocate( texDataSize );

        try
        {
            inputProvider.read( texelData, texDataSize );
        }
        catch( ... )
        {
            engineInterface->PixelFree( texelData );

            throw;
        }

        readCount += texDataSize;
    }

    this->texels = texelData;

    return readCount;
}

eTexNativeCompatibility ps2NativeTextureTypeProvider::IsCompatibleTextureBlock( BlockProvider& inputProvider ) const
{
    eTexNativeCompatibility returnCompat = RWTEXCOMPAT_NONE;

    // Go into the master header.
    BlockProvider texNativeMasterHeader( &inputProvider );

    texNativeMasterHeader.EnterContext();

    try
    {
        if ( texNativeMasterHeader.getBlockID() == CHUNK_STRUCT )
        {
            // We simply verify the checksum.
            // If it matches, we believe it definately is a PS2 texture.
            uint32 checksum = texNativeMasterHeader.readUInt32();

            if ( checksum == PS2_FOURCC )
            {
                returnCompat = RWTEXCOMPAT_ABSOLUTE;
            }
        }
    }
    catch( ... )
    {
        texNativeMasterHeader.LeaveContext();

        throw;
    }

    texNativeMasterHeader.LeaveContext();

    // We are not a PS2 texture for whatever reason.
    return returnCompat;
}

inline bool isValidRasterFormat(eRasterFormat rasterFormat)
{
    // This is a legacy function.

    bool isValidRaster = false;

    if (rasterFormat == RASTER_1555 ||
        rasterFormat == RASTER_565 ||
        rasterFormat == RASTER_4444 ||
        rasterFormat == RASTER_LUM ||
        rasterFormat == RASTER_8888 ||
        rasterFormat == RASTER_888 ||
        rasterFormat == RASTER_555)
    {
        isValidRaster = true;
    }

    return isValidRaster;
}

void ps2NativeTextureTypeProvider::DeserializeTexture( TextureBase *theTexture, PlatformTexture *nativeTex, BlockProvider& inputProvider ) const
{
    Interface *engineInterface = theTexture->engineInterface;

    // Read the PS2 master header struct.
    {
        BlockProvider texNativeMasterHeader( &inputProvider );

        texNativeMasterHeader.EnterContext();

        try
        {
            if ( texNativeMasterHeader.getBlockID() == CHUNK_STRUCT )
            {
	            uint32 checksum = texNativeMasterHeader.readUInt32();

	            if (checksum != PS2_FOURCC)
                {
                    throw RwException( "invalid platform for PS2 texture reading" );
                }

                texFormatInfo formatInfo;
                formatInfo.readFromBlock( texNativeMasterHeader );

                // Read texture format.
                formatInfo.parse( *theTexture );
            }
            else
            {
                throw RwException( "could not find texture native master header struct for PS2 texture native" );
            }
        }
        catch( ... )
        {
            texNativeMasterHeader.LeaveContext();

            throw;
        }

        texNativeMasterHeader.LeaveContext();
    }

    int engineWarningLevel = engineInterface->GetWarningLevel();

    // Cast our native texture.
    NativeTexturePS2 *platformTex = (NativeTexturePS2*)nativeTex;

    // Read the name chunk section.
    {
        rwStaticString <char> nameOut;

        utils::readStringChunkANSI( engineInterface, inputProvider, nameOut );

        theTexture->SetName( nameOut.GetConstString() );
    }

    // Read the mask name chunk section.
    {
        rwStaticString <char> nameOut;

        utils::readStringChunkANSI( engineInterface, inputProvider, nameOut );

        theTexture->SetMaskName( nameOut.GetConstString() );
    }

    // Absolute maximum of mipmaps.
    const size_t maxMipmaps = 7;

    // Graphics Synthesizer package struct.
    {
        BlockProvider gsNativeBlock( &inputProvider );

        gsNativeBlock.EnterContext();

        try
        {
            if ( gsNativeBlock.getBlockID() == CHUNK_STRUCT )
            {
                // Texture Meta Struct.
                textureMetaDataHeader textureMeta;
                {
                    BlockProvider textureMetaChunk( &gsNativeBlock );

                    textureMetaChunk.EnterContext();

                    try
                    {
                        if ( textureMetaChunk.getBlockID() == CHUNK_STRUCT )
                        {
                            // Read it.
                            textureMetaChunk.read( &textureMeta, sizeof( textureMeta ) );
                        }
                        else
                        {
                            throw RwException( "could not find texture meta information struct in PS2 texture native" );
                        }
                    }
                    catch( ... )
                    {
                        textureMetaChunk.LeaveContext();

                        throw;
                    }

                    textureMetaChunk.LeaveContext();
                }

                uint32 depth = textureMeta.depth;

                // Deconstruct the rasterFormat.
                bool hasMipmaps = false;        // TODO: actually use this flag.

                readRasterFormatFlags( textureMeta.rasterFormat, platformTex->rasterFormat, platformTex->paletteType, hasMipmaps, platformTex->autoMipmaps );

                // Verify the raster format.
                eRasterFormat rasterFormat = platformTex->rasterFormat;

                if ( !isValidRasterFormat( rasterFormat ) )
                {
                    throw RwException( "invalid raster format in PS2 texture" );
                }

                // Verify the texture depth.
                {
                    uint32 texDepth = 0;

                    if (platformTex->paletteType == PALETTE_4BIT)
                    {
                        texDepth = 4;
                    }
                    else if (platformTex->paletteType == PALETTE_8BIT)
                    {
                        texDepth = 8;
                    }
                    else
                    {
                        texDepth = Bitmap::getRasterFormatDepth(rasterFormat);
                    }

                    if (texDepth != depth)
                    {
                        throw RwException( "texture " + theTexture->GetName() + " has an invalid depth" );
                    }
                }

                platformTex->requiresHeaders = ( textureMeta.rasterFormat & 0x20000 ) != 0;
                platformTex->hasSwizzle = ( textureMeta.rasterFormat & 0x10000 ) != 0;

                // Store the raster type.
                platformTex->rasterType = ( textureMeta.rasterFormat & 0xFF );

                platformTex->depth = depth;

                // Store unique parameters from the texture registers.
                ps2GSRegisters::TEX0_REG tex0( textureMeta.tex0 );
                ps2GSRegisters::TEX1_REG tex1( textureMeta.tex1 );

                ps2GSRegisters::MIPTBP1_REG miptbp1( textureMeta.miptbp1 );
                ps2GSRegisters::MIPTBP2_REG miptbp2( textureMeta.miptbp2 );

                platformTex->gsParams.maxMIPLevel = tex1.maximumMIPLevel;
                platformTex->gsParams.mtba = tex1.mtba;
                platformTex->gsParams.textureFunction = tex0.texFunction;
                platformTex->gsParams.lodCalculationModel = tex1.lodCalculationModel;
                platformTex->gsParams.mmag = tex1.mmag;
                platformTex->gsParams.mmin = tex1.mmin;
                platformTex->gsParams.lodParamL = tex1.lodParamL;
                platformTex->gsParams.lodParamK = tex1.lodParamK;

                platformTex->gsParams.gsTEX1Unknown1 = tex1.unknown;
                platformTex->gsParams.gsTEX1Unknown2 = tex1.unknown2;

                // If we are on the GTA III engine, we need to store the recommended buffer base pointer.
                LibraryVersion libVer = inputProvider.getBlockVersion();

                if (libVer.rwLibMinor <= 3)
                {
                    platformTex->recommendedBufferBasePointer = tex0.textureBasePointer;
                }

                uint32 dataSize = textureMeta.dataSize;
                //uint32 paletteDataSize = textureMeta.paletteDataSize;

                platformTex->skyMipMapVal = textureMeta.skyMipmapVal;

                // 0x00000 means the texture is not swizzled and has no headers
                // 0x10000 means the texture is swizzled and has no headers
                // 0x20000 means swizzling information is contained in the header
                // the rest is the same as the generic raster format
                bool hasHeader = platformTex->requiresHeaders;
                //bool hasSwizzle = platformTex->hasSwizzle;      // TODO: this appears to have no use?

                // GS packet struct.
                {
                    BlockProvider gsPacketBlock( &gsNativeBlock );

                    gsPacketBlock.EnterContext();

                    try
                    {
                        if ( gsPacketBlock.getBlockID() == CHUNK_STRUCT )
                        {
                            // Decide about texture properties.
                            eFormatEncodingType imageEncodingType = platformTex->getHardwareRequiredEncoding(inputProvider.getBlockVersion());

                            // Get the format we should decode to.
                            eFormatEncodingType actualEncodingType = getFormatEncodingFromRasterFormat(rasterFormat, platformTex->paletteType);

                            if (imageEncodingType == FORMAT_UNKNOWN)
                            {
                                throw RwException( "unknown image decoding format" );
                            }
                            if (actualEncodingType == FORMAT_UNKNOWN)
                            {
                                throw RwException( "unknown image encoding format" );
                            }

                            platformTex->swizzleEncodingType = imageEncodingType;

                            // Reserve that much space for mipmaps.
                            //platformTex->mipmaps.reserve( maxMipmaps );

                            ps2MipmapTransmissionData _origMipmapTransData[ maxMipmaps ];
                            bool _hasOrigMipmapTransData[ maxMipmaps ];

                            for ( uint32 n = 0; n < maxMipmaps; n++ )
                            {
                                _hasOrigMipmapTransData[ n ] = false;
                            }

                            // TODO: are PS2 rasters always RGBA?
                            // If not, adjust the color order parameter!

                            /* Pixels/Indices */
                            int64 end = gsPacketBlock.tell();
                            end += (long)dataSize;
                            uint32 i = 0;

                            long remainingImageData = dataSize;

                            mipGenLevelGenerator mipLevelGen( textureMeta.width, textureMeta.height );

                            if ( !mipLevelGen.isValidLevel() )
                            {
                                throw RwException( "texture " + theTexture->GetName() + " has invalid dimensions" );
                            }

                            while (gsPacketBlock.tell() < end)
                            {
                                if (i == maxMipmaps)
                                {
                                    // We cannot have more than the maximum mipmaps.
                                    break;
                                }

                                if (i > 0 && !hasMipmaps)
                                {
                                    break;
                                }

                                // half dimensions if we have mipmaps
                                bool couldEstablishMipmap = true;

                                if (i > 0)
                                {
                                    couldEstablishMipmap = mipLevelGen.incrementLevel();
                                }

                                if ( !couldEstablishMipmap )
                                {
                                    break;
                                }

                                // Create a new mipmap.
                                platformTex->mipmaps.Resize( i + 1 );

                                NativeTexturePS2::GSMipmap& newMipmap = platformTex->mipmaps[i];

                                newMipmap.width = mipLevelGen.getLevelWidth();
                                newMipmap.height = mipLevelGen.getLevelHeight();

                                // Calculate the encoded dimensions.
                                {
                                    uint32 packedWidth, packedHeight;

                                    bool gotPackedDimms =
                                        ps2GSPixelEncodingFormats::getPackedFormatDimensions(
                                            actualEncodingType, imageEncodingType,
                                            newMipmap.width, newMipmap.height,
                                            packedWidth, packedHeight
                                        );

                                    if ( gotPackedDimms == false )
                                    {
                                        throw RwException( "failed to get encoded dimensions for mipmap" );
                                    }

                                    newMipmap.swizzleWidth = packedWidth;
                                    newMipmap.swizzleHeight = packedHeight;
                                }

                                // Calculate the texture data size.
                                newMipmap.dataSize = newMipmap.getDataSize( imageEncodingType );

                                // Read the GIF packet data.
                                bool hasCorruptedHeaders = false;

                                uint32 readCount = newMipmap.readGIFPacket(engineInterface, gsPacketBlock, hasHeader, hasCorruptedHeaders);

                                if ( readCount > (uint32)remainingImageData )
                                {
                                    throw RwException( "invalid image data bounds for PS2 native texture" );
                                }

                                remainingImageData -= readCount;

                                if ( !hasCorruptedHeaders )
                                {
                                    // Verify this mipmap.
                                    verifyTexture( engineInterface, newMipmap, hasHeader, imageEncodingType, actualEncodingType, _origMipmapTransData[i] );

                                    // Remember that we have an original transmission offset.
                                    _hasOrigMipmapTransData[ i ] = true;
                                }
                                else
                                {
                                    engineInterface->PushWarning( "texture " + theTexture->GetName() + " has corrupted image GIF packets" );
                                }

                                i++;
                            }

                            // Assume we have at least one texture.
                            if ( platformTex->mipmaps.GetCount() == 0 )
                            {
                                throw RwException( "empty texture" );
                            }

                            if ( remainingImageData > 0 )
                            {
                                engineInterface->PushWarning( "texture " + theTexture->GetName() + " has image meta data" );

                                // Make sure we are past the image data.
                                gsPacketBlock.skip( remainingImageData );
                            }

                            /* Palette */
                            // vc dyn_trash.txd is weird here
                            ps2MipmapTransmissionData palTransData;
                            bool hasPalTransData = false;

                            long remainingPaletteData = textureMeta.paletteDataSize;

                            if (platformTex->paletteType != PALETTE_NONE)
                            {
                                // Craft the palette texture.
                                NativeTexturePS2::GSTexture& palTex = platformTex->paletteTex;

                                // The dimensions of this texture depend on game version.
                                getPaletteTextureDimensions(platformTex->paletteType, gsPacketBlock.getBlockVersion(), palTex.swizzleWidth, palTex.swizzleHeight);

                                // Decide about encoding type.
                                // Only a limited amount of types are truly supported.
                                eFormatEncodingType palEncodingType = getFormatEncodingFromRasterFormat(rasterFormat, PALETTE_NONE);

                                if (palEncodingType != FORMAT_TEX32 && palEncodingType != FORMAT_TEX16)
                                {
                                    throw RwException( "invalid palette raster format" );
                                }

                                platformTex->paletteSwizzleEncodingType = palEncodingType;

                                // Calculate the texture data size.
                                palTex.dataSize = palTex.getDataSize( palEncodingType );

                                // Read the GIF packet.
                                bool hasCorruptedHeaders = false;

                                uint32 readCount = palTex.readGIFPacket(engineInterface, gsPacketBlock, hasHeader, hasCorruptedHeaders);

                                if ( readCount > (uint32)remainingPaletteData )
                                {
                                    throw RwException( "invalid palette texture data in PS2 native texture" );
                                }

                                if ( !hasCorruptedHeaders )
                                {
                                    // Also debug this texture.
                                    verifyTexture( engineInterface, palTex, hasHeader, palEncodingType, palEncodingType, palTransData );
                                }
                                else
                                {
                                    engineInterface->PushWarning( "texture " + theTexture->GetName() + " has corrupted palette GIF packets" );
                                }

                                remainingPaletteData -= readCount;

                                if (hasHeader)
                                {
                                    hasPalTransData = true;
                                }
                            }

                            if ( remainingPaletteData > 0 )
                            {
                                engineInterface->PushWarning( "texture " + theTexture->GetName() + " has palette meta data" );

                                // Make sure we are past the palette data.
                                gsPacketBlock.skip( remainingPaletteData );
                            }

                            // Allocate texture memory.
                            uint32 mipmapBasePointer[ maxMipmaps ];
                            uint32 mipmapMemorySize[ maxMipmaps ];
                            uint32 mipmapBufferWidth[ maxMipmaps ];

                            ps2MipmapTransmissionData mipmapTransData[ maxMipmaps ];

                            uint32 clutBasePointer;
                            uint32 clutMemSize;
                            ps2MipmapTransmissionData clutTransData;

                            eMemoryLayoutType decodedMemLayoutType;

                            bool hasAllocatedMemory =
                                platformTex->allocateTextureMemory(mipmapBasePointer, mipmapBufferWidth, mipmapMemorySize, mipmapTransData, maxMipmaps, decodedMemLayoutType, clutBasePointer, clutMemSize, clutTransData);

                            // Could fail if no memory left.
                            if ( !hasAllocatedMemory )
                            {
                                throw RwException( "failed to allocate texture memory" );
                            }

                            // Verify that our memory calculation routine is correct.
                            uint32 gpuMinMemory = platformTex->calculateGPUDataSize(mipmapBasePointer, mipmapMemorySize, maxMipmaps, decodedMemLayoutType, clutBasePointer, clutMemSize);

                            if ( textureMeta.combinedGPUDataSize > gpuMinMemory )
                            {
                                // If this assertion is triggered, then adjust the gpu size calculation algorithm
                                // so it outputs a big enough number.
                                engineInterface->PushWarning( "too small GPU data size for texture " + theTexture->GetName() );

                                // TODO: handle this as error?
                            }
                            else if ( textureMeta.combinedGPUDataSize != gpuMinMemory )
                            {
                                // It would be perfect if this condition were never triggered for official R* games textures.
                                engineInterface->PushWarning( "invalid GPU data size for texture " + theTexture->GetName() );
                            }

                            // Verify that our GPU data calculation routine is correct.
                            ps2GSRegisters gpuData;

                            bool isValidTexture = platformTex->generatePS2GPUData(gsPacketBlock.getBlockVersion(), gpuData, mipmapBasePointer, mipmapBufferWidth, mipmapMemorySize, maxMipmaps, decodedMemLayoutType, clutBasePointer);

                            // If any of those assertions fail then either our is routine incomplete
                            // or the input texture is invalid (created by wrong tool probably.)
                            if ( !isValidTexture )
                            {
                                throw RwException( "invalid texture format" );
                            }

                            if ( gpuData.tex0 != tex0 )
                            {
                                if ( engineWarningLevel >= 3 )
                                {
                                    engineInterface->PushWarning( "texture " + theTexture->GetName() + " has invalid TEX0 register" );
                                }
                            }
                            if ( gpuData.tex1 != tex1 )
                            {
                                if ( engineWarningLevel >= 2 )
                                {
                                    engineInterface->PushWarning( "texture " + theTexture->GetName() + " has invalid TEX1 register" );
                                }
                            }
                            if ( gpuData.miptbp1 != miptbp1 )
                            {
                                if ( engineWarningLevel >= 1 )
                                {
                                    engineInterface->PushWarning( "texture " + theTexture->GetName() + " has invalid MIPTBP1 register" );
                                }
                            }
                            if ( gpuData.miptbp2 != miptbp2 )
                            {
                                if ( engineWarningLevel >= 1 )
                                {
                                    engineInterface->PushWarning( "texture " + theTexture->GetName() + " has invalid MIPTBP2 register" );
                                }
                            }

                            // Verify transmission rectangle same-ness.
                            if (hasHeader)
                            {
                                bool hasValidTransmissionRects = true;

                                size_t mipmapCount = platformTex->mipmaps.GetCount();

                                for ( size_t n = 0; n < mipmapCount; n++ )
                                {
                                    const ps2MipmapTransmissionData& srcTransData = _origMipmapTransData[ n ];
                                    const ps2MipmapTransmissionData& dstTransData = mipmapTransData[ n ];

                                    if ( _hasOrigMipmapTransData[ n ] )
                                    {
                                        if ( srcTransData.destX != dstTransData.destX ||
                                             srcTransData.destY != dstTransData.destY )
                                        {
                                            hasValidTransmissionRects = false;
                                            break;
                                        }
                                    }
                                }

                                if ( hasValidTransmissionRects == false )
                                {
                                    engineInterface->PushWarning( "texture " + theTexture->GetName() + " has invalid mipmap transmission offsets" );
                                }
                            }

                            // Verify palette transmission rectangle.
                            if (platformTex->paletteType != PALETTE_NONE && hasPalTransData)
                            {
                                if ( clutTransData.destX != palTransData.destX ||
                                     clutTransData.destY != palTransData.destY )
                                {
                                    engineInterface->PushWarning( "texture " + theTexture->GetName() + " has invalid CLUT transmission offset" );
                                }
                            }

                            // Fix filtering mode.
                            fixFilteringMode( *theTexture, (uint32)platformTex->mipmaps.GetCount() );
                        }
                        else
                        {
                            engineInterface->PushWarning( "could not find GS image packet block in PS2 texture native" );
                        }
                    }
                    catch( ... )
                    {
                        gsPacketBlock.LeaveContext();

                        throw;
                    }

                    gsPacketBlock.LeaveContext();
                }

                // Done reading native block.
            }
            else
            {
                engineInterface->PushWarning( "could not find GS native data chunk in PS2 texture native" );
            }
        }
        catch( ... )
        {
            gsNativeBlock.LeaveContext();

            throw;
        }

        gsNativeBlock.LeaveContext();
    }

    // Deserialize extensions aswell.
    engineInterface->DeserializeExtensions( theTexture, inputProvider );
}

static PluginDependantStructRegister <ps2NativeTextureTypeProvider, RwInterfaceFactory_t> ps2NativeTexturePlugin;

void registerPS2NativePlugin( void )
{
    ps2NativeTexturePlugin.RegisterPlugin( engineFactory );
}

inline void* TruncateMipmapLayerPS2(
    Interface *engineInterface,
    const void *srcTexels, uint32 srcMipWidth, uint32 srcMipHeight, uint32 srcDepth, uint32 srcRowAlignment,
    uint32 dstMipWidth, uint32 dstMipHeight, uint32 dstRowAlignment,
    uint32& dstDataSizeOut
)
{
    // Allocate a new layer.
    uint32 dstRowSize = getRasterDataRowSize( dstMipWidth, srcDepth, dstRowAlignment );

    uint32 dstDataSize = getRasterDataSizeByRowSize( dstRowSize, dstMipHeight );

    void *dstTexels = engineInterface->PixelAllocate( dstDataSize );

    if ( !dstTexels )
    {
        throw RwException( "failed to allocate texel buffer for mipmap truncation" );
    }

    try
    {
        // Perform the truncation.
        // We want to fill the entire destination buffer with data, but only fill it with source pixels if they exist.
        // The other texels are cleared.
        uint32 srcRowSize = getRasterDataRowSize( srcMipWidth, srcDepth, srcRowAlignment );

        for ( uint32 row = 0; row < dstMipHeight; row++ )
        {
            const void *srcRow = nullptr;
            void *dstRow = getTexelDataRow( dstTexels, dstRowSize, row );

            if ( row < srcMipHeight )
            {
                srcRow = getConstTexelDataRow( srcTexels, srcRowSize, row );
            }

            for ( uint32 col = 0; col < dstMipWidth; col++ )
            {
                // If we have a row, we fetch a color item and apply it.
                // Otherwise we just clear the coordinate.
                if ( srcRow && col < srcMipWidth )
                {
                    moveDataByDepth( dstRow, srcRow, srcDepth, eByteAddressingMode::MOST_SIGNIFICANT, col, col );
                }
                else
                {
                    setDataByDepth( dstRow, srcDepth, col, eByteAddressingMode::MOST_SIGNIFICANT, 0 );
                }
            }
        }
    }
    catch( ... )
    {
        engineInterface->PixelFree( dstTexels );

        throw;
    }

    // Return the data size aswell.
    dstDataSizeOut = dstDataSize;

    return dstTexels;
}

inline void GetPS2TextureTranscodedMipmapData(
    Interface *engineInterface,
    uint32 layerWidth, uint32 layerHeight, uint32 swizzleWidth, uint32 swizzleHeight, const void *srcTexels, uint32 srcTexDataSize,
    eFormatEncodingType mipmapSwizzleEncodingType, eFormatEncodingType mipmapDecodeFormat,
    eRasterFormat srcRasterFormat, uint32 srcDepth, eColorOrdering srcColorOrder,
    eRasterFormat dstRasterFormat, uint32 dstDepth, eColorOrdering dstColorOrder,
    ePaletteType paletteType, uint32 paletteSize,
    void*& dstTexelsOut, uint32& dstDataSizeOut
)
{
    bool hasToTranscode =
        ( mipmapSwizzleEncodingType != mipmapDecodeFormat );

    void *texelData = nullptr;
    uint32 dstDataSize = 0;

    bool doesSourceNeedDeletion = false;

    uint32 srcRowAlignment;
    uint32 dstRowAlignment = getPS2ExportTextureDataRowAlignment(); // it _must_ be this.

    uint32 srcLayerWidth = swizzleWidth;
    uint32 srcLayerHeight = swizzleHeight;

    // Take care about a stable source texel buffer.
    if ( hasToTranscode )
    {
        uint32 newDataSize;

        void *newTexels =
            ps2GSPixelEncodingFormats::transformImageData(
                engineInterface,
                mipmapSwizzleEncodingType, mipmapDecodeFormat,
                srcTexels,
                swizzleWidth, swizzleHeight,
                getPS2TextureDataRowAlignment(), dstRowAlignment,
                layerWidth, layerHeight,
                newDataSize,
                true
            );

        if ( newTexels == nullptr )
        {
            throw RwException( "failed to transform PS2 mipmap layer into a linear raster format (unswizzle)" );
        }

        srcLayerWidth = layerWidth;
        srcLayerHeight = layerHeight;

        // Now we have the swizzled data taken care of.
        srcTexels = newTexels;
        srcTexDataSize = newDataSize;

        doesSourceNeedDeletion = true;

        // The source texels are always permuted.
        srcRowAlignment = dstRowAlignment;
    }
    else
    {
        // If the encoded texture has a bigger buffer size than the raw format should have,
        // we actually must trimm it!
        if ( swizzleWidth != layerWidth || swizzleHeight != layerHeight )
        {
            srcRowAlignment = getPS2ExportTextureDataRowAlignment();

            srcTexels = TruncateMipmapLayerPS2(
                engineInterface,
                srcTexels,
                srcLayerWidth, srcLayerHeight, srcDepth, getPS2TextureDataRowAlignment(),
                layerWidth, layerHeight, srcRowAlignment, srcTexDataSize
            );

            if ( srcTexels == nullptr )
            {
                throw RwException( "failed to truncate PS2 mipmap layer in mipmap transcoding routine" );
            }

            srcLayerWidth = layerWidth;
            srcLayerHeight = layerHeight;

            doesSourceNeedDeletion = true;
        }
        else
        {
            srcRowAlignment = getPS2TextureDataRowAlignment();  // crossing my fingers here!
        }
    }

    try
    {
        // Cache important values.
        bool isConversionComplyingItemSwap = !hasConflictingAddressing( srcLayerWidth, srcDepth, srcRowAlignment, paletteType, dstDepth, dstRowAlignment, paletteType );

        if ( hasToTranscode )
        {
            if ( isConversionComplyingItemSwap )
            {
                texelData = (void*)srcTexels;   // safe cast, because srcTexels is mutable buffer.
                dstDataSize = srcTexDataSize;

                // We want to operate the conversion on ourselves.
                // Also, the source texel buffer will just be taken.
                doesSourceNeedDeletion = false;
            }
            else
            {
                uint32 dstRowSize = getRasterDataRowSize( srcLayerWidth, dstDepth, dstRowAlignment );

                dstDataSize = getRasterDataSizeByRowSize( dstRowSize, srcLayerHeight );

                texelData = engineInterface->PixelAllocate( dstDataSize );

                if ( texelData == nullptr )
                {
                    throw RwException( "failed to allocate texel buffer for PS2 mipmap transcoding" );
                }

                // We need to transcode into a bigger array.
            }
        }
        else
        {
            if ( isConversionComplyingItemSwap )
            {
                // At best, we simply want to copy the texels.
                dstDataSize = srcTexDataSize;
            }
            else
            {
                uint32 dstRowSize = getRasterDataRowSize( layerWidth, dstDepth, dstRowAlignment );

                dstDataSize = getRasterDataSizeByRowSize( dstRowSize, layerHeight );
            }

            texelData = engineInterface->PixelAllocate( dstDataSize );
        }

        // Now that the texture is in linear format, we can prepare it.
        bool fixAlpha = false;

        // TODO: do we have to fix alpha for 16bit raster depths?

        if (srcRasterFormat == RASTER_8888)
        {
            fixAlpha = true;
        }

        // Prepare colors.
        if (paletteType == PALETTE_NONE)
        {
            convertTexelsFromPS2(
                srcTexels, texelData, layerWidth, layerHeight, srcTexDataSize,
                srcRasterFormat, srcDepth, srcRowAlignment, srcColorOrder,
                dstRasterFormat, dstDepth, dstRowAlignment, dstColorOrder,
                fixAlpha
            );
        }
        else
        {
            if ( texelData != srcTexels )
            {
                if ( isConversionComplyingItemSwap )
                {
                    memcpy( texelData, srcTexels, std::min(dstDataSize, srcTexDataSize) );
                }
                else
                {
                    // We need to convert the palette indice into another bit depth.
                    ConvertPaletteDepth(
                        srcTexels, texelData,
                        layerWidth, layerHeight,
                        paletteType, paletteType, paletteSize,
                        srcDepth, dstDepth,
                        srcRowAlignment, dstRowAlignment
                    );
                }
            }
        }
    }
    catch( ... )
    {
        // Clean up memory, because we failed for some reason.
        if ( texelData != nullptr && texelData != srcTexels )
        {
            engineInterface->PixelFree( texelData );
        }

        if ( doesSourceNeedDeletion )
        {
            engineInterface->PixelFree( (void*)srcTexels );
        }

        throw;
    }

    // Make sure we delete temporary texel data.
    if ( doesSourceNeedDeletion )
    {
        engineInterface->PixelFree( (void*)srcTexels );
    }

    // Return stuff to the runtime.
    dstTexelsOut = texelData;
    dstDataSizeOut = dstDataSize;
}

void ps2NativeTextureTypeProvider::GetPixelDataFromTexture( Interface *engineInterface, void *objMem, pixelDataTraversal& pixelsOut )
{
    // Cast to our native platform texture.
    NativeTexturePS2 *platformTex = (NativeTexturePS2*)objMem;

    size_t mipmapCount = platformTex->mipmaps.GetCount();

    eRasterFormat rasterFormat = platformTex->rasterFormat;
    ePaletteType paletteType = platformTex->paletteType;

    // Copy over general attributes.
    uint32 depth = platformTex->depth;

    // Fix wrong auto mipmap property.
    bool hasAutoMipmaps = false;

    if ( mipmapCount == 1 )
    {
        // Direct3D textures can only have mipmaps if they dont come with mipmaps.
        hasAutoMipmaps = platformTex->autoMipmaps;
    }

    pixelsOut.autoMipmaps = hasAutoMipmaps;
    pixelsOut.rasterType = platformTex->rasterType;

    pixelsOut.cubeTexture = false;

    // We will have to swap colors.
    eColorOrdering ps2ColorOrder = platformTex->colorOrdering;
    eColorOrdering d3dColorOrder = COLOR_BGRA;

    // First we want to decode the CLUT.
    void *palTexels = nullptr;
    uint32 palSize = 0;

    if (paletteType != PALETTE_NONE)
    {
        // We need to decode the PS2 CLUT.
        GetPS2TexturePalette(
            engineInterface,
            platformTex->paletteTex.swizzleWidth, platformTex->paletteTex.swizzleHeight, platformTex->paletteSwizzleEncodingType, platformTex->paletteTex.texels,
            rasterFormat, ps2ColorOrder,
            rasterFormat, d3dColorOrder,
            paletteType,
            palTexels, palSize
        );
    }

    // Process the mipmaps.
    if ( mipmapCount != 0 )
    {
        eFormatEncodingType mipmapSwizzleEncodingType = platformTex->swizzleEncodingType;

        eFormatEncodingType mipmapDecodeFormat = getFormatEncodingFromRasterFormat(rasterFormat, paletteType);

        // Make sure there is no unknown format.
        assert( mipmapSwizzleEncodingType != FORMAT_UNKNOWN && mipmapDecodeFormat != FORMAT_UNKNOWN );

        pixelsOut.mipmaps.Resize( mipmapCount );

        for (size_t j = 0; j < mipmapCount; j++)
        {
            NativeTexturePS2::GSMipmap& gsTex = platformTex->mipmaps[ j ];

            // We have to create a new texture buffer that is unswizzled to linear format.
            uint32 layerWidth = gsTex.width;
            uint32 layerHeight = gsTex.height;

            const void *srcTexels = gsTex.texels;
            uint32 texDataSize = gsTex.dataSize;

            void *dstTexels = nullptr;
            uint32 dstDataSize = 0;

            GetPS2TextureTranscodedMipmapData(
                engineInterface,
                layerWidth, layerHeight, gsTex.swizzleWidth, gsTex.swizzleHeight, srcTexels, texDataSize,
                mipmapSwizzleEncodingType, mipmapDecodeFormat,
                rasterFormat, depth, ps2ColorOrder,
                rasterFormat, depth, d3dColorOrder,
                paletteType, palSize,
                dstTexels, dstDataSize
            );

            // Move over the texture data to pixel storage.
            pixelDataTraversal::mipmapResource newLayer;

            newLayer.width = layerWidth;
            newLayer.height = layerHeight;
            newLayer.layerWidth = layerWidth;   // layer dimensions.
            newLayer.layerHeight = layerHeight;

            newLayer.texels = dstTexels;
            newLayer.dataSize = dstDataSize;

            // Store the layer.
            pixelsOut.mipmaps[ j ] = newLayer;
        }
    }

    // Set up general raster attributes.
    pixelsOut.rasterFormat = rasterFormat;
    pixelsOut.colorOrder = d3dColorOrder;
    pixelsOut.depth = depth;
    pixelsOut.rowAlignment = getPS2ExportTextureDataRowAlignment();

    // Copy over more advanced attributes.
    pixelsOut.paletteData = palTexels;
    pixelsOut.paletteSize = palSize;
    pixelsOut.paletteType = paletteType;

    // We are an uncompressed raster.
    pixelsOut.compressionType = RWCOMPRESS_NONE;

    // Actually, since there is no alpha flag in PS2 textures, we should recalculate the alpha flag here.
    pixelsOut.hasAlpha = calculateHasAlpha( engineInterface, pixelsOut );

    // For now, we will always allocate new pixels due to the complexity of the encoding.
    pixelsOut.isNewlyAllocated = true;
}

inline void ConvertMipmapToPS2Format(
    Interface *engineInterface,
    uint32 mipWidth, uint32 mipHeight, const void *srcTexelData, uint32 srcDataSize,
    eFormatEncodingType linearMipmapInternalFormat, eFormatEncodingType swizzleMipmapRequiredEncoding,
    eRasterFormat srcRasterFormat, uint32 srcItemDepth, eColorOrdering srcColorOrder,
    eRasterFormat dstRasterFormat, uint32 dstItemDepth, eColorOrdering dstColorOrder,
    ePaletteType srcPaletteType, ePaletteType dstPaletteType, uint32 paletteSize,
    uint32 srcRowAlignment,
    uint32& swizzleWidthOut, uint32& swizzleHeightOut,
    void*& dstSwizzledTexelsOut, uint32& dstSwizzledDataSizeOut
)
{
    // We need to convert the texels before storing them in the PS2 texture.
    bool fixAlpha = false;

    // TODO: do we have to fix alpha for 16bit rasters?

    if (dstRasterFormat == RASTER_8888)
    {
        fixAlpha = true;
    }

    // TODO: optimize for the situation where we do not need to allocate a new texel buffer but
    // use the source texel buffer directly.

    // Allocate a new copy of the texel data.
    uint32 swizzledRowAlignment = getPS2TextureDataRowAlignment();

    uint32 dstLinearRowSize = getRasterDataRowSize( mipWidth, dstItemDepth, swizzledRowAlignment );

    uint32 dstLinearDataSize = getRasterDataSizeByRowSize( dstLinearRowSize, mipHeight );

    void *dstLinearTexelData = engineInterface->PixelAllocate( dstLinearDataSize );

    if ( dstLinearTexelData == nullptr )
    {
        throw RwException( "failed to allocate memory for PS2 texture data conversion" );
    }

    // Swizzle the mipmap.
    // We need to store dimensions into the texture of the current encoding.
    uint32 packedWidth, packedHeight;

    void *dstSwizzledTexelData = nullptr;
    uint32 dstSwizzledDataSize = 0;

    try
    {
        // Convert the texels.
        if (srcPaletteType == PALETTE_NONE)
        {
            convertTexelsToPS2(
                srcTexelData, dstLinearTexelData, mipWidth, mipHeight, srcDataSize,
                srcRasterFormat, dstRasterFormat,
                srcItemDepth, srcRowAlignment, dstItemDepth, swizzledRowAlignment,
                srcColorOrder, dstColorOrder,
                fixAlpha
            );
        }
        else
        {
            // Maybe we need to fix the indice (if the texture comes from PC or XBOX architecture).
            ConvertPaletteDepth(
                srcTexelData, dstLinearTexelData,
                mipWidth, mipHeight,
                srcPaletteType, dstPaletteType, paletteSize,
                srcItemDepth, dstItemDepth,
                srcRowAlignment, swizzledRowAlignment
            );
        }

        // Perform swizzling.
        if ( linearMipmapInternalFormat != swizzleMipmapRequiredEncoding )
        {
            dstSwizzledTexelData =
                ps2GSPixelEncodingFormats::transformImageData(
                    engineInterface,
                    linearMipmapInternalFormat, swizzleMipmapRequiredEncoding,
                    dstLinearTexelData,
                    mipWidth, mipHeight,
                    swizzledRowAlignment, swizzledRowAlignment,
                    packedWidth, packedHeight,
                    dstSwizzledDataSize,
                    false
                );

            if ( dstSwizzledTexelData == nullptr )
            {
                // The probability of this failing is medium.
                throw RwException( "failed to swizzle texture" );
            }
        }
        else
        {
            // Just get the encoding dimensions manually.
            bool hasDimensions = ps2GSPixelEncodingFormats::getPackedFormatDimensions(
                linearMipmapInternalFormat, swizzleMipmapRequiredEncoding,
                mipWidth, mipHeight,
                packedWidth, packedHeight
            );

            if ( !hasDimensions )
            {
                throw RwException( "failed to get PS2 swizzle format dimensions for linear encoding" );
            }

            // We have to make sure that we extend the texture dimensions properly!
            // The texture data _must_ be in memory layout, after all!
            if ( mipWidth != packedWidth || mipHeight != packedHeight )
            {
                uint32 dstSwizzledRowSize = getPS2RasterDataRowSize( packedWidth, dstItemDepth );

                dstSwizzledDataSize = getRasterDataSizeByRowSize( dstSwizzledRowSize, packedHeight );

                dstSwizzledTexelData = TruncateMipmapLayerPS2(
                    engineInterface,
                    dstLinearTexelData,
                    mipWidth, mipHeight, dstItemDepth, swizzledRowAlignment,
                    packedWidth, packedHeight, swizzledRowAlignment,
                    dstSwizzledDataSize
                );
            }
            else
            {
                // We are properly sized and optimized, so just take us.
                dstSwizzledTexelData = dstLinearTexelData;
                dstSwizzledDataSize = dstLinearDataSize;
            }
        }

        // Free temporary unswizzled texels.
        if ( dstSwizzledTexelData != dstLinearTexelData )
        {
            engineInterface->PixelFree( dstLinearTexelData );
        }
    }
    catch( ... )
    {
        engineInterface->PixelFree( dstLinearTexelData );

        if ( dstSwizzledTexelData && dstSwizzledTexelData != dstLinearTexelData )
        {
            engineInterface->PixelFree( dstSwizzledTexelData );
        }

        throw;
    }

    // Give results to the runtime.
    swizzleWidthOut = packedWidth;
    swizzleHeightOut = packedHeight;

    dstSwizzledTexelsOut = dstSwizzledTexelData;
    dstSwizzledDataSizeOut = dstSwizzledDataSize;
}

void ps2NativeTextureTypeProvider::SetPixelDataToTexture( Interface *engineInterface, void *objMem, const pixelDataTraversal& pixelsIn, acquireFeedback_t& feedbackOut )
{
    // Cast to our native format.
    NativeTexturePS2 *ps2tex = (NativeTexturePS2*)objMem;

    // Verify mipmap dimensions.
    {
        nativeTextureSizeRules sizeRules;
        getPS2NativeTextureSizeRules( sizeRules );

        if ( !sizeRules.verifyPixelData( pixelsIn ) )
        {
            throw RwException( "invalid mipmap dimension in PS2 native texture pixel acquisition" );
        }
    }

    LibraryVersion currentVersion = ps2tex->texVersion;

    // Make sure that we got uncompressed bitmap data.
    assert( pixelsIn.compressionType == RWCOMPRESS_NONE );

    // The maximum amount of mipmaps supported by PS2 textures.
    const size_t maxMipmaps = 7;

    {
        // The PlayStation 2 does NOT support all raster formats.
        // We need to avoid giving it raster formats that are prone to crashes, like RASTER_888.
        eRasterFormat srcRasterFormat = pixelsIn.rasterFormat;
        uint32 srcItemDepth = pixelsIn.depth;
        uint32 srcRowAlignment = pixelsIn.rowAlignment;

        eRasterFormat targetRasterFormat = srcRasterFormat;
        uint32 dstItemDepth = srcItemDepth;

        ePaletteType paletteType = pixelsIn.paletteType;
        uint32 paletteSize = pixelsIn.paletteSize;

        ePaletteType dstPaletteType = paletteType;

        if (targetRasterFormat == RASTER_888)
        {
            // Since this raster takes the same memory space as RASTER_8888, we can silently convert it.
            targetRasterFormat = RASTER_8888;
        }
        else if (targetRasterFormat != RASTER_1555)
        {
            // We need to change the format of the texture, as we do not support it.
            targetRasterFormat = RASTER_8888;
        }

        if (dstPaletteType != PALETTE_NONE)
        {
            // Make sure we are a known palette mapping.
            if (dstPaletteType == PALETTE_4BIT_LSB)
            {
                dstPaletteType = PALETTE_4BIT;
            }
            else if (dstPaletteType != PALETTE_4BIT && dstPaletteType != PALETTE_8BIT)
            {
                dstPaletteType = PALETTE_8BIT;
            }

            // The architecture does not support 8bit PALETTE_4BIT rasters.
            // Fix that.
            if (dstPaletteType == PALETTE_4BIT)
            {
                dstItemDepth = 4;
            }
            else if (dstPaletteType == PALETTE_8BIT)
            {
                dstItemDepth = 8;
            }
        }

        uint32 targetRasterDepth = Bitmap::getRasterFormatDepth(targetRasterFormat);

        if (dstPaletteType == PALETTE_NONE)
        {
            dstItemDepth = targetRasterDepth;
        }

        // Set the palette type.
        ps2tex->paletteType = dstPaletteType;

        // Finally, set the raster format.
        ps2tex->rasterFormat = targetRasterFormat;

        // Prepare mipmap data.
        eColorOrdering d3dColorOrder = pixelsIn.colorOrder;
        eColorOrdering ps2ColorOrder = ps2tex->colorOrdering;

        // Prepare swizzling parameters.
        eFormatEncodingType linearMipmapInternalFormat =
            getFormatEncodingFromRasterFormat(targetRasterFormat, dstPaletteType);

        assert(linearMipmapInternalFormat != FORMAT_UNKNOWN);

        // Get the format we need to encode mipmaps in.
        eFormatEncodingType swizzleMipmapRequiredEncoding = ps2tex->getHardwareRequiredEncoding(currentVersion);

        assert(swizzleMipmapRequiredEncoding != FORMAT_UNKNOWN);

        size_t mipmapCount = pixelsIn.mipmaps.GetCount();
        {
            size_t mipProcessCount = std::min( maxMipmaps, mipmapCount );

            ps2tex->mipmaps.Resize( mipProcessCount );

            for ( size_t n = 0; n < mipProcessCount; n++ )
            {
                // Process every mipmap individually.
                NativeTexturePS2::GSMipmap& newMipmap = ps2tex->mipmaps[ n ];

                const pixelDataTraversal::mipmapResource& oldMipmap = pixelsIn.mipmaps[ n ];

                uint32 layerWidth = oldMipmap.width;
                uint32 layerHeight = oldMipmap.height;
                uint32 srcDataSize = oldMipmap.dataSize;

                const void *srcTexelData = oldMipmap.texels;

                // Convert the mipmap and fetch new parameters.
                uint32 packedWidth, packedHeight;

                void *dstSwizzledTexelData;
                uint32 dstSwizzledDataSize;

                ConvertMipmapToPS2Format(
                    engineInterface,
                    layerWidth, layerHeight, srcTexelData, srcDataSize,
                    linearMipmapInternalFormat, swizzleMipmapRequiredEncoding,
                    srcRasterFormat, srcItemDepth, d3dColorOrder,
                    targetRasterFormat, dstItemDepth, ps2ColorOrder,
                    paletteType, dstPaletteType, paletteSize,
                    srcRowAlignment,
                    packedWidth, packedHeight,
                    dstSwizzledTexelData, dstSwizzledDataSize
                );

                // Store the new mipmap.
                newMipmap.width = layerWidth;
                newMipmap.height = layerHeight;

                newMipmap.swizzleWidth = packedWidth;
                newMipmap.swizzleHeight = packedHeight;

                newMipmap.texels = dstSwizzledTexelData;
                newMipmap.dataSize = dstSwizzledDataSize;
            }
        }

        // We are now properly encoded.
        ps2tex->swizzleEncodingType = swizzleMipmapRequiredEncoding;

        // Copy over general attributes.
        ps2tex->depth = dstItemDepth;

        // Make sure we apply autoMipmap property just like the R* converter.
        bool hasAutoMipmaps = pixelsIn.autoMipmaps;

        if ( mipmapCount > 1 )
        {
            hasAutoMipmaps = true;
        }

        ps2tex->autoMipmaps = hasAutoMipmaps;
        ps2tex->rasterType = pixelsIn.rasterType;

        //ps2tex->hasAlpha = pixelsIn.hasAlpha;     the PlayStation 2 is said to have "free" alpha blending.

        // Move over the palette texels.
        if (dstPaletteType != PALETTE_NONE)
        {
            eFormatEncodingType clutRequiredEncoding = getFormatEncodingFromRasterFormat(targetRasterFormat, PALETTE_NONE);

            // Prepare the palette texels.
            const void *srcPalTexelData = pixelsIn.paletteData;

            uint32 srcPalFormatDepth = Bitmap::getRasterFormatDepth( srcRasterFormat );
            uint32 targetPalFormatDepth = targetRasterDepth;

            // Swizzle the CLUT.
            uint32 palWidth, palHeight;
            getPaletteTextureDimensions( dstPaletteType, currentVersion, palWidth, palHeight );

            void *clutSwizzledTexels = nullptr;
            uint32 newPalDataSize = 0;

            GeneratePS2CLUT(
                engineInterface, palWidth, palHeight,
                srcPalTexelData, dstPaletteType, paletteSize, clutRequiredEncoding,
                srcRasterFormat, srcPalFormatDepth, d3dColorOrder,
                targetRasterFormat, targetPalFormatDepth, ps2ColorOrder,
                clutSwizzledTexels, newPalDataSize
            );

            // Store the palette texture.
            NativeTexturePS2::GSTexture& palTex = ps2tex->paletteTex;

            palTex.swizzleWidth = palWidth;
            palTex.swizzleHeight = palHeight;
            palTex.dataSize = newPalDataSize;

            ps2tex->paletteSwizzleEncodingType = clutRequiredEncoding;

            palTex.texels = clutSwizzledTexels;
        }
    }

    // TODO: improve exception safety.

    // Generate valid gsParams for this texture, as we lost our original ones.
    ps2tex->getOptimalGSParameters(ps2tex->gsParams);

    // We do not take the pixels directly, because we need to decode them.
    feedbackOut.hasDirectlyAcquired = false;
}

void ps2NativeTextureTypeProvider::UnsetPixelDataFromTexture( Interface *engineInterface, void *objMem, bool deallocate )
{
    NativeTexturePS2 *nativeTex = (NativeTexturePS2*)objMem;

    if ( deallocate )
    {
        size_t mipmapCount = nativeTex->mipmaps.GetCount();

        // Free all mipmaps.
        for ( size_t n = 0; n < mipmapCount; n++ )
        {
            NativeTexturePS2::GSMipmap& mipLayer = nativeTex->mipmaps[ n ];

            mipLayer.FreeTexels( engineInterface );
        }

        // Free the palette texture.
        nativeTex->paletteTex.FreeTexels( engineInterface );
    }

    // Clear our mipmaps.
    nativeTex->mipmaps.Clear();

    // Clear the palette texture.
    nativeTex->paletteTex.DetachTexels();

    // For debugging purposes, reset the texture raster information.
    nativeTex->rasterFormat = RASTER_DEFAULT;
    nativeTex->depth = 0;
    nativeTex->paletteType = PALETTE_NONE;
    nativeTex->recommendedBufferBasePointer = 0;
    nativeTex->swizzleEncodingType = FORMAT_UNKNOWN;
    nativeTex->paletteSwizzleEncodingType = FORMAT_UNKNOWN;
    nativeTex->autoMipmaps = false;
    nativeTex->rasterType = 4;
    nativeTex->colorOrdering = COLOR_RGBA;
}

template <bool isConst>
struct ps2MipmapManager
{
    typedef typename std::conditional <isConst, const NativeTexturePS2, NativeTexturePS2>::type nativeType;

    nativeType *nativeTex;

    inline ps2MipmapManager( nativeType *nativeTex )
    {
        this->nativeTex = nativeTex;
    }

    inline void GetLayerDimensions(
        const NativeTexturePS2::GSMipmap& mipLayer,
        uint32& layerWidth, uint32& layerHeight
    )
    {
        layerWidth = mipLayer.width;
        layerHeight = mipLayer.height;
    }

    inline void GetSizeRules( nativeTextureSizeRules& rulesOut ) const
    {
        getPS2NativeTextureSizeRules( rulesOut );
    }

    inline void Deinternalize(
        Interface *engineInterface,
        const NativeTexturePS2::GSMipmap& mipLayer,
        uint32& widthOut, uint32& heightOut, uint32& layerWidthOut, uint32& layerHeightOut,
        eRasterFormat& dstRasterFormat, eColorOrdering& dstColorOrder, uint32& dstDepth,
        uint32& dstRowAlignment,
        ePaletteType& dstPaletteType, void*& dstPaletteData, uint32& dstPaletteSize,
        eCompressionType& dstCompressionType, bool& hasAlpha,
        void*& dstTexelsOut, uint32& dstDataSizeOut,
        bool& isNewlyAllocatedOut, bool& isPaletteNewlyAllocatedOut
    )
    {
        // We need to decode our mipmap layer.
        uint32 layerWidth = mipLayer.width;
        uint32 layerHeight = mipLayer.height;

        void *srcTexels = mipLayer.texels;
        uint32 dataSize = mipLayer.dataSize;

        eRasterFormat srcRasterFormat = nativeTex->rasterFormat;
        uint32 srcDepth = nativeTex->depth;
        eColorOrdering srcColorOrder = nativeTex->colorOrdering;

        ePaletteType srcPaletteType = nativeTex->paletteType;

        // Get the decoded palette data.
        void *decodedPaletteData = nullptr;
        uint32 decodedPaletteSize = 0;

        if (srcPaletteType != PALETTE_NONE)
        {
            // We need to decode the PS2 CLUT.
            GetPS2TexturePalette(
                engineInterface,
                nativeTex->paletteTex.swizzleWidth, nativeTex->paletteTex.swizzleHeight, nativeTex->paletteSwizzleEncodingType, nativeTex->paletteTex.texels,
                srcRasterFormat, srcColorOrder,
                srcRasterFormat, srcColorOrder,
                srcPaletteType,
                decodedPaletteData, decodedPaletteSize
            );
        }

        // Process the mipmap texels.
        eFormatEncodingType mipmapSwizzleEncodingType = nativeTex->swizzleEncodingType;

        eFormatEncodingType mipmapDecodeFormat = getFormatEncodingFromRasterFormat(srcRasterFormat, srcPaletteType);

        // Make sure there is no unknown format.
        assert( mipmapSwizzleEncodingType != FORMAT_UNKNOWN && mipmapDecodeFormat != FORMAT_UNKNOWN );

        // Get the unswizzled texel data.
        void *dstTexels = nullptr;
        uint32 dstDataSize = 0;

        GetPS2TextureTranscodedMipmapData(
            engineInterface,
            layerWidth, layerHeight, mipLayer.swizzleWidth, mipLayer.swizzleHeight, srcTexels, dataSize,
            mipmapSwizzleEncodingType, mipmapDecodeFormat,
            srcRasterFormat, srcDepth, srcColorOrder,
            srcRasterFormat, srcDepth, srcColorOrder,
            srcPaletteType, decodedPaletteSize,
            dstTexels, dstDataSize
        );

        // Return parameters to the runtime.
        widthOut = layerWidth;
        heightOut = layerHeight;

        layerWidthOut = layerWidth;
        layerHeightOut = layerHeight;

        dstRasterFormat = srcRasterFormat;
        dstDepth = srcDepth;
        dstRowAlignment = getPS2ExportTextureDataRowAlignment();
        dstColorOrder = srcColorOrder;

        dstPaletteType = srcPaletteType;
        dstPaletteData = decodedPaletteData;
        dstPaletteSize = decodedPaletteSize;

        dstCompressionType = RWCOMPRESS_NONE;

        // Since the PS2 native texture does not care about the alpha status,
        // we have to always calculate this field, because the virtual framework _does_ care.
        hasAlpha =
            rawMipmapCalculateHasAlpha(
                engineInterface,
                layerWidth, layerHeight, dstTexels, dstDataSize,
                srcRasterFormat, srcDepth, getPS2ExportTextureDataRowAlignment(), srcColorOrder,
                srcPaletteType, decodedPaletteData, decodedPaletteSize
            );

        dstTexelsOut = dstTexels;
        dstDataSizeOut = dstDataSize;

        // We have newly allocated both texels and palette data.
        isNewlyAllocatedOut = true;
        isPaletteNewlyAllocatedOut = true;
    }

    inline void Internalize(
        Interface *engineInterface,
        NativeTexturePS2::GSMipmap& mipLayer,
        uint32 width, uint32 height, uint32 layerWidth, uint32 layerHeight, void *srcTexels, uint32 dataSize,
        eRasterFormat rasterFormat, eColorOrdering colorOrder, uint32 depth,
        uint32 rowAlignment,
        ePaletteType paletteType, void *paletteData, uint32 paletteSize,
        eCompressionType compressionType, bool hasAlpha,
        bool& hasDirectlyAcquiredOut
    )
    {
        // Check whether we have reached the maximum mipmap count.
        const uint32 maxMipmaps = 7;

        if ( nativeTex->mipmaps.GetCount() >= maxMipmaps )
        {
            throw RwException( "cannot add mipmap in PS2 texture because too many" );
        }

        LibraryVersion currentVersion = nativeTex->texVersion;

        // Get the texture properties on the stack.
        eRasterFormat texRasterFormat = nativeTex->rasterFormat;
        uint32 texDepth = nativeTex->depth;
        eColorOrdering texColorOrder = nativeTex->colorOrdering;

        ePaletteType texPaletteType = nativeTex->paletteType;

        // If we are a palette texture, decode our palette for remapping.
        void *texPaletteData = nullptr;
        uint32 texPaletteSize = 0;

        if ( texPaletteType != PALETTE_NONE )
        {
            // We need to decode the PS2 CLUT.
            GetPS2TexturePalette(
                engineInterface,
                nativeTex->paletteTex.swizzleWidth, nativeTex->paletteTex.swizzleHeight, nativeTex->paletteSwizzleEncodingType, nativeTex->paletteTex.texels,
                texRasterFormat, texColorOrder,
                texRasterFormat, texColorOrder,
                texPaletteType,
                texPaletteData, texPaletteSize
            );
        }

        // Convert the input data to our texture's format.
        bool srcTexelsNewlyAllocated = false;

        bool hasConverted =
            ConvertMipmapLayerNative(
                engineInterface,
                width, height, layerWidth, layerHeight, srcTexels, dataSize,
                rasterFormat, depth, rowAlignment, colorOrder, paletteType, paletteData, paletteSize, compressionType,
                texRasterFormat, texDepth, rowAlignment, texColorOrder, texPaletteType, texPaletteData, texPaletteSize, RWCOMPRESS_NONE,
                false,
                width, height,
                srcTexels, dataSize
            );

        if ( hasConverted )
        {
            srcTexelsNewlyAllocated = true;
        }

        // We do not need the CLUT anymore, if we allocated it.
        if ( texPaletteData )
        {
            engineInterface->PixelFree( texPaletteData );
        }

        // Prepare swizzling parameters.
        eFormatEncodingType linearMipmapInternalFormat =
            getFormatEncodingFromRasterFormat(texRasterFormat, texPaletteType);

        assert(linearMipmapInternalFormat != FORMAT_UNKNOWN);

        // Get the format we need to encode mipmaps in.
        eFormatEncodingType swizzleMipmapRequiredEncoding = nativeTex->getHardwareRequiredEncoding(currentVersion);

        assert(swizzleMipmapRequiredEncoding != FORMAT_UNKNOWN);

        // Now we have to encode our texels.
        void *dstSwizzledTexels = nullptr;
        uint32 dstSwizzledDataSize = 0;

        uint32 packedWidth, packedHeight;

        ConvertMipmapToPS2Format(
            engineInterface,
            layerWidth, layerHeight, srcTexels, dataSize,
            linearMipmapInternalFormat, swizzleMipmapRequiredEncoding,
            texRasterFormat, texDepth, texColorOrder,
            texRasterFormat, texDepth, texColorOrder,
            texPaletteType, texPaletteType, texPaletteSize,
            rowAlignment,
            packedWidth, packedHeight,
            dstSwizzledTexels, dstSwizzledDataSize
        );

        // Free the linear data.
        if ( srcTexelsNewlyAllocated )
        {
            engineInterface->PixelFree( srcTexels );
        }

        // Store the encoded texels.
        mipLayer.width = layerWidth;
        mipLayer.height = layerHeight;

        mipLayer.swizzleWidth = packedWidth;
        mipLayer.swizzleHeight = packedHeight;

        mipLayer.texels = dstSwizzledTexels;
        mipLayer.dataSize = dstSwizzledDataSize;

        // Since we encoded the texels, we cannot ever directly acquire them.
        hasDirectlyAcquiredOut = false;
    }
};

bool ps2NativeTextureTypeProvider::GetMipmapLayer( Interface *engineInterface, void *objMem, uint32 mipIndex, rawMipmapLayer& layerOut )
{
    NativeTexturePS2 *nativeTex = (NativeTexturePS2*)objMem;

    ps2MipmapManager <false> mipMan( nativeTex );

    return
        virtualGetMipmapLayer <NativeTexturePS2::GSMipmap> (
            engineInterface, mipMan,
            mipIndex,
            nativeTex->mipmaps, layerOut
        );
}

bool ps2NativeTextureTypeProvider::AddMipmapLayer( Interface *engineInterface, void *objMem, const rawMipmapLayer& layerIn, acquireFeedback_t& feedbackOut )
{
    NativeTexturePS2 *nativeTex = (NativeTexturePS2*)objMem;

    ps2MipmapManager <false> mipMan( nativeTex );

    return
        virtualAddMipmapLayer <NativeTexturePS2::GSMipmap> (
            engineInterface, mipMan,
            nativeTex->mipmaps,
            layerIn, feedbackOut
        );
}

void ps2NativeTextureTypeProvider::ClearMipmaps( Interface *engineInterface, void *objMem )
{
    NativeTexturePS2 *nativeTex = (NativeTexturePS2*)objMem;

    return
        virtualClearMipmaps <NativeTexturePS2::GSMipmap> ( engineInterface, nativeTex->mipmaps );
}

bool ps2NativeTextureTypeProvider::DoesTextureHaveAlpha( const void *objMem )
{
    const NativeTexturePS2 *nativeTex = (const NativeTexturePS2*)objMem;

    Interface *engineInterface = nativeTex->engineInterface;

    // The PS2 native texture does not store the alpha status, because it uses alpha blending all the time.
    // Hence we have to calculate the alpha flag if the framework wants it.
    // This is an expensive operation, actually, because we have to decode the texture.

    // Let's just use the methods we already wrote.
    ps2MipmapManager <true> mipMan( nativeTex );

    rw::rawMipmapLayer rawLayer;

    bool gotLayer = virtualGetMipmapLayer <NativeTexturePS2::GSMipmap> (
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

void ps2NativeTextureTypeProvider::GetTextureInfo( Interface *engineInterface, void *objMem, nativeTextureBatchedInfo& infoOut )
{
    NativeTexturePS2 *nativeTex = (NativeTexturePS2*)objMem;

    size_t mipmapCount = nativeTex->mipmaps.GetCount();

    infoOut.mipmapCount = (uint32)mipmapCount;

    uint32 baseWidth = 0;
    uint32 baseHeight = 0;

    if ( mipmapCount > 0 )
    {
        baseWidth = nativeTex->mipmaps[ 0 ].width;
        baseHeight = nativeTex->mipmaps[ 0 ].height;
    }

    infoOut.baseWidth = baseWidth;
    infoOut.baseHeight = baseHeight;
}

void ps2NativeTextureTypeProvider::GetTextureFormatString( Interface *engineInterface, void *objMem, char *buf, size_t bufLen, size_t& lengthOut ) const
{
    NativeTexturePS2 *nativeTex = (NativeTexturePS2*)objMem;

    // We are just a standard raster.
    // The PS2 specific encoding does not matter.
    rwStaticString <char> formatString = "PS2 ";

    getDefaultRasterFormatString( nativeTex->rasterFormat, nativeTex->depth, nativeTex->paletteType, nativeTex->colorOrdering, formatString );

    if ( buf )
    {
        strncpy( buf, formatString.GetConstString(), bufLen );
    }

    lengthOut = formatString.GetLength();
}

};

#endif //RWLIB_INCLUDE_NATIVETEX_PLAYSTATION2
