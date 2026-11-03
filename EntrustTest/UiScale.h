// UiScale.h: 介面大小 (UI 縮放) 與視窗適應螢幕。header-only，不使用 Q_OBJECT。
//
// 為什麼需要：程式會在解析度 / 縮放比例不同的電腦上執行 (1366x768 的筆電、4K 螢幕、Windows 顯示縮放 125% / 150% …)。
//   - 介面大小：整個程式的字型與「寫死的像素尺寸」(尺規、欄位最小寬度、工具列間距…) 一起依比例縮放。
//   - 視窗適應螢幕：視窗大小依「可用螢幕範圍」決定，記住上次的位置與面板配置，並保證不會跑出螢幕外。
// 設定存在 QSettings("EntrustTest", "CardDesigner") 的 ui/scalePercent：0 = 自動，其餘 = 百分比。
#pragma once
#include <QApplication>
#include <QCursor>
#include <QFont>
#include <QGuiApplication>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QToolTip>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <vector>

namespace uiscale {

    constexpr int kPresets[] = { 75, 80, 90, 100, 110, 125, 150, 175, 200 };

    struct State {
        int stored = 0;                                 // 設定值：0 = 自動
        int percent = 100;                              // 目前實際套用的百分比
        QFont baseFont;                                 // 程式啟動時的系統預設字型 (縮放的基準)
        bool inited = false;
        std::vector<std::function<void()>> listeners;   // 比例改變時要通知的視窗
    };
    inline State& state() { static State s; return s; }

    inline double factor() { return state().percent / 100.0; }
    // 把「寫死的像素尺寸」依目前比例換算 (至少 1)
    inline int px(int v) { return v <= 0 ? v : std::max(1, qRound(v * factor())); }

    inline QSettings settings() { return QSettings(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner")); }

    // 視窗所在螢幕的「可用範圍」(已扣掉工作列)；widget 為空時取游標所在的螢幕
    inline QRect availableGeometry(const QWidget* w = nullptr) {
        const QScreen* s = (w && w->screen()) ? w->screen() : nullptr;
        if (!s) s = QGuiApplication::screenAt(QCursor::pos());
        if (!s) s = QGuiApplication::primaryScreen();
        return s ? s->availableGeometry() : QRect(0, 0, 1280, 720);
    }

    // 自動：只會「縮小」(小螢幕)，不會放大 (大螢幕 / 高解析度請手動選較大的比例)。
    // 以邏輯像素高度 800 為基準 (Qt 已經套用 Windows 的顯示縮放)：768 高的筆電約 90%，1080p 以上 100%。
    inline int autoPercent() {
        const int p = int(100.0 * availableGeometry().height() / 800.0) / 5 * 5;
        return std::clamp(p, 75, 100);
    }

    inline int effectivePercent(int stored) { return stored > 0 ? std::clamp(stored, 50, 300) : autoPercent(); }

    // 套用字型：一般控制項 + Windows 主題會另外指定字型的幾個類別 (選單、狀態列、對話框…)
    inline void applyFont() {
        const QFont& b = state().baseFont;
        QFont f = b;
        if (b.pointSizeF() > 0) f.setPointSizeF(std::max(1.0, b.pointSizeF() * factor()));
        else if (b.pixelSize() > 0) f.setPixelSize(std::max(1, qRound(b.pixelSize() * factor())));
        QApplication::setFont(f);
        static const char* const kClasses[] = { "QMenu", "QMenuBar", "QStatusBar", "QDockWidget", "QMessageBox",
            "QHeaderView", "QAbstractItemView", "QToolButton", "QTabBar", "QToolBar", "QGroupBox" };
        for (const char* c : kClasses) QApplication::setFont(f, c);
        QToolTip::setFont(f);
    }

    inline void notify() {
        const auto copy = state().listeners;             // 通知過程中可能有人新增 / 移除
        for (const auto& fn : copy) fn();
    }

    inline void addListener(std::function<void()> fn) { state().listeners.push_back(std::move(fn)); }

    // 程式啟動時呼叫一次 (QApplication 建立之後、顯示視窗之前)
    inline void init() {
        State& s = state();
        if (!s.inited) { s.baseFont = QApplication::font(); s.inited = true; }
        s.stored = settings().value(QStringLiteral("ui/scalePercent"), 0).toInt();
        s.percent = effectivePercent(s.stored);
        applyFont();
    }

    // 0 = 自動，其餘 = 百分比；會存檔、立即套用並通知視窗
    inline void setStored(int percent) {
        State& s = state();
        s.stored = percent <= 0 ? 0 : std::clamp(percent, 50, 300);
        s.percent = effectivePercent(s.stored);
        settings().setValue(QStringLiteral("ui/scalePercent"), s.stored);
        applyFont();
        notify();
    }

    // 放大 (+1) / 縮小 (-1) 一級：在 kPresets 之間移動
    inline void step(int dir) {
        const int cur = state().percent;
        int next = cur;
        if (dir > 0) { for (int p : kPresets) if (p > cur) { next = p; break; } }
        else { for (int i = int(std::size(kPresets)) - 1; i >= 0; --i) if (kPresets[i] < cur) { next = kPresets[i]; break; } }
        if (next != cur) setStored(next);
    }

    // 只調整大小 (不移動)：預設大小依比例縮放，但不超過螢幕可用範圍的 95%。對話框用。
    inline void fitSize(QWidget* w, int baseW, int baseH) {
        const QRect a = availableGeometry(w);
        w->resize(std::min(px(baseW), int(a.width() * 0.95)), std::min(px(baseH), int(a.height() * 0.93)));
    }

    // 主視窗：調整大小並置中於螢幕可用範圍 (預留標題列 / 邊框空間)
    inline void fitWindowToScreen(QWidget* w, int baseW, int baseH) {
        const QRect a = availableGeometry(w);
        const int ww = std::min(px(baseW), int(a.width() * 0.96));
        const int hh = std::min(px(baseH), int(a.height() * 0.93));
        w->resize(ww, hh);
        w->move(a.center() - QPoint(ww / 2, hh / 2));
    }

    // 確保整個視窗 (含標題列) 在螢幕可用範圍內：太大就縮小，位置跑出去就移回來。
    // 換螢幕 / 換解析度 / 拔掉外接螢幕後，還原舊的視窗位置時特別有用。
    inline void clampToScreen(QWidget* w) {
        if (w->isMaximized() || w->isFullScreen()) return;
        const QRect a = availableGeometry(w);
        QRect g = w->frameGeometry();
        const int fw = g.width() - w->width(), fh = g.height() - w->height();     // 邊框與標題列
        const int nw = std::min(w->width(), a.width() - fw), nh = std::min(w->height(), a.height() - fh);
        if (nw != w->width() || nh != w->height()) { w->resize(nw, nh); g = w->frameGeometry(); }
        const int x = std::clamp(g.x(), a.left(), std::max(a.left(), a.right() - g.width() + 1));
        const int y = std::clamp(g.y(), a.top(), std::max(a.top(), a.bottom() - g.height() + 1));
        if (x != g.x() || y != g.y()) w->move(x, y);
    }

} // namespace uiscale