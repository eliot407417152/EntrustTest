#include <QApplication>
#include "DesignerWindow.h"
#include "UiScale.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    uiscale::init();                       // 介面大小 (依螢幕自動 / 使用者上次的設定)
    DesignerWindow w;
    w.show();
    return app.exec();
}