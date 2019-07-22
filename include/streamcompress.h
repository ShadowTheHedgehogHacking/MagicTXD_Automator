#pragma once

struct compressionProvider abstract
{
    virtual bool        Decompress( CFile *inputStream, CFile *outputStream ) = 0;
    virtual bool        Compress( CFile *inputStream, CFile *outputStream ) = 0;
};

struct compressionManager abstract
{
    virtual bool        IsStreamCompressed( CFile *stream ) const = 0;

    virtual compressionProvider*    CreateProvider( void ) = 0;
    virtual void                    DestroyProvider( compressionProvider *prov ) = 0;
};

// API to decode possibly compressed streams.
CFile* CreateDecompressedStream( MainWindow *mainWnd, CFile *compressed );

// Register your own providers.
bool RegisterStreamCompressionManager( MainWindow *mainWnd, compressionManager *manager );
bool UnregisterStreamCompressionManager( MainWindow *mainWnd, compressionManager *manager );