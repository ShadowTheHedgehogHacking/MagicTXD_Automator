#include "mainwindow.h"

bool imageImportMethods::impMeth_loadImage( rw::Stream *imgStream, loadActionResult& action_result ) const
{
    //rw::Interface *rwEngine = imgStream->engineInterface;

    rw::Raster *platOrig = this->MakeRaster();

    if ( platOrig )
    {
        try
        {
            // Try to load the image data.
            platOrig->readImage( imgStream );

            // Return the stuff.
            action_result.texRaster = platOrig;
            action_result.texHandle = NULL;

            return true;
        }
        catch( ... )
        {
            rw::DeleteRaster( platOrig );

            throw;
        }
    }

    return false;
}

bool imageImportMethods::impMeth_loadTexChunk( rw::Stream *chunkStream, loadActionResult& action_result ) const
{
    // We take all properties from the original texture chunk.
    rw::Interface *rwEngine = chunkStream->engineInterface;

    rw::RwObject *rwObj = rwEngine->Deserialize( chunkStream );

    if ( rwObj )
    {
        try
        {
            // We could have gotten any kind of RW object.
            // Tho we are only interrested in texture chunks.
            rw::TextureBase *texHandle = rw::ToTexture( rwEngine, rwObj );

            if ( texHandle )
            {
                // We have got a texture!
                // That means we also should have a raster.
                rw::Raster *texRaster = texHandle->GetRaster();

                if ( texRaster )
                {
                    // Return the stuff.
                    action_result.texRaster = rw::AcquireRaster( texRaster );
                    action_result.texHandle = texHandle;

                    // Good to go!
                    return true;
                }
                else
                {
                    rwEngine->PushWarning( "parsed texture image contains no raster" );
                }
            }
            else
            {
                rwEngine->PushWarning( rw::rwStaticString <char> ( "tried parsing a texture, got " ) + rwEngine->GetObjectTypeName( rwObj ) + " instead" );
            }
        }
        catch( ... )
        {
            rwEngine->DeleteRwObject( rwObj );

            throw;
        }

        // Release stuff.
        rwEngine->DeleteRwObject( rwObj );
    }

    return false;
}

bool imageImportMethods::LoadImage( rw::Stream *stream, eImportExpectation imp_exp, loadActionResult& action_result ) const
{
    rw::Interface *rwEngine = stream->engineInterface;

    // The idea of this logic is to provide the correct feedback in the correct moment
    // depending on the actual data. This translates to matching the expected image data that
    // was deduced from the image path type to the data that the file actually contains.
    // - warn the user if the image contains unexpected data
    // - print internal warnings only if the image data was expected

    rw::utils::bufferedWarningManager exp_format_warnings;
    rw::rwStaticString <char> exp_format_error;
    const char *exp_name = nullptr;

    bool foundExpectedFormat = false;

    rw::int64 streamPos = stream->tell();

    bool needsStreamReset = false;

    // First try the expected image data type, if available.
    if ( imp_exp != IMPORTE_NONE )
    {
        importMethod_t loader = nullptr;

        for ( const meth_reg& reg : this->methods )
        {
            if ( reg.img_exp == imp_exp )
            {
                loader = reg.cb;

                exp_name = reg.name;
                foundExpectedFormat = true;
                break;
            }
        }

        if ( loader )
        {
            // We have to collect the warnings in a buffer.
            // If the data was loaded as expected, then we print the warnings.
            // If the data failed to load and it turns out it was another, then we dont print the warnings.
            // If the data failed to load and there was no another, then we print the warnings.
            // I think this should suffice.

            bool hasExpectedData = false;

            try
            {
                rw::utils::stacked_warnman_scope warnman_scope( rwEngine, &exp_format_warnings );

                needsStreamReset = true;

                hasExpectedData = (this->*loader)( stream, action_result );
            }
            catch( rw::RwException& except )
            {
                // Things failed, so we store the error and continue.
                exp_format_error = except.message;

                hasExpectedData = false;
            }

            if ( hasExpectedData )
            {
                // We are done here.
                exp_format_warnings.forward( rwEngine );

                return true;
            }
        }
    }

    // Since we do not have the expected format, we need to check every other format.
    // This time around we do not print any internal warnings.
    bool hasUnexpectedFormat = false;
    {
        rw::utils::stacked_warnlevel_scope ignore_warnings( rwEngine, 0 );

        for ( const meth_reg& reg : this->methods )
        {
            // Make sure we did not try this one before.
            if ( reg.img_exp != imp_exp )
            {
                try
                {
                    if ( needsStreamReset )
                    {
                        stream->seek( streamPos, rw::RWSEEK_BEG );
                    }

                    needsStreamReset = true;

                    bool hasFoundFormat = (this->*reg.cb)( stream, action_result );

                    if ( hasFoundFormat )
                    {
                        if ( foundExpectedFormat )
                        {
                            this->OnWarning(
                                rw::rwStaticString <char> ( "tried parsing \"" ) + exp_name + "\" but found \"" + reg.name + "\"\n"
                            );
                        }

                        hasUnexpectedFormat = true;
                        break;
                    }
                }
                catch( rw::RwException& except )
                {
                    // A format simply did not work out. Continue.
                }
            }
        }
    }

    // If we had no different format, then we expect that the original format was broken.
    // That is why we should output the warnings and errors of the expected format, if available.
    if ( hasUnexpectedFormat == false )
    {
        exp_format_warnings.forward( rwEngine );

        if ( exp_format_error.GetLength() > 0 )
        {
            this->OnError( "error while loading image data: " + exp_format_error + "\n" );
        }
    }

    return hasUnexpectedFormat;
}

void imageImportMethods::RegisterImportMethod( const char *name, importMethod_t meth, eImportExpectation impExp )
{
    assert( impExp != IMPORTE_NONE );

    meth_reg reg;
    reg.img_exp = impExp;
    reg.cb = std::move( meth );
    reg.name = name;

    this->methods.push_back( std::move( reg ) );
}
