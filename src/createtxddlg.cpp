#include "mainwindow.h"
#include "qtutils.h"
#include <regex>
#include "testmessage.h"

#include "createtxddlg.h"

static inline const QRegularExpression& get_forb_path_chars( void )
{
    // IMPORTANT: always put statics into functions.
    static const QRegularExpression forbPathChars("[/:?\"<>|\\[\\]\\\\]");

    return forbPathChars;
}

CreateTxdDialog::CreateTxdDialog(MainWindow *mainWnd) : QDialog(mainWnd), versionGUI( mainWnd, this )
{
    this->mainWnd = mainWnd;

    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setWindowModality(Qt::WindowModality::WindowModal);
    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Create our GUI interface
    MagicLayout<QVBoxLayout> layout(this);

    QHBoxLayout *nameLayout = new QHBoxLayout;
    QLabel *nameLabel = CreateLabelL("New.Name");
    nameLabel->setObjectName("label25px");
    MagicLineEdit *nameEdit = new MagicLineEdit();

    connect(nameEdit, &QLineEdit::textChanged, this, &CreateTxdDialog::OnUpdateTxdName);

    nameEdit->setFixedWidth(300);
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(nameEdit);
    this->txdName = nameEdit;

    layout.top->addLayout(nameLayout);
    layout.top->addSpacing(8);

    layout.top->addLayout( this->versionGUI.GetVersionRootLayout() );

    QPushButton *buttonAccept = CreateButtonL("New.Create");
    QPushButton *buttonCancel = CreateButtonL("New.Cancel");

    this->applyButton = buttonAccept;

    connect(buttonAccept, &QPushButton::clicked, this, &CreateTxdDialog::OnRequestAccept);
    connect(buttonCancel, &QPushButton::clicked, this, &CreateTxdDialog::OnRequestCancel);

    layout.bottom->addWidget(buttonAccept);
    layout.bottom->addWidget(buttonCancel);

    // Initiate the ready dialog.
    this->versionGUI.InitializeVersionSelect();

    this->UpdateAccessibility();

    RegisterTextLocalizationItem( this );
}

CreateTxdDialog::~CreateTxdDialog( void )
{
    UnregisterTextLocalizationItem( this );
}

void CreateTxdDialog::updateContent( MainWindow *mainWnd )
{
    this->setWindowTitle(MAGIC_TEXT("New.Desc"));
}

void CreateTxdDialog::UpdateAccessibility(void)
{
    rw::LibraryVersion libVer;

    bool hasValidVersion = this->versionGUI.GetSelectedVersion(libVer);

    // Alright, set enabled-ness based on valid version.
    if(!hasValidVersion || this->txdName->text().isEmpty() || this->txdName->text().contains(get_forb_path_chars()))
        this->applyButton->setDisabled(true);
    else
        this->applyButton->setDisabled(false);
}

void CreateTxdDialog::OnRequestAccept(bool clicked)
{
    // Just create an empty TXD.
    rw::TexDictionary *newTXD = NULL;

    try
    {
        newTXD = rw::CreateTexDictionary(this->mainWnd->rwEngine);
    }
    catch (rw::RwException& except)
    {
        this->mainWnd->txdLog->showError(QString("failed to create TXD: ") + ansi_to_qt(except.message));

        // We failed.
        return;
    }

    if (newTXD == NULL)
    {
        this->mainWnd->txdLog->showError("unknown error in TXD creation");

        return;
    }

    this->mainWnd->setCurrentTXD(newTXD);

    this->mainWnd->clearCurrentFilePath();

    // Set the version of the entire TXD.
    this->mainWnd->newTxdName = this->txdName->text();

    rw::LibraryVersion libVer;
    this->versionGUI.GetSelectedVersion(libVer);

    newTXD->SetEngineVersion(libVer);

    char const *currentPlatform = this->versionGUI.GetSelectedEnginePlatform();

    this->mainWnd->SetRecommendedPlatform(currentPlatform);

    // Update the MainWindow stuff.
    this->mainWnd->updateWindowTitle();

    // Since the version has changed, the friendly icons should have changed.
    this->mainWnd->updateFriendlyIcons();

    this->close();
}

void CreateTxdDialog::OnRequestCancel(bool clicked)
{
    this->close();
}

void CreateTxdDialog::OnUpdateTxdName(const QString& newText)
{
    this->UpdateAccessibility();
}
