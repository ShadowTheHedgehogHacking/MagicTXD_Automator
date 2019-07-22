#pragma once

#include <QtWidgets/QDialog>
#include "qtutils.h"
#include "languages.h"

struct TexNameWindow : public QDialog, public magicTextLocalizationItem
{
    TexNameWindow( MainWindow *mainWnd, TexInfoWidget *texInfo );
    ~TexNameWindow( void );

    void updateContent( MainWindow *mainWnd ) override;

public slots:
    void OnUpdateTexName( const QString& newText )
    {
        this->UpdateAccessibility();
    }

    void OnRequestSet( bool clicked );
    void OnRequestCancel( bool clicked )
    {
        this->close();
    }

private:
    void UpdateAccessibility( void );

    MainWindow *mainWnd;

    TexInfoWidget *texInfo;

    MagicLineEdit *texNameEdit;

    QPushButton *buttonSet;
};
