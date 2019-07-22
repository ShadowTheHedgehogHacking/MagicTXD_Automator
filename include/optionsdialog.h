#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/qtabwidget.h>

struct OptionsDialog : public QDialog, public magicTextLocalizationItem
{
    OptionsDialog( MainWindow *mainWnd );
    ~OptionsDialog( void );

    void updateContent( MainWindow *mainWnd ) override;

public slots:
    void OnRequestApply( bool checked );
    void OnRequestCancel( bool checked );
    void OnChangeSelectedLanguage(int newIndex);

private:
    void serialize( void );

    MainWindow *mainWnd;

    QTabWidget *optTabs;
    int mainTabIndex;
    int rwTabIndex;

    // Main tab.
    QCheckBox *optionShowLogOnWarning;
    QCheckBox *optionShowGameIcon;

    QComboBox *languageBox;
    QLabel *languageAuthorLabel;

    // Advanced tab.
    QCheckBox *optionDeserWithoutBlocklengths;
    QComboBox *selectWarningLevel;
};
