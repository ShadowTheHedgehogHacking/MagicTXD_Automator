#include "mainwindow.h"
#include <QtWidgets/QApplication>
#include "styles.h"
#include <QtCore/QTimer>
#include "resource.h"

#include <NativeExecutive/CExecutiveManager.h>

#include <QtCore/QtPlugin>

#include <exception>

#ifdef _WIN32
#include <Windows.h>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#elif defined(__linux__)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif //CROSS PLATFORM CODE

// Import all required image formats.
Q_IMPORT_PLUGIN(QGifPlugin)

#include <QtGui/QImageWriter>

struct ScopedSystemEventFilter
{
    inline ScopedSystemEventFilter(QObject *receiver, QEvent *evt)
    {
        _currentFilter = this;

        QWidget *theWidget = nullptr;

        this->evt = nullptr;
        this->handlerWidget = nullptr;

        if ( receiver )
        {
            if (receiver->isWidgetType())
            {
                theWidget = (QWidget*)receiver;
            }
        }

        if (theWidget)
        {
            // We are not there yet.
            // Check whether this widget supports system event signalling.
            SystemEventHandlerWidget *hWidget = dynamic_cast <SystemEventHandlerWidget*> (theWidget);

            if (hWidget)
            {
                hWidget->beginSystemEvent(evt);

                this->evt = evt;
                this->handlerWidget = hWidget;
            }
        }
    }

    inline ~ScopedSystemEventFilter(void)
    {
        if (handlerWidget)
        {
            handlerWidget->endSystemEvent(evt);
        }

        _currentFilter = nullptr;
    }

    static ScopedSystemEventFilter *_currentFilter;

    QEvent *evt;
    SystemEventHandlerWidget *handlerWidget;
};

ScopedSystemEventFilter* ScopedSystemEventFilter::_currentFilter = nullptr;

SystemEventHandlerWidget::~SystemEventHandlerWidget(void)
{
    ScopedSystemEventFilter *sysEvtFilter = ScopedSystemEventFilter::_currentFilter;

    if (sysEvtFilter)
    {
        if (sysEvtFilter->handlerWidget == this)
        {
            sysEvtFilter->handlerWidget = nullptr;
            sysEvtFilter->evt = nullptr;
        }
    }
}

struct MagicTXDApplication : public QApplication
{
    inline MagicTXDApplication(int& argc, char *argv[]) : QApplication(argc, argv)
    {
        return;
    }

    bool notify(QObject *receiver, QEvent *evt) override
    {
        ScopedSystemEventFilter filter(receiver, evt);

        return QApplication::notify(receiver, evt);
    }
};

// Main window factory.
mainWindowFactory_t mainWindowFactory;

struct mainWindowConstructor
{
    QString appPath;
    rw::Interface *rwEngine;
    CFileSystem *fsHandle;

    inline mainWindowConstructor(QString&& appPath, rw::Interface *rwEngine, CFileSystem *fsHandle)
        : appPath(appPath), rwEngine(rwEngine), fsHandle(fsHandle)
    {}

    inline MainWindow* Construct(void *mem) const
    {
        return new (mem) MainWindow(appPath, rwEngine, fsHandle);
    }
};

// Global plugins.
extern void registerQtFileSystem( void );

extern void unregisterQtFileSystem( void );

// Main window plugin entry points.
extern void InitializeRWFileSystemWrap(void);
extern void InitializeTaskCompletionWindowEnv( void );
extern void InitializeSerializationStorageEnv( void );
extern void InitializeMainWindowSerializationBlock( void );
extern void InitializeMagicLanguages( void );
extern void InitializeHelperRuntime( void );
extern void InitializeMainWindowHelpEnv( void );
extern void InitializeTextureAddDialogEnv( void );
extern void InitializeExportAllWindowSerialization( void );
extern void InitializeMassconvToolEnvironment(void);
extern void InitializeMassExportToolEnvironment( void );
extern void InitializeMassBuildEnvironment( void );
extern void InitializeGUISerialization(void);
extern void InitializeStreamCompressionEnvironment( void );

extern void DbgHeap_Validate( void );

static void important_message( const char *msg, const char *title )
{
#ifdef _WIN32
    MessageBoxA( nullptr, msg, title, MB_OK );
#elif defined(__linux__)
    printf( "%s\n", msg );
#endif //CROSS PLATFORM CODE
}

// Global app-root-only system file translator.
CFileTranslator *sysAppRoot = nullptr;

int main(int argc, char *argv[])
{
    // Initialize global plugins.
    registerQtFileSystem();

    // Initialize all main window plugins.
    InitializeRWFileSystemWrap();
    InitializeTaskCompletionWindowEnv();
    InitializeSerializationStorageEnv();
    InitializeMainWindowSerializationBlock();
    InitializeMagicLanguages();
    InitializeHelperRuntime();
    InitializeMainWindowHelpEnv();
    InitializeTextureAddDialogEnv();
    InitializeExportAllWindowSerialization();
    InitializeMassconvToolEnvironment();
    InitializeMassExportToolEnvironment();
    InitializeMassBuildEnvironment();
    InitializeGUISerialization();
    InitializeStreamCompressionEnvironment();

    int iRet = -1;

    try
    {
        // Initialize the RenderWare engine.
        rw::LibraryVersion engineVersion;

        // This engine version is the default version we create resources in.
        // Resources can change their version at any time, so we do not have to change this.
        engineVersion.rwLibMajor = 3;
        engineVersion.rwLibMinor = 6;
        engineVersion.rwRevMajor = 0;
        engineVersion.rwRevMinor = 3;

        rw::Interface *rwEngine = rw::CreateEngine( engineVersion );

        if ( rwEngine == nullptr )
        {
            throw std::exception(); // "failed to initialize the RenderWare engine"
        }

        try
        {
            NativeExecutive::CExecutiveManager *nativeExec = (NativeExecutive::CExecutiveManager*)rw::GetThreadingNativeManager( rwEngine );

            // Set some typical engine properties.
            // The MainWindow will override these.
            rwEngine->SetIgnoreSerializationBlockRegions( true );
            rwEngine->SetIgnoreSecureWarnings( false );

            rwEngine->SetWarningLevel( 3 );

            rwEngine->SetCompatTransformNativeImaging( true );
            rwEngine->SetPreferPackedSampleExport( true );

            rwEngine->SetDXTRuntime( rw::DXTRUNTIME_SQUISH );
            rwEngine->SetPaletteRuntime( rw::PALRUNTIME_PNGQUANT );

            // Give RenderWare some info about us!
            rw::softwareMetaInfo metaInfo;
            metaInfo.applicationName = "Magic.TXD";
            metaInfo.applicationVersion = MTXD_VERSION_STRING;
            metaInfo.description = "by DK22Pac and The_GTA (https://github.com/quiret/magic-txd)";

            rwEngine->SetApplicationInfo( metaInfo );

            // Initialize the filesystem.
            fs_construction_params fsParams;
            fsParams.nativeExecMan = nativeExec;
            fsParams.fileRootPath = "//";

            CFileSystem *fsHandle = CFileSystem::Create( fsParams );

            if ( !fsHandle )
            {
                throw std::exception(); // "failed to initialize the FileSystem module"
            }

            try
            {
                // Create a translator whose root is placed in the application's directory.
                FileSystem::fileTrans sysAppRoot = fsHandle->CreateTranslator( fsParams.fileRootPath );

                if ( !sysAppRoot.is_good() )
                {
                    throw std::exception();
                }

                ::sysAppRoot = sysAppRoot;

                if ( fileRoot == nullptr )
                {
                    throw std::exception();
                }

                // Set our application root translator to outbreak mode.
                // This is important because we need full access to the machine.
                fileRoot->SetOutbreakEnabled( true );

                // Put our application path as first source of fresh files.
                register_file_translator( fileRoot );

                // Register embedded resources if present.
                initialize_embedded_resources();

                try
                {
                    // removed library path stuff, because we statically link.

                    MagicTXDApplication a(argc, argv);
                    {
                        QString styleSheet = styles::get(a.applicationDirPath(), "resources/dark.shell");

                        if ( styleSheet.isEmpty() )
                        {
                            important_message(
                                "Failed to load stylesheet resource \"resources/dark.shell\".\n" \
                                "Please verify whether you have installed Magic.TXD correctly!",
                                "Error"
                            );

                            // Even without stylesheet, we allow launching Magic.TXD.
                            // The user gets what he asked for.
                        }
                        else
                        {
                            a.setStyleSheet(styleSheet);
                        }
                    }
                    mainWindowConstructor wnd_constr(a.applicationDirPath(), rwEngine, fsHandle);

                    rw::RwStaticMemAllocator memAlloc;

                    MainWindow *w = mainWindowFactory.ConstructTemplate(memAlloc, wnd_constr);

                    if ( w == nullptr )
                    {
                        throw rw::RwException( "Failed to construct the Qt MainWindow" );
                    }

                    try
                    {
                        w->setWindowIcon(QIcon(w->makeAppPath("resources/icons/stars.png")));
                        w->show();

                        w->launchDetails();

                        QApplication::processEvents();

                        QStringList appargs = a.arguments();

                        if (appargs.size() >= 2) {
                            QString txdFileToBeOpened = appargs.at(1);
                            if (!txdFileToBeOpened.isEmpty()) {
                                w->openTxdFile(txdFileToBeOpened);

                                w->adjustDimensionsByViewport();
                            }
                        }

                        // Try to catch some known C++ exceptions and display things for them.
                        try
                        {
                            iRet = a.exec();
                        }
                        catch( rw::RwException& except )
                        {
                            auto errMsg = "uncaught RenderWare exception: " + except.message;

                            important_message(
                                errMsg.GetConstString(),
                                "Uncaught C++ Exception"
                            );

                            // Continue execution.
                            iRet = -1;
                        }
                        catch( std::exception& except )
                        {
                            std::string errMsg = std::string( "uncaught C++ STL exception: " ) + except.what();

                            important_message(
                                errMsg.c_str(),
                                "Uncaught C++ Exception"
                            );

                            // Continue execution.
                            iRet = -2;
                        }
                    }
                    catch( ... )
                    {
                        mainWindowFactory.Destroy( memAlloc, w );

                        throw;
                    }

                    mainWindowFactory.Destroy(memAlloc, w);
                }
                catch( ... )
                {
                    shutdown_embedded_resources();

                    throw;
                }

                shutdown_embedded_resources();
            }
            catch( ... )
            {
                CFileSystem::Destroy( fsHandle );

                throw;
            }

            CFileSystem::Destroy( fsHandle );
        }
        catch( ... )
        {
            rw::DeleteEngine( rwEngine );

            throw;
        }

        rw::DeleteEngine( rwEngine );
    }
    catch( rw::RwException& except )
    {
        auto errMsg = "uncaught RenderWare error during init: " + except.message;

        important_message(
            errMsg.GetConstString(),
            "Uncaught C++ Exception"
        );

        // Just continue.
    }
    catch( std::exception& except )
    {
        std::string errMsg = std::string( "uncaught C++ STL error during init: " ) + except.what();

        important_message(
            errMsg.c_str(),
            "Uncaught C++ Exception"
        );

        // Just continue.
    }
    catch( ... )
    {
#ifdef _DEBUG
        auto cur_except = std::current_exception();
#endif //_DEBUG

#ifdef _DEBUG
        // We want to be notified about any uncaught exceptions in DEBUG mode.
        throw;
#else
        important_message(
            "Magic.TXD has encountered an unknown C++ exception and was forced to close. Please report this to the developers with appropriate steps to reproduce.",
            "Uncaught C++ Exception"
        );

        // Continue for safe quit.

#endif //_DEBUG
    }

    DbgHeap_Validate();

    // Unregister main plugins.
    unregisterQtFileSystem();

    return iRet;
}
