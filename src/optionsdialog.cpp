#include "mainwindow.h"
#include "optionsdialog.h"
#include "qtutils.h"

#include "languages.hxx"

OptionsDialog::OptionsDialog( MainWindow *mainWnd ) : QDialog( mainWnd )
{
    this->mainWnd = mainWnd;

    setWindowFlags( this->windowFlags() & ~Qt::WindowContextHelpButtonHint );

    setAttribute( Qt::WA_DeleteOnClose );

    // This will be a fairly complicated dialog.
    MagicLayout<QVBoxLayout> layout( this );

    layout.top->setContentsMargins( 5, 5, 5, 5 );

    QTabWidget *optTabs = new QTabWidget();

    this->optTabs = optTabs;

    layout.top->addWidget( optTabs );

    // Create a tab with main editor configurations.
    QWidget *mainTab = new QWidget();

    this->mainTabIndex = optTabs->addTab( mainTab, "" );

    mainTab->setObjectName( "optionsMainTabWidget" );

    QVBoxLayout *mainTabLayout = new QVBoxLayout();

    mainTabLayout->setAlignment( Qt::AlignTop );

    this->optionShowLogOnWarning = CreateCheckBoxL( "Main.Options.ShowLog" );
    this->optionShowLogOnWarning->setChecked( mainWnd->showLogOnWarning );
    mainTabLayout->addWidget(optionShowLogOnWarning);
    this->optionShowGameIcon = CreateCheckBoxL( "Main.Options.DispIcn" );
    this->optionShowGameIcon->setChecked(mainWnd->showGameIcon);
    mainTabLayout->addWidget( optionShowGameIcon );

    // Display language select.
    this->languageBox = new QComboBox();

    this->languageBox->setFixedWidth(300);
    {
        size_t languageCount = ourLanguages.languages.size();

        for (unsigned int i = 0; i < languageCount; i++)
        {
            const MagicLanguage& lang = ourLanguages.languages[ i ];

            this->languageBox->addItem(lang.info.nameInOriginal + " - " + lang.info.name);
        }
    }

    connect(this->languageBox, static_cast<void (QComboBox::*)(int index)>(&QComboBox::currentIndexChanged), this, &OptionsDialog::OnChangeSelectedLanguage);

    QFormLayout *languageFormLayout = new QFormLayout();
    languageFormLayout->addRow(CreateLabelL( "Lang.Lang" ), languageBox);

    mainTabLayout->addLayout(languageFormLayout);

    this->languageAuthorLabel = new QLabel();

    mainTabLayout->addWidget(this->languageAuthorLabel);

    this->languageBox->setCurrentIndex(ourLanguages.currentLanguage);

    mainTabLayout->setAlignment(this->languageAuthorLabel, Qt::AlignRight);

    mainTab->setLayout( mainTabLayout );

    QWidget *advTab = new QWidget();

    this->rwTabIndex = optTabs->addTab( advTab, "" );

    advTab->setObjectName( "optionsAdvTabWidget" );

    QVBoxLayout *advTabLayout = new QVBoxLayout();

    advTabLayout->setAlignment( Qt::AlignTop );

    // Add some amazing RenderWare global options here :)
    rw::Interface *rwEngine = mainWnd->rwEngine;

    QCheckBox *optIgnoreBlocklengths = CreateCheckBoxL( "Main.Options.SerSpc" );

    this->optionDeserWithoutBlocklengths = optIgnoreBlocklengths;

    optIgnoreBlocklengths->setChecked( rwEngine->GetIgnoreSerializationBlockRegions() );

    advTabLayout->addWidget( optIgnoreBlocklengths );

    advTabLayout->addSpacing( 10 );

    QHBoxLayout *layoutWarningLevel = new QHBoxLayout();

    layoutWarningLevel->addWidget( CreateLabelL( "Main.Options.WarnLvl" ), 0, Qt::AlignLeft );

    QComboBox *selectWarningLevel = new QComboBox();
    selectWarningLevel->addItem( "disabled" );
    selectWarningLevel->addItem( "low" );
    selectWarningLevel->addItem( "medium" );
    selectWarningLevel->addItem( "high" );
    selectWarningLevel->addItem( "all" );

    this->selectWarningLevel = selectWarningLevel;

    // Select the current item.
    {
        int warningLevel = rwEngine->GetWarningLevel();

        int selectItem = std::min( 4, std::max( 0, warningLevel ) );

        selectWarningLevel->setCurrentIndex( selectItem );
    }

    layoutWarningLevel->addWidget( selectWarningLevel );
 
    advTabLayout->addLayout( layoutWarningLevel );

    advTab->setLayout( advTabLayout );

    QPushButton *buttonAccept = CreateButtonL( "Main.Options.Accept" );
    layout.bottom->addWidget(buttonAccept);
    QPushButton *buttonCancel = CreateButtonL( "Main.Options.Cancel" );
    layout.bottom->addWidget(buttonCancel);

    connect( buttonAccept, &QPushButton::clicked, this, &OptionsDialog::OnRequestApply );
    connect( buttonCancel, &QPushButton::clicked, this, &OptionsDialog::OnRequestCancel );

    RegisterTextLocalizationItem( this );

    mainWnd->optionsDlg = this;
}

OptionsDialog::~OptionsDialog( void )
{
    mainWnd->optionsDlg = NULL;

    UnregisterTextLocalizationItem( this );
}

void OptionsDialog::updateContent( MainWindow *mainWnd )
{
    setWindowTitle( MAGIC_TEXT("Main.Options.Desc") );

    optTabs->setTabText( this->mainTabIndex, MAGIC_TEXT( "Main.Options.MainTab" ) );
    optTabs->setTabText( this->rwTabIndex, MAGIC_TEXT( "Main.Options.AdvTab" ) );

    selectWarningLevel->setItemText( 0, MAGIC_TEXT( "Main.Options.WDisabled" ) );
    selectWarningLevel->setItemText( 1, MAGIC_TEXT( "Main.Options.WLow" ) );
    selectWarningLevel->setItemText( 2, MAGIC_TEXT( "Main.Options.WMedium" ) );
    selectWarningLevel->setItemText( 3, MAGIC_TEXT( "Main.Options.WHigh" ) );
    selectWarningLevel->setItemText( 4, MAGIC_TEXT( "Main.Options.WAll" ) );
}

void OptionsDialog::OnRequestApply( bool checked )
{
    this->serialize();

    this->close();
}

void OptionsDialog::OnRequestCancel( bool checked )
{
    this->close();
}

void OptionsDialog::serialize( void )
{
    // We want to store things into the main window.
    MainWindow *mainWnd = this->mainWnd;

    mainWnd->showLogOnWarning = this->optionShowLogOnWarning->isChecked();

    if (mainWnd->showGameIcon != this->optionShowGameIcon->isChecked()) {
        mainWnd->showGameIcon = this->optionShowGameIcon->isChecked();
        mainWnd->updateFriendlyIcons();
    }

    if (ourLanguages.currentLanguage != -1) {
        MagicLanguage& magLang = ourLanguages.languages[ ourLanguages.currentLanguage ];

        if (mainWnd->lastLanguageFileName != magLang.languageFileName) {
            mainWnd->lastLanguageFileName = magLang.languageFileName;
        }
    }

    // Advanced tab.
    rw::Interface *rwEngine = mainWnd->rwEngine;

    //* The "deserialize without blockregions" property specifies whether RenderWare should read
    //  blocks without acknowledging the block length, purely relying on its knowledge of the 
    //  original structure. This is required for RW files which have broken block lengths (like
    //  TXD files generated by Wardrum Studios and Rockstar Vienna). For TXDs generated by
    //  Magic.TXD and others this may not be required. This option has been added because certain
    //  people in the scene have invented "obfuscation" by puttin worthless blocks between things.
    bool serializerHack = this->optionDeserWithoutBlocklengths->isChecked();

    rwEngine->SetIgnoreSerializationBlockRegions( serializerHack );

    //* The "warning level" property defines the intensity of warning output by RenderWare.
    int desiredWarningLevel = this->selectWarningLevel->currentIndex();

    rwEngine->SetWarningLevel( desiredWarningLevel );
}

void OptionsDialog::OnChangeSelectedLanguage(int newIndex)
{
    this->languageAuthorLabel->setText("");

    ourLanguages.selectLanguageByIndex( newIndex );

    if (newIndex >= 0 && ourLanguages.languages[newIndex].info.authors != "Magic.TXD Team") {
        QString names;
        bool found = false;
        QString namesFormat = MAGIC_TEXT_CHECK_AVAILABLE("Lang.Authors", &found);

        if (found)
            names = QString(namesFormat).arg(ourLanguages.languages[newIndex].info.authors);
        else
            names = QString("by " + ourLanguages.languages[newIndex].info.authors);
        this->languageAuthorLabel->setText(names);
    }
}