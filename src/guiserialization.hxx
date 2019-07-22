#pragma once

#define MAGICTXD_UNICODE_STRING_ID      0xBABE0001
#define MAGICTXD_CONFIG_BLOCK           0xBABE0002
#define MAGICTXD_ANSI_STRING_ID         0xBABE0003

enum magic_serializer_ids
{
    MAGICSERIALIZE_MAINWINDOW,
    MAGICSERIALIZE_MASSCONV,
    MAGICSERIALIZE_MASSEXPORT,
    MAGICSERIALIZE_EXPORTALLWINDOW,
    MAGICSERIALIZE_MASSBUILD,
    MAGICSERIALIZE_LANGUAGE,
    MAGICSERIALIZE_HELPERRUNTIME
};

// Our string blocks.
inline void RwWriteUnicodeString( rw::BlockProvider& prov, const rw::rwStaticString <wchar_t>& in )
{
    rw::BlockProvider stringBlock( &prov, false );

    stringBlock.EnterContext();
    
    // NOTE: this function is not cross-platform, because wchar_t is platform dependant.

    try
    {
        stringBlock.setBlockID( MAGICTXD_UNICODE_STRING_ID );

        // Simply write stuff, without zero termination.
        stringBlock.write( in.GetConstString(), in.GetLength() * sizeof( wchar_t ) );

        // Done.
    }
    catch( ... )
    {
        stringBlock.LeaveContext();

        throw;
    }

    stringBlock.LeaveContext();
}

inline bool RwReadUnicodeString( rw::BlockProvider& prov, rw::rwStaticString <wchar_t>& out )
{
    bool gotString = false;

    rw::BlockProvider stringBlock( &prov, false );

    stringBlock.EnterContext();

    try
    {
        if ( stringBlock.getBlockID() == MAGICTXD_UNICODE_STRING_ID )
        {
            // Simply read stuff.
            rw::int64 blockLength = stringBlock.getBlockLength();

            // We need to get a valid unicode string length.
            size_t unicodeLength = ( blockLength / sizeof( wchar_t ) );

            rw::int64 unicodeDataLength = ( unicodeLength * sizeof( wchar_t ) );

            out.Resize( unicodeLength );

            // Read into the unicode string implementation.
            stringBlock.read( (wchar_t*)out.GetConstString(), unicodeDataLength );

            // Skip the remainder.
            stringBlock.skip( blockLength - unicodeDataLength );

            gotString = true;
        }
    }
    catch( ... )
    {
        stringBlock.LeaveContext();

        throw;
    }

    stringBlock.LeaveContext();

    return gotString;
}

// ANSI string stuff.
inline void RwWriteANSIString( rw::BlockProvider& parentBlock, const rw::rwStaticString <char>& str )
{
    rw::BlockProvider stringBlock( &parentBlock );

    stringBlock.EnterContext();

    try
    {
        stringBlock.setBlockID( MAGICTXD_ANSI_STRING_ID );

        stringBlock.write( str.GetConstString(), str.GetLength() );
    }
    catch( ... )
    {
        stringBlock.LeaveContext();

        throw;
    }

    stringBlock.LeaveContext();
}

inline bool RwReadANSIString( rw::BlockProvider& parentBlock, rw::rwStaticString <char>& stringOut )
{
    bool gotString = false;

    rw::BlockProvider stringBlock( &parentBlock );

    stringBlock.EnterContext();

    try
    {
        if ( stringBlock.getBlockID() == MAGICTXD_ANSI_STRING_ID )
        {
            // We read as much as we can into a memory buffer.
            rw::int64 blockSize = stringBlock.getBlockLength();

            size_t ansiStringLength = (size_t)blockSize;

            stringOut.Resize( ansiStringLength );

            stringBlock.read( (void*)stringOut.GetConstString(), ansiStringLength );

            // Skip the rest.
            stringBlock.skip( blockSize - ansiStringLength );

            gotString = true;
        }
    }
    catch( ... )
    {
        stringBlock.LeaveContext();

        throw;
    }

    stringBlock.LeaveContext();

    return gotString;
}

#include <sdk/PluginHelpers.h>

// Serialization for MainWindow modules.
struct magicSerializationProvider abstract
{
    virtual void Load( MainWindow *mainWnd, rw::BlockProvider& configBlock ) = 0;
    virtual void Save( const MainWindow *mainWnd, rw::BlockProvider& configBlock ) const = 0;
};

rw::uint32 GetAmountOfMainWindowSerializers( const MainWindow *mainWnd );
magicSerializationProvider* FindMainWindowSerializer( MainWindow *mainWnd, unsigned short unique_id );
void ForAllMainWindowSerializers( const MainWindow *mainWnd, std::function <void ( magicSerializationProvider *prov, unsigned short id )> cb );

// API to register serialization.
bool RegisterMainWindowSerialization( MainWindow *mainWnd, unsigned short unique_id, magicSerializationProvider *prov );
bool UnregisterMainWindowSerialization( MainWindow *mainWnd, unsigned short unique_id );