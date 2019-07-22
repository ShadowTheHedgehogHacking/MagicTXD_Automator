#include "StdInc.h"

#include "rwfile.system.hxx"

#include "pluginutil.hxx"

namespace rw
{

struct dataRepositoryEnv
{
    inline void Initialize( EngineInterface *engineInterface )
    {
        // We just keep a pointer to the translator.
        this->fileTranslator = nullptr;
    }

    inline void Shutdown( EngineInterface *engineInterface )
    {
        // nothing to do here.
    }

    // The FileTranslator class is used to access the data directories on the host's device.
    // We expect it to support input as standard path trees like "prim/sec/file.dat" and then
    // it can do its own transformations into OS path format.
    FileTranslator *fileTranslator;
};

static PluginDependantStructRegister <dataRepositoryEnv, RwInterfaceFactory_t> dataRepositoryEnvRegister;

Stream* OpenDataStream( Interface *intf, const wchar_t *filePath, eStreamMode mode )
{
    EngineInterface *engineInterface = (EngineInterface*)intf;

    dataRepositoryEnv *repoEnv = dataRepositoryEnvRegister.GetPluginStruct( engineInterface );

    // If we have a data environment and a file translator, we pass all data requests through it.
    // Otherwise we just give the requests to the main file interface.
    if ( repoEnv )
    {
        FileTranslator *fileTrans = repoEnv->fileTranslator;

        if ( fileTrans )
        {
            rw::Stream *transLocalStream = nullptr;

            rwStaticString <wchar_t> sysDataFilePath;

            bool couldGetValidDir = fileTrans->GetBasedDirectory( filePath, sysDataFilePath );

            if ( couldGetValidDir )
            {
                // Let's access it :)
                rw::streamConstructionFileParamW_t wFileParam( sysDataFilePath.GetConstString() );

                transLocalStream = engineInterface->CreateStream( rw::RWSTREAMTYPE_FILE_W, mode, &wFileParam );
            }

            return transLocalStream;
        }
    }

    // This is the fallback, which passes the file request _directly_ to the system.
    rw::streamConstructionFileParamW_t wFileParam( filePath );

    return engineInterface->CreateStream( rw::RWSTREAMTYPE_FILE_W, mode, &wFileParam );
}

void SetDataDirectoryTranslator( Interface *intf, FileTranslator *trans )
{
    // Sets the currently active data repository access parser (translator).

    EngineInterface *engineInterface = (EngineInterface*)intf;

    dataRepositoryEnv *repoEnv = dataRepositoryEnvRegister.GetPluginStruct( engineInterface );

    if ( repoEnv )
    {
        repoEnv->fileTranslator = trans;
    }
}

void registerFileSystemDataRepository( void )
{
    dataRepositoryEnvRegister.RegisterPlugin( engineFactory );
}

};