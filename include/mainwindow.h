#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// DEBUG DEFINES.
#ifdef _DEBUG
#define _DEBUG_HELPER_TEXT
#endif

#include <QtCore/qconfig.h>

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QListWidget>
#include <QtCore/QFileInfo>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QAction>
#include <QtWidgets/QMessageBox>

#include <renderware.h>

#include <sdk/MemoryUtils.h>

#include <CFileSystemInterface.h>
#include <CFileSystem.h>

#define NUMELMS(x)      ( sizeof(x) / sizeof(*x) )

#include "defs.h"

class MainWindow;

#include "qtfilesystem.h"
#include "embedded_resources.h"

#include "versionsets.h"
#include "textureViewport.h"

#include "mainwindow.actions.h"

struct SystemEventHandlerWidget abstract
{
    ~SystemEventHandlerWidget( void );

    virtual void beginSystemEvent( QEvent *evt ) = 0;
    virtual void endSystemEvent( QEvent *evt ) = 0;
};

// Global conversion from QString to c-str and other way round.
inline std::string qt_to_ansi( const QString& str )
{
    QByteArray charBuf = str.toLatin1();

    return std::string( charBuf.data(), charBuf.size() );
}

inline rw::rwStaticString <char> qt_to_ansirw( const QString& str )
{
    QByteArray charBuf = str.toLatin1();

    return rw::rwStaticString <char> ( charBuf.data(), charBuf.size() );
}

inline rw::rwStaticString <wchar_t> qt_to_widerw( const QString& str )
{
    QByteArray charBuf = str.toUtf8();

    return CharacterUtil::ConvertStringsLength <char8_t, wchar_t, rw::RwStaticMemAllocator> ( (const char8_t*)charBuf.data(), charBuf.size() );
}

inline filePath qt_to_filePath( const QString& str )
{
    QByteArray charBuf = str.toUtf8();

    return filePath( (const char8_t*)charBuf.data(), charBuf.size() );
}

inline QString ansi_to_qt( const std::string& str )
{
    return QString::fromLatin1( str.c_str(), str.size() );
}

template <typename allocatorType>
inline QString ansi_to_qt( const eir::String <char, allocatorType>& str )
{
    return QString::fromLatin1( str.GetConstString(), str.GetLength() );
}

template <typename allocatorType>
inline QString wide_to_qt( const eir::String <wchar_t, allocatorType>& str )
{
    eir::String <char8_t, allocatorType> utf8String = CharacterUtil::ConvertStrings <wchar_t, char8_t, allocatorType> ( str, str.GetAllocData() );

    return QString::fromUtf8( (const char*)utf8String.GetConstString(), utf8String.GetLength() );
}

inline QString filePath_to_qt( const filePath& path )
{
    auto widePath = path.convert_unicode <FileSysCommonAllocator> ();

    return wide_to_qt( widePath );
}

// The editor may have items that depend on a certain theme.
// We must be aware of theme changes!
struct magicThemeAwareItem abstract
{
    // Called when the theme has changed.
    virtual void updateTheme( MainWindow *mainWnd ) = 0;
};

#include "texinfoitem.h"
#include "txdlog.h"
#include "txdadddialog.h"
#include "rwfswrap.h"
#include "guiserialization.h"
#include "aboutdialog.h"
#include "streamcompress.h"
#include "helperruntime.h"

#include "MagicExport.h"

// Global app-root system translator in jail-mode.
extern CFileTranslator *sysAppRoot;

#define _FEATURES_NOT_IN_CURRENT_RELEASE

class MainWindow : public QMainWindow, public magicTextLocalizationItem
{
    friend class TexAddDialog;
    friend class RwVersionDialog;
    friend class TexNameWindow;
    friend class RenderPropWindow;
    friend class TexResizeWindow;
    //friend class PlatformSelWindow;
    friend class ExportAllWindow;
    friend class AboutDialog;
    friend class OptionsDialog;
    friend class mainWindowSerializationEnv;
    friend class CreateTxdDialog;

public:
    MainWindow(QString appPath, rw::Interface *rwEngine, CFileSystem *fsHandle, QWidget *parent = 0);
    ~MainWindow();

    void updateContent( MainWindow *mainWnd );

private:
    void initializeNativeFormats(void);
    void shutdownNativeFormats(void);

    void UpdateExportAccessibility(void);
    void UpdateAccessibility(void);

    // Drag and drop support.
    void dragEnterEvent( QDragEnterEvent *evt ) override;
    void dragLeaveEvent( QDragLeaveEvent *evt ) override;
    void dropEvent( QDropEvent *evt ) override;

public:
    bool openTxdFile(QString fileName, bool silent = false);
    void setCurrentTXD(rw::TexDictionary *txdObj);
    rw::TexDictionary* getCurrentTXD(void)              { return this->currentTXD; }
    void updateTextureList(bool selectLastItemInList);

    void updateFriendlyIcons();

    void adjustDimensionsByViewport();

public:
    void updateWindowTitle(void);
    void updateTextureMetaInfo(void);
    void updateAllTextureMetaInfo(void);

    void updateTextureView(void);

    void updateTextureViewport(void);

    bool saveCurrentTXDAt(QString location);

    void clearViewImage(void);

    rw::Interface* GetEngine(void) { return this->rwEngine; }

    QString GetCurrentPlatform();

    void SetRecommendedPlatform(QString platform);

    void ChangeTXDPlatform(rw::TexDictionary *txd, QString platform);

    const char* GetTXDPlatform(rw::TexDictionary *txd);

    void launchDetails( void );

    // Theme registration API.
    void RegisterThemeItem( magicThemeAwareItem *item );
    void UnregisterThemeItem( magicThemeAwareItem *item );

private:
    void DefaultTextureAddAndPrepare( rw::TextureBase *rwtex, const char *name, const char *maskName );

    void DoAddTexture(const TexAddDialog::texAddOperation& params);

    inline void setCurrentFilePath(const QString& newPath)
    {
        this->openedTXDFileInfo = QFileInfo(newPath);
        this->hasOpenedTXDFileInfo = true;

        this->updateWindowTitle();
    }

    inline void clearCurrentFilePath(void)
    {
        this->hasOpenedTXDFileInfo = false;

        this->updateWindowTitle();
    }

    void UpdateTheme( void );

public:
    void NotifyChange( void );

private:
    void ClearModifiedState( void );

    void closeEvent( QCloseEvent *evt ) override;

    typedef std::function <void (void)> modifiedEndCallback_t;

    void ModifiedStateBarrier( bool blocking, modifiedEndCallback_t cb );

    bool performSaveTXD( void );
    bool performSaveAsTXD( void );

public slots:
    void onCreateNewTXD(bool checked);
    void onOpenFile(bool checked);
    void onCloseCurrent(bool checked);

    void onTextureItemChanged(QListWidgetItem *texInfoItem, QListWidgetItem *prevTexInfoItem);

    void onToggleShowFullImage(bool checked);
    void onToggleShowMipmapLayers(bool checked);
    void onToggleShowBackground(bool checked);
    void onToggleShowLog(bool checked);
    void onSetupMipmapLayers(bool checked);
    void onClearMipmapLayers(bool checked);

    void onRequestSaveTXD(bool checked);
    void onRequestSaveAsTXD(bool checked);

    void onSetupRenderingProps(bool checked);
    void onSetupTxdVersion(bool checked);
    void onShowOptions(bool checked);

    void onRequestMassConvert(bool checked);
    void onRequestMassExport(bool checked);
    void onRequestMassBuild(bool checked);

    void onToogleDarkTheme(bool checked);
    void onToogleLightTheme(bool checked);

    void onRequestOpenWebsite(bool checked);
    void onAboutUs(bool checked);

private:
    QString requestValidImagePath( const QString *imageName = NULL );

    void spawnTextureAddDialog( QString imgPath );

public slots:
    void onAddTexture(bool checked);
    void onReplaceTexture(bool checked);
    void onRemoveTexture(bool checked);
    void onRenameTexture(bool checked);
    void onResizeTexture(bool checked);
    void onManipulateTexture(bool checked);
    void onExportTexture(bool checked);
    void onExportAllTextures(bool checked);

protected:
    void addTextureFormatExportLinkToMenu(QMenu *theMenu, const char *displayName, const char *defaultExt, const char *formatName);

private:
    class rwPublicWarningDispatcher : public rw::WarningManagerInterface
    {
    public:
        inline rwPublicWarningDispatcher(MainWindow *theWindow)
        {
            this->mainWnd = theWindow;
        }

        void OnWarning(rw::rwStaticString <char>&& msg) override
        {
            this->mainWnd->txdLog->addLogMessage(ansi_to_qt(msg), LOGMSG_WARNING);
        }

    private:
        MainWindow *mainWnd;
    };

    rwPublicWarningDispatcher rwWarnMan;

    rw::Interface *rwEngine;
    rw::TexDictionary *currentTXD;

    TexInfoWidget *currentSelectedTexture;

    QFileInfo openedTXDFileInfo;
    bool hasOpenedTXDFileInfo;

    // We currently have a very primitive change-tracking system.
    // If we made any action that could have modified the TXD, we remember that.
    // Then if the user wants to discard the TXD, we ask if he wants to save it first.
    bool wasTXDModified;

    QString newTxdName;

    QString recommendedTxdPlatform;

    QListWidget *textureListWidget;

    TexViewportWidget *imageView; // we handle full 2d-viewport as a scroll-area
    QLabel *imageWidget;    // we use label to put image on it

    QLabel *txdNameLabel;

    QPushButton *rwVersionButton;

    QMovie *starsMovie;

    QSplitter *mainSplitter;

    bool showFullImage;
    bool drawMipmapLayers;
    bool showBackground;

    // Editor theme awareness.
    std::vector <magicThemeAwareItem*> themeItems;

    QMenu *fileMenu;
    QMenu *editMenu;
    QMenu *toolsMenu;
    QMenu *exportMenu;
    QMenu *viewMenu;
    QMenu *infoMenu;

    // Accessibility management variables (menu items).
    // FILE MENU.
    QAction *actionNewTXD;
    QAction *actionOpenTXD;
    QAction *actionSaveTXD;
    QAction *actionSaveTXDAs;
    QAction *actionCloseTXD;

    // EDIT MENU.
    QAction *actionAddTexture;
    QAction *actionReplaceTexture;
    QAction *actionRemoveTexture;
    QAction *actionRenameTexture;
    QAction *actionResizeTexture;
    QAction *actionManipulateTexture;
    QAction *actionSetupMipmaps;
    QAction *actionClearMipmaps;
    QAction *actionRenderProps;
#ifndef _FEATURES_NOT_IN_CURRENT_RELEASE
    QAction *actionViewAllChanges;
    QAction *actionCancelAllChanges;
    QAction *actionAllTextures;
#endif //_FEATURES_NOT_IN_CURRENT_RELEASE
    QAction *actionSetupTXDVersion;
    QAction *actionShowOptions;
    QAction *actionThemeDark;
    QAction *actionThemeLight;

    QHBoxLayout *friendlyIconRow;
    QLabel *friendlyIconGame;
    QWidget *friendlyIconSeparator;
    QLabel *friendlyIconPlatform;

    bool bShowFriendlyIcons;

    bool recheckingThemeItem;

    // EXPORT MENU.
    class TextureExportAction : public QAction
    {
    public:
        TextureExportAction( QString&& defaultExt, QString&& displayName, QString&& formatName, QWidget *parent ) : QAction( QString( "&" ) + displayName, parent )
        {
            this->defaultExt = defaultExt;
            this->displayName = displayName;
            this->formatName = formatName;
        }

        QString defaultExt;
        QString displayName;
        QString formatName;
    };

    std::list <TextureExportAction*> actionsExportItems;
    QAction *exportAllImages;

    // Action system for multi-threading.
    struct EditorActionSystem : public MagicActionSystem
    {
        EditorActionSystem( MainWindow *mainWnd );
        ~EditorActionSystem( void );

        void OnStartAction( void ) override;
        void OnStopAction( void ) override;
        void OnUpdateStatusMessage( const char *statusString ) override;

        void ReportException( const std::exception& except ) override;
        void ReportException( const rw::RwException& except ) override;
    };
    // TODO: actually use EditorActionSystem to spawn parallel tasks to GUI activity.

    // REMEMBER TO DELETE EVERY WIDGET THAT DEPENDS ON MAINWINDOW INSIDE OF MAINWINDOW DESTRUCTOR.
    // OTHERWISE THE EDITOR COULD CRASH.

	TxdLog *txdLog; // log management class
    class RwVersionDialog *verDlg; // txd version setup class
    class TexNameWindow *texNameDlg; // dialog to change texture name
    class RenderPropWindow *renderPropDlg; // change a texture's wrapping or filtering
    class TexResizeWindow *resizeDlg; // change raster dimensions
    //class PlatformSelWindow *platformDlg; // set TXD platform
    class AboutDialog *aboutDlg;  // about us. :-)
    QDialog *optionsDlg;    // many options.

    struct magf_extension
    {
        D3DFORMAT_SDK d3dformat;
        void *loadedLibrary;
        void *handler;
    };

    typedef std::list <magf_extension> magf_formats_t;

    magf_formats_t magf_formats;

    // Cache of registered image formats and their interfaces.
    struct registered_image_format
    {
        std::string formatName;
        std::string defaultExt;

        std::list <std::string> ext_array;

        bool isNativeFormat;
    };

    typedef std::list <registered_image_format> imageFormats_t;

    imageFormats_t reg_img_formats;

public:
    QString m_appPath;
    QString m_appPathForStyleSheet;

    // Use this if you need to get a path relatively to app directory
    QString makeAppPath(QString subPath);

    RwVersionSets versionSets;  // we need access to this in utilities.

    // NOTE: there are multiple ways to get absolute path to app directory coded in this editor!

public:
    CFileSystem *fileSystem;

    // Serialization properties.
    QString lastTXDOpenDir;     // maybe.
    QString lastTXDSaveDir;
    QString lastImageFileOpenDir;

    bool addImageGenMipmaps;
    bool lockDownTXDPlatform;
    bool adjustTextureChunksOnImport;
    bool texaddViewportFill;
    bool texaddViewportScaled;
    bool texaddViewportBackground;

    bool isLaunchedForTheFirstTime;

    // Options.
    bool showLogOnWarning;
    bool showGameIcon;

    QString lastLanguageFileName;

    // ExportAllWindow
    rw::rwStaticString <char> lastUsedAllExportFormat;
    rw::rwStaticString <wchar_t> lastAllExportTarget;
};

typedef StaticPluginClassFactory <MainWindow, rw::RwStaticMemAllocator> mainWindowFactory_t;

extern mainWindowFactory_t mainWindowFactory;

#endif // MAINWINDOW_H
