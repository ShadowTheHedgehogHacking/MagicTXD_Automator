#include "StdInc.h"

#ifdef RWLIB_INCLUDE_NATIVETEX_PSP

#include "txdread.psp.hxx"

#include "txdread.psp.mem.hxx"

#include "pluginutil.hxx"

namespace rw
{

eTexNativeCompatibility pspNativeTextureTypeProvider::IsCompatibleTextureBlock( BlockProvider& inputProvider ) const
{
    eTexNativeCompatibility compatOut = RWTEXCOMPAT_NONE;

    // We just check the meta block.
    {
        BlockProvider metaBlock( &inputProvider );

        metaBlock.EnterContext();

        try
        {
            if ( metaBlock.getBlockID() == CHUNK_STRUCT )
            {
                // Check checksum.
                uint32 checksum = metaBlock.readUInt32();
                
                if ( checksum == PSP_FOURCC )
                {
                    // Only the PSP native texture could have the PSP checksum.
                    compatOut = RWTEXCOMPAT_ABSOLUTE;
                }
            }
        }
        catch( ... )
        {
            metaBlock.LeaveContext();

            throw;
        }

        metaBlock.LeaveContext();
    }

    return compatOut;
}

void pspNativeTextureTypeProvider::DeserializeTexture( TextureBase *theTexture, PlatformTexture *nativeTex, BlockProvider& outputProvider ) const
{
    Interface *engineInterface = theTexture->engineInterface;

    // First deserialize the top meta block.
    {
        BlockProvider metaBlock( &outputProvider );

        metaBlock.EnterContext();

        try
        {
            // First field is the checksum.
            uint32 checksum = metaBlock.readUInt32();

            if ( checksum != PSP_FOURCC )
            {
                throw RwException( "invalid checksum for PSP native texture" );
            }

            // Just like the PS2 native texture there was supposed to be the filtering mode settings here.
            // Unfortunately, it never made it into production?
            texFormatInfo formatInfo;
            formatInfo.readFromBlock( metaBlock );

            formatInfo.parse( *theTexture, true );
        }
        catch( ... )
        {
            metaBlock.LeaveContext();

            throw;
        }

        metaBlock.LeaveContext();
    }

    // Now comes the texture name...
    {
        rwStaticString <char> texName;

        utils::readStringChunkANSI( engineInterface, outputProvider, texName );

        theTexture->SetName( texName.GetConstString() );
    }
    // ... and alpha mask name.
    {
        rwStaticString <char> maskName;

        utils::readStringChunkANSI( engineInterface, outputProvider, maskName );

        theTexture->SetMaskName( maskName.GetConstString() );
    }

    // Create access to the PSP native texture.
    NativeTexturePSP *pspTex = (NativeTexturePSP*)nativeTex;

    // Now comes the graphical data master block.
    // It contains the main image data.
    {
        BlockProvider colorMainBlock( &outputProvider );

        colorMainBlock.EnterContext();

        try
        {
            // We need meta information about the graphical data to-be-processed.
            uint32 baseWidth, baseHeight;
            uint32 depth;
            uint32 mipmapCount;
            {
                BlockProvider imageMetaBlock( &colorMainBlock );

                imageMetaBlock.EnterContext();

                try
                {
                    psp::textureMetaDataHeader metaInfo;
                    imageMetaBlock.readStruct( metaInfo );

                    baseWidth = metaInfo.width;
                    baseHeight = metaInfo.height;
                    depth = metaInfo.depth;
                    mipmapCount = metaInfo.mipmapCount;

                    uint32 unknown = metaInfo.unknown;

                    if ( unknown != 0 )
                    {
                        engineInterface->PushWarning( "unknown field non-zero in PSP native texture" );
                    }

                    pspTex->unk = unknown;
                }
                catch( ... )
                {
                    imageMetaBlock.LeaveContext();

                    throw;
                }

                imageMetaBlock.LeaveContext();
            }

            // Not all depths are supported.
            if ( depth != 4 && depth != 8 && depth != 16 && depth != 32 )
            {
                throw RwException( "unknown PSP native texture depth" );
            }

            // Determine some parameters based on the meta info.
            ePaletteType paletteType;
            eColorOrdering colorOrder;

            eRasterFormat rasterFormat = decodeDepthRasterFormat( depth, colorOrder, paletteType );

            if ( rasterFormat == RASTER_DEFAULT )
            {
                throw RwException( "unknown raster format for PSP native texture" );
            }

            // This native texture format is pretty dumbed down to what the PSP can actually support I think.
            eFormatEncodingType encodingType = FORMAT_UNKNOWN;
            {
                encodingType = getPSPHardwareColorBufferFormat( depth );

                if ( encodingType == FORMAT_UNKNOWN )
                {
                    throw RwException( "unknown PSP hardware color buffer format in PSP native texture deserialization" );
                }
            }

            pspTex->depth = depth;
            pspTex->colorBufferFormat = encodingType;

            // GPU Data :-)
            {
                BlockProvider gpuDataBlock( &colorMainBlock );

                gpuDataBlock.EnterContext();

                try
                {
                    // There are no more GIF packets, I guess.
                    // So things are not complicated.

                    // Read the image data of all mipmaps first.
                    uint32 mip_index = 0;

                    mipGenLevelGenerator mipGen( baseWidth, baseHeight );

                    if ( !mipGen.isValidLevel() )
                    {
                        throw RwException( "invalid texture dimensions for PSP native texture" );
                    }

                    while ( mip_index < mipmapCount )
                    {
                        // Advance mipmap dimensions.
                        bool establishedLevel = true;

                        if ( mip_index != 0 )
                        {
                            establishedLevel = mipGen.incrementLevel();
                        }

                        if ( !establishedLevel )
                        {
                            // We abort if the mipmap generation cannot determine any more valid dimensions.
                            break;
                        }

                        uint32 layerWidth = mipGen.getLevelWidth();
                        uint32 layerHeight = mipGen.getLevelHeight();

                        // The PSP native texture has broken color buffer storage for swizzled formats
                        // because the actual memory space required for PSMCT32 is not honored when writing
                        // the texture data to disk.
                        // Unfortunately we have to keep the broken behavior.

                        // For a matter of fact, we do not handle packed dimensions ever.
                        // We just deal with the raw dimensions, which is broken!
                        uint32 mipRowSize = getPSPRasterDataRowSize( layerWidth, depth );

                        uint32 mipDataSize = getRasterDataSizeByRowSize( mipRowSize, layerHeight );

                        gpuDataBlock.check_read_ahead( mipDataSize );

                        void *texels = engineInterface->PixelAllocate( mipDataSize );

                        if ( !texels )
                        {
                            throw RwException( "failed to allocate sufficient memory for PSP native texture data" );
                        }

                        try
                        {
                            gpuDataBlock.read( texels, mipDataSize );
                        }
                        catch( ... )
                        {
                            engineInterface->PixelFree( texels );

                            throw;
                        }

                        // Store this new layer.
                        NativeTexturePSP::GETexture newLayer;
                        newLayer.width = layerWidth;
                        newLayer.height = layerHeight;
                        newLayer.texels = texels;
                        newLayer.dataSize = mipDataSize;

                        // Store swizzling property.
                        {
                            bool isMipSwizzled = isPSPSwizzlingRequired( layerWidth, layerHeight, depth );

                            newLayer.isSwizzled = isMipSwizzled;
                        }

                        pspTex->mipmaps.AddToBack( std::move( newLayer ) );

                        mip_index++;
                    }

                    if ( mip_index == 0 )
                    {
                        throw RwException( "empty texture" );
                    }

                    // After the mipmap data, comes the palette, so let's read it!
                    void *paletteData = nullptr;
                    uint32 paletteSize = 0;

                    if ( paletteType != PALETTE_NONE )
                    {
                        paletteSize = getPaletteItemCount( paletteType );

                        // The PSP native texture does only support 32bit RGBA palette entries (RASTER_8888 RGBA).
                        uint32 palRasterDepth = Bitmap::getRasterFormatDepth( rasterFormat );

                        uint32 palDataSize = getPaletteDataSize( paletteSize, palRasterDepth );

                        gpuDataBlock.check_read_ahead( palDataSize );

                        paletteData = engineInterface->PixelAllocate( palDataSize );

                        if ( !paletteData )
                        {
                            throw RwException( "failed to allocate palette data for PSP native texture" );
                        }

                        try
                        {
                            gpuDataBlock.read( paletteData, palDataSize );
                        }
                        catch( ... )
                        {
                            engineInterface->PixelFree( paletteData );

                            throw;
                        }
                    }

                    // Store the palette information into the texture.
                    pspTex->palette = paletteData;

                    // Fix the filtering settings.
                    fixFilteringMode( *theTexture, mip_index );
                    
                    // Sometimes there is strange padding added to the GPU data block.
                    // We want to skip that and warn the user.
                    {
                        int64 currentBlockSeek = gpuDataBlock.tell();

                        int64 leftToEnd = gpuDataBlock.getBlockLength() - currentBlockSeek;

                        if ( leftToEnd > 0 )
                        {
                            if ( engineInterface->GetWarningLevel() >= 3 )
                            {
                                engineInterface->PushWarning( "skipped meta-data at the end of PSP native texture GPU data block" );
                            }

                            gpuDataBlock.skip( (size_t)leftToEnd );
                        }
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

    // Read extension info.
    engineInterface->DeserializeExtensions( theTexture, outputProvider );
}

static PluginDependantStructRegister <pspNativeTextureTypeProvider, RwInterfaceFactory_t> pspNativeTextureTypeRegister;

void registerPSPNativeTextureType( void )
{
    pspNativeTextureTypeRegister.RegisterPlugin( engineFactory );
}

};

#endif //RWLIB_INCLUDE_NATIVETEX_PSP