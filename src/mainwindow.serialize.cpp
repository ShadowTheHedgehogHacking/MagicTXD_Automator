#include "mainwindow.h"

#include "guiserialization.hxx"

struct mainWindowSerializationEnv : public magicSerializationProvider
{
    inline void Initialize( MainWindow *mainWnd )
    {
        RegisterMainWindowSerialization( mainWnd, MAGICSERIALIZE_MAINWINDOW, this );
    }

    inline void Shutdown( MainWindow *mainWnd )
    {
        UnregisterMainWindowSerialization( mainWnd, MAGICSERIALIZE_MAINWINDOW );
    }

    enum eSelectedTheme
    {
        THEME_DARK,
        THEME_LIGHT
    };

    struct mtxd_cfg_struct
    {
        bool addImageGenMipmaps;
        bool lockDownTXDPlatform;
        endian::little_endian <eSelectedTheme> selectedTheme;
        bool showLogOnWarning;
        bool showGameIcon;
        bool adjustTextureChunksOnImport;
        bool texaddViewportFill;
        bool texaddViewportScaled;
        bool texaddViewportBackground;
    };

    // We want to serialize RW config too.
    struct rwengine_cfg_struct
    {
        bool metaDataTagging;
        endian::little_endian <rw::int32> warning_level;
        bool ignoreSecureWarnings;
        bool fixIncompatibleRasters;
        bool compatTransformNativeImaging;
        bool preferPackedSampleExport;
        bool dxtPackedDecompression;
        bool ignoreBlockSerializationRegions;
    };

    void Load( MainWindow *mainwnd, rw::BlockProvider& mtxdConfig ) override
    {
        // last directory we were in to save TXD file.
        {
            rw::rwStaticString <wchar_t> lastTXDSaveDir;

            bool gotDir = RwReadUnicodeString( mtxdConfig, lastTXDSaveDir );

            if ( gotDir )
            {
                mainwnd->lastTXDSaveDir = wide_to_qt( lastTXDSaveDir );
            }
        }

        // last directory we were in to add an image file.
        {
            rw::rwStaticString <wchar_t> lastImageFileOpenDir;

            bool gotDir = RwReadUnicodeString( mtxdConfig, lastImageFileOpenDir );

            if ( gotDir )
            {
                mainwnd->lastImageFileOpenDir = wide_to_qt( lastImageFileOpenDir );
            }
        }

        mtxd_cfg_struct cfgStruct;
        mtxdConfig.readStruct( cfgStruct );

        mainwnd->addImageGenMipmaps = cfgStruct.addImageGenMipmaps;
        mainwnd->lockDownTXDPlatform = cfgStruct.lockDownTXDPlatform;

        // Select the appropriate theme.
        eSelectedTheme themeOption = cfgStruct.selectedTheme;

        if ( themeOption == THEME_DARK )
        {
            mainwnd->onToogleDarkTheme( true );
            mainwnd->actionThemeDark->setChecked( true );
        }
        else if ( themeOption == THEME_LIGHT )
        {
            mainwnd->onToogleLightTheme( true );
            mainwnd->actionThemeLight->setChecked( true );
        }

        mainwnd->showLogOnWarning = cfgStruct.showLogOnWarning;
        mainwnd->showGameIcon = cfgStruct.showGameIcon;
        mainwnd->adjustTextureChunksOnImport = cfgStruct.adjustTextureChunksOnImport;
        mainwnd->texaddViewportFill = cfgStruct.texaddViewportFill;
        mainwnd->texaddViewportScaled = cfgStruct.texaddViewportScaled;
        mainwnd->texaddViewportBackground = cfgStruct.texaddViewportBackground;

        // TXD log settings.
        {
            rw::BlockProvider logGeomBlock( &mtxdConfig );

            logGeomBlock.EnterContext();

            try
            {
                if ( logGeomBlock.getBlockID() == rw::CHUNK_STRUCT )
                {
                    int geomSize = (int)logGeomBlock.getBlockLength();

                    QByteArray tmpArr( geomSize, 0 );

                    logGeomBlock.read( tmpArr.data(), geomSize );

                    // Restore geometry.
                    mainwnd->txdLog->restoreGeometry( tmpArr );
                }
            }
            catch( ... )
            {
                logGeomBlock.LeaveContext();

                throw;
            }

            logGeomBlock.LeaveContext();
        }

        // Read RenderWare settings.
        {
            rw::Interface *rwEngine = mainwnd->rwEngine;

            rw::BlockProvider rwsettingsBlock( &mtxdConfig );

            rwsettingsBlock.EnterContext();

            try
            {
                rwengine_cfg_struct rwcfg;
                rwsettingsBlock.readStruct( rwcfg );

                rwEngine->SetMetaDataTagging( rwcfg.metaDataTagging );
                rwEngine->SetWarningLevel( rwcfg.warning_level );
                rwEngine->SetIgnoreSecureWarnings( rwcfg.ignoreSecureWarnings );
                rwEngine->SetFixIncompatibleRasters( rwcfg.fixIncompatibleRasters );
                rwEngine->SetCompatTransformNativeImaging( rwcfg.compatTransformNativeImaging );
                rwEngine->SetPreferPackedSampleExport( rwcfg.preferPackedSampleExport );
                rwEngine->SetDXTPackedDecompression( rwcfg.dxtPackedDecompression );
                rwEngine->SetIgnoreSerializationBlockRegions( rwcfg.ignoreBlockSerializationRegions );
            }
            catch( ... )
            {
                rwsettingsBlock.LeaveContext();

                throw;
            }

            rwsettingsBlock.LeaveContext();
        }

        // If we had valid configuration, we are not for the first time.
        mainwnd->isLaunchedForTheFirstTime = false;
    }

    void Save( const MainWindow *mainwnd, rw::BlockProvider& mtxdConfig ) const override
    {
        RwWriteUnicodeString( mtxdConfig, qt_to_widerw( mainwnd->lastTXDSaveDir ) );
        RwWriteUnicodeString( mtxdConfig, qt_to_widerw( mainwnd->lastImageFileOpenDir ) );

        mtxd_cfg_struct cfgStruct;
        cfgStruct.addImageGenMipmaps = mainwnd->addImageGenMipmaps;
        cfgStruct.lockDownTXDPlatform = mainwnd->lockDownTXDPlatform;

        // Write theme.
        eSelectedTheme themeOption = THEME_DARK;

        if ( mainwnd->actionThemeDark->isChecked() )
        {
            themeOption = THEME_DARK;
        }
        else if ( mainwnd->actionThemeLight->isChecked() )
        {
            themeOption = THEME_LIGHT;
        }

        cfgStruct.selectedTheme = themeOption;
        cfgStruct.showLogOnWarning = mainwnd->showLogOnWarning;
        cfgStruct.showGameIcon = mainwnd->showGameIcon;
        cfgStruct.adjustTextureChunksOnImport = mainwnd->adjustTextureChunksOnImport;
        cfgStruct.texaddViewportFill = mainwnd->texaddViewportFill;
        cfgStruct.texaddViewportScaled = mainwnd->texaddViewportScaled;
        cfgStruct.texaddViewportBackground = mainwnd->texaddViewportBackground;

        mtxdConfig.writeStruct( cfgStruct );

        // TXD log properties.
        {
            QByteArray logGeom = mainwnd->txdLog->saveGeometry();

            rw::BlockProvider logGeomBlock( &mtxdConfig );

            logGeomBlock.EnterContext();

            try
            {
                int geomSize = logGeom.size();

                logGeomBlock.write( logGeom.constData(), geomSize );
            }
            catch( ... )
            {
                logGeomBlock.LeaveContext();

                throw;
            }

            logGeomBlock.LeaveContext();
        }

        // RW engine properties.
        // Actually write those safely.
        {
            rw::Interface *rwEngine = mainwnd->rwEngine;

            rw::BlockProvider rwsettingsBlock( &mtxdConfig );
            
            rwsettingsBlock.EnterContext();

            try
            {
                rwengine_cfg_struct engineCfg;
                engineCfg.metaDataTagging = rwEngine->GetMetaDataTagging();
                engineCfg.warning_level = rwEngine->GetWarningLevel();
                engineCfg.ignoreSecureWarnings = rwEngine->GetIgnoreSecureWarnings();
                engineCfg.fixIncompatibleRasters = rwEngine->GetFixIncompatibleRasters();
                engineCfg.compatTransformNativeImaging = rwEngine->GetCompatTransformNativeImaging();
                engineCfg.preferPackedSampleExport = rwEngine->GetPreferPackedSampleExport();
                engineCfg.dxtPackedDecompression = rwEngine->GetDXTPackedDecompression();
                engineCfg.ignoreBlockSerializationRegions = rwEngine->GetIgnoreSerializationBlockRegions();

                rwsettingsBlock.writeStruct( engineCfg );
            }
            catch( ... )
            {
                rwsettingsBlock.LeaveContext();

                throw;
            }

            rwsettingsBlock.LeaveContext();
        }
    }
};

static PluginDependantStructRegister <mainWindowSerializationEnv, mainWindowFactory_t> mainWindowSerializationEnvRegister;

void InitializeMainWindowSerializationBlock( void )
{
    mainWindowSerializationEnvRegister.RegisterPlugin( mainWindowFactory );
}