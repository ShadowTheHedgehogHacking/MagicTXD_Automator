// Code related to saving changes in critical situations.
// Here is the code for save-changes-before-closing, and stuff.

#include "mainwindow.h"

void MainWindow::ModifiedStateBarrier( bool blocking, modifiedEndCallback_t cb )
{
    // If the current TXD was modified, we maybe want to save changes to
    // disk before proceeding.

    struct SaveChangesDialog : public QDialog, public magicTextLocalizationItem
    {
        inline SaveChangesDialog( MainWindow *mainWnd, modifiedEndCallback_t endCB ) : QDialog( mainWnd )
        {
            this->mainWnd = mainWnd;
            this->postCallback = std::move( endCB );

            // We are a modal dialog.
            setWindowModality( Qt::WindowModal );
            setWindowFlags( ( windowFlags() | Qt::WindowStaysOnTopHint ) & ~Qt::WindowContextHelpButtonHint );
            setAttribute( Qt::WA_DeleteOnClose );

            // This dialog consists of a warning message and selectable
            // options to the user, typically buttons.

            QVBoxLayout *rootLayout = new QVBoxLayout( this );

            rootLayout->addWidget( CreateLabelL( "Main.SavChange.Warn" ) );

            rootLayout->addSpacing( 15 );

            // Now for the button.
            QHBoxLayout *buttonRow = new QHBoxLayout( this );

            buttonRow->setAlignment( Qt::AlignCenter );

            QPushButton *saveAcceptButton = CreateButtonL( "Main.SavChange.Save" );

            saveAcceptButton->setDefault( true );

            connect( saveAcceptButton, &QPushButton::clicked, this, &SaveChangesDialog::onRequestSave );

            buttonRow->addWidget( saveAcceptButton );

            QPushButton *notSaveButton = CreateButtonL( "Main.SavChange.Ignore" );

            connect( notSaveButton, &QPushButton::clicked, this, &SaveChangesDialog::onRequestIgnore );

            buttonRow->addWidget( notSaveButton );

            QPushButton *cancelButton = CreateButtonL( "Main.SavChange.Cancel" );

            connect( cancelButton, &QPushButton::clicked, this, &SaveChangesDialog::onRequestCancel );

            buttonRow->addWidget( cancelButton );

            rootLayout->addLayout( buttonRow );

            this->setLayout( rootLayout );

            RegisterTextLocalizationItem( this );
        }

        ~SaveChangesDialog( void )
        {
            UnregisterTextLocalizationItem( this );

            // TODO.
        }

        void updateContent( MainWindow *mainWnd ) override
        {
            setWindowTitle( MAGIC_TEXT( "Main.SavChange.Title" ) );
        }

        inline void terminate( void )
        {
            MainWindow::modifiedEndCallback_t stack_postCallback = std::move( this->postCallback );

            // Need to set ourselves invisible already, because Qt is event based and it wont quit here.
            this->setVisible( false );

            // Destroy ourselves.
            this->close();

            // Since we saved, we can just perform the callback now.
            stack_postCallback();
        }

    private:
        void onRequestSave( bool checked )
        {
            // If the user successfully saved the changes, we quit.
            bool didSave = mainWnd->performSaveTXD();

            if ( didSave )
            {
                terminate();
            }
        }

        void onRequestIgnore( bool checked )
        {
            // The user probably does not care anymore.
            this->mainWnd->ClearModifiedState();

            terminate();
        }

        void onRequestCancel( bool checked )
        {
            // We really do not want to do anything.
            this->close();
        }

        MainWindow *mainWnd;

        MainWindow::modifiedEndCallback_t postCallback;
    };

    bool hasPostponedExec = false;

    if ( this->currentTXD != nullptr )
    {
        if ( this->wasTXDModified )
        {
            // We want to postpone activity.
            SaveChangesDialog *saveDlg = new SaveChangesDialog( this, std::move( cb ) );

            if ( blocking )
            {
                saveDlg->exec();
            }
            else
            {
                saveDlg->setVisible( true );
            }

            hasPostponedExec = true;
        }
    }

    if ( !hasPostponedExec )
    {
        // Nothing has happened specially, so we just execute here.
        cb();
    }
}
