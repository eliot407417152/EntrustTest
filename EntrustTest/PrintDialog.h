// PrintDialog: 階段 3「批次列印」視窗。
//   - 把卡面依印表機解析度渲染成點陣圖 (card::renderCard)，再交給 CardPrinter 以滿版方式列印
//   - 逐筆進度表、整體進度條與預估剩餘時間、取消、失敗重試
//   - 匯出 Log (.txt) 與逐筆結果 (.csv)
//   - 「測試模式」：不送印表機，只把每張卡面輸出成 PNG，用來預先檢查
// 刻意不使用 Q_OBJECT：連線與跨執行緒都用 lambda，所以不需要 moc 處理。
#pragma once
#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <atomic>
#include <memory>
#include "CardLayout.h"
#include "ExcelReader.h"
#include "ImageMatcher.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;

// 由 DesignerWindow 準備好、複製一份交給對話框 (列印期間即使主視窗版面被改動也不受影響)
struct PrintInput {
    card::CardLayout layout;
    card::DataTable data;            // 要列印的資料 (未載入 Excel 時，只有 1 筆「預覽資料」)
    card::ImageMatcher matcher;      // 圖片資料夾 (圖片路徑留空的圖片元素使用)
    QString matchField;              // 用哪個欄位比對圖片檔名
    QList<int> visibleRows;          // 資料表目前顯示的列 (受搜尋條件過濾)
    int currentRow = -1;             // 資料表目前選取的列
    bool previewOnly = false;        // true = 沒有載入 Excel，資料是「預覽資料」欄位的內容
};

class PrintDialog : public QDialog {
public:
    explicit PrintDialog(const PrintInput& in, QWidget* parent = nullptr);

    bool isRunning() const { return running_; }
    // false = 不跳出確認/結果訊息視窗 (自動化測試用)
    void setInteractive(bool on) { interactive_ = on; }

    bool exportLog(const QString& path) const;          // 完整 Log (含設定摘要)
    bool exportResultCsv(const QString& path) const;    // 逐筆結果 (可用來找出要補印的資料)

    void reject() override;                              // 列印中不允許關閉視窗 (按 Esc / 關閉 / 關閉鈕都會經過這裡)

private:
    enum class State { Pending, Skipped, Printing, Done, Failed, Unprocessed };
    struct Item {
        int row = 0;                 // 在 data.rows 內的索引
        State state = State::Pending;
        QString message;
        double seconds = 0;
    };

    // UI
    void buildUi();
    void refreshPrinters();
    void updateModeUi();
    void setControlsEnabled(bool on);
    void loadSettings();
    void saveSettings() const;

    // 範圍與檢查
    QList<int> selectedRows() const;
    QStringList problemsOf(int row) const;
    void rebuildPlan();
    void updateSummary();
    void updateRow(int i);

    // 執行
    void startJob();
    void retryFailed();
    void requestCancel();
    QString renderItem(int itemIndex, int dpi, bool dryRun, const QString& outDir, QString* error);
    void markPrinting(int itemIndex, int seq, int total);
    void setItemResult(int itemIndex, bool ok, const QString& message, double seconds);
    void finishJob();
    void updateProgress();

    // Log
    void log(const QString& line);
    void logItem(int itemIndex, const QString& line);
    QString settingsSummary() const;

    PrintInput in_;
    QSet<QString> usedFields_;                 // 版面中 {欄位} 用到的欄位
    bool hasAutoImage_ = false;                // 有「圖片路徑留空」的圖片元素 (依資料夾自動比對)
    QHash<QString, int> matchValueCount_;      // 比對欄位的值在整份資料出現幾次 (偵測資料重名)

    QList<Item> items_;
    QList<int> retryRows_;
    bool retryMode_ = false;
    bool resultsShown_ = false;
    int printableCount_ = 0;
    int doneCount_ = 0;
    QString runDpiText_;

    std::atomic<bool> cancel_{ false };
    bool running_ = false;
    bool interactive_ = true;
    bool stoppedByError_ = false;
    QElapsedTimer jobTimer_;
    std::unique_ptr<QTemporaryDir> tempDir_;

    // 印表機
    QGroupBox* printerBox_ = nullptr;
    QComboBox* printerCombo_ = nullptr;
    QPushButton* statusBtn_ = nullptr;
    QComboBox* hopperCombo_ = nullptr;
    QComboBox* ejectCombo_ = nullptr;
    QCheckBox* checkHopperCheck_ = nullptr;
    QCheckBox* pollCheck_ = nullptr;
    QCheckBox* continueCheck_ = nullptr;
    QLabel* rotateLabel_ = nullptr;
    QComboBox* rotateCombo_ = nullptr;
    // 測試模式
    QGroupBox* dryBox_ = nullptr;
    QCheckBox* dryCheck_ = nullptr;
    QLineEdit* dryDirEdit_ = nullptr;
    QPushButton* dryBrowse_ = nullptr;
    QComboBox* dryDpiCombo_ = nullptr;
    // 範圍
    QGroupBox* rangeBox_ = nullptr;
    QRadioButton* radioCurrent_ = nullptr;
    QRadioButton* radioVisible_ = nullptr;
    QRadioButton* radioRange_ = nullptr;
    QSpinBox* fromSpin_ = nullptr;
    QSpinBox* toSpin_ = nullptr;
    QCheckBox* skipCheck_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    // 進度
    QProgressBar* progress_ = nullptr;
    QLabel* currentLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    // 按鈕
    QPushButton* startBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QPushButton* retryBtn_ = nullptr;
    QPushButton* exportLogBtn_ = nullptr;
    QPushButton* exportCsvBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;
};