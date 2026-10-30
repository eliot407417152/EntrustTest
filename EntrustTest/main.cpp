#include <QApplication>
#include "DesignerWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    DesignerWindow w;
    w.show();
    return app.exec();
}