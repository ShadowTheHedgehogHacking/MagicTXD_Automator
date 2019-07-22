#pragma once

#include <QtWidgets/QDialog>

class MainWindow;

struct AboutDialog : public QDialog, public magicTextLocalizationItem, public magicThemeAwareItem
{
    AboutDialog( MainWindow *mainWnd );
    ~AboutDialog( void );

    void updateContent( MainWindow *mainWnd ) override;

    void updateTheme( MainWindow *mainWnd ) override;

public slots:
    void OnRequestClose( bool checked );

private:
    MainWindow *mainWnd;

    QLabel *mainLogoLabel;
};
