#pragma once

#include <QtWidgets/QScrollArea>

class MainWindow;

class TexViewportWidget : public QScrollArea
{
public:
    TexViewportWidget(MainWindow *MainWnd);
protected:
    void resizeEvent(QResizeEvent *resEvent);
private:
    MainWindow *mainWnd;
};
