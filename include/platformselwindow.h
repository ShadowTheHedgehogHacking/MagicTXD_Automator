#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>

struct PlatformSelWindow : public QDialog
{
    inline PlatformSelWindow( MainWindow *mainWnd ) : QDialog( mainWnd )
    {
        this->setWindowTitle( "Platform Select" );
        this->setWindowFlags( this->windowFlags() & ~Qt::WindowContextHelpButtonHint );

        this->mainWnd = mainWnd;

        this->setWindowModality( Qt::WindowModal );
        this->setAttribute( Qt::WA_DeleteOnClose );

        // This dialog should consist of a single combo box and our usual two buttons.
        QVBoxLayout *rootLayout = new QVBoxLayout( this );

        rootLayout->setSizeConstraint( QLayout::SetFixedSize );

        // Single row with label and combo box.
        QHBoxLayout *platformRow = new QHBoxLayout();

        platformRow->addWidget( new QLabel( "Platform:" ) );

        QComboBox *platformComboBox = TexAddDialog::createPlatformSelectComboBox( mainWnd );

        // We want to select the platform of the current TXD container, if available.
        if ( rw::TexDictionary *currentTXD = mainWnd->currentTXD )
        {
            const char *platformString = MainWindow::GetTXDPlatformString( currentTXD );

            if ( platformString )
            {
                platformComboBox->setCurrentText( platformString );
            }
        }

        this->platformComboBox = platformComboBox;

        connect( platformComboBox, (void (QComboBox::*)( const QString& ))&QComboBox::activated, this, &PlatformSelWindow::OnPlatformSelect );

        platformRow->addWidget( platformComboBox );

        rootLayout->addLayout( platformRow );

        // Make sure we leave some space to the bottom.
        platformRow->setContentsMargins( QMargins( 0, 0, 0, 10 ) );

        // Now the usual button row.
        QHBoxLayout *buttonRow = new QHBoxLayout();

        QPushButton *buttonSet = new QPushButton( "Set" );

        this->buttonSet = buttonSet;

        connect( buttonSet, &QPushButton::clicked, this, &PlatformSelWindow::OnRequestSet );

        QPushButton *buttonCancel = new QPushButton( "Cancel" );

        connect( buttonCancel, &QPushButton::clicked, this, &PlatformSelWindow::OnRequestClose );

        buttonRow->addWidget( buttonSet );
        buttonRow->addWidget( buttonCancel );

        rootLayout->addLayout( buttonRow );

        mainWnd->platformDlg = this;

        // Initialize this dialog.
        this->UpdateAccessibility();
    }

    inline ~PlatformSelWindow( void )
    {
        // TODO.
        this->mainWnd->platformDlg = NULL;
    }

public slots:
    void OnPlatformSelect( const QString& newText )
    {
        this->UpdateAccessibility();
    }

    void OnRequestSet( bool checked )
    {
        // Just set our platform.
        if ( rw::TexDictionary *currentTXD = this->mainWnd->currentTXD )
        {
            QString selPlatform = this->platformComboBox->currentText();

            std::string ansiSelPlatform = qt_to_ansi( selPlatform );

            // Use a helper.
            this->mainWnd->SetTXDPlatformString( currentTXD, ansiSelPlatform.c_str() );

            // Update the main window view and texture descriptions.
            this->mainWnd->updateTextureView();

            this->mainWnd->updateAllTextureMetaInfo();

            // An important change like that should be logged.
            this->mainWnd->txdLog->addLogMessage( QString( "changed TXD platform to " ) + selPlatform );
        }

        this->close();
    }

    void OnRequestClose( bool checked )
    {
        this->close();
    }

private:
    void UpdateAccessibility( void )
    {
        // If the platform has not changed, then there is no point in setting it.
        bool allowSet = true;

        if ( rw::TexDictionary *currentTXD = this->mainWnd->currentTXD )
        {
            const char *curPlatform = MainWindow::GetTXDPlatformString( currentTXD );

            QString selPlatform = this->platformComboBox->currentText();

            if ( selPlatform == curPlatform )
            {
                allowSet = false;
            }
        }

        this->buttonSet->setDisabled( !allowSet );
    }

    MainWindow *mainWnd;

    QPushButton *buttonSet;
    QComboBox *platformComboBox;
};
