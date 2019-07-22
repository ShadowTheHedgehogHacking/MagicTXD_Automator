#include "StdInc.h"

#ifdef RWLIB_INCLUDE_NATIVETEX_ATC_MOBILE

#include "pixelformat.hxx"

#include "txdread.d3d.hxx"
#include "txdread.atc.hxx"

#include "streamutil.hxx"

namespace rw
{

eTexNativeCompatibility atcNativeTextureTypeProvider::IsCompatibleTextureBlock( BlockProvider& inputProvider ) const
{
    eTexNativeCompatibility texCompat = RWTEXCOMPAT_NONE;

    BlockProvider texNativeImageBlock( &inputProvider );

    texNativeImageBlock.EnterContext();

    try
    {
        if ( texNativeImageBlock.getBlockID() == CHUNK_STRUCT )
        {
            // Here we can check the platform descriptor, since we know it is unique.
            uint32 platformDescriptor = texNativeImageBlock.readUInt32();

            if ( platformDescriptor == PLATFORM_ATC )
            {
                texCompat = RWTEXCOMPAT_ABSOLUTE;
            }
        }
    }
    catch( ... )
    {
        texNativeImageBlock.LeaveContext();

        throw;
    }

    texNativeImageBlock.LeaveContext();

    return texCompat;
}

void atcNativeTextureTypeProvider::SerializeTexture( TextureBase *theTexture, PlatformTexture *nativeTex, BlockProvider& outputProvider ) const
{
    Interface *engineInterface = theTexture->engineInterface;

    NativeTextureATC *platformTex = (NativeTextureATC*)nativeTex;

    size_t mipmapCount = platformTex->mipmaps.GetCount();

    if ( mipmapCount == 0 )
    {
        throw RwException( "attempt to write ATC native texture which has no mipmap layers" );
    }

	{
		// Write the actual struct.
        BlockProvider texNativeImageStruct( &outputProvider );

        texNativeImageStruct.EnterContext();

        try
        {
            // Write the header with meta information.
            amdtc::textureNativeGenericHeader metaHeader;
            metaHeader.platformDescriptor = PLATFORM_ATC;
            metaHeader.formatInfo.set( *theTexture );
            
            memset( metaHeader.pad1, 0, sizeof(metaHeader.pad1) );

            // Correctly write the name strings (for safety).
            // Even though we can read those name fields with zero-termination safety,
            // the engines are not guarranteed to do so.
            // Also, print a warning if the name is changed this way.
            writeStringIntoBufferSafe( engineInterface, theTexture->GetName(), metaHeader.name, sizeof( metaHeader.name ), theTexture->GetName(), "name" );
            writeStringIntoBufferSafe( engineInterface, theTexture->GetMaskName(), metaHeader.maskName, sizeof( metaHeader.maskName ), theTexture->GetName(), "mask name" );

            metaHeader.mipmapCount = (uint8)mipmapCount;
            metaHeader.unk1 = platformTex->unk1;
            metaHeader.hasAlpha = platformTex->hasAlpha;
            metaHeader.pad2 = 0;

            metaHeader.width = platformTex->mipmaps[ 0 ].layerWidth;
            metaHeader.height = platformTex->mipmaps[ 0 ].layerHeight;

            metaHeader.internalFormat = platformTex->internalFormat;
            
            // Calculate the image data section size.
            uint32 imageDataSectionSize = 0;

            for ( size_t n = 0; n < mipmapCount; n++ )
            {
                uint32 mipDataSize = platformTex->mipmaps[ n ].dataSize;

                imageDataSectionSize += mipDataSize;
                imageDataSectionSize += sizeof( uint32 );
            }

            metaHeader.imageSectionStreamSize = imageDataSectionSize;
            metaHeader.unk2 = platformTex->unk2;

            // Write the meta header.
            texNativeImageStruct.write((const char*)&metaHeader, sizeof(metaHeader));

            // Write the mipmap data sizes.
            for ( size_t n = 0; n < mipmapCount; n++ )
            {
                uint32 mipDataSize = platformTex->mipmaps[ n ].dataSize;

                texNativeImageStruct.writeUInt32( mipDataSize );
            }

            // Write the picture data now.
            for ( size_t n = 0; n < mipmapCount; n++ )
            {
                NativeTextureATC::mipmapLayer& mipLayer = platformTex->mipmaps[ n ];

                uint32 mipDataSize = mipLayer.dataSize;

                texNativeImageStruct.write((const char*)mipLayer.texels, mipDataSize);
            }
        }
        catch( ... )
        {
            // We must remember to leave the context ourselves.
            texNativeImageStruct.LeaveContext();

            throw;
        }

        texNativeImageStruct.LeaveContext();
	}

	// Write the extensions last.
    engineInterface->SerializeExtensions( theTexture, outputProvider );
}

};

#endif //RWLIB_INCLUDE_NATIVETEX_ATC_MOBILE