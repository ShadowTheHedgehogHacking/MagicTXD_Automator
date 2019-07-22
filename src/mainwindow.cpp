#include "mainwindow.h"
#include "texinfoitem.h"
#include <QtWidgets/QCommonStyle>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/qsplitter.h>
#include <QtGui/qmovie.h>
#include <QtWidgets/QFileDialog>
#include <QtCore/QDir>
#include <QtGui/QDesktopServices>
#include <QtGui/qdrag.h>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDragLeaveEvent>
#include <QtGui/QDropEvent>
#include <QtCore/qmimedata.h>

#include "styles.h"
#include "rwversiondialog.h"
#include "texnamewindow.h"
#include "renderpropwindow.h"
#include "resizewindow.h"
#include "massconvert.h"
#include "exportallwindow.h"
#include "massexport.h"
#include "massbuild.h"
#include "optionsdialog.h"
#include "createtxddlg.h"
#include "languages.h"
//#include "platformselwindow.h"

#include "tools/txdgen.h"
#include "tools/imagepipe.hxx"

#include "qtrwutils.hxx"

#define FONT_SIZE_MENU_PX 26

#define MAIN_MIN_WIDTH 700
#define MAIN_WIDTH 800
#define MAIN_MIN_HEIGHT 300
#define MAIN_HEIGHT 560

MainWindow::MainWindow(QString appPath, rw::Interface *engineInterface, CFileSystem *fsHandle, QWidget *parent) :
    QMainWindow(parent),
    rwWarnMan( this )
{
    m_appPath = appPath;
    m_appPathForStyleSheet = appPath;
    m_appPathForStyleSheet.replace('\\', '/');
    // Initialize variables.
    this->currentTXD = nullptr;
    this->txdNameLabel = nullptr;
    this->currentSelectedTexture = nullptr;
    this->txdLog = nullptr;
    this->verDlg = nullptr;

    this->texNameDlg = nullptr;
    this->renderPropDlg = nullptr;
    this->resizeDlg = nullptr;
    //this->platformDlg = nullptr;
    this->aboutDlg = nullptr;
    this->optionsDlg = nullptr;
    this->rwVersionButton = nullptr;
    this->recheckingThemeItem = false;

    this->recommendedTxdPlatform = "Direct3D9";

    // Initialize configuration to default.
    {
        this->lastTXDOpenDir = QDir::current().absolutePath();
        this->lastTXDSaveDir = this->lastTXDOpenDir;
        this->lastImageFileOpenDir = this->makeAppPath( "" );
        this->lastLanguageFileName = "";
        this->addImageGenMipmaps = true;
        this->lockDownTXDPlatform = true;
        this->adjustTextureChunksOnImport = true;
        this->texaddViewportFill = false;
        this->texaddViewportScaled = true;
        this->texaddViewportBackground = false;
        this->isLaunchedForTheFirstTime = true;
        this->showLogOnWarning = true;
        this->showGameIcon = true;
        this->lastUsedAllExportFormat = "PNG";
        this->lastAllExportTarget = qt_to_widerw( this->makeAppPath( "" ) );
    }

    this->wasTXDModified = false;

    this->showFullImage = false;
    this->drawMipmapLayers = false;
	this->showBackground = false;

    this->hasOpenedTXDFileInfo = false;

    this->rwEngine = engineInterface;

    // Set-up the warning manager.
    this->rwEngine->SetWarningManager( &this->rwWarnMan );

    this->fileSystem = fsHandle;

    try
    {
	    /* --- Window --- */
        updateWindowTitle();
        //setMinimumSize(620, 300);
	    //resize(800, 600);

        // We do support drag and drop.
        this->setAcceptDrops( true );

        SetupWindowSize(this, MAIN_WIDTH, MAIN_HEIGHT, MAIN_MIN_WIDTH, MAIN_MIN_HEIGHT);

	    /* --- Log --- */
	    this->txdLog = new TxdLog(this, this->m_appPath, this);

	    /* --- List --- */
	    QListWidget *listWidget = new QListWidget();
	    listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		//listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        listWidget->setMaximumWidth(350);
	    //listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);

        connect( listWidget, &QListWidget::currentItemChanged, this, &MainWindow::onTextureItemChanged );

        // We will store all our texture names in this.
        this->textureListWidget = listWidget;

	    /* --- Viewport --- */
		imageView = new TexViewportWidget(this);
		imageView->setFrameShape(QFrame::NoFrame);
		imageView->setObjectName("textureViewBackground");
		imageWidget = new QLabel;
		//imageWidget->setObjectName("transparentBackground"); // "chessBackground" > grid background
		imageWidget->setStyleSheet("background-color: rgba(255, 255, 255, 0);");
		imageView->setWidget(imageWidget);
		imageView->setAlignment(Qt::AlignCenter);

	    /* --- Splitter --- */
        mainSplitter = new QSplitter;
	    mainSplitter->addWidget(listWidget);
		mainSplitter->addWidget(imageView);
	    QList<int> sizes;
	    sizes.push_back(200);
	    sizes.push_back(mainSplitter->size().width() - 200);
        mainSplitter->setSizes(sizes);
        mainSplitter->setChildrenCollapsible(false);

	    /* --- Top panel --- */
	    QWidget *txdNameBackground = new QWidget();
	    txdNameBackground->setFixedHeight(60);
	    txdNameBackground->setObjectName("background_0");
	    QLabel *txdName = new QLabel();
	    txdName->setObjectName("label36px");
	    txdName->setAlignment(Qt::AlignCenter);

        this->txdNameLabel = txdName;

        QGridLayout *txdNameLayout = new QGridLayout();
        QLabel *starsBox = new QLabel;
        starsMovie = new QMovie;

        // set default theme movie
        starsMovie->setFileName(makeAppPath("resources/dark/stars.gif"));
        starsBox->setMovie(starsMovie);
        starsMovie->start();
        txdNameLayout->addWidget(starsBox, 0, 0);
        txdNameLayout->addWidget(txdName, 0, 0);
        txdNameLayout->setContentsMargins(0, 0, 0, 0);
        txdNameLayout->setMargin(0);
        txdNameLayout->setSpacing(0);
        txdNameBackground->setLayout(txdNameLayout);

	    QWidget *txdOptionsBackground = new QWidget();
	    txdOptionsBackground->setFixedHeight(54);
	    txdOptionsBackground->setObjectName("background_1");

	    /* --- Menu --- */
	    QMenuBar *menu = new QMenuBar;

	    fileMenu = menu->addMenu("");
        QAction *actionNew = CreateMnemonicActionL( "Main.File.New", this );
        actionNew->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_N ) );
        fileMenu->addAction(actionNew);

        this->actionNewTXD = actionNew;

        connect( actionNew, &QAction::triggered, this, &MainWindow::onCreateNewTXD );

	    QAction *actionOpen = CreateMnemonicActionL( "Main.File.Open", this );
        actionOpen->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_O ) );
	    fileMenu->addAction(actionOpen);

        this->actionOpenTXD = actionOpen;

        connect( actionOpen, &QAction::triggered, this, &MainWindow::onOpenFile );

	    QAction *actionSave = CreateMnemonicActionL( "Main.File.Save", this );
        actionSave->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_S ) );
	    fileMenu->addAction(actionSave);

        this->actionSaveTXD = actionSave;

        connect( actionSave, &QAction::triggered, this, &MainWindow::onRequestSaveTXD );

	    QAction *actionSaveAs = CreateMnemonicActionL( "Main.File.SaveAs", this );
        actionSaveAs->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_A ) );
	    fileMenu->addAction(actionSaveAs);

        this->actionSaveTXDAs = actionSaveAs;

        connect( actionSaveAs, &QAction::triggered, this, &MainWindow::onRequestSaveAsTXD );

	    QAction *closeCurrent = CreateMnemonicActionL( "Main.File.Close", this );
	    fileMenu->addAction(closeCurrent);
	    fileMenu->addSeparator();

        this->actionCloseTXD = closeCurrent;

        connect( closeCurrent, &QAction::triggered, this, &MainWindow::onCloseCurrent );

	    QAction *actionQuit = CreateMnemonicActionL( "Main.File.Quit", this );
	    fileMenu->addAction(actionQuit);

	    editMenu = menu->addMenu("");
	    QAction *actionAdd = CreateMnemonicActionL( "Main.Edit.Add", this );
        actionAdd->setShortcut( Qt::Key_Insert );
	    editMenu->addAction(actionAdd);

        this->actionAddTexture = actionAdd;

        connect( actionAdd, &QAction::triggered, this, &MainWindow::onAddTexture );

	    QAction *actionReplace = CreateMnemonicActionL( "Main.Edit.Replace", this );
        actionReplace->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_R ) );
	    editMenu->addAction(actionReplace);

        this->actionReplaceTexture = actionReplace;

        connect( actionReplace, &QAction::triggered, this, &MainWindow::onReplaceTexture );

	    QAction *actionRemove = CreateMnemonicActionL( "Main.Edit.Remove", this );
        actionRemove->setShortcut( Qt::Key_Delete );
	    editMenu->addAction(actionRemove);

        this->actionRemoveTexture = actionRemove;

        connect( actionRemove, &QAction::triggered, this, &MainWindow::onRemoveTexture );

	    QAction *actionRename = CreateMnemonicActionL( "Main.Edit.Rename", this );
        actionRename->setShortcut( Qt::Key_F2 );
	    editMenu->addAction(actionRename);

        this->actionRenameTexture = actionRename;

        connect( actionRename, &QAction::triggered, this, &MainWindow::onRenameTexture );

	    QAction *actionResize = CreateMnemonicActionL( "Main.Edit.Resize", this );
        actionResize->setShortcut( QKeySequence( Qt::ALT | Qt::Key_S ) );
	    editMenu->addAction(actionResize);

        this->actionResizeTexture = actionResize;

        connect( actionResize, &QAction::triggered, this, &MainWindow::onResizeTexture );

        QAction *actionManipulate = CreateMnemonicActionL( "Main.Edit.Modify", this );
        actionManipulate->setShortcut( Qt::Key_M );
        editMenu->addAction(actionManipulate);

        this->actionManipulateTexture = actionManipulate;

        connect( actionManipulate, &QAction::triggered, this, &MainWindow::onManipulateTexture );

	    QAction *actionSetupMipLevels = CreateMnemonicActionL( "Main.Edit.SetupML", this );
        actionSetupMipLevels->setShortcut( Qt::CTRL | Qt::Key_M );
	    editMenu->addAction(actionSetupMipLevels);

        this->actionSetupMipmaps = actionSetupMipLevels;

        connect( actionSetupMipLevels, &QAction::triggered, this, &MainWindow::onSetupMipmapLayers );

        QAction *actionClearMipLevels = CreateMnemonicActionL( "Main.Edit.ClearML", this );
        actionClearMipLevels->setShortcut( Qt::CTRL | Qt::Key_C );
        editMenu->addAction(actionClearMipLevels);

        this->actionClearMipmaps = actionClearMipLevels;

        connect( actionClearMipLevels, &QAction::triggered, this, &MainWindow::onClearMipmapLayers );

	    QAction *actionSetupRenderingProperties = CreateMnemonicActionL( "Main.Edit.SetupRP", this );
	    editMenu->addAction(actionSetupRenderingProperties);

        this->actionRenderProps = actionSetupRenderingProperties;

        connect( actionSetupRenderingProperties, &QAction::triggered, this, &MainWindow::onSetupRenderingProps );

#ifndef _FEATURES_NOT_IN_CURRENT_RELEASE

	    editMenu->addSeparator();
	    QAction *actionViewAllChanges = new QAction("&View all changes", this);
	    editMenu->addAction(actionViewAllChanges);

        this->actionViewAllChanges = actionViewAllChanges;

	    QAction *actionCancelAllChanges = new QAction("&Cancel all changes", this);
	    editMenu->addAction(actionCancelAllChanges);

        this->actionCancelAllChanges = actionCancelAllChanges;

	    editMenu->addSeparator();
	    QAction *actionAllTextures = new QAction("&All textures", this);
	    editMenu->addAction(actionAllTextures);

        this->actionAllTextures = actionAllTextures;

#endif //_FEATURES_NOT_IN_CURRENT_RELEASE

	    editMenu->addSeparator();
	    QAction *actionSetupTxdVersion = CreateMnemonicActionL( "Main.Edit.SetupTV", this );
	    editMenu->addAction(actionSetupTxdVersion);

        this->actionSetupTXDVersion = actionSetupTxdVersion;

		connect(actionSetupTxdVersion, &QAction::triggered, this, &MainWindow::onSetupTxdVersion);

        editMenu->addSeparator();

        QAction *actionShowOptions = CreateMnemonicActionL( "Main.Edit.Options", this );
        editMenu->addAction(actionShowOptions);

        this->actionShowOptions = actionShowOptions;

        connect(actionShowOptions, &QAction::triggered, this, &MainWindow::onShowOptions);

        toolsMenu = menu->addMenu("");

        QAction *actionMassConvert = CreateMnemonicActionL( "Main.Tools.MassCnv", this );
        toolsMenu->addAction(actionMassConvert);

        connect( actionMassConvert, &QAction::triggered, this, &MainWindow::onRequestMassConvert );

        QAction *actionMassExport = CreateMnemonicActionL( "Main.Tools.MassExp", this );
        toolsMenu->addAction(actionMassExport);

        connect( actionMassExport, &QAction::triggered, this, &MainWindow::onRequestMassExport );

        QAction *actionMassBuild = CreateMnemonicActionL( "Main.Tools.MassBld", this );
        toolsMenu->addAction(actionMassBuild);

        connect( actionMassBuild, &QAction::triggered, this, &MainWindow::onRequestMassBuild );

	    exportMenu = menu->addMenu("");

        // We should check if formats are available first :)
        if ( rw::IsImagingFormatAvailable( rwEngine, "PNG" ) )
        {
            this->addTextureFormatExportLinkToMenu( exportMenu, "PNG", "PNG", "Portable Network Graphics" );
        }

        // RWTEX should always be available. Otherwise there'd be no purpose in Magic.TXD :p
        this->addTextureFormatExportLinkToMenu( exportMenu, "RWTEX", "RWTEX", "RW Texture Chunk" );

        if ( rw::IsNativeImageFormatAvailable( rwEngine, "DDS" ) )
        {
            this->addTextureFormatExportLinkToMenu( exportMenu, "DDS", "DDS", "DirectDraw Surface" );
        }

        if ( rw::IsNativeImageFormatAvailable( rwEngine, "PVR" ) )
        {
            this->addTextureFormatExportLinkToMenu( exportMenu, "PVR", "PVR", "PowerVR Image" );
        }

        if ( rw::IsImagingFormatAvailable( rwEngine, "BMP" ) )
        {
            this->addTextureFormatExportLinkToMenu( exportMenu, "BMP", "BMP", "Raw Bitmap" );
        }

        // Add remaining formats that rwlib supports.
        {
            rw::registered_image_formats_t regFormats;

            rw::GetRegisteredImageFormats( this->rwEngine, regFormats );

            for ( const rw::registered_image_format& theFormat : regFormats )
            {
                rw::uint32 num_ext = theFormat.num_ext;
                const rw::imaging_filename_ext *ext_array = theFormat.ext_array;

                // Decide what the most friendly name of this format is.
                // The friendly name is the longest extension available.
                const char *displayName =
                    rw::GetLongImagingFormatExtension( num_ext, ext_array );

                const char *defaultExt = nullptr;

                bool gotDefaultExt = rw::GetDefaultImagingFormatExtension( theFormat.num_ext, theFormat.ext_array, defaultExt );

                if ( gotDefaultExt && displayName != nullptr )
                {
                    if ( !StringEqualToZero( defaultExt, "PNG", false ) &&
                         !StringEqualToZero( defaultExt, "DDS", false ) &&
                         !StringEqualToZero( defaultExt, "PVR", false ) &&
                         !StringEqualToZero( defaultExt, "BMP", false ) )
                    {
                        this->addTextureFormatExportLinkToMenu( exportMenu, displayName, defaultExt, theFormat.formatName );
                    }

                    // We want to cache the available formats.
                    registered_image_format imgformat;

                    imgformat.formatName = theFormat.formatName;
                    imgformat.defaultExt = defaultExt;

                    for ( rw::uint32 n = 0; n < theFormat.num_ext; n++ )
                    {
                        imgformat.ext_array.push_back( std::string( theFormat.ext_array[ n ].ext ) );
                    }

                    imgformat.isNativeFormat = false;

                    this->reg_img_formats.push_back( std::move( imgformat ) );
                }
            }

            // Also add image formats from native texture types.
            rw::registered_image_formats_t regNatImgTypes;

            rw::GetRegisteredNativeImageTypes( engineInterface, regNatImgTypes );

            for ( const rw::registered_image_format& info : regNatImgTypes )
            {
                const char *defaultExt = nullptr;

                if ( rw::GetDefaultImagingFormatExtension( info.num_ext, info.ext_array, defaultExt ) )
                {
                    registered_image_format imgformat;

                    imgformat.formatName = info.formatName;
                    imgformat.defaultExt = defaultExt;
                    imgformat.isNativeFormat = true;

                    // Add all extensions to the array.
                    {
                        size_t extCount = info.num_ext;

                        for ( size_t n = 0; n < extCount; n++ )
                        {
                            const char *extName = info.ext_array[ n ].ext;

                            imgformat.ext_array.push_back( extName );
                        }
                    }

                    this->reg_img_formats.push_back( std::move( imgformat ) );
                }
            }
        }

#ifndef _FEATURES_NOT_IN_CURRENT_RELEASE

	    QAction *actionExportTTXD = new QAction("&Text-based TXD", this);
	    exportMenu->addAction(actionExportTTXD);

        this->actionsExportImage.push_back( actionExportTTXD );

#endif //_FEATURES_NOT_IN_CURRENT_RELEASE

	    exportMenu->addSeparator();
	    QAction *actionExportAll = CreateMnemonicActionL( "Main.Export.ExpAll", this );
	    exportMenu->addAction(actionExportAll);

        this->exportAllImages = actionExportAll;

        connect( actionExportAll, &QAction::triggered, this, &MainWindow::onExportAllTextures );

	    viewMenu = menu->addMenu("");

        QAction *actionShowFullImage = CreateMnemonicActionL( "Main.View.FullImg", this);
        // actionBackground->setShortcut(Qt::Key_F4);
        actionShowFullImage->setCheckable(true);
        viewMenu->addAction(actionShowFullImage);

        connect(actionShowFullImage, &QAction::triggered, this, &MainWindow::onToggleShowFullImage);

	    QAction *actionBackground = CreateMnemonicActionL( "Main.View.Backgr", this);
        actionBackground->setShortcut( Qt::Key_F5 );
		actionBackground->setCheckable(true);
	    viewMenu->addAction(actionBackground);

		connect(actionBackground, &QAction::triggered, this, &MainWindow::onToggleShowBackground);

#ifndef _FEATURES_NOT_IN_CURRENT_RELEASE

	    QAction *action3dView = new QAction("&3D View", this);
		action3dView->setCheckable(true);
	    viewMenu->addAction(action3dView);

#endif //_FEATURES_NOT_IN_CURRENT_RELEASE

	    QAction *actionShowMipLevels = CreateMnemonicActionL( "Main.View.DispML", this );
        actionShowMipLevels->setShortcut( Qt::Key_F6 );
		actionShowMipLevels->setCheckable(true);
	    viewMenu->addAction(actionShowMipLevels);

        connect( actionShowMipLevels, &QAction::triggered, this, &MainWindow::onToggleShowMipmapLayers );

        QAction *actionShowLog = CreateMnemonicActionL( "Main.View.ShowLog", this );
        actionShowLog->setShortcut( Qt::Key_F7 );
        viewMenu->addAction(actionShowLog);

        connect( actionShowLog, &QAction::triggered, this, &MainWindow::onToggleShowLog );

	    viewMenu->addSeparator();

        this->actionThemeDark = CreateMnemonicActionL( "Main.View.DarkThm", this );
        this->actionThemeDark->setCheckable(true);
        this->actionThemeLight = CreateMnemonicActionL( "Main.View.LightTm", this );
        this->actionThemeLight->setCheckable(true);

        // enable needed theme in menu before connecting a slot
        actionThemeDark->setChecked(true);

        connect(this->actionThemeDark, &QAction::triggered, this, &MainWindow::onToogleDarkTheme);
        connect(this->actionThemeLight, &QAction::triggered, this, &MainWindow::onToogleLightTheme);

        viewMenu->addAction(this->actionThemeDark);
        viewMenu->addAction(this->actionThemeLight);

	    actionQuit->setShortcut(QKeySequence::Quit);
	    connect(actionQuit, &QAction::triggered, this, &MainWindow::close);

        infoMenu = menu->addMenu("");

        QAction *actionOpenWebsite = CreateMnemonicActionL( "Main.Info.Website", this );
        infoMenu->addAction(actionOpenWebsite);

        connect( actionOpenWebsite, &QAction::triggered, this, &MainWindow::onRequestOpenWebsite );

        infoMenu->addSeparator();

        QAction *actionAbout = CreateMnemonicActionL( "Main.Info.About", this );
        infoMenu->addAction(actionAbout);

        connect( actionAbout, &QAction::triggered, this, &MainWindow::onAboutUs );

	    QHBoxLayout *hlayout = new QHBoxLayout;
	    txdOptionsBackground->setLayout(hlayout);
	    hlayout->setMenuBar(menu);

        // Layout for rw version, with right-side alignment
        QHBoxLayout *rwVerLayout = new QHBoxLayout;
        rwVersionButton = new QPushButton;
        rwVersionButton->setObjectName("rwVersionButton");
        rwVersionButton->setMaximumWidth(100);
        rwVersionButton->hide();
        rwVerLayout->addWidget(rwVersionButton);
        rwVerLayout->setAlignment(Qt::AlignRight);

        connect( rwVersionButton, &QPushButton::clicked, this, &MainWindow::onSetupTxdVersion );

        // Layout to mix menu and rw version label/button
        QGridLayout *menuVerLayout = new QGridLayout();
        menuVerLayout->addWidget(txdOptionsBackground, 0, 0);
        menuVerLayout->addLayout(rwVerLayout, 0, 0, Qt::AlignRight);
        menuVerLayout->setContentsMargins(0, 0, 0, 0);
        menuVerLayout->setMargin(0);
        menuVerLayout->setSpacing(0);

	    QWidget *hLineBackground = new QWidget();
	    hLineBackground->setFixedHeight(1);
	    hLineBackground->setObjectName("hLineBackground");

	    QVBoxLayout *topLayout = new QVBoxLayout;
	    topLayout->addWidget(txdNameBackground);
        topLayout->addLayout(menuVerLayout);
	    topLayout->addWidget(hLineBackground);
	    topLayout->setContentsMargins(0, 0, 0, 0);
	    topLayout->setMargin(0);
	    topLayout->setSpacing(0);

	    /* --- Bottom panel --- */
	    QWidget *hLineBackground2 = new QWidget;
	    hLineBackground2->setFixedHeight(1);
	    hLineBackground2->setObjectName("hLineBackground");
	    QWidget *txdOptionsBackground2 = new QWidget;
	    txdOptionsBackground2->setFixedHeight(59);
	    txdOptionsBackground2->setObjectName("background_1");

        /* --- Friendly Icons --- */
        QHBoxLayout *friendlyIconRow = new QHBoxLayout();
        friendlyIconRow->setContentsMargins( 0, 0, 15, 0 );
        friendlyIconRow->setAlignment( Qt::AlignRight | Qt::AlignVCenter );

        this->friendlyIconRow = friendlyIconRow;

        QLabel *friendlyIconGame = new QLabel();
        friendlyIconGame->setObjectName("label25px_dim");
        friendlyIconGame->setVisible( false );

        this->friendlyIconGame = friendlyIconGame;

        friendlyIconRow->addWidget( friendlyIconGame );

        QWidget *friendlyIconSeparator = new QWidget();
        friendlyIconSeparator->setFixedWidth(1);
        friendlyIconSeparator->setObjectName( "friendlyIconSeparator" );
        friendlyIconSeparator->setVisible( false );

        this->friendlyIconSeparator = friendlyIconSeparator;

        friendlyIconRow->addWidget( friendlyIconSeparator );

        QLabel *friendlyIconPlatform = new QLabel();
        friendlyIconPlatform->setObjectName("label25px_dim");
        friendlyIconPlatform->setVisible( false );

        this->friendlyIconPlatform = friendlyIconPlatform;

        friendlyIconRow->addWidget( friendlyIconPlatform );

        txdOptionsBackground2->setLayout( friendlyIconRow );

	    QVBoxLayout *bottomLayout = new QVBoxLayout;
	    bottomLayout->addWidget(hLineBackground2);
	    bottomLayout->addWidget(txdOptionsBackground2);
	    bottomLayout->setContentsMargins(0, 0, 0, 0);
	    bottomLayout->setMargin(0);
	    bottomLayout->setSpacing(0);

	    /* --- Main layout --- */
	    QVBoxLayout *mainLayout = new QVBoxLayout;
	    mainLayout->addLayout(topLayout);
	    mainLayout->addWidget(mainSplitter);
	    mainLayout->addLayout(bottomLayout);

	    mainLayout->setContentsMargins(0, 0, 0, 0);
	    mainLayout->setMargin(0);
	    mainLayout->setSpacing(0);

	    QWidget *window = new QWidget();
	    window->setLayout(mainLayout);

        window->setObjectName("background_0");
        setObjectName("background_0");

	    setCentralWidget(window);

		imageWidget->hide();

        // Read data files

        this->versionSets.readSetsFile(this->makeAppPath("data/versionsets.dat"));

        //

		// Initialize our native formats.
		this->initializeNativeFormats();

        // Initialize the GUI.
        this->UpdateAccessibility();

        RegisterTextLocalizationItem( this );
    }
    catch( ... )
    {
        rwEngine->SetWarningManager( nullptr );

        throw;
    }
}

template <typename objType>
inline void SafeDelete( objType *ptr )
{
    if ( ptr )
    {
        delete ptr;
    }
}

MainWindow::~MainWindow()
{
    UnregisterTextLocalizationItem( this );

    // If we have a loaded TXD, get rid of it.
    if ( this->currentTXD )
    {
        this->rwEngine->DeleteRwObject( this->currentTXD );

        this->currentTXD = nullptr;
    }

    // DELETE ALL SUB DIALOGS THAT DEPEND ON MAINWINDOW HERE.

    SafeDelete( txdLog );
    SafeDelete( verDlg );
    SafeDelete( texNameDlg );
    SafeDelete( renderPropDlg );
    SafeDelete( resizeDlg );
    //SafeDelete( platformDlg );
    SafeDelete( aboutDlg );
    SafeDelete( optionsDlg );

    // Kill any sub windows.
    // Less dangerous than killing by language context.
    {
        QObjectList children = this->children();

        for ( QObject *obj : children )
        {
            if ( QDialog *subDlg = dynamic_cast <QDialog*> ( obj ) )
            {
                // Kills off any remnants that could depend on the main window.
                delete subDlg;
            }
        }
    }

    // Cannot do this, because it is VERY dangerous.
#if 0
    // Kill off localization items that are dialogs and not the main window.
    {
        localizations_t culturalItems = GetTextLocalizationItems();

        for ( magicTextLocalizationItem *locale_obj : culturalItems )
        {
            if ( locale_obj != this )
            {
                if ( QDialog *dlg = dynamic_cast <QDialog*> ( locale_obj ) )
                {
                    delete dlg;
                }
            }
        }
    }
#endif

    // Remove the warning manager again.
    this->rwEngine->SetWarningManager( NULL );

    // Shutdown the native format handlers.
    this->shutdownNativeFormats();
}

void MainWindow::updateContent( MainWindow *mainWnd )
{
    unsigned int menuLineWidth = 0;

    QString sFileMenu = MAGIC_TEXT("Main.File");
    menuLineWidth += GetTextWidthInPixels(sFileMenu, FONT_SIZE_MENU_PX);

    fileMenu->setTitle( "&" + sFileMenu );

    QString sEditMenu = MAGIC_TEXT("Main.Edit");
    menuLineWidth += GetTextWidthInPixels(sEditMenu, FONT_SIZE_MENU_PX);

    editMenu->setTitle( "&" + sEditMenu );

    QString sToolsMenu = MAGIC_TEXT("Main.Tools");
    menuLineWidth += GetTextWidthInPixels(sToolsMenu, FONT_SIZE_MENU_PX);

    toolsMenu->setTitle( "&" + sToolsMenu );

    QString sExportMenu = MAGIC_TEXT("Main.Export");
    menuLineWidth += GetTextWidthInPixels(sExportMenu, FONT_SIZE_MENU_PX);

    exportMenu->setTitle( sExportMenu );

    QString sViewMenu = MAGIC_TEXT("Main.View");
    menuLineWidth += GetTextWidthInPixels(sViewMenu, FONT_SIZE_MENU_PX);

    viewMenu->setTitle( sViewMenu );

    QString sInfoMenu = MAGIC_TEXT("Main.Info");
    menuLineWidth += GetTextWidthInPixels(sInfoMenu, FONT_SIZE_MENU_PX);

    infoMenu->setTitle( sInfoMenu );

    menuLineWidth += 240; // space between menu items ( 5 * 40 ) + 20 + 20
    menuLineWidth += 100;  // buttons size

    RecalculateWindowSize(this, menuLineWidth, MAIN_MIN_WIDTH, MAIN_MIN_HEIGHT);
}

// We want to have some help tokens in the main window too.
struct mainWindowHelpEnv
{
    inline void Initialize( MainWindow *mainWnd )
    {
        RegisterHelperWidget( mainWnd, "mgbld_welcome", eHelperTextType::DIALOG_WITH_TICK, "Tools.MassBld.Welcome", true );
    }

    inline void Shutdown( MainWindow *mainWnd )
    {
        UnregisterHelperWidget( mainWnd, "mgbld_welcome" );
    }
};

void InitializeMainWindowHelpEnv( void )
{
    mainWindowFactory.RegisterDependantStructPlugin <mainWindowHelpEnv> ();
}

void MainWindow::addTextureFormatExportLinkToMenu( QMenu *theMenu, const char *displayName, const char *defaultExt, const char *formatName )
{
    TextureExportAction *formatActionExport = new TextureExportAction( defaultExt, displayName, QString( formatName ), this );
    theMenu->addAction( formatActionExport );

    this->actionsExportItems.push_back( formatActionExport );

    // Connect it to the export signal handler.
    connect( formatActionExport, &QAction::triggered, this, &MainWindow::onExportTexture );
}

void MainWindow::UpdateExportAccessibility( void )
{
    // Export options are available depending on what texture has been selected.
    bool has_txd = ( this->currentTXD != nullptr );

    for ( TextureExportAction *exportAction : this->actionsExportItems )
    {
        bool shouldEnable = has_txd;

        if ( shouldEnable )
        {
            // We should only enable if the currently selected texture actually supports us.
            bool hasSupport = false;

            if ( !hasSupport )
            {
                try
                {
                    if ( TexInfoWidget *curSelTex = this->currentSelectedTexture )
                    {
                        if ( rw::Raster *texRaster = curSelTex->GetTextureHandle()->GetRaster() )
                        {
                            std::string ansiMethodName = qt_to_ansi( exportAction->displayName );

                            if ( StringEqualToZero( ansiMethodName.c_str(), "RWTEX", false ) )
                            {
                                hasSupport = true;
                            }
                            else
                            {
                                hasSupport = texRaster->supportsImageMethod( ansiMethodName.c_str() );
                            }
                        }
                    }
                }
                catch( rw::RwException& )
                {
                    // If we failed to request support capability, we just abort.
                    hasSupport = false;
                }
            }

            if ( !hasSupport )
            {
                // No texture item selected means we cannot export anyway.
                shouldEnable = false;
            }
        }

        exportAction->setDisabled( !shouldEnable );
    }

    this->exportAllImages->setDisabled( !has_txd );
}

void MainWindow::UpdateAccessibility( void )
{
    // If we have no TXD available, we should not allow the user to pick TXD related options.
    bool has_txd = ( this->currentTXD != nullptr );

    this->actionSaveTXD->setDisabled( !has_txd );
    this->actionSaveTXDAs->setDisabled( !has_txd );
    this->actionCloseTXD->setDisabled( !has_txd );
    this->actionAddTexture->setDisabled( !has_txd );
    this->actionReplaceTexture->setDisabled( !has_txd );
    this->actionRemoveTexture->setDisabled( !has_txd );
    this->actionRenameTexture->setDisabled( !has_txd );
    this->actionResizeTexture->setDisabled( !has_txd );
    this->actionManipulateTexture->setDisabled( !has_txd );
    this->actionSetupMipmaps->setDisabled( !has_txd );
    this->actionClearMipmaps->setDisabled( !has_txd );
    this->actionRenderProps->setDisabled( !has_txd );
#ifndef _FEATURES_NOT_IN_CURRENT_RELEASE
    this->actionViewAllChanges->setDisabled( !has_txd );
    this->actionCancelAllChanges->setDisabled( !has_txd );
    this->actionAllTextures->setDisabled( !has_txd );
#endif //_FEATURES_NOT_IN_CURRENT_RELEASE
    this->actionSetupTXDVersion->setDisabled( !has_txd );

    this->UpdateExportAccessibility();
}

// I propose to seperate the TXD actions from this source file into a seperate file.
// Point is that the actions are entirely unrelated to Qt, just RenderWare. They
// are required to run entirely asynchronous. They need to trigger events back to
// the GUI, possibly inside RenderWare itself.
// But of course this is a very difficult task.

// Drag and drop stuff.
void MainWindow::dragEnterEvent( QDragEnterEvent *evt )
{
    // Check if we are a TXD or image file, based on the extention.
    const QMimeData *mimeStuff = evt->mimeData();

    // Basically, we receive a number of files in a drag operation.
    // We only support a single TXD file in the

    if ( mimeStuff )
    {
        rw::Interface *rwEngine = this->rwEngine;

        QList <QUrl> urls = mimeStuff->urls();

        bool areLocationsLookingGood = false;
        bool hasValidFile = false;
        bool hasTXDFile = false;

        for ( QUrl location : urls )
        {
            QString qtPath = location.toLocalFile();

            if ( qtPath.isEmpty() == false )
            {
                std::wstring widePath = qtPath.toStdWString();

                filePath extention;

                FileSystem::GetFileNameItem <FileSysCommonAllocator> ( widePath.c_str(), false, nullptr, &extention );

                if ( extention.size() != 0 )
                {
                    bool recognizedData = false;

                    bool hasNewTXDFile = false;

                    // We do support opening TXD files.
                    if ( extention.equals( L"TXD", false ) )
                    {
                        // If we had a valid file already, we quit.
                        if ( hasValidFile )
                        {
                            areLocationsLookingGood = false;
                            break;
                        }

                        recognizedData = true;

                        hasNewTXDFile = true;
                    }

                    // Recognize image data if we have an open TXD file.
                    if ( this->currentTXD )
                    {
                        eImportExpectation imp_exp = getActualImageImportExpectation( rwEngine, extention );

                        if ( imp_exp != IMPORTE_NONE )
                        {
                            recognizedData = true;
                        }
                    }

                    // If we support it, then we accept it.
                    if ( recognizedData )
                    {
                        // If we had a TXD file, there is no point.
                        if ( hasTXDFile )
                        {
                            areLocationsLookingGood = false;
                            break;
                        }

                        areLocationsLookingGood = true;

                        hasValidFile = true;
                    }

                    if ( hasNewTXDFile )
                    {
                        hasTXDFile = true;
                    }
                }
            }
        }

        if ( areLocationsLookingGood )
        {
            evt->acceptProposedAction();
        }
        else
        {
            evt->ignore();
        }
    }
}

void MainWindow::dragLeaveEvent( QDragLeaveEvent *evt )
{
    // Nothin to do here.
    return;
}

void MainWindow::dropEvent( QDropEvent *evt )
{
    // Check what kind of data we got.
    if ( const QMimeData *mimeStuff = evt->mimeData() )
    {
        rw::Interface *rwEngine = this->rwEngine;

        QList <QUrl> urls = mimeStuff->urls();

        // We want to display the image config dialog if we add just one image.
        bool isSingleFile = ( urls.size() == 1 );

        for ( QUrl location : urls )
        {
            QString qtPath = location.toLocalFile();

            if ( qtPath.isEmpty() == false )
            {
                std::wstring widePath = qtPath.toStdWString();

                filePath extention;

                filePath nameItem = FileSystem::GetFileNameItem <FileSysCommonAllocator> ( widePath.c_str(), false, NULL, &extention );

                bool hasHandledFile = false;

                // We should ignore any RW error.
                try
                {
                    // * TXD file?
                    if ( extention.equals( L"TXD", false ) )
                    {
                        bool loadedTXD = this->openTxdFile( qtPath, false );

                        if ( loadedTXD )
                        {
                            hasHandledFile = true;
                        }
                    }

                    if ( !hasHandledFile )
                    {
                        // * image file?
                        if ( rw::TexDictionary *txd = this->currentTXD )
                        {
                            if ( isSingleFile )
                            {
                                // Should verify if this is even an image file candidate.
                                eImportExpectation imp_exp = getActualImageImportExpectation( rwEngine, extention );

                                if ( imp_exp != IMPORTE_NONE )
                                {
                                    // We want to allow configuration.
                                    spawnTextureAddDialog( qtPath );
                                }
                            }
                            else
                            {
                                rw::streamConstructionFileParamW_t fileParam( widePath.c_str() );

                                rw::Stream *imgStream = rwEngine->CreateStream( rw::RWSTREAMTYPE_FILE_W, rw::RWSTREAMMODE_READONLY, &fileParam );

                                if ( imgStream )
                                {
                                    try
                                    {
                                        struct mainWindowMakeRasterImageImportMethods : public makeRasterImageImportMethods
                                        {
                                            inline mainWindowMakeRasterImageImportMethods( rw::Interface *rwEngine, MainWindow *mainWnd ) : makeRasterImageImportMethods( rwEngine )
                                            {
                                                this->mainWnd = mainWnd;
                                            }

                                            std::string GetNativeTextureName( void ) const override
                                            {
                                                return qt_to_ansi( mainWnd->GetCurrentPlatform() );
                                            }

                                            void OnWarning( rw::rwStaticString <char>&& msg ) const override
                                            {
                                                this->mainWnd->txdLog->addLogMessage( ansi_to_qt( msg ), LOGMSG_WARNING );
                                            }

                                            void OnError( rw::rwStaticString <char>&& msg ) const override
                                            {
                                                this->mainWnd->txdLog->showError( ansi_to_qt( msg ) );
                                            }

                                        private:
                                            MainWindow *mainWnd;
                                        };

                                        // Grab the image properties.
                                        mainWindowMakeRasterImageImportMethods imp_methods( rwEngine, this );

                                        if ( rw::TextureBase *rwtex = RwMakeTextureFromStream( rwEngine, imgStream, extention, imp_methods ) )
                                        {
                                            try
                                            {
                                                // Set the texture version to the TXD version.
                                                rwtex->SetEngineVersion( txd->GetEngineVersion() );

                                                // Give the texture an ANSI name.
                                                // NOTE that we overwrite any original name that the texture chunk might have come with.
                                                auto ansiTexName = nameItem.convert_ansi <rw::RwStaticMemAllocator> ();

                                                DefaultTextureAddAndPrepare( rwtex, ansiTexName.GetConstString(), "" );
                                            }
                                            catch( ... )
                                            {
                                                // Well, the texture add has failed, so we kill the thing.
                                                rwEngine->DeleteRwObject( rwtex );

                                                throw;
                                            }
                                        }
                                    }
                                    catch( ... )
                                    {
                                        rwEngine->DeleteStream( imgStream );

                                        throw;
                                    }

                                    rwEngine->DeleteStream( imgStream );
                                }
                            }
                        }
                    }
                }
                catch( rw::RwException& error )
                {
                    // Ignore it and continue.
                }
            }
        }
    }
}

void MainWindow::setCurrentTXD( rw::TexDictionary *txdObj )
{
    if ( this->currentTXD == txdObj )
        return;

    if ( this->currentTXD != nullptr )
    {
        // Make sure we have no more texture in our viewport.
		clearViewImage();

        // Since we have no selected texture, we can hide the friendly icons.
        // this->hideFriendlyIcons();

        this->currentSelectedTexture = nullptr;

        this->rwEngine->DeleteRwObject( this->currentTXD );

        this->currentTXD = nullptr;

        this->ClearModifiedState();

        // Clear anything in the GUI that represented the previous TXD.
        this->textureListWidget->clear();
    }

    if ( txdObj != nullptr )
    {
        this->currentTXD = txdObj;

        this->updateTextureList(false);
    }

    // We should update how we let the user access the GUI.
    this->UpdateAccessibility();
}

void MainWindow::updateTextureList( bool selectLastItemInList )
{
    rw::TexDictionary *txdObj = this->currentTXD;

    QListWidget *listWidget = this->textureListWidget;

    listWidget->clear();

    // We have no more selected texture item.
    this->currentSelectedTexture = nullptr;

    // this->hideFriendlyIcons();

    if ( txdObj )
    {
        TexInfoWidget *texInfoToSelect = nullptr;

	    for ( rw::TexDictionary::texIter_t iter( txdObj->GetTextureIterator() ); iter.IsEnd() == false; iter.Increment() )
	    {
            rw::TextureBase *texItem = iter.Resolve();

	        QListWidgetItem *item = new QListWidgetItem;
	        listWidget->addItem(item);
            TexInfoWidget *texInfoWidget = new TexInfoWidget(item, texItem);
	        listWidget->setItemWidget(item, texInfoWidget);
		    item->setSizeHint(QSize(listWidget->sizeHintForColumn(0), 54));

		    // select first or last item in a list
		    if (!texInfoToSelect || selectLastItemInList)
                texInfoToSelect = texInfoWidget;
	    }

        if (texInfoToSelect)
            listWidget->setCurrentItem(texInfoToSelect->listItem);
    }
}

void MainWindow::updateWindowTitle( void )
{
    QString windowTitleString;

    if ( this->wasTXDModified )
    {
        windowTitleString += "* ";
    }

    windowTitleString += "Magic.TXD";

#if defined(_M_AMD64) || defined(__x86_x64__)
    windowTitleString += " x64";
#endif

#ifdef _DEBUG
    windowTitleString += " DEBUG";
#endif

    // Put a little version info.
    windowTitleString += " " MTXD_VERSION_STRING;

    // If we are using a legacy OS, put that into the title.
    if ( this->fileSystem->IsInLegacyMode() )
    {
        windowTitleString += " (legacy)";
    }

    if ( this->currentTXD != nullptr )
    {
        if ( this->hasOpenedTXDFileInfo )
        {
            windowTitleString += " (" + QString( this->openedTXDFileInfo.absoluteFilePath() ) + ")";
        }
    }

    setWindowTitle( windowTitleString );

    // Also update the top label.
    if (this->txdNameLabel)
    {
        if ( this->currentTXD != nullptr )
        {
            QString topLabelDisplayString;

            if ( this->hasOpenedTXDFileInfo )
            {
                topLabelDisplayString = this->openedTXDFileInfo.fileName();
            }
            else
            {
                topLabelDisplayString = this->newTxdName;
            }

            this->txdNameLabel->setText( topLabelDisplayString );
        }
        else
        {
            this->txdNameLabel->clear();
        }
    }

    // Update version button
    if (this->rwVersionButton)
    {
        if (rw::TexDictionary *txd = this->currentTXD)
        {
            const rw::LibraryVersion& txdVersion = txd->GetEngineVersion();

            QString text;
            text.sprintf("%u.%u.%u.%u",
                txdVersion.rwLibMajor, txdVersion.rwLibMinor,
                txdVersion.rwRevMajor, txdVersion.rwRevMinor
            );
            this->rwVersionButton->setText(text);
            this->rwVersionButton->show();
        }
        else
        {
            this->rwVersionButton->hide();
        }
    }
}

void MainWindow::updateTextureMetaInfo( void )
{
    if ( TexInfoWidget *infoWidget = this->currentSelectedTexture )
    {
        // Update it.
        infoWidget->updateInfo();

        // We also want to update the exportability, as the format may have changed.
        this->UpdateExportAccessibility();
    }
}

void MainWindow::updateAllTextureMetaInfo( void )
{
    QListWidget *textureList = this->textureListWidget;

    int rowCount = textureList->count();

    for ( int row = 0; row < rowCount; row++ )
    {
        QListWidgetItem *item = textureList->item( row );

        TexInfoWidget *texInfo = dynamic_cast <TexInfoWidget*> ( textureList->itemWidget( item ) );

        if ( texInfo )
        {
            texInfo->updateInfo();
        }
    }

    // Make sure we update exportability.
    this->UpdateExportAccessibility();
}

void MainWindow::onCreateNewTXD( bool checked )
{
    this->ModifiedStateBarrier( false,
        [=]( void )
    {
        CreateTxdDialog *createDlg = new CreateTxdDialog(this);
        createDlg->setVisible(true);
    });
}

#include "tools/dirtools.h"

static CFile* OpenGlobalFile( MainWindow *mainWnd, const filePath& path, const filePath& mode )
{
    CFile *theFile = RawOpenGlobalFile( mainWnd->fileSystem, path, mode );

    if ( theFile )
    {
        try
        {
            theFile = CreateDecompressedStream( mainWnd, theFile );
        }
        catch( ... )
        {
            delete theFile;

            throw;
        }
    }

    return theFile;
}

bool MainWindow::openTxdFile(QString fileName, bool silent)
{
    bool success = false;

    if ( !silent )
    {
        this->txdLog->beforeTxdLoading();
    }

    if (fileName.length() != 0)
    {
        // We got a file name, try to load that TXD file into our editor.
        std::wstring unicodeFileName = fileName.toStdWString();

        CFile *fileStream = OpenGlobalFile( this, unicodeFileName.c_str(), L"rb" );

        if ( fileStream )
        {
            try
            {
                rw::Stream *txdFileStream = RwStreamCreateTranslated( this->rwEngine, fileStream );

                // If the opening succeeded, process things.
                if (txdFileStream)
                {
                    if ( !silent )
                    {
                        this->txdLog->addLogMessage(QString("loading TXD: ") + fileName);
                    }

                    // Parse the input file.
                    rw::RwObject *parsedObject = nullptr;

                    try
                    {
                        parsedObject = this->rwEngine->Deserialize(txdFileStream);
                    }
                    catch (rw::RwException& except)
                    {
                        if ( !silent )
                        {
                            this->txdLog->showError(QString("failed to load the TXD archive: %1").arg(ansi_to_qt(except.message)));
                        }
                    }

                    if (parsedObject)
                    {
                        try
                        {
                            // Try to cast it to a TXD. If it fails we did not get a TXD.
                            rw::TexDictionary *newTXD = rw::ToTexDictionary(this->rwEngine, parsedObject);

                            if (newTXD)
                            {
                                // Okay, we got a new TXD.
                                // Set it as our current object in the editor.
                                this->setCurrentTXD(newTXD);

                                this->setCurrentFilePath(fileName);

                                this->updateFriendlyIcons();

                                success = true;
                            }
                            else
                            {
                                const char *objTypeName = this->rwEngine->GetObjectTypeName(parsedObject);

                                if ( !silent )
                                {
                                    this->txdLog->addLogMessage(QString("found %1 but expected a texture dictionary").arg(objTypeName), LOGMSG_WARNING);
                                }

                                // Get rid of the object that is not a TXD.
                                this->rwEngine->DeleteRwObject(parsedObject);
                            }
                        }
                        catch( ... )
                        {
                            // We failed in some way, so delete the RW object.
                            this->rwEngine->DeleteRwObject( parsedObject );

                            throw;
                        }
                    }
                    // if parsedObject is NULL, the RenderWare implementation should have error'ed us already.

                    // Remember to close the stream again.
                    this->rwEngine->DeleteStream(txdFileStream); //Open TXD file...
                }
            }
            catch( ... )
            {
                delete fileStream;

                throw;
            }

            delete fileStream;
        }
    }

    if ( !silent )
    {
        this->txdLog->afterTxdLoading();
    }

    return success;
}

void MainWindow::onOpenFile( bool checked )
{
    ModifiedStateBarrier( false,
        [=]( void )
    {
        QString fileName = QFileDialog::getOpenFileName( this, MAGIC_TEXT( "Main.Open.Desc" ), this->lastTXDOpenDir, tr( "RW Texture Archive (*.txd);;Any File (*.*)" ) );

        if ( fileName.length() != 0 )
        {
            // Store the new dir
            this->lastTXDOpenDir = QFileInfo( fileName ).absoluteDir().absolutePath();

            this->openTxdFile(fileName);
        }
    });
}

void MainWindow::onCloseCurrent( bool checked )
{
    this->ModifiedStateBarrier( false,
        [=]( void )
    {
        this->currentSelectedTexture = nullptr;
        this->hasOpenedTXDFileInfo = false;

	    clearViewImage();

        // Make sure we got no TXD active.
        this->setCurrentTXD( nullptr );

        this->updateWindowTitle();

        this->updateFriendlyIcons();
    });
}

void MainWindow::onTextureItemChanged(QListWidgetItem *listItem, QListWidgetItem *prevTexInfoItem)
{
    QListWidget *texListWidget = this->textureListWidget;

    QWidget *listItemWidget = texListWidget->itemWidget( listItem );

    TexInfoWidget *texItem = dynamic_cast <TexInfoWidget*> ( listItemWidget );

    this->currentSelectedTexture = texItem;

    this->updateTextureView();

    // Change what textures we can export to.
    this->UpdateExportAccessibility();
}

void MainWindow::adjustDimensionsByViewport( void )
{
    // If opening a TXD file, the editor window can be too small to view the entire image.
    // We should carefully increase the editor size so that everything is visible.

    // TODO.
}

void MainWindow::updateTextureView( void )
{
    TexInfoWidget *texItem = this->currentSelectedTexture;

    if ( texItem != nullptr )
    {
		// Get the actual texture we are associated with and present it on the output pane.
		rw::TextureBase *theTexture = texItem->GetTextureHandle();
		rw::Raster *rasterData = theTexture->GetRaster();
		if (rasterData)
		{
            try
            {
			    // Get a bitmap to the raster.
			    // This is a 2D color component surface.
			    rw::Bitmap rasterBitmap( this->rwEngine, 32, rw::RASTER_8888, rw::COLOR_BGRA );

                if ( this->drawMipmapLayers && rasterData->getMipmapCount() > 1 )
                {
                    rasterBitmap.setBgColor( 1.0, 1.0, 1.0, 0.0 );

                    rw::DebugDrawMipmaps( this->rwEngine, rasterData, rasterBitmap );
                }
                else
                {
                    rasterBitmap = rasterData->getBitmap();
                }

			    QImage texImage = convertRWBitmapToQImage( rasterBitmap );

			    imageWidget->setPixmap(QPixmap::fromImage(texImage));
                this->updateTextureViewport();
			    imageWidget->show();
            }
            catch( rw::RwException& except )
            {
				this->txdLog->addLogMessage(QString("failed to get bitmap from texture: ") + except.message.GetConstString(), LOGMSG_WARNING);

                // We hide the image widget.
                this->clearViewImage();
            }
		}
    }
}

void MainWindow::updateTextureViewport() {
    QLabel *imageWidget = this->imageWidget;
    if (const QPixmap *widgetPixMap = imageWidget->pixmap()){
        if (this->showFullImage) {
            float w, h, border_w, border_h;
            w = widgetPixMap->width(); h = widgetPixMap->height();
            border_w = imageView->width();
            border_h = imageView->height();
            float scaleFactor = std::min(border_w / w, border_h / h);
            if (scaleFactor < 1.0f) {
                imageWidget->setFixedSize(scaleFactor * w, scaleFactor * h);
            }
            else {
                imageWidget->setFixedSize(widgetPixMap->width(), widgetPixMap->height());
            }
        }
        else {
            imageWidget->setFixedSize(widgetPixMap->width(), widgetPixMap->height());
        }
    }
}

void MainWindow::onToggleShowFullImage(bool checked)
{
    this->showFullImage = !(this->showFullImage);
    this->imageWidget->setScaledContents(this->showFullImage);
    this->updateTextureViewport();
}

void MainWindow::onToggleShowMipmapLayers( bool checked )
{
    this->drawMipmapLayers = !( this->drawMipmapLayers );

    // Update the texture view.
    this->updateTextureView();
}

void MainWindow::onToggleShowBackground(bool checked)
{
	this->showBackground = !(this->showBackground);
	if (showBackground)
		imageWidget->setStyleSheet("background-image: url(\"" + this->m_appPathForStyleSheet + "/resources/viewBackground.png\");");
	else
		imageWidget->setStyleSheet("background-color: rgba(255, 255, 255, 0);");
}

void MainWindow::onToggleShowLog( bool checked )
{
    // Make sure the log is visible.
	this->txdLog->show();
}

void MainWindow::onToogleDarkTheme(bool checked) {
    if (checked && !this->recheckingThemeItem) {
        this->actionThemeLight->setChecked(false);
        this->starsMovie->stop();
        this->setStyleSheet(styles::get(this->m_appPath, "resources/dark.shell"));
        this->starsMovie->setFileName(makeAppPath("resources/dark/stars.gif"));
        this->starsMovie->start();

        this->UpdateTheme();
    }
    else {
        this->recheckingThemeItem = true;
        this->actionThemeDark->setChecked(true);
        this->recheckingThemeItem = false;
    }
}

void MainWindow::onToogleLightTheme(bool checked) {
    if (checked && !this->recheckingThemeItem) {
        this->actionThemeDark->setChecked(false);
        this->starsMovie->stop();
        this->setStyleSheet(styles::get(this->m_appPath, "resources/light.shell"));
        this->starsMovie->setFileName(makeAppPath("resources/light/stars.gif"));
        this->starsMovie->start();

        this->UpdateTheme();
    }
    else {
        this->recheckingThemeItem = true;
        this->actionThemeLight->setChecked(true);
        this->recheckingThemeItem = false;
    }
}

void MainWindow::onSetupMipmapLayers( bool checked )
{
    // We just generate up to the top mipmap level for now.
    if ( TexInfoWidget *texInfo = this->currentSelectedTexture )
    {
        rw::TextureBase *texture = texInfo->GetTextureHandle();

        // Generate mipmaps of raster.
        if ( rw::Raster *texRaster = texture->GetRaster() )
        {
            bool hasModifiedRaster = false;

            try
            {
                texRaster->generateMipmaps( 32, rw::MIPMAPGEN_DEFAULT );

                // Fix texture filtering modes.
                texture->fixFiltering();

                hasModifiedRaster = true;
            }
            catch( rw::RwException& except )
            {
                this->txdLog->addLogMessage(
                    QString( "failed to generate mipmaps for raster: " ) + ansi_to_qt( except.message ),
                    LOGMSG_ERROR
                );

                // Just continue ahead.
            }

            if ( hasModifiedRaster )
            {
                // Make sure we update the info.
                this->updateTextureMetaInfo();

                // Update the texture view.
                this->updateTextureView();

                // We have modified the TXD.
                this->NotifyChange();
            }
        }
    }
}

void MainWindow::onClearMipmapLayers( bool checked )
{
    // Here is a quick way to clear mipmap layers from a texture.
    if ( TexInfoWidget *texInfo = this->currentSelectedTexture )
    {
        rw::TextureBase *texture = texInfo->GetTextureHandle();

        // Clear the mipmaps from the raster.
        if ( rw::Raster *texRaster = texture->GetRaster() )
        {
            bool hasModifiedRaster = false;

            try
            {
                texRaster->clearMipmaps();

                // Fix the filtering.
                texture->fixFiltering();

                hasModifiedRaster = true;
            }
            catch( rw::RwException& except )
            {
                this->txdLog->addLogMessage(
                    QString( "failed to clear mipmaps for raster: " ) +
                    ansi_to_qt( except.message ),
                    LOGMSG_ERROR
                );

                // Also continue ahead.
            }

            if ( hasModifiedRaster )
            {
                // Update the info.
                this->updateTextureMetaInfo();

                // Update the texture view.
                this->updateTextureView();

                // We have modified the TXD.
                this->NotifyChange();
            }
        }
    }
}

bool MainWindow::saveCurrentTXDAt( QString txdFullPath )
{
    bool didSave = false;

    if ( rw::TexDictionary *currentTXD = this->currentTXD )
    {
        // We serialize what we have at the location we loaded the TXD from.
        std::wstring unicodeFullPath = txdFullPath.toStdWString();

        rw::streamConstructionFileParamW_t fileOpenParam( unicodeFullPath.c_str() );

        rw::Stream *newTXDStream = this->rwEngine->CreateStream( rw::RWSTREAMTYPE_FILE_W, rw::RWSTREAMMODE_CREATE, &fileOpenParam );

        if ( newTXDStream )
        {
            // TODO: add security measures to prevent overwriting a valid TXD with garbage.

            // Write the TXD into it.
            try
            {
                this->rwEngine->Serialize( currentTXD, newTXDStream );

                // Success, so lets update our target filename.
                this->setCurrentFilePath( txdFullPath );

                // We are no longer modified.
                this->ClearModifiedState();

                // Tell the runtime about the success :)
                didSave = true;
            }
            catch( rw::RwException& except )
            {
				this->txdLog->addLogMessage(QString("failed to save the TXD archive: %1").arg(except.message.GetConstString()), LOGMSG_ERROR);
            }

            // Close the stream.
            this->rwEngine->DeleteStream( newTXDStream );
        }
        else
        {
            this->txdLog->addLogMessage( QString( "failed to make stream to TXD archive (maybe lack of permission)" ) );
        }
    }

    return didSave;
}

bool MainWindow::performSaveTXD( void )
{
    bool didSave = false;

    if ( this->currentTXD != nullptr )
    {
        if ( this->hasOpenedTXDFileInfo )
        {
            QString txdFullPath = this->openedTXDFileInfo.absoluteFilePath();

            if ( txdFullPath.length() != 0 )
            {
                didSave = this->saveCurrentTXDAt( txdFullPath );
            }
        }
        else
        {
            didSave = this->performSaveAsTXD();
        }
    }

    return didSave;
}

void MainWindow::onRequestSaveTXD( bool checked )
{
    this->performSaveTXD();
}

bool MainWindow::performSaveAsTXD( void )
{
    bool didSave = false;

    if ( this->currentTXD != nullptr )
    {
        QString txdSavePath;

        if (!(this->lastTXDSaveDir.isEmpty()) && this->currentTXD) {
            if (this->hasOpenedTXDFileInfo)
                txdSavePath = this->lastTXDSaveDir + "/" + this->openedTXDFileInfo.fileName();
            else
                txdSavePath = this->lastTXDSaveDir + "/" + this->newTxdName;
        }

        QString newSaveLocation = QFileDialog::getSaveFileName( this, MAGIC_TEXT("Main.SaveAs.Desc"), txdSavePath, "RW Texture Dictionary (*.txd)" );

        if ( newSaveLocation.length() != 0 )
        {
            // Save location.
            this->lastTXDSaveDir = QFileInfo( newSaveLocation ).absoluteDir().absolutePath();

            didSave = this->saveCurrentTXDAt( newSaveLocation );
        }
    }

    return didSave;
}

void MainWindow::onRequestSaveAsTXD( bool checked )
{
    this->performSaveAsTXD();
}

static void serializeRaster( rw::Stream *outputStream, rw::Raster *texRaster, const char *method )
{
    // TODO: add DDS file writer functionality, by checking method for "DDS"

    // Serialize it.
    texRaster->writeImage( outputStream, method );
}

void MainWindow::DefaultTextureAddAndPrepare( rw::TextureBase *newTexture, const char *name, const char *maskName )
{
    // We need to set default texture rendering properties.
    newTexture->SetFilterMode( rw::RWFILTER_LINEAR );
    newTexture->SetUAddressing( rw::RWTEXADDRESS_WRAP );
    newTexture->SetVAddressing( rw::RWTEXADDRESS_WRAP );

    // Actually adjust filtering based on its raster.
    newTexture->fixFiltering();

    // Give it a name.
    newTexture->SetName( name );
    newTexture->SetMaskName( maskName );

    // Now put it into the TXD.
    newTexture->AddToDictionary( currentTXD );

    // Update the texture list.
    this->updateTextureList(true);

    // We have modified the TXD.
    this->NotifyChange();
}

void MainWindow::DoAddTexture( const TexAddDialog::texAddOperation& params )
{
    TexAddDialog::texAddOperation::eAdditionType add_type = params.add_type;

    bool hadEmptyTXD = ( this->currentTXD->GetTextureCount() == 0 );

    if ( add_type == TexAddDialog::texAddOperation::ADD_TEXCHUNK )
    {
        // This is just adding the texture chunk to our TXD.
        rw::TextureBase *texHandle = (rw::TextureBase*)rw::AcquireObject( params.add_texture.texHandle );

        texHandle->AddToDictionary( this->currentTXD );

        // Update the texture list.
        this->updateTextureList(true);

        this->NotifyChange();
    }
    else if ( add_type == TexAddDialog::texAddOperation::ADD_RASTER )
    {
        rw::Raster *newRaster = params.add_raster.raster;

        if ( newRaster )
        {
            try
            {
                // We want to create a texture and put it into our TXD.
                rw::TextureBase *newTexture = rw::CreateTexture( this->rwEngine, newRaster );

                if ( newTexture )
                {
                    try
                    {
                        DefaultTextureAddAndPrepare(
                            newTexture,
                            params.add_raster.texName.c_str(),
                            params.add_raster.maskName.c_str()
                        );
                    }
                    catch( ... )
                    {
                        // We kinda should get rid of this texture.
                        this->rwEngine->DeleteRwObject( newTexture );

                        throw;
                    }
                }
                else
                {
                    this->txdLog->showError( "failed to create texture" );
                }
            }
            catch( rw::RwException& except )
            {
                this->txdLog->showError( QString( "failed to add texture: " ) + ansi_to_qt( except.message ) );

                // Just continue.
            }
        }
    }

    // Update the friendly icons, since if TXD was empty, platform was set by giving first texture to TXD.
    if ( hadEmptyTXD )
    {
        this->updateFriendlyIcons();
    }
}

QString MainWindow::requestValidImagePath( const QString *imageName )
{
    static const char *MULTI_ITEM_SEPERATOR = " ";

    // Get the name of a texture to add.
    // For that we want to construct a list of all possible image extensions.
    QString imgExtensionSelect;

    bool hasEntry = false;

    const imageFormats_t& avail_formats = this->reg_img_formats;

    // Add any image file.
    if ( hasEntry )
    {
        imgExtensionSelect += ";;";
    }

    imgExtensionSelect += "Image file (";

    bool hasExtEntry = false;

    for ( const MainWindow::registered_image_format& entry : avail_formats )
    {
        if ( hasExtEntry )
        {
            imgExtensionSelect += MULTI_ITEM_SEPERATOR;
        }

        bool needsExtSep = false;

        for ( const std::string& extName : entry.ext_array )
        {
            if ( needsExtSep )
            {
                imgExtensionSelect += MULTI_ITEM_SEPERATOR;
            }

            imgExtensionSelect += QString( "*." ) + ansi_to_qt( extName ).toLower();

            needsExtSep = true;
        }

        hasExtEntry = true;
    }

    // TEX CHUNK.
    {
        if ( hasExtEntry )
        {
            imgExtensionSelect += MULTI_ITEM_SEPERATOR;
        }

        imgExtensionSelect += QString( "*.rwtex" );
    }

    imgExtensionSelect += ")";

    hasEntry = true;

    for ( const MainWindow::registered_image_format& entry : avail_formats )
    {
        if ( hasEntry )
        {
            imgExtensionSelect += ";;";
        }

        imgExtensionSelect += ansi_to_qt( entry.formatName ) + QString( " (" );

        bool needsExtSep = false;

        for ( const std::string& extName : entry.ext_array )
        {
            if ( needsExtSep )
            {
                imgExtensionSelect += MULTI_ITEM_SEPERATOR;
            }

            imgExtensionSelect +=
                QString( "*." ) + ansi_to_qt( extName ).toLower();

            needsExtSep = true;
        }

        imgExtensionSelect += QString( ")" );

        hasEntry = true;
    }

    // Add any file.
    if ( hasEntry )
    {
        imgExtensionSelect += ";;";
    }

    imgExtensionSelect += "RW Texture Chunk (*.rwtex);;Any file (*.*)";

    hasEntry = true;

    // As a convenience feature the imageName parameter could be given so that we
    // check if in the currently selected directory a file of that name exists, under a known image extension.
    QString actualImageFileOpenPath = this->lastImageFileOpenDir;

    if ( imageName )
    {
        QString maybeImagePath = actualImageFileOpenPath;
        maybeImagePath += '/';
        maybeImagePath += *imageName;

        bool hasFoundKnownFile = false;
        {
            // We just check using image extensions we know and pick the first one we find.
            // Might improve this in the future by actually picking what image format the user likes the most first.
            for ( const MainWindow::registered_image_format& entry : avail_formats )
            {
                for ( const std::string& ext_name : entry.ext_array )
                {
                    // Check for this image file availability.
                    QString pathToImageFile = maybeImagePath + '.' + ansi_to_qt( ext_name ).toLower();

                    QFileInfo fileInfo( pathToImageFile );

                    if ( fileInfo.exists() && fileInfo.isFile() )
                    {
                        // Just pick this one.
                        actualImageFileOpenPath = std::move( pathToImageFile );

                        hasFoundKnownFile = true;
                        break;
                    }
                }

                // Check for .rwtex, too.
                if ( !hasFoundKnownFile )
                {
                    QString pathToImageFile = maybeImagePath + ".rwtex";

                    QFileInfo fileInfo( pathToImageFile );

                    if ( fileInfo.exists() && fileInfo.isFile() )
                    {
                        // We found a native texture, so pick that one.
                        actualImageFileOpenPath = std::move( pathToImageFile );

                        hasFoundKnownFile = true;
                    }
                }
            }
        }

        if ( !hasFoundKnownFile )
        {
            actualImageFileOpenPath = std::move( maybeImagePath );
        }
    }

    QString imagePath = QFileDialog::getOpenFileName( this, MAGIC_TEXT("Main.Edit.Add.Desc"), actualImageFileOpenPath, imgExtensionSelect );

    // Remember the directory.
    if ( imagePath.length() != 0 )
    {
        this->lastImageFileOpenDir = QFileInfo( imagePath ).absoluteDir().absolutePath();
    }

    return imagePath;
}

void MainWindow::spawnTextureAddDialog( QString fileName )
{
    auto cb_lambda = [=] ( const TexAddDialog::texAddOperation& params )
    {
        this->DoAddTexture( params );
    };

    TexAddDialog::dialogCreateParams params;
    params.actionName = "Modify.Add";
    params.actionDesc = "Modify.Desc.Add";
    params.type = TexAddDialog::CREATE_IMGPATH;
    params.img_path.imgPath = fileName;

    TexAddDialog *texAddTask = new TexAddDialog( this, params, std::move( cb_lambda ) );

    //texAddTask->move( 200, 250 );
    texAddTask->setVisible( true );
}

void MainWindow::onAddTexture( bool checked )
{
    // Allow importing of a texture.
    rw::TexDictionary *currentTXD = this->currentTXD;

    if ( currentTXD != nullptr )
    {
        QString fileName = this->requestValidImagePath();

        if ( fileName.length() != 0 )
        {
            spawnTextureAddDialog( fileName );
        }
    }
}

void MainWindow::onReplaceTexture( bool checked )
{
    // Replacing a texture means that we search for another texture on disc.
    // We prompt the user to input a replacement that has exactly the same texture properties
    // (name, addressing mode, etc) but different raster properties (maybe).

    // We need to have a texture selected to replace.
    if ( TexInfoWidget *curSelTexItem = this->currentSelectedTexture )
    {
        QString overwriteTexName = ansi_to_qt( curSelTexItem->GetTextureHandle()->GetName() );

        QString replaceImagePath = this->requestValidImagePath( &overwriteTexName );

        if ( replaceImagePath.length() != 0 )
        {
            auto cb_lambda = [=] ( const TexAddDialog::texAddOperation& params )
            {
                rw::Interface *rwEngine = this->GetEngine();

                // Replace stuff.
                TexAddDialog::texAddOperation::eAdditionType add_type = params.add_type;

                if ( add_type == TexAddDialog::texAddOperation::ADD_TEXCHUNK )
                {
                    // We just take the texture and replace our existing texture with it.
                    if ( rw::TextureBase *curTex = curSelTexItem->GetTextureHandle() )
                    {
                        curSelTexItem->SetTextureHandle( nullptr );

                        rwEngine->DeleteRwObject( curTex );
                    }

                    rw::TextureBase *newTex = (rw::TextureBase*)rw::AcquireObject( params.add_texture.texHandle );

                    if ( newTex )
                    {
                        curSelTexItem->SetTextureHandle( newTex );

                        // Add the new texture to the dictionary.
                        newTex->AddToDictionary( this->currentTXD );
                    }
                }
                else if ( add_type == TexAddDialog::texAddOperation::ADD_RASTER )
                {
                    rw::TextureBase *tex = curSelTexItem->GetTextureHandle();

                    TexAddDialog::RwTextureAssignNewRaster(
                        tex, params.add_raster.raster,
                        params.add_raster.texName, params.add_raster.maskName
                    );
                }

                // Update info.
                this->updateTextureMetaInfo();

                this->updateTextureView();

                // We have modified the TXD.
                this->NotifyChange();
            };

            TexAddDialog::dialogCreateParams params;
            params.actionName = "Modify.Replace";
            params.actionDesc = "Modify.Desc.Replace";
            params.type = TexAddDialog::CREATE_IMGPATH;
            params.img_path.imgPath = replaceImagePath;

            // Overwrite some properties.
            params.overwriteTexName = &overwriteTexName;

            TexAddDialog *texAddTask = new TexAddDialog( this, params, std::move( cb_lambda ) );

            texAddTask->move( 200, 250 );
            texAddTask->setVisible( true );
        }
    }
}

void MainWindow::onRemoveTexture( bool checked )
{
    // Pretty simple. We get rid of the currently selected texture item.

    if ( TexInfoWidget *curSelTexItem = this->currentSelectedTexture )
    {
        // Forget about this selected item.
        this->currentSelectedTexture = nullptr;

        // We kill the texture in this item.
        rw::TextureBase *tex = curSelTexItem->GetTextureHandle();

        // First delete this item from the list.
        curSelTexItem->remove();

        // Now kill the texture.
        this->rwEngine->DeleteRwObject( tex );

        // If we have no more items in the list widget, we should hide our texture view page.
        if ( this->textureListWidget->selectedItems().count() == 0 )
        {
            this->clearViewImage();

            // We should also hide the friendly icons, since they only should show if a texture is selected.
            // this->hideFriendlyIcons();
        }

        // We have modified the TXD.
        this->NotifyChange();
    }
}

void MainWindow::onRenameTexture( bool checked )
{
    // Change the name of the currently selected texture.

    if ( this->texNameDlg )
        return;

    if ( TexInfoWidget *texInfo = this->currentSelectedTexture )
    {
        TexNameWindow *texNameDlg = new TexNameWindow( this, texInfo );

        texNameDlg->setVisible( true );
    }
}

void MainWindow::onResizeTexture( bool checked )
{
    // Change the texture dimensions.

    if ( TexInfoWidget *texInfo = this->currentSelectedTexture )
    {
        if ( TexResizeWindow *curDlg = this->resizeDlg )
        {
            curDlg->setFocus();
        }
        else
        {
            TexResizeWindow *dialog = new TexResizeWindow( this, texInfo );
            dialog->setVisible( true );
        }
    }
}

void MainWindow::onManipulateTexture( bool checked )
{
    // Manipulating a raster is taking that raster and creating a new copy that is more beautiful.
    // We can easily reuse the texture add dialog for this task.

    // For that we need a selected texture.
    if ( TexInfoWidget *curSelTexItem = this->currentSelectedTexture )
    {
        auto cb_lambda = [=] ( const TexAddDialog::texAddOperation& params )
        {
            assert( params.add_type == TexAddDialog::texAddOperation::ADD_RASTER );

            // Update the stored raster.
            rw::TextureBase *tex = curSelTexItem->GetTextureHandle();

            TexAddDialog::RwTextureAssignNewRaster(
                tex, params.add_raster.raster,
                params.add_raster.texName, params.add_raster.maskName
            );

            // We have changed the TXD.
            this->NotifyChange();

            // Update info.
            this->updateTextureMetaInfo();

            this->updateTextureView();
        };

        TexAddDialog::dialogCreateParams params;
        params.actionName = "Modify.Modify";
        params.actionDesc = "Modify.Desc.Modify";
        params.type = TexAddDialog::CREATE_RASTER;
        params.orig_raster.tex = curSelTexItem->GetTextureHandle();

        TexAddDialog *texAddTask = new TexAddDialog( this, params, std::move( cb_lambda ) );

        texAddTask->move( 200, 250 );
        texAddTask->setVisible( true );
    }
}

void MainWindow::onExportTexture( bool checked )
{
    // We are always sent by a QAction object.
    TextureExportAction *senderAction = (TextureExportAction*)this->sender();

    // Make sure we have selected a texture in the texture list.
    // Get it.
    TexInfoWidget *selectedTexture = this->currentSelectedTexture;

    if ( selectedTexture != nullptr )
    {
        rw::TextureBase *texHandle = selectedTexture->GetTextureHandle();

        if ( texHandle )
        {
            try
            {
                const QString& defaultExt = senderAction->defaultExt;
                const QString& exportFunction = senderAction->displayName;
                const QString& formatName = senderAction->formatName;

                std::string ansiExportFunction = qt_to_ansi( exportFunction );

                const QString actualExt = defaultExt.toLower();

                // Construct a default filename for the object.
                QString defaultFileName = ansi_to_qt( texHandle->GetName() ) + "." + actualExt;

                // Request a filename and do the export.
                QString caption;
                bool found = false;
                QString captionFormat = MAGIC_TEXT_CHECK_AVAILABLE("Main.Export.Desc", &found);

                if (found)
                    caption = QString(captionFormat).arg(exportFunction);
                else
                    caption = QString("Save ") + exportFunction + QString(" as...");


                QString finalFilePath = QFileDialog::getSaveFileName(this, caption, defaultFileName, formatName + " (*." + actualExt + ");;Any (*.*)");

                if ( finalFilePath.length() != 0 )
                {
                    // Try to open that file for writing.
                    std::wstring unicodeImagePath = finalFilePath.toStdWString();

                    rw::streamConstructionFileParamW_t fileParam( unicodeImagePath.c_str() );

                    rw::Stream *imageStream = this->rwEngine->CreateStream( rw::RWSTREAMTYPE_FILE_W, rw::RWSTREAMMODE_CREATE, &fileParam );

                    if ( imageStream )
                    {
                        try
                        {
                            // Directly write us.
                            if ( StringEqualToZero( ansiExportFunction.c_str(), "RWTEX", false ) )
                            {
                                rwEngine->Serialize( texHandle, imageStream );
                            }
                            else
                            {
                                rw::Raster *texRaster = texHandle->GetRaster();

                                if ( texRaster )
                                {
                                    serializeRaster( imageStream, texRaster, ansiExportFunction.c_str() );
                                }
                            }
                        }
                        catch( ... )
                        {
                            this->rwEngine->DeleteStream( imageStream );

                            // Since we failed, we do not want that image stream anymore.
#ifdef _WIN32
                            _wremove( unicodeImagePath.c_str() );
#elif defined(__linux__)
                            std::string ansiPathName = finalFilePath.toStdString();

                            remove( ansiPathName.c_str() );
#else
#error no image unlink file implementation
#endif //CROSS PLATFORM CODE

                            throw;
                        }

                        // Close the stream again.
                        this->rwEngine->DeleteStream( imageStream );
                    }
                }
            }
            catch( rw::RwException& except )
            {
                this->txdLog->showError( QString( "error during image output: " ) + except.message.GetConstString() );

                // We proceed.
            }
        }
    }
}

void MainWindow::onExportAllTextures( bool checked )
{
    if ( rw::TexDictionary *texDict = this->currentTXD )
    {
        // No point in exporting empty TXD.
        if ( texDict->GetTextureCount() != 0 )
        {
            ExportAllWindow *curDlg = new ExportAllWindow( this, texDict );

            curDlg->setVisible( true );
        }
    }
}

void MainWindow::clearViewImage()
{
	imageWidget->clear();
    imageWidget->setFixedSize(1, 1);
	imageWidget->hide();
}

void MainWindow::NotifyChange( void )
{
    // Call this function if there has been a change in the currently open TXD.
    if ( this->currentTXD == nullptr )
        return;

    bool isTXDChanged = this->wasTXDModified;

    if ( isTXDChanged )
    {
        // No need to notify twice.
        return;
    }

    this->wasTXDModified = true;

    // Update the editor title.
    this->updateWindowTitle();
}

void MainWindow::ClearModifiedState( void )
{
    // Call this function if the change is not important anymore.
    bool isTXDChanged = this->wasTXDModified;

    if ( !isTXDChanged )
        return;

    this->wasTXDModified = false;

    // Time to remove hints that the TXD was changed.
    this->updateWindowTitle();
}

void MainWindow::closeEvent( QCloseEvent *evt )
{
    // Maybe we have to do some save changes before closing.
    bool doClose = false;

    ModifiedStateBarrier( true,
        [&]( void )
    {
        doClose = true;
    });

    if ( doClose )
    {
        evt->accept();
    }
    else
    {
        evt->ignore();
    }
}

// Set txd plaform when we open
// or create new txd
// Creating a txd with different platform textures doesn't make any sense.

QString MainWindow::GetCurrentPlatform()
{
    // Attempt to get the platform from the current TXD, if present.
    if ( rw::TexDictionary *currentTXD = this->currentTXD )
    {
        if ( const char *txdPlatName = this->GetTXDPlatform( currentTXD ) )
        {
            return txdPlatName;
        }
    }

    // If we cannot get it from the actual TXD, the user's choice is just as important.
    return this->recommendedTxdPlatform;
}

void MainWindow::SetRecommendedPlatform(QString platform)
{
    // Please use this function only to set the user's preference.
    // User selects something in GUI that should be honored by the editor.
    this->recommendedTxdPlatform = platform;
}

const char* MainWindow::GetTXDPlatform(rw::TexDictionary *txd)
{
    if (txd->GetTextureCount() > 0) {
        for (rw::TexDictionary::texIter_t iter(txd->GetTextureIterator()); !iter.IsEnd(); iter.Increment())
        {
            rw::TextureBase *texHandle = iter.Resolve();

            rw::Raster *texRaster = texHandle->GetRaster();

            if (texRaster)
            {
                return texRaster->getNativeDataTypeName();
            }
        }
    }
    return nullptr;
}

void MainWindow::launchDetails( void )
{
    if ( this->isLaunchedForTheFirstTime )
    {
        // We should make ourselves known.
        this->onAboutUs( false );
    }
}

void MainWindow::ChangeTXDPlatform( rw::TexDictionary *txd, QString platform )
{
    // To change the platform of a TXD we have to set all of it's textures platforms.
    for ( rw::TexDictionary::texIter_t iter( txd->GetTextureIterator() ); !iter.IsEnd(); iter.Increment() )
    {
        rw::TextureBase *texHandle = iter.Resolve();

        rw::Raster *texRaster = texHandle->GetRaster();

        if ( texRaster )
        {
            try
            {
                rw::ConvertRasterTo( texRaster, qt_to_ansi(platform).c_str() );
            }
            catch( rw::RwException& except )
            {
                this->txdLog->showError( ansi_to_qt( "failed to change platform of texture '" + texHandle->GetName() + "': " + except.message ) );

                // Continue changing platform.
            }
        }
    }
}

void MainWindow::onSetupRenderingProps( bool checked )
{
    if ( checked == true )
        return;

    if ( TexInfoWidget *texInfo = this->currentSelectedTexture )
    {
        if ( RenderPropWindow *curDlg = this->renderPropDlg )
        {
            curDlg->setFocus();
        }
        else
        {
            RenderPropWindow *dialog = new RenderPropWindow( this, texInfo );
            dialog->setVisible( true );
        }
    }
}

void MainWindow::onSetupTxdVersion(bool checked) {
    if ( checked == true )
        return;

    if ( RwVersionDialog *curDlg = this->verDlg )
    {
        curDlg->setFocus();
    }
    else
    {
	    RwVersionDialog *dialog = new RwVersionDialog( this );

        dialog->setVisible( true );

        this->verDlg = dialog;
    }

    this->verDlg->updateVersionConfig();
}

void MainWindow::onShowOptions(bool checked)
{
    if ( QDialog *curDlg = this->optionsDlg )
    {
        curDlg->setFocus();
    }
    else
    {
        OptionsDialog *optionsDlg = new OptionsDialog( this );

        optionsDlg->setVisible( true );
    }
}

void MainWindow::onRequestMassConvert(bool checked)
{
    MassConvertWindow *massconv = new MassConvertWindow( this );

    massconv->setVisible( true );
}

void MainWindow::onRequestMassExport(bool checked)
{
    MassExportWindow *massexport = new MassExportWindow( this );

    massexport->setVisible( true );
}

void MainWindow::onRequestMassBuild(bool checked)
{
    MassBuildWindow *massbuild = new MassBuildWindow( this );

    massbuild->setVisible( true );

    TriggerHelperWidget( this, "mgbld_welcome", massbuild );
}

void MainWindow::onRequestOpenWebsite(bool checked)
{
    QDesktopServices::openUrl( QUrl( "http://www.gtamodding.com/wiki/Magic.TXD" ) );
}

void MainWindow::onAboutUs(bool checked)
{
    if ( QDialog *curDlg = this->aboutDlg )
    {
        curDlg->setFocus();
    }
    else
    {
        AboutDialog *aboutDlg = new AboutDialog( this );

        aboutDlg->setVisible( true );
    }
}

QString MainWindow::makeAppPath(QString subPath)
{
    return m_appPath + "/" + subPath;
}

// Theme management.
void MainWindow::RegisterThemeItem( magicThemeAwareItem *item )
{
    // Register the item.
    this->themeItems.push_back( item );

    // Initialize the theme.
    item->updateTheme( this );
}

void MainWindow::UnregisterThemeItem( magicThemeAwareItem *item )
{
    // Remove the item, if found.
    auto findItem = std::find( this->themeItems.begin(), this->themeItems.end(), item );

    if ( findItem != this->themeItems.end() )
    {
        this->themeItems.erase( findItem );
    }
}

void MainWindow::UpdateTheme( void )
{
    // Notify all items.
    for ( magicThemeAwareItem *item : this->themeItems )
    {
        item->updateTheme( this );
    }
}
