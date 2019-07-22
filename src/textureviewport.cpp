#include "mainwindow.h"

#include "textureViewport.h"

TexViewportWidget::TexViewportWidget(MainWindow *MainWnd)
{
    this->mainWnd = MainWnd;
}

void TexViewportWidget::resizeEvent(QResizeEvent *resEvent)
{
    QScrollArea::resizeEvent(resEvent);
    if (mainWnd)
        mainWnd->updateTextureViewport();
}
