#include "mainwindow.h"
#include "rwversiondialog.h"

RwVersionDialog::RwVersionDialog( MainWindow *mainWnd ) : QDialog( mainWnd ), versionGUI( mainWnd, this )
{
	setObjectName("background_1");
    setWindowFlags( this->windowFlags() & ~Qt::WindowContextHelpButtonHint );
    setAttribute( Qt::WA_DeleteOnClose );

    setWindowModality( Qt::WindowModal );

    this->mainWnd = mainWnd;

    MagicLayout<QVBoxLayout> layout(this);

    layout.top->addLayout( this->versionGUI.GetVersionRootLayout() );

	QPushButton *buttonAccept = CreateButtonL( "Main.SetupTV.Accept" );
	QPushButton *buttonCancel = CreateButtonL( "Main.SetupTV.Cancel" );

    this->applyButton = buttonAccept;

    connect( buttonAccept, &QPushButton::clicked, this, &RwVersionDialog::OnRequestAccept );
    connect( buttonCancel, &QPushButton::clicked, this, &RwVersionDialog::OnRequestCancel );

	layout.bottom->addWidget(buttonAccept);
    layout.bottom->addWidget(buttonCancel);

    // Initiate the ready dialog.
    this->versionGUI.InitializeVersionSelect();

    RegisterTextLocalizationItem( this );
}

RwVersionDialog::~RwVersionDialog( void )
{
    UnregisterTextLocalizationItem( this );

    // There can only be one version dialog.
    this->mainWnd->verDlg = nullptr;
}

void RwVersionDialog::updateContent( MainWindow *mainWnd )
{
    // Update localization items.
    this->setWindowTitle( getLanguageItemByKey( "Main.SetupTV.Desc" ) );
}

void RwVersionDialog::UpdateAccessibility( void )
{
    rw::LibraryVersion libVer;

    // Check whether we should allow setting this version.
    bool hasValidVersion = this->versionGUI.GetSelectedVersion( libVer );

    // Alright, set enabled-ness based on valid version.
    this->applyButton->setDisabled( !hasValidVersion );
}

void RwVersionDialog::OnRequestAccept( bool clicked )
{
    // Set the version and close.
    rw::LibraryVersion libVer;

    bool hasVersion = this->versionGUI.GetSelectedVersion( libVer );

    if ( !hasVersion )
        return;

    // Set the version of the entire TXD.
    // Also patch the platform if feasible.
    if ( rw::TexDictionary *currentTXD = this->mainWnd->currentTXD )
    {
        // todo: Maybe make SetEngineVersion set the version for all children objects?
        currentTXD->SetEngineVersion(libVer);

        bool hasChangedVersion = false;

        if (currentTXD->GetTextureCount() > 0)
        {
            for (rw::TexDictionary::texIter_t iter(currentTXD->GetTextureIterator()); !iter.IsEnd(); iter.Increment())
            {
                rw::TextureBase *theTexture = iter.Resolve();

                try
                {
                    theTexture->SetEngineVersion(libVer);
                }
                catch( rw::RwException& except )
                {
                    this->mainWnd->txdLog->addLogMessage(
                        QString( "failed to set version for texture \"" ) +
                        ansi_to_qt( theTexture->GetName() ) +
                        QString( "\": " ) +
                        ansi_to_qt( except.message ),
                        LOGMSG_WARNING
                    );
                }

                // Pretty naive, but in the context very okay.
                hasChangedVersion = true;
            }
        }

        QString previousPlatform = this->mainWnd->GetCurrentPlatform();
        QString currentPlatform = this->versionGUI.GetSelectedEnginePlatform();

        // If platform was changed
        bool hasChangedPlatform = false;

        if (previousPlatform != currentPlatform)
        {
            this->mainWnd->SetRecommendedPlatform(currentPlatform);
            this->mainWnd->ChangeTXDPlatform(currentTXD, currentPlatform);

            // The user might want to be notified of the platform change.
            this->mainWnd->txdLog->addLogMessage(
                QString("changed the TXD platform to match version (") + previousPlatform +
                QString(">") + currentPlatform + QString(")"),
                LOGMSG_INFO
            );

            hasChangedPlatform = true;
        }

        if ( hasChangedVersion || hasChangedPlatform )
        {
            // Update texture item info, because it may have changed.
            this->mainWnd->updateAllTextureMetaInfo();

            // The visuals of the texture _may_ have changed.
            this->mainWnd->updateTextureView();

            // Remember that we changed stuff.
            this->mainWnd->NotifyChange();
        }

        // Done. :)
    }

    // Update the MainWindow stuff.
    this->mainWnd->updateWindowTitle();

    // Since the version has changed, the friendly icons should have changed.
    this->mainWnd->updateFriendlyIcons();

    this->close();
}

void RwVersionDialog::OnRequestCancel( bool clicked )
{
    this->close();
}

void RwVersionDialog::updateVersionConfig()
{
    MainWindow *mainWnd = this->mainWnd;

    // Try to find a set for current txd version
    bool setFound = false;

    if (rw::TexDictionary *currentTXD = mainWnd->getCurrentTXD())
    {
        rw::LibraryVersion version = currentTXD->GetEngineVersion();

        QString platformName = mainWnd->GetCurrentPlatform();

        if (!platformName.isEmpty())
        {
            RwVersionSets::eDataType platformDataTypeId = RwVersionSets::dataIdFromEnginePlatformName(platformName);

            if (platformDataTypeId != RwVersionSets::RWVS_DT_NOT_DEFINED)
            {
                int setIndex, platformIndex, dataTypeIndex;

                if (mainWnd->versionSets.matchSet(version, platformDataTypeId, setIndex, platformIndex, dataTypeIndex))
                {
                    this->versionGUI.gameSelectBox->setCurrentIndex(setIndex + 1);
                    this->versionGUI.platSelectBox->setCurrentIndex(platformIndex);
                    this->versionGUI.dataTypeSelectBox->setCurrentIndex(dataTypeIndex);

                    setFound = true;
                }
            }
        }
    }

    // If we could not find a correct set, we still try to display good settings.
    if (!setFound)
    {
        if (this->versionGUI.gameSelectBox->currentIndex() != 0)
        {
            this->versionGUI.gameSelectBox->setCurrentIndex(0);
        }
        else
        {
            this->versionGUI.InitializeVersionSelect();
        }

        if (rw::TexDictionary *currentTXD = mainWnd->getCurrentTXD())
        {
            // Deduce the best data type from the current platform of the TXD.
            {
                QString platformName = mainWnd->GetCurrentPlatform();

                if ( platformName.isEmpty() == false )
                {
                    this->versionGUI.dataTypeSelectBox->setCurrentText( platformName );
                }
            }

            // Fill out the custom version string
            {
                const rw::LibraryVersion& txdVersion = currentTXD->GetEngineVersion();

                rw::rwStaticString <char> verString = rwVersionToString( txdVersion );
                rw::rwStaticString <char> buildString;

                if (txdVersion.buildNumber != 0xFFFF)
                {
                    buildString = eir::to_string <char, rw::RwStaticMemAllocator> ( txdVersion.buildNumber, 16 );
                }

                this->versionGUI.versionLineEdit->setText(ansi_to_qt(verString));
                this->versionGUI.buildLineEdit->setText(ansi_to_qt(buildString));
            }
        }
    }
}
