// Image import helpers.

#pragma once

struct makeRasterImageImportMethods abstract : public imageImportMethods
{
    inline makeRasterImageImportMethods( rw::Interface *engineInterface )
    {
        this->engineInterface = engineInterface;

        // You have to extend this class with a way to get a consistent native texture name.
    }

    virtual std::string GetNativeTextureName( void ) const = 0;

    rw::Raster* MakeRaster( void ) const override
    {
        rw::Interface *rwEngine = this->engineInterface;

        rw::Raster *texRaster = rw::CreateRaster( rwEngine );

        if ( texRaster )
        {
            try
            {
                // We need to give this raster a start native format.
                // For that we should give it the format it actually should have.
                std::string nativeName = GetNativeTextureName();

                texRaster->newNativeData( nativeName.c_str() );
            }
            catch( ... )
            {
                // Clean up after error.
                rw::DeleteRaster( texRaster );

                throw;
            }
        }

        return texRaster;
    }

protected:
    rw::Interface *engineInterface;
};

static rw::TextureBase* RwMakeTextureFromStream(
    rw::Interface *rwEngine, rw::Stream *imgStream, const filePath& extention,
    const makeRasterImageImportMethods& imgImporter
)
{
    // Based on the extention, try to figure out what the user wants to import.
    // For that we better verify that it really is an image type extention.
    eImportExpectation defImpExp = getActualImageImportExpectation( rwEngine, extention );

    // Load texture data.
    makeRasterImageImportMethods::loadActionResult load_result;

    bool didLoad = imgImporter.LoadImage( imgStream, defImpExp, load_result );

    if ( didLoad )
    {
        rw::Raster *texRaster = load_result.texRaster;

        rw::TextureBase *texReturn = nullptr;

        try
        {
            // If we have a texture, we just return it.
            if ( rw::TextureBase *loadedTex = load_result.texHandle )
            {
                // Prepare the texture.
                if ( rw::Raster *texRaster = loadedTex->GetRaster() )
                {
                    std::string nativeName = imgImporter.GetNativeTextureName();

                    // Convert the raster to the desired platform.
                    bool couldConvert = rw::ConvertRasterTo( texRaster, nativeName.c_str() );

                    if ( !couldConvert )
                    {
                        rwEngine->PushWarning( "failed to convert raster to platform '" + rw::rwStaticString <char> ( nativeName.c_str(), nativeName.size() ) + "'\n" );
                    }
                }

                texReturn = loadedTex;
            }
            else
            {
                // OK, we got an image. This means we should put the raster into a texture and return it!
                texReturn = rw::CreateTexture( rwEngine, texRaster );
            }
        }
        catch( ... )
        {
            // Clean up the stuff.
            load_result.cleanUpSuccessful();
            throw;
        }

        // We have to release our reference to the raster.
        rw::DeleteRaster( texRaster );

        return texReturn;
    }

    // Nothing I guess.
    return NULL;
}