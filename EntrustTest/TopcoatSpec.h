// TopcoatSpec: 鍍膜 (Topcoat / Overlay) 設定 → 印表機 PrintTicket 片段 + Escape 指令。
// 只用標準 C++ (不依賴 Qt / Windows)，因此 CardPrinter.cpp (Windows) 與 Qt 端、單元測試都能共用。
//
// 三種機制 (來源：Datacard SDK 範例 print / magstripe / smartcard)：
//   1. PrintTicket：整面卡的「基本」鍍膜設定
//        - 全卡鍍膜 (PageFrontPrintAndTopcoat = All)
//        - 全卡鍍膜但避開晶片/磁條 (PageFrontPrintAndTopcoat = Except + PageFrontTopcoatException = ...)
//   2. Escape：用 GDI TextOut 送進去的指令，優先於 PrintTicket；單位 mm，格式為「上 左 寬 高」，
//        多個矩形用 ';' 分隔、以 '?' 結尾，座標以「橫式卡面」為基準：
//        ~TA%top left w h;top left w h?   鍍膜區 (Topcoat Add)：整面先關閉鍍膜，只在這些矩形內鍍膜
//        ~TR%top left w h?                移除鍍膜 (Topcoat Remove)：這些矩形內不鍍膜
//        ~PB%top left w h?                列印阻擋 (Print Blocking)：這些矩形內不列印
//   3. 完全不鍍膜 (Base::None)：PrintTicket 不送；改送一個極小的 ~TA 區域，把整面鍍膜先關閉 (SDK 範例註解：
//        「a 'topcoat Add' escape will force topcoat OFF for the entire card side」)。此模式下鍍膜區 (~TA) 與
//        移除鍍膜 (~TR) 沒有意義，會被忽略；列印阻擋 (~PB) 仍然有效。
#pragma once
#include <string>
#include <vector>

// 「完全不鍍膜」時用來關閉整面鍍膜的極小 ~TA 區域邊長 (mm)，放在卡面左上角
constexpr double kNoTopcoatDotMm = 0.1;

struct TopcoatRect {
    double topMm = 0, leftMm = 0, widthMm = 0, heightMm = 0;
};

struct TopcoatSpec {
    enum class Base { DriverDefault, All, Except, None };   // None = 完全不鍍膜
    enum class Exception { None, Chip7816, MagJis, MagIso2, MagIso3 };

    Base base = Base::DriverDefault;            // PrintTicket 的基本設定
    Exception exception = Exception::None;      // base == Except 時，要避開哪種模組
    std::vector<TopcoatRect> addAreas;          // ~TA
    std::vector<TopcoatRect> removeAreas;       // ~TR
    std::vector<TopcoatRect> blockAreas;        // ~PB

    // true = 完全沿用驅動程式設定，不送任何 PrintTicket / Escape
    bool isDefault() const {
        return base == Base::DriverDefault && addAreas.empty() && removeAreas.empty() && blockAreas.empty();
    }
};

namespace topcoat {

    // 數字格式：最多 2 位小數、去掉多餘的 0，一律用 '.' (不受系統地區設定影響)。例：25.4 -> "25.4"、5 -> "5"
    std::wstring formatMm(double v);

    // 單一 Escape 指令字串，例如 "~TA%25.4 0 50.8 10;40 60 7 7?"。rects 為空時回傳空字串。
    std::wstring escape(const wchar_t* code /* L"TA" / L"TR" / L"PB" */, const std::vector<TopcoatRect>& rects);

    // 依序要用 TextOut 送出的 Escape：TA、TR、PB (沒有矩形的會略過)
    std::vector<std::wstring> escapes(const TopcoatSpec& spec);

    // 要放進 PrintTicket (<psf:PrintTicket>…</psf:PrintTicket> 之內) 的 Feature 片段；DriverDefault 回傳空字串
    std::wstring frontTicketXml(const TopcoatSpec& spec);

    // 直式卡面要旋轉 90° 才能送進橫向的印表機：把「直式設計座標」換成「橫式列印座標」。
    // portraitWidthMm/HeightMm = 直式卡面的寬、高 (例如 53.98、85.6)；clockwise 要與影像旋轉方向一致。
    TopcoatRect rotatePortraitToLandscape(const TopcoatRect& r, double portraitWidthMm, double portraitHeightMm, bool clockwise);
    TopcoatSpec rotatePortraitToLandscape(const TopcoatSpec& s, double portraitWidthMm, double portraitHeightMm, bool clockwise);

    // 給人看的摘要 (寫進 Log / 預覽用)：describeBase 只描述 PrintTicket 部分，describe 還包含所有 Escape
    std::wstring describeBase(const TopcoatSpec& spec);
    std::wstring describe(const TopcoatSpec& spec);

} // namespace topcoat