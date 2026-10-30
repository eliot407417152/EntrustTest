// CardPrinter: 把 Datacard SDK 的 print 範例包成一個不依賴 Qt 的類別。
// 這個標頭只用標準 C++，所以 Qt 的程式碼可以安全 include (避免 windows.h / Qt 巨集衝突)。
#pragma once
#include <functional>
#include <string>
#include <vector>

struct PrintOptions {
    std::wstring printerName;
    std::wstring hopperId = L"1";          // "1".."6" 或 "r1" (後方進卡)
    std::wstring cardEjectSide = L"Default"; // Default / Front / Back
    bool duplex = false;
    bool checkHopper = false;
    bool pollCompletion = true;

    bool magstripe = false;
    std::wstring track1 = L"ABC 123";
    std::wstring track2 = L"456";
    std::wstring track3 = L"789";

    std::wstring frontText;
    std::wstring backText;
    std::wstring frontImagePath;           // 空字串 = 不印圖
    std::wstring backImagePath;
};

class CardPrinter {
public:
    using LogFn = std::function<void(const std::wstring&)>;

    // 列出系統上的印表機名稱
    static std::vector<std::wstring> listPrinters();

    // 查詢印表機狀態 (簡短文字)；失敗時丟出 std::runtime_error
    static std::wstring queryStatus(const std::wstring& printerName, const LogFn& log);

    // 送出列印工作；失敗時丟出 std::runtime_error
    // 注意：必須在「有自己 COM 套間的執行緒」呼叫 (函式內會 CoInitialize)。
    static void print(const PrintOptions& options, const LogFn& log);
};