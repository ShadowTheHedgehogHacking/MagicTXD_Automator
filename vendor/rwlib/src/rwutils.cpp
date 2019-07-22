#include "StdInc.h"

// This file contains utilities that do not depend on the internal state of the RenderWare framework.

namespace rw
{

namespace utils
{

void bufferedWarningManager::OnWarning( rwStaticString <char>&& msg )
{
    // We can take the string directly.
    this->messages.AddToBack( std::move( msg ) );
}

void bufferedWarningManager::forward( Interface *engineInterface )
{
    // Move away the warnings from us.
    auto warnings_to_push = std::move( this->messages );

    // We give all warnings to the engine.
    for ( size_t n = 0; n < warnings_to_push.GetCount(); n++ )
    {
        engineInterface->PushWarning( std::move( warnings_to_push[n] ) );
    }
}

// String chunk handling.
void writeStringChunkANSI( Interface *engineInterface, BlockProvider& outputProvider, const char *string, size_t strLen )
{
    BlockProvider stringChunk( &outputProvider );

    stringChunk.EnterContext();

    try
    {
        // We are writing a string.
        stringChunk.setBlockID( CHUNK_STRING );

        // Write the string.
        stringChunk.write(string, strLen);

        // Pad to multiples of four.
        // This will automatically zero-terminate the string.
        // We do this for performance reasons.
        size_t remainder = 4 - (strLen % 4);

        // Write zeroes.
        for ( size_t n = 0; n < remainder; n++ )
        {
            stringChunk.writeUInt8( 0 );
        }
    }
    catch( ... )
    {
        stringChunk.LeaveContext();

        throw;
    }

    stringChunk.LeaveContext();
}

void readStringChunkANSI( Interface *engineInterface, BlockProvider& inputProvider, rwStaticString <char>& stringOut )
{
    BlockProvider stringBlock( &inputProvider );

    stringBlock.EnterContext();

    try
    {
        if ( stringBlock.getBlockID() == CHUNK_STRING )
        {
            int64 chunkLength = stringBlock.getBlockLength();

            if ( chunkLength < 0x80000000L )
            {
                size_t strLen = (size_t)chunkLength;

                rwStaticString <char> strRead;
                strRead.Resize( strLen );

                stringBlock.read( (char*)strRead.GetConstString(), strLen );

                stringOut = std::move( strRead );
            }
            else
            {
                engineInterface->PushWarning( "too long string in string chunk" );
            }
        }
        else
        {
            engineInterface->PushWarning( "could not find string chunk" );
        }
    }
    catch( ... )
    {
        stringBlock.LeaveContext();

        throw;
    }

    stringBlock.LeaveContext();
}

};

};