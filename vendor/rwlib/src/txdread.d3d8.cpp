#include "StdInc.h"

#ifdef RWLIB_INCLUDE_NATIVETEX_D3D8

#include "txdread.d3d8.hxx"

#include "pixelformat.hxx"

#include "txdread.d3d.dxt.hxx"

#include "pluginutil.hxx"

namespace rw
{

void d3d8NativeTextureTypeProvider::DeserializeTexture( TextureBase *theTexture, PlatformTexture *nativeTex, BlockProvider& inputProvider ) const
{
    Interface *engineInterface = theTexture->engineInterface;

    {
        BlockProvider texNativeImageStruct( &inputProvider );

        texNativeImageStruct.EnterContext();

        try
        {
            if ( texNativeImageStruct.getBlockID() == CHUNK_STRUCT )
            {
                d3d8::textureMetaHeaderStructGeneric metaHeader;
                texNativeImageStruct.read( &metaHeader, sizeof(metaHeader) );

	            uint32 platform = metaHeader.platformDescriptor;

	            if (platform != PLATFORM_D3D8)
                {
                    throw RwException( "invalid platform type in Direct3D 8 texture reading" );
                }

                // Recast the texture to our native type.
                NativeTextureD3D8 *platformTex = (NativeTextureD3D8*)nativeTex;

                //int engineWarningLevel = engineInterface->GetWarningLevel();

                bool engineIgnoreSecureWarnings = engineInterface->GetIgnoreSecureWarnings();

                // Read the texture names.
                {
                    char tmpbuf[ sizeof( metaHeader.name ) + 1 ];

                    // Make sure the name buffer is zero terminted.
                    tmpbuf[ sizeof( metaHeader.name ) ] = '\0';

                    // Move over the texture name.
                    memcpy( tmpbuf, metaHeader.name, sizeof( metaHeader.name ) );

                    theTexture->SetName( tmpbuf );

                    // Move over the texture mask name.
                    memcpy( tmpbuf, metaHeader.maskName, sizeof( metaHeader.maskName ) );

                    theTexture->SetMaskName( tmpbuf );
                }

                // Read texture format.
                texFormatInfo formatInfo = metaHeader.texFormat;

                formatInfo.parse( *theTexture );

                // Deconstruct the format flags.
                bool hasMipmaps = false;    // TODO: actually use this flag?

                readRasterFormatFlags( metaHeader.rasterFormat, platformTex->rasterFormat, platformTex->paletteType, hasMipmaps, platformTex->autoMipmaps );

	            platformTex->hasAlpha = ( metaHeader.hasAlpha != 0 ? true : false );

                uint32 depth = metaHeader.depth;
                uint32 maybeMipmapCount = metaHeader.mipmapCount;

                eRasterFormat rasterFormat = platformTex->rasterFormat;

                platformTex->rasterType = metaHeader.rasterType;

                // Decide about the color order.
                ePaletteType paletteType = platformTex->paletteType;
                {
                    eColorOrdering colorOrder = COLOR_BGRA;

                    if ( paletteType != PALETTE_NONE )
                    {
                        colorOrder = COLOR_RGBA;
                    }

                    platformTex->colorOrdering = colorOrder;
                }

                // Read compression information.
                uint32 dxtCompression = metaHeader.dxtCompression;
                {
                    platformTex->dxtCompression = dxtCompression;

                    // If we are compressed, it must be a compression we know about.
                    if ( dxtCompression != 0 )
                    {
                        if ( dxtCompression != 1 &&
                             dxtCompression != 2 && dxtCompression != 3 &&
                             dxtCompression != 4 && dxtCompression != 5 )
                        {
                            throw RwException( "invalid Direct3D texture compression format" );
                        }
                    }
                }

                // Verify raster properties and attempt to fix broken textures.
                // Broken textures travel with mods like San Andreas Retextured.
                // - Verify depth.
                {
                    bool hasInvalidDepth = false;

                    bool hasProcessedDepth = false;

                    if (paletteType == PALETTE_4BIT)
                    {
                        if (depth != 4 && depth != 8)
                        {
                            hasInvalidDepth = true;
                        }

                        hasProcessedDepth = true;
                    }
                    else if (paletteType == PALETTE_8BIT)
                    {
                        if (depth != 8)
                        {
                            hasInvalidDepth = true;
                        }

                        hasProcessedDepth = true;
                    }

                    if (hasInvalidDepth == true)
                    {
                        throw RwException( "texture " + theTexture->GetName() + " has an invalid depth" );

                        // We cannot fix an invalid depth.
                    }

                    (void)hasProcessedDepth;

                    // Note: this texture just takes the depth as it is, for now.
                }

                // Now that depth is verified, we can remember it.
                platformTex->depth = depth;

                if (paletteType != PALETTE_NONE)
                {
                    uint32 reqPalItemCount = getD3DPaletteCount( paletteType );

                    uint32 palDepth = Bitmap::getRasterFormatDepth( rasterFormat );

                    assert( palDepth != 0 );

                    size_t paletteDataSize = getPaletteDataSize( reqPalItemCount, palDepth );

                    // Check whether we have palette data in the stream.
                    texNativeImageStruct.check_read_ahead( paletteDataSize );

                    void *palData = engineInterface->PixelAllocate( paletteDataSize );

                    try
                    {
	                    texNativeImageStruct.read( palData, paletteDataSize );
                    }
                    catch( ... )
                    {
                        engineInterface->PixelFree( palData );

                        throw;
                    }

                    // Store the palette.
                    platformTex->palette = palData;
                    platformTex->paletteSize = reqPalItemCount;
                }

                mipGenLevelGenerator mipLevelGen( metaHeader.width, metaHeader.height );

                if ( !mipLevelGen.isValidLevel() )
                {
                    throw RwException( "texture " + theTexture->GetName() + " has invalid dimensions" );
                }

                uint32 mipmapCount = 0;

                uint32 processedMipmapCount = 0;

                bool hasDamagedMipmaps = false;

                for (uint32 i = 0; i < maybeMipmapCount; i++)
                {
                    bool couldEstablishLevel = true;

	                if (i > 0)
                    {
                        couldEstablishLevel = mipLevelGen.incrementLevel();
                    }

                    if (!couldEstablishLevel)
                    {
                        break;
                    }

                    // Create a new mipmap layer.
                    NativeTextureD3D8::mipmapLayer newLayer;

                    newLayer.layerWidth = mipLevelGen.getLevelWidth();
                    newLayer.layerHeight = mipLevelGen.getLevelHeight();

                    // Process dimensions.
                    uint32 texWidth = newLayer.layerWidth;
                    uint32 texHeight = newLayer.layerHeight;
                    {
		                // DXT compression works on 4x4 blocks,
		                // no smaller values allowed
		                if (dxtCompression != 0)
                        {
			                texWidth = ALIGN_SIZE( texWidth, 4u );
                            texHeight = ALIGN_SIZE( texHeight, 4u );
		                }
                    }

                    newLayer.width = texWidth;
                    newLayer.height = texHeight;

	                uint32 texDataSize = texNativeImageStruct.readUInt32();

                    // We started processing this mipmap.
                    processedMipmapCount++;

                    // Verify the data size.
                    bool isValidMipmap = true;
                    {
                        uint32 actualDataSize = 0;

                        if (dxtCompression != 0)
                        {
                            uint32 texItemCount = ( texWidth * texHeight );

                            actualDataSize = getDXTRasterDataSize(dxtCompression, texItemCount);
                        }
                        else
                        {
                            uint32 rowSize = getD3DRasterDataRowSize( texWidth, depth );

                            actualDataSize = getRasterDataSizeByRowSize( rowSize, texHeight );
                        }

                        if (actualDataSize != texDataSize)
                        {
                            isValidMipmap = false;
                        }
                    }

                    if ( !isValidMipmap )
                    {
                        // Even the Rockstar games texture generator appeared to have problems with mipmap generation.
                        // This is why textures appear to have the size of zero.

                        if (texDataSize != 0)
                        {
                            if ( !engineIgnoreSecureWarnings )
                            {
                               engineInterface->PushWarning( "texture " + theTexture->GetName() + " has damaged mipmaps (ignoring)" );
                            }

                            hasDamagedMipmaps = true;
                        }

                        // Skip the damaged bytes.
                        if (texDataSize != 0)
                        {
                            texNativeImageStruct.skip( texDataSize );
                        }
                        break;
                    }

                    // We first have to check whether there is enough data in the stream.
                    // Otherwise we would just flood the memory in case of an error;
                    // that could be abused by exploiters.
                    texNativeImageStruct.check_read_ahead( texDataSize );

                    void *texelData = engineInterface->PixelAllocate( texDataSize );

                    try
                    {
	                    texNativeImageStruct.read( texelData, texDataSize );
                    }
                    catch( ... )
                    {
                        engineInterface->PixelFree( texelData );

                        throw;
                    }

                    // Store mipmap properties.
	                newLayer.dataSize = texDataSize;

                    // Store the image data pointer.
	                newLayer.texels = texelData;

                    // Put the layer.
                    platformTex->mipmaps.AddToBack( std::move( newLayer ) );

                    mipmapCount++;
                }

                if ( mipmapCount == 0 )
                {
                    throw RwException( "texture " + theTexture->GetName() + " is empty" );
                }

                // mipmapCount can only be smaller than maybeMipmapCount.
                // This is logically true and would be absurd to assert here.

                if ( processedMipmapCount < maybeMipmapCount )
                {
                    // Skip the remaining mipmaps (most likely zero-sized).
                    bool hasSkippedNonZeroSized = false;

                    for ( uint32 n = processedMipmapCount; n < maybeMipmapCount; n++ )
                    {
                        uint32 mipSize = texNativeImageStruct.readUInt32();

                        if ( mipSize != 0 )
                        {
                            hasSkippedNonZeroSized = true;

                            // Skip the section.
                            texNativeImageStruct.skip( mipSize );
                        }
                    }

                    if ( !engineIgnoreSecureWarnings && !hasDamagedMipmaps )
                    {
                        // Print the debug message.
                        if ( !hasSkippedNonZeroSized )
                        {
                            engineInterface->PushWarning( "texture " + theTexture->GetName() + " has zero sized mipmaps" );
                        }
                        else
                        {
                            engineInterface->PushWarning( "texture " + theTexture->GetName() + " violates mipmap rules" );
                        }
                    }
                }

                // Fix filtering mode.
                fixFilteringMode( *theTexture, mipmapCount );

                // - Verify auto mipmap
                {
                    bool hasAutoMipmaps = platformTex->autoMipmaps;

                    if ( hasAutoMipmaps )
                    {
                        bool canHaveAutoMipmaps = ( mipmapCount == 1 );

                        if ( !canHaveAutoMipmaps )
                        {
                            engineInterface->PushWarning( "texture " + theTexture->GetName() + " has an invalid auto-mipmap flag (fixing)" );

                            platformTex->autoMipmaps = false;
                        }
                    }
                }
            }
            else
            {
                engineInterface->PushWarning( "failed to find texture native image struct in D3D texture native" );
            }
        }
        catch( ... )
        {
            texNativeImageStruct.LeaveContext();

            throw;
        }

        texNativeImageStruct.LeaveContext();
    }

    // Read extensions.
    engineInterface->DeserializeExtensions( theTexture, inputProvider );
}

static PluginDependantStructRegister <d3d8NativeTextureTypeProvider, RwInterfaceFactory_t> d3dNativeTexturePluginRegister;

void registerD3D8NativePlugin( void )
{
    d3dNativeTexturePluginRegister.RegisterPlugin( engineFactory );
}

};

#endif //RWLIB_INCLUDE_NATIVETEX_D3D8
