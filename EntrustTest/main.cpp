#include <QApplication>
#include <QIcon>
#include <windows.h>      // 1. 引入 WinAPI 標頭檔
#include "resource.h"     // 2. 引入 Visual Studio 自動產生的資源 ID 標頭檔
#include "DesignerWindow.h"
#include "UiScale.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 3. 從執行檔內部直接載入圖示資源（IDI_ICON1 是資源檔中的預設名稱）
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    if (hIcon) {
        // 將 Win32 的 HICON 轉換為 Qt 的 QIcon 並設定給全域應用程式
        app.setWindowIcon(QIcon(QPixmap::fromImage(QImage::fromHICON(hIcon))));
    }

    uiscale::init();
    DesignerWindow w;
    w.show();
    return app.exec();
}