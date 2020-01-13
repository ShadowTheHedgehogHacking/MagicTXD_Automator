#include "mainwindow.h"

#include "guiserialization.hxx"

struct exportAllWindowSerializationEnv : public magicSerializationProvider
{
    inline void Initialize( MainWindow *mainWnd )
    {
        RegisterMainWindowSerialization( mainWnd, MAGICSERIALIZE_EXPORTALLWINDOW, this );
    }

    inline void Shutdown( MainWindow *mainWnd )
    {
        UnregisterMainWindowSerialization( mainWnd, MAGICSERIALIZE_EXPORTALLWINDOW );
    }

    void Load( MainWindow *mainwnd, rw::BlockProvider& exportAllBlock ) override
    {
        RwReadANSIString( exportAllBlock, mainwnd->lastUsedAllExportFormat );
        RwReadUnicodeString( exportAllBlock, mainwnd->lastAllExportTarget );
    }

    void Save( const MainWindow *mainwnd, rw::BlockProvider& exportAllBlock ) const override
    {
        RwWriteANSIString( exportAllBlock, mainwnd->lastUsedAllExportFormat );
        RwWriteUnicodeString( exportAllBlock, mainwnd->lastAllExportTarget );
    }
};

static PluginDependantStructRegister <exportAllWindowSerializationEnv, mainWindowFactory_t> exportAllWindowSerializationEnvRegister;

void InitializeExportAllWindowSerialization( void )
{
    exportAllWindowSerializationEnvRegister.RegisterPlugin( mainWindowFactory );
}
