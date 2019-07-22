#include "mainwindow.h"

// We allow embedding data post-compilation.

struct embedded_data
{
    void *dataptr;
    size_t datasize;
};

#ifdef _WIN32
extern "C" __declspec(dllexport) volatile const embedded_data _export_embedded_resources = { 0 };
#endif //_WIN32

static CFileTranslator *embedded_archive = nullptr;

void initialize_embedded_resources( void )
{
    assert( fileSystem != nullptr );

#ifdef _WIN32
    if ( _export_embedded_resources.dataptr != nullptr )
    {
        CFile *bufFile = fileSystem->CreateUserBufferFile( (void*)_export_embedded_resources.dataptr, _export_embedded_resources.datasize );

        if ( bufFile != nullptr )
        {
            embedded_archive = fileSystem->OpenZIPArchive( *bufFile );

            if ( embedded_archive != nullptr )
            {
                register_file_translator( embedded_archive );
            }
            else
            {
                delete bufFile;
            }
        }
    }
#endif //_WIN32
}

void shutdown_embedded_resources( void )
{
    if ( CFileTranslator *archive = embedded_archive )
    {
        unregister_file_translator( archive );

        delete archive;

        embedded_archive = nullptr;
    }
}