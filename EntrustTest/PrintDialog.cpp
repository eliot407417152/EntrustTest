#include "PrintDialog.h"
#include "CardPainter.h"
#include "CardPrinter.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include "UiScale.h"
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QTime>
#include <QTransform>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>

namespace {

    QSettings appSettings() {
        return QSettings(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner"));
    }

    std::wstring w(const QString& s) { return s.toStdWString(); }

    QString sanitizeFileName(const QString& s) {
        QString r = s.trimmed();
        for (QChar& c : r)
            if (QStringLiteral("\\/:*?\"<>|").contains(c) || c.unicode() < 32) c = QLatin1Char('_');
        return r;
    }

    QString csvCell(QString s) {
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    }

    bool writeUtf8Bom(const QString& path, QString text) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));           // Windows 記事本/Excel 友善
        f.write("\xEF\xBB\xBF");
        f.write(text.toUtf8());
        return true;
    }

    QString fmtDuration(qint64 ms) {
        const qint64 s = ms / 1000;
        return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    }

    // 版面中用到的 {欄位}
    const QRegularExpression& placeholderRe() {
        static const QRegularExpression re(QStringLiteral("\\{([^{}]+)\\}"));
        return re;
    }

    // 進度表 / Log 用來辨識一筆資料的文字，例如「王小明 (315130106)」
    QString titleOf(const QStringList& labelFields, const card::Record& rec) {
        QStringList parts;
        for (const QString& f : labelFields) {
            const QString v = rec.value(f).trimmed();
            if (!v.isEmpty()) parts << v;
        }
        if (parts.isEmpty()) return QString();
        QString s = parts.first();
        if (parts.size() > 1) s += QStringLiteral(" (%1)").arg(parts.at(1));
        return s;
    }

} // namespace

// ================================================================ construction ===

// 取得表格目前選取的列所對應的 data.rows 索引 (依表格顯示順序)
static QList<int> rowsOfSelection(QTableWidget* t) {
    QList<int> tr_;
    for (const QModelIndex& m : t->selectionModel()->selectedRows()) tr_ << m.row();
    std::sort(tr_.begin(), tr_.end());
    QList<int> out;
    for (int r : tr_) if (auto* it = t->item(r, 0)) out << it->data(Qt::UserRole).toInt();
    return out;
}

PrintDialog::PrintDialog(const PrintInput& in, QWidget* parent) : QDialog(parent), in_(in) {
    setWindowTitle(tr("批次列印"));
    uiscale::fitSize(this, 980, 820);       // 依介面大小與螢幕可用範圍決定，不會超出螢幕

    // 分析版面：用到哪些欄位、有沒有「自動比對圖片」的圖片元素
    for (const auto& e : in_.layout.elements) {
        const bool dynImage = e.type == card::ElementType::Image && e.imageSource == card::ImageSource::Dynamic;
        const bool dynBarcode = (e.type == card::ElementType::Barcode || e.type == card::ElementType::QRCode) && e.barcodeSource == card::ImageSource::Dynamic;
        const QString src = e.type == card::ElementType::Text ? e.text : (dynImage || dynBarcode ? QString() : (e.type == card::ElementType::Image ? e.imagePath : QString()));
        if (dynBarcode && !e.barcodeField.trimmed().isEmpty()) usedFields_.insert(e.barcodeField.trimmed());
        if (dynImage) {                                            // 每個動態圖片各自統計「比對欄位」的值有沒有重名
            hasAutoImage_ = true;
            QHash<QString, int>& cnt = matchValueCount_[e.id];
            for (const auto& rec : in_.data.rows) {
                const QString k = rec.value(e.matchField).trimmed().toLower();
                if (!k.isEmpty()) cnt[k]++;
            }
        }
        auto it = placeholderRe().globalMatch(src);
        while (it.hasNext()) usedFields_.insert(it.next().captured(1));
    }
    if (in_.layout.background.print) {                         // 要印出的卡面底圖，路徑可含 {欄位}
        auto it = placeholderRe().globalMatch(in_.layout.background.path);
        while (it.hasNext()) usedFields_.insert(it.next().captured(1));
    }
    // 辨識每一筆資料用的欄位：優先用版面有用到的欄位 (照 Excel 欄位順序)，最多 2 個；不夠再用前面的欄位補
    for (const QString& f : in_.data.fields)
        if (labelFields_.size() < 2 && usedFields_.contains(f)) labelFields_ << f;
    for (const QString& f : in_.data.fields)
        if (labelFields_.size() < 2 && !labelFields_.contains(f)) labelFields_ << f;
    {   // 使用者上次在「進度表顯示欄位」選的欄位 (欄位仍存在才採用)；沒有紀錄就用上面自動挑選的結果
        QSettings s = appSettings();
        s.beginGroup(QStringLiteral("print"));
        if (s.contains(QStringLiteral("labelField1"))) {
            const QString f1 = s.value(QStringLiteral("labelField1")).toString();
            const QString f2 = s.value(QStringLiteral("labelField2")).toString();
            if (in_.data.fields.contains(f1)) {
                labelFields_.clear();
                labelFields_ << f1;
                if (!f2.isEmpty() && in_.data.fields.contains(f2)) labelFields_ << f2;
            }
        }
    }
    buildUi();
    loadSettings();
    updateModeUi();
    rebuildPlan();
}

void PrintDialog::buildUi() {
    // 外層：上方是「可捲動的內容區」，下方的功能按鈕固定在視窗底部。
    // 這樣即使螢幕解析度很低、視窗已縮到螢幕大小，內容也只是出現捲軸，按鈕列永遠看得到、按得到。
    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(0, 0, 0, 0);

    // ------------------------------------------------ 上半：印表機 + 範圍 (左右並排)
    auto* top = new QHBoxLayout;

    auto* left = new QVBoxLayout;
    printerBox_ = new QGroupBox(tr("印表機"));
    auto* pf = new QFormLayout(printerBox_);
    printerCombo_ = new QComboBox;
    auto* refreshBtn = new QPushButton(tr("重新整理"));
    statusBtn_ = new QPushButton(tr("查詢狀態"));
    auto* prow = new QWidget;
    auto* prl = new QHBoxLayout(prow);
    prl->setContentsMargins(0, 0, 0, 0);
    prl->addWidget(printerCombo_, 1);
    prl->addWidget(refreshBtn);
    prl->addWidget(statusBtn_);
    pf->addRow(tr("印表機"), prow);

    hopperCombo_ = new QComboBox;
    hopperCombo_->setEditable(true);
    hopperCombo_->addItems({ "1", "2", "3", "4", "5", "6", "r1" });
    ejectCombo_ = new QComboBox;
    ejectCombo_->addItems({ "Default", "Front", "Back" });
    auto* hrow = new QWidget;
    auto* hl = new QHBoxLayout(hrow);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(hopperCombo_, 1);
    hl->addWidget(new QLabel(tr("出卡側")));
    hl->addWidget(ejectCombo_, 1);
    pf->addRow(tr("進卡匣"), hrow);

    checkHopperCheck_ = new QCheckBox(tr("每張列印前檢查進卡匣是否為空"));
    pollCheck_ = new QCheckBox(tr("每張都等待印表機回報完成 (較慢但較安全)"));
    pollCheck_->setChecked(true);
    continueCheck_ = new QCheckBox(tr("某一張失敗時繼續處理下一張 (預設為停止)"));
    pf->addRow(checkHopperCheck_);
    pf->addRow(pollCheck_);
    pf->addRow(continueCheck_);

    rotateCombo_ = new QComboBox;
    rotateCombo_->addItems({ tr("順時針 90°"), tr("逆時針 90°") });
    rotateLabel_ = new QLabel(tr("直式卡面旋轉"));
    pf->addRow(rotateLabel_, rotateCombo_);
    const bool portrait = in_.layout.orientation == card::Orientation::Portrait;
    rotateLabel_->setVisible(portrait);
    rotateCombo_->setVisible(portrait);
    rotateCombo_->setToolTip(tr("印表機進卡為橫向；直式卡面需旋轉後才能印。方向請先用一張測試卡確認。"));
    topcoatCheck_ = new QCheckBox;
    pf->addRow(topcoatCheck_);
    if (in_.layout.topcoat.isDefault()) {
        topcoatCheck_->setText(tr("鍍膜：版面未設定 (沿用驅動程式設定)"));
        topcoatCheck_->setEnabled(false);
    }
    else {
        QString what = QString::fromStdWString(topcoat::describeBase(card::toTopcoatSpec(in_.layout.topcoat)));
        if (!in_.layout.topcoat.regions.isEmpty()) what += tr(" + %1 個額外範圍").arg(in_.layout.topcoat.regions.size());
        topcoatCheck_->setText(tr("套用版面的鍍膜設定：%1").arg(what));
        topcoatCheck_->setChecked(true);
        topcoatCheck_->setToolTip(tr("取消勾選時，本次列印不送出任何鍍膜 PrintTicket / Escape，完全沿用驅動程式設定"));
    }
    left->addWidget(printerBox_);

    dryBox_ = new QGroupBox(tr("測試模式"));
    auto* df = new QFormLayout(dryBox_);
    dryCheck_ = new QCheckBox(tr("不送印表機，只把每張卡面輸出成 PNG (用來先檢查版面與圖片)"));
    dryDirEdit_ = new QLineEdit;
    dryDirEdit_->setPlaceholderText(tr("輸出資料夾"));
    dryBrowse_ = new QPushButton(tr("選擇…"));
    dryDpiCombo_ = new QComboBox;
    dryDpiCombo_->addItems({ "300", "600" });
    auto* drow = new QWidget;
    auto* dl = new QHBoxLayout(drow);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->addWidget(dryDirEdit_, 1);
    dl->addWidget(dryBrowse_);
    dl->addWidget(new QLabel(tr("dpi")));
    dl->addWidget(dryDpiCombo_);
    df->addRow(dryCheck_);
    df->addRow(drow);
    left->addWidget(dryBox_);
    top->addLayout(left, 3);

    rangeBox_ = new QGroupBox(tr("列印範圍"));
    auto* rl = new QVBoxLayout(rangeBox_);
    radioCurrent_ = new QRadioButton(in_.previewOnly ? tr("以「預覽資料」欄位的內容印 1 張 (尚未載入 Excel)")
        : tr("目前選取的這一筆"));
    radioVisible_ = new QRadioButton(tr("資料表目前顯示的全部 (含搜尋過濾結果)"));
    radioRange_ = new QRadioButton(tr("指定範圍："));
    radioCustom_ = new QRadioButton(tr("自訂清單 (依欄位搜尋後加入，可不連續)"));
    auto* group = new QButtonGroup(this);
    group->addButton(radioCurrent_);
    group->addButton(radioVisible_);
    group->addButton(radioRange_);
    group->addButton(radioCustom_);
    const int n = int(in_.data.rows.size());
    fromSpin_ = new QSpinBox;
    toSpin_ = new QSpinBox;
    for (auto* s : { fromSpin_, toSpin_ }) { s->setRange(1, qMax(1, n)); }
    fromSpin_->setValue(1);
    toSpin_->setValue(qMax(1, n));
    auto* rrow = new QWidget;
    auto* rrl = new QHBoxLayout(rrow);
    rrl->setContentsMargins(0, 0, 0, 0);
    rrl->addWidget(radioRange_);
    rrl->addWidget(new QLabel(tr("第")));
    rrl->addWidget(fromSpin_);
    rrl->addWidget(new QLabel(tr("到")));
    rrl->addWidget(toSpin_);
    rrl->addWidget(new QLabel(tr("筆 (共 %1 筆)").arg(n)));
    rrl->addStretch(1);
    // ---- 自訂清單面板 (放在視窗整個寬度)：選欄位 + 關鍵字搜尋 → 把要印的資料加入清單 ----
    customPanel_ = new QWidget;
    auto* cpl = new QVBoxLayout(customPanel_);
    cpl->setContentsMargins(0, 0, 0, 0);
    fieldCombo_ = new QComboBox;
    fieldCombo_->addItem(tr("全部欄位"), QString());
    for (const QString& f : in_.data.fields) fieldCombo_->addItem(f, f);
    fieldCombo_->setMinimumWidth(160);
    keywordEdit_ = new QLineEdit;
    keywordEdit_->setPlaceholderText(tr("關鍵字 (留空 = 全部；多個關鍵字用逗號分隔，符合任一個即可)"));
    keywordEdit_->setClearButtonEnabled(true);
    keywordEdit_->installEventFilter(this);
    searchBtn_ = new QPushButton(tr("搜尋"));
    auto* srow = new QHBoxLayout;
    srow->addWidget(new QLabel(tr("搜尋欄位")));
    srow->addWidget(fieldCombo_);
    srow->addWidget(keywordEdit_, 1);
    srow->addWidget(searchBtn_);
    cpl->addLayout(srow);

    // 表格顯示「序號 + 所有資料欄位」，欄位多時可橫向捲動、拖曳調整欄寬
    auto makeTable = [this]() {
        auto* t = new QTableWidget(0, 1 + int(in_.data.fields.size()));
        QStringList hd{ tr("序號") };
        hd << in_.data.fields;
        t->setHorizontalHeaderLabels(hd);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::ExtendedSelection);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setAlternatingRowColors(true);
        t->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        t->verticalHeader()->setVisible(false);
        t->verticalHeader()->setDefaultSectionSize(22);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        t->setMinimumHeight(uiscale::px(120));      // 太高的最小高度會讓小螢幕的視窗放不下
        return t;
        };
    hitTable_ = makeTable();
    hitTable_->installEventFilter(this);
    pickedTable_ = makeTable();
    pickedTable_->installEventFilter(this);
    hitLabel_ = new QLabel(tr("搜尋結果：尚未搜尋"));
    pickedLabel_ = new QLabel;
    addSelBtn_ = new QPushButton(tr("加入選取 ▶ (Enter)"));
    addAllBtn_ = new QPushButton(tr("全部加入 ▶"));
    removeBtn_ = new QPushButton(tr("◀ 移除選取 (Del)"));
    clearBtn_ = new QPushButton(tr("清空清單"));
    for (auto* b : { searchBtn_, addSelBtn_, addAllBtn_, removeBtn_, clearBtn_ }) b->setAutoDefault(false);

    auto* leftW = new QWidget;
    auto* lcol = new QVBoxLayout(leftW);
    lcol->setContentsMargins(0, 0, 0, 0);
    lcol->addWidget(hitLabel_);
    lcol->addWidget(hitTable_, 1);
    auto* lbt = new QHBoxLayout;
    lbt->addWidget(addSelBtn_);
    lbt->addWidget(addAllBtn_);
    lbt->addStretch(1);
    lcol->addLayout(lbt);
    auto* rightW = new QWidget;
    auto* rcol = new QVBoxLayout(rightW);
    rcol->setContentsMargins(0, 0, 0, 0);
    rcol->addWidget(pickedLabel_);
    rcol->addWidget(pickedTable_, 1);
    auto* rbt = new QHBoxLayout;
    rbt->addWidget(removeBtn_);
    rbt->addWidget(clearBtn_);
    rbt->addStretch(1);
    rcol->addLayout(rbt);
    auto* tableSplit = new QSplitter(Qt::Horizontal);       // 左右可拖曳調整寬度
    tableSplit->addWidget(leftW);
    tableSplit->addWidget(rightW);
    tableSplit->setChildrenCollapsible(false);
    tableSplit->setStretchFactor(0, 1);
    tableSplit->setStretchFactor(1, 1);
    cpl->addWidget(tableSplit, 1);
    auto* keyHint = new QLabel(tr("快捷鍵：搜尋框 Enter = 搜尋、↓ = 跳到結果　|　結果表 Enter = 加入選取 (Ctrl+A 全選)、"
        "雙擊 = 加入　|　清單 Delete / Backspace = 移除、雙擊 = 移除　|　Ctrl+F = 回到搜尋框"));
    keyHint->setStyleSheet(QStringLiteral("color: gray;"));
    keyHint->setWordWrap(true);
    cpl->addWidget(keyHint);
    customPanel_->setVisible(false);
    refreshCustomUi();

    skipCheck_ = new QCheckBox(tr("略過有問題的資料 (版面用到的欄位空白、找不到圖片、重名)"));
    skipCheck_->setChecked(true);
    skipCheck_->setToolTip(tr("取消勾選時，有問題的資料仍會列印 (缺少的欄位/圖片會是空白或提示框)"));
    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);
    rl->addWidget(radioCurrent_);
    rl->addWidget(radioVisible_);
    rl->addWidget(rrow);
    rl->addWidget(radioCustom_);
    rl->addWidget(skipCheck_);
    rl->addStretch(1);
    rl->addWidget(summaryLabel_);
    top->addWidget(rangeBox_, 2);
    root->addLayout(top);
    root->addWidget(customPanel_, 2);      // 自訂清單面板：整個視窗寬度，選了「自訂清單」才顯示

    if (in_.previewOnly) {
        radioVisible_->setEnabled(false);
        radioRange_->setEnabled(false);
        radioCustom_->setEnabled(false);
        fromSpin_->setEnabled(false);
        toSpin_->setEnabled(false);
    }
    if (in_.currentRow >= 0) radioCurrent_->setChecked(true);
    else { radioCurrent_->setEnabled(false); radioVisible_->setChecked(true); }

    // ------------------------------------------------ 進度
    auto* prog = new QHBoxLayout;
    progress_ = new QProgressBar;
    progress_->setFormat(QStringLiteral("%v / %m"));
    prog->addWidget(progress_, 1);
    timeLabel_ = new QLabel;
    timeLabel_->setMinimumWidth(uiscale::px(240));
    prog->addWidget(timeLabel_);
    root->addLayout(prog);
    currentLabel_ = new QLabel(tr("尚未開始"));
    root->addWidget(currentLabel_);

    // ------------------------------------------------ 逐筆進度表 + Log
    auto* labelRow = new QHBoxLayout;
    labelRow->addWidget(new QLabel(tr("進度表顯示欄位：")));
    for (int k = 0; k < 2; ++k) {
        labelCombo_[k] = new QComboBox;
        if (k == 1) labelCombo_[k]->addItem(tr("(不顯示)"), QString());
        for (const QString& f : in_.data.fields) labelCombo_[k]->addItem(f, f);
        const int idx = labelCombo_[k]->findData(labelFields_.value(k));
        labelCombo_[k]->setCurrentIndex(idx >= 0 ? idx : 0);
        labelCombo_[k]->setMinimumWidth(uiscale::px(110));
        labelCombo_[k]->setToolTip(tr("進度表與列印記錄用這些欄位辨識每一筆資料；測試模式輸出的圖檔檔名用第 1 個欄位。\n選擇會記住，下次開啟自動套用。"));
        labelRow->addWidget(labelCombo_[k]);
        connect(labelCombo_[k], QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyLabelFields(); });
    }
    labelRow->addStretch(1);
    root->addLayout(labelRow);

    auto* split = new QSplitter(Qt::Vertical);
    table_ = new QTableWidget(0, 5);
    table_->setHorizontalHeaderLabels({ tr("序號"), labelFields_.value(0, tr("資料")), labelFields_.value(1), tr("狀態"), tr("訊息") });
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 60);
    table_->setColumnWidth(1, 130);
    table_->setColumnWidth(2, 110);
    table_->setColumnWidth(3, 70);
    split->addWidget(table_);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(20000);
    split->addWidget(log_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    // ------------------------------------------------ 按鈕
    auto* btns = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("開始列印"));
    startBtn_->setDefault(true);
    cancelBtn_ = new QPushButton(tr("取消"));
    cancelBtn_->setToolTip(tr("目前這張卡完成後停止；已送出的卡不會被中斷"));
    retryBtn_ = new QPushButton(tr("重試失敗項目"));
    exportLogBtn_ = new QPushButton(tr("匯出 Log…"));
    exportCsvBtn_ = new QPushButton(tr("匯出結果 CSV…"));
    closeBtn_ = new QPushButton(tr("關閉"));
    btns->addWidget(startBtn_);
    btns->addWidget(cancelBtn_);
    btns->addWidget(retryBtn_);
    btns->addStretch(1);
    btns->addWidget(exportLogBtn_);
    btns->addWidget(exportCsvBtn_);
    btns->addWidget(closeBtn_);
    outer->addLayout(btns);          // 按鈕列放在捲動區外面
    cancelBtn_->setEnabled(false);
    retryBtn_->setEnabled(false);

    // ------------------------------------------------ 連線
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refreshPrinters(); });
    connect(statusBtn_, &QPushButton::clicked, this, [this] {
        const std::wstring printer = w(printerCombo_->currentText());
        if (printer.empty()) return;
        statusBtn_->setEnabled(false);
        log(tr("查詢印表機狀態…"));
        auto* t = QThread::create([this, printer] {
            QString result;
            try { result = QString::fromStdWString(CardPrinter::queryStatus(printer, nullptr)); }
            catch (const std::exception& e) { result = tr("查詢失敗：") + QString::fromLocal8Bit(e.what()); }
            QMetaObject::invokeMethod(this, [this, result] { log(result); statusBtn_->setEnabled(!running_); },
                Qt::QueuedConnection);
            });
        connect(t, &QThread::finished, t, &QObject::deleteLater);
        t->start();
        });
    connect(dryCheck_, &QCheckBox::toggled, this, [this] { updateModeUi(); });
    connect(dryBrowse_, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("選擇輸出資料夾"), dryDirEdit_->text());
        if (!d.isEmpty()) dryDirEdit_->setText(QDir::toNativeSeparators(d));
        });

    auto replan = [this] { retryMode_ = false; rebuildPlan(); };
    connect(radioCurrent_, &QRadioButton::toggled, this, replan);
    connect(radioVisible_, &QRadioButton::toggled, this, replan);
    connect(radioRange_, &QRadioButton::toggled, this, replan);
    connect(fromSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, replan](int) { if (radioRange_->isChecked()) replan(); });
    connect(toSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, replan](int) { if (radioRange_->isChecked()) replan(); });
    connect(skipCheck_, &QCheckBox::toggled, this, replan);
    connect(radioCustom_, &QRadioButton::toggled, this, [this, replan](bool on) {
        customPanel_->setVisible(on);
        if (on) {       // 自動放大視窗，方便看欄位多的資料；大小依「這個視窗所在的螢幕」與介面大小決定，不會超出螢幕
            if (!property("preCustomSize").isValid()) setProperty("preCustomSize", size());   // 記住原本大小，取消自訂清單時還原
            const QRect av = uiscale::availableGeometry(this);
            const int fw = frameGeometry().width() - width(), fh = frameGeometry().height() - height();   // 邊框與標題列
            const int maxW = av.width() - fw, maxH = av.height() - fh;
            const int w = qMin(qMax(width(), uiscale::px(1300)), maxW);
            const int h = qMin(qMax(height(), uiscale::px(900)), maxH);
            resize(w, h);
            uiscale::clampToScreen(this);       // 放大後若跑出螢幕，移回螢幕內
            QTimer::singleShot(0, this, [this] { uiscale::clampToScreen(this); });   // layout 重算完再檢查一次
        }
        else if (property("preCustomSize").isValid()) {
            resize(property("preCustomSize").toSize());
            setProperty("preCustomSize", QVariant());
            uiscale::clampToScreen(this);
        }
        replan();
        });
    connect(searchBtn_, &QPushButton::clicked, this, [this] { runSearch(); });
    connect(addSelBtn_, &QPushButton::clicked, this, [this] { addToCustom(rowsOfSelection(hitTable_)); });
    connect(addAllBtn_, &QPushButton::clicked, this, [this] { addToCustom(hitRows_); });
    connect(hitTable_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        if (auto* it = hitTable_->item(r, 0)) addToCustom({ it->data(Qt::UserRole).toInt() });
        });
    connect(removeBtn_, &QPushButton::clicked, this, [this] { removeFromCustom(); });
    connect(pickedTable_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        pickedTable_->selectRow(r);
        removeFromCustom();
        });
    connect(clearBtn_, &QPushButton::clicked, this, [this] { customRows_.clear(); refreshCustomUi(); });

    connect(startBtn_, &QPushButton::clicked, this, [this] { startJob(); });
    connect(cancelBtn_, &QPushButton::clicked, this, [this] { requestCancel(); });
    connect(retryBtn_, &QPushButton::clicked, this, [this] { retryFailed(); });
    connect(closeBtn_, &QPushButton::clicked, this, [this] { reject(); });
    connect(exportLogBtn_, &QPushButton::clicked, this, [this] {
        const QString def = QStringLiteral("print_log_%1.txt").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        const QString path = QFileDialog::getSaveFileName(this, tr("匯出 Log"), def, tr("文字檔 (*.txt)"));
        if (path.isEmpty()) return;
        if (!exportLog(path)) QMessageBox::warning(this, tr("匯出失敗"), tr("無法寫入 %1").arg(path));
        });
    connect(exportCsvBtn_, &QPushButton::clicked, this, [this] {
        const QString def = QStringLiteral("print_result_%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        const QString path = QFileDialog::getSaveFileName(this, tr("匯出結果"), def, tr("CSV (*.csv)"));
        if (path.isEmpty()) return;
        if (!exportResultCsv(path)) QMessageBox::warning(this, tr("匯出失敗"), tr("無法寫入 %1").arg(path));
        });

    refreshPrinters();
}

void PrintDialog::refreshPrinters() {
    const QString keep = printerCombo_->currentText();
    printerCombo_->clear();
    int preferred = -1;
    for (const auto& n : CardPrinter::listPrinters()) {
        const QString name = QString::fromStdWString(n);
        printerCombo_->addItem(name);
        if (preferred < 0 && (name.contains(QStringLiteral("Card"), Qt::CaseInsensitive) ||
            name.contains(QStringLiteral("XPS"), Qt::CaseInsensitive)))
            preferred = printerCombo_->count() - 1;
    }
    const int again = keep.isEmpty() ? -1 : printerCombo_->findText(keep);
    if (again >= 0) printerCombo_->setCurrentIndex(again);
    else if (preferred >= 0) printerCombo_->setCurrentIndex(preferred);
}

void PrintDialog::loadSettings() {
    QSettings s = appSettings();
    s.beginGroup(QStringLiteral("print"));
    const int pi = printerCombo_->findText(s.value(QStringLiteral("printer")).toString());
    if (pi >= 0) printerCombo_->setCurrentIndex(pi);
    hopperCombo_->setCurrentText(s.value(QStringLiteral("hopper"), QStringLiteral("1")).toString());
    ejectCombo_->setCurrentIndex(qMax(0, ejectCombo_->findText(s.value(QStringLiteral("eject"), QStringLiteral("Default")).toString())));
    checkHopperCheck_->setChecked(s.value(QStringLiteral("checkHopper"), false).toBool());
    pollCheck_->setChecked(s.value(QStringLiteral("poll"), true).toBool());
    continueCheck_->setChecked(s.value(QStringLiteral("continueOnError"), false).toBool());
    rotateCombo_->setCurrentIndex(s.value(QStringLiteral("rotate"), 0).toInt() == 1 ? 1 : 0);
    dryDirEdit_->setText(s.value(QStringLiteral("dryDir")).toString());
    dryDpiCombo_->setCurrentIndex(qMax(0, dryDpiCombo_->findText(s.value(QStringLiteral("dryDpi"), QStringLiteral("300")).toString())));
    skipCheck_->setChecked(s.value(QStringLiteral("skipProblems"), true).toBool());
    // 「測試模式」每次開啟都預設關閉，避免誤以為有印出卡片
}

void PrintDialog::saveSettings() const {
    QSettings s = appSettings();
    s.beginGroup(QStringLiteral("print"));
    s.setValue(QStringLiteral("printer"), printerCombo_->currentText());
    s.setValue(QStringLiteral("hopper"), hopperCombo_->currentText());
    s.setValue(QStringLiteral("eject"), ejectCombo_->currentText());
    s.setValue(QStringLiteral("checkHopper"), checkHopperCheck_->isChecked());
    s.setValue(QStringLiteral("poll"), pollCheck_->isChecked());
    s.setValue(QStringLiteral("continueOnError"), continueCheck_->isChecked());
    s.setValue(QStringLiteral("rotate"), rotateCombo_->currentIndex());
    s.setValue(QStringLiteral("dryDir"), dryDirEdit_->text());
    s.setValue(QStringLiteral("dryDpi"), dryDpiCombo_->currentText());
    s.setValue(QStringLiteral("skipProblems"), skipCheck_->isChecked());
}

void PrintDialog::updateModeUi() {
    const bool dry = dryCheck_->isChecked();
    dryDirEdit_->setEnabled(dry);
    dryBrowse_->setEnabled(dry);
    dryDpiCombo_->setEnabled(dry);
    printerCombo_->setEnabled(!dry);
    statusBtn_->setEnabled(!dry && !running_);
    hopperCombo_->setEnabled(!dry);
    ejectCombo_->setEnabled(!dry);
    checkHopperCheck_->setEnabled(!dry);
    pollCheck_->setEnabled(!dry);
    rotateCombo_->setEnabled(!dry);
    if (in_.layout.topcoat.isDefault() == false) topcoatCheck_->setEnabled(!dry);
    startBtn_->setText(dry ? tr("輸出圖片") : tr("開始列印"));
}

void PrintDialog::applyLabelFields() {
    if (!labelCombo_[0] || !labelCombo_[1] || !table_) return;
    labelFields_.clear();
    labelFields_ << labelCombo_[0]->currentData().toString();
    const QString f2 = labelCombo_[1]->currentData().toString();
    if (!f2.isEmpty()) labelFields_ << f2;
    {
        QSettings s = appSettings();
        s.beginGroup(QStringLiteral("print"));
        s.setValue(QStringLiteral("labelField1"), labelFields_.value(0));
        s.setValue(QStringLiteral("labelField2"), labelFields_.value(1));
    }
    table_->setHorizontalHeaderItem(1, new QTableWidgetItem(labelFields_.value(0, tr("資料"))));
    table_->setHorizontalHeaderItem(2, new QTableWidgetItem(labelFields_.value(1)));
    for (int i = 0; i < items_.size(); ++i) updateRow(i);
}

void PrintDialog::setControlsEnabled(bool on) {
    if (labelCombo_[0]) { labelCombo_[0]->setEnabled(on); labelCombo_[1]->setEnabled(on); }   // 列印中不改，免得輸出檔名中途改變
    printerBox_->setEnabled(on);
    dryBox_->setEnabled(on);
    rangeBox_->setEnabled(on);
    customPanel_->setEnabled(on);
    startBtn_->setEnabled(on && printableCount_ > 0);
    cancelBtn_->setEnabled(!on);
    exportLogBtn_->setEnabled(on);
    exportCsvBtn_->setEnabled(on);
    closeBtn_->setEnabled(on);
    if (on) updateModeUi();
}

// ============================================================ range & checks ===

QList<int> PrintDialog::selectedRows() const {
    if (retryMode_) return retryRows_;
    const int n = int(in_.data.rows.size());
    QList<int> rows;
    if (radioCurrent_->isChecked()) {
        if (in_.currentRow >= 0 && in_.currentRow < n) rows << in_.currentRow;
    }
    else if (radioVisible_->isChecked()) {
        for (int r : in_.visibleRows) if (r >= 0 && r < n) rows << r;
    }
    else if (radioCustom_->isChecked()) {
        for (int r : customRows_) if (r >= 0 && r < n) rows << r;
    }
    else {
        const int a = qMin(fromSpin_->value(), toSpin_->value());
        const int b = qMax(fromSpin_->value(), toSpin_->value());
        for (int r = a - 1; r <= b - 1 && r < n; ++r) rows << r;
    }
    return rows;
}

// ============================================================ 自訂清單 ===

void PrintDialog::fillRowTable(QTableWidget* t, const QList<int>& rows, bool fitColumns) const {
    const QStringList& fields = in_.data.fields;
    t->setUpdatesEnabled(false);
    t->clearContents();
    t->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const int r = rows[i];
        const card::Record& rec = in_.data.rows[r];
        auto* no = new QTableWidgetItem(QString::number(r + 1));
        no->setData(Qt::UserRole, r);                       // 記住在 data.rows 內的索引
        t->setItem(i, 0, no);
        for (int f = 0; f < fields.size(); ++f)
            t->setItem(i, f + 1, new QTableWidgetItem(rec.value(fields[f])));
    }
    if (fitColumns) {                                        // 依內容調整欄寬 (每欄最寬 240 px，超過可自行拖曳)
        t->resizeColumnsToContents();
        for (int c = 0; c < t->columnCount(); ++c)
            if (t->columnWidth(c) > 240) t->setColumnWidth(c, 240);
    }
    t->setUpdatesEnabled(true);
}

void PrintDialog::markHitsInList() {
    QSet<int> have;
    for (int r : customRows_) have.insert(r);
    for (int i = 0; i < hitTable_->rowCount(); ++i) {
        auto* no = hitTable_->item(i, 0);
        if (!no) continue;
        const int r = no->data(Qt::UserRole).toInt();
        const bool in = have.contains(r);
        no->setText(in ? QStringLiteral("✓ %1").arg(r + 1) : QString::number(r + 1));
        for (int c = 0; c < hitTable_->columnCount(); ++c)
            if (auto* x = hitTable_->item(i, c)) x->setForeground(in ? QBrush(Qt::gray) : QBrush());
    }
}

void PrintDialog::runSearch() {
    const QString field = fieldCombo_->currentData().toString();      // 空 = 全部欄位
    QStringList keys;
    for (const QString& k : keywordEdit_->text().split(QRegularExpression(QStringLiteral("[,;，；、]")))) {
        const QString t = k.trimmed();
        if (!t.isEmpty()) keys << t;
    }
    hitRows_.clear();
    const int n = int(in_.data.rows.size());
    for (int r = 0; r < n; ++r) {
        const card::Record& rec = in_.data.rows[r];
        bool hit = keys.isEmpty();                                     // 沒輸入關鍵字 = 列出全部
        for (int i = 0; !hit && i < keys.size(); ++i) {
            if (field.isEmpty()) {
                for (const QString& f : in_.data.fields)
                    if (rec.value(f).contains(keys[i], Qt::CaseInsensitive)) { hit = true; break; }
            }
            else hit = rec.value(field).contains(keys[i], Qt::CaseInsensitive);
        }
        if (hit) hitRows_ << r;
    }
    fillRowTable(hitTable_, hitRows_, true);
    markHitsInList();
    hitLabel_->setText(tr("搜尋結果：%1 筆 (✓ = 已在清單內)").arg(hitRows_.size()));
}

void PrintDialog::addToCustom(const QList<int>& rows) {
    if (rows.isEmpty()) return;
    QSet<int> have;
    for (int r : customRows_) have.insert(r);
    for (int r : rows) {
        if (have.contains(r)) continue;                                // 已在清單內就不重複加入
        have.insert(r);
        customRows_ << r;
    }
    refreshCustomUi();
    pickedTable_->scrollToBottom();
}

void PrintDialog::removeFromCustom() {
    const QModelIndexList sel = pickedTable_->selectionModel()->selectedRows();
    const QList<int> del = rowsOfSelection(pickedTable_);
    if (del.isEmpty()) return;
    int firstRow = pickedTable_->rowCount();
    for (const QModelIndex& m : sel) firstRow = qMin(firstRow, m.row());
    QSet<int> gone;
    for (int r : del) gone.insert(r);
    QList<int> keep;
    for (int r : customRows_) if (!gone.contains(r)) keep << r;
    customRows_ = keep;
    refreshCustomUi();
    if (pickedTable_->rowCount() > 0)                                  // 連續按 Delete 時，選取會停在原位置的下一筆
        pickedTable_->selectRow(qMin(firstRow, pickedTable_->rowCount() - 1));
}

void PrintDialog::refreshCustomUi() {
    fillRowTable(pickedTable_, customRows_, pickedTable_->rowCount() == 0);
    markHitsInList();
    pickedLabel_->setText(tr("列印清單：%1 筆 (依加入順序列印)").arg(customRows_.size()));
    if (radioCustom_->isChecked()) { retryMode_ = false; rebuildPlan(); }   // 清單變動 → 重新規劃
}

bool PrintDialog::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::KeyPress && (obj == keywordEdit_ || obj == hitTable_ || obj == pickedTable_)) {
        const auto* ke = static_cast<QKeyEvent*>(ev);
        const int k = ke->key();
        const bool enter = (k == Qt::Key_Return || k == Qt::Key_Enter);
        if (obj != keywordEdit_ && k == Qt::Key_F && (ke->modifiers() & Qt::ControlModifier)) {
            keywordEdit_->setFocus();
            keywordEdit_->selectAll();
            return true;
        }
        if (obj == keywordEdit_) {
            if (enter) {
                runSearch();
                if (!hitRows_.isEmpty()) hitTable_->setFocus();        // 搜尋完直接把焦點交給結果表，可立刻選取
                return true;
            }
            if (k == Qt::Key_Down && hitTable_->rowCount() > 0) {
                hitTable_->setFocus();
                if (!hitTable_->currentIndex().isValid()) hitTable_->selectRow(0);
                return true;
            }
        }
        else if (obj == hitTable_) {
            if (enter) { addToCustom(rowsOfSelection(hitTable_)); return true; }
        }
        else {      // pickedTable_
            if (k == Qt::Key_Delete || k == Qt::Key_Backspace) { removeFromCustom(); return true; }
            if (enter) return true;
        }
        // 上面都吃掉 Enter，避免觸發預設按鈕「開始列印」
    }
    return QDialog::eventFilter(obj, ev);
}

// 回傳這一筆資料「會讓卡面不完整」的原因 (空 = 沒問題)。只檢查「版面真的用到」的東西。
QStringList PrintDialog::problemsOf(int row) const {
    QStringList p;
    const card::Record& rec = in_.data.rows[row];

    QStringList sorted(usedFields_.begin(), usedFields_.end());
    sorted.sort();
    for (const QString& f : sorted) {
        if (!rec.contains(f)) p << tr("版面用到不存在的欄位「%1」").arg(f);
        else if (rec.value(f).trimmed().isEmpty()) p << tr("「%1」無資料").arg(f);
    }

    if (in_.layout.background.print) {                                // 卡面底圖要印出，檔案不在就是問題 (不然會印出一張沒有底圖的卡)
        const QString bgPath = card::substitute(in_.layout.background.path, rec);
        if (in_.layout.background.isEmpty()) p << tr("卡面底圖已設定為印出，但未指定圖片檔");
        else if (!QFileInfo::exists(bgPath)) p << tr("找不到卡面底圖：%1").arg(bgPath);
    }

    for (const auto& e : in_.layout.elements) {
        if (e.type == card::ElementType::Barcode || e.type == card::ElementType::QRCode) {
            const QString err = card::checkCodeElement(e, rec);
            if (!err.isEmpty()) p << tr("條碼「%1」無法產生：%2").arg(e.name, err);
            continue;
        }
        if (e.type != card::ElementType::Image) continue;
        const QString who = tr("圖片「%1」").arg(e.name);
        if (e.imageSource == card::ImageSource::Fixed) {              // 固定圖片
            if (e.imagePath.trimmed().isEmpty()) { p << tr("%1 未指定圖片檔").arg(who); continue; }
            const QString path = card::substitute(e.imagePath, rec);
            if (!QFileInfo::exists(path)) p << tr("%1 找不到圖片檔：%2").arg(who, path);
            continue;
        }
        const QString folderProblem = in_.imagePool.folderProblem(e.imageFolder);   // 動態圖片：依它自己的資料夾與比對欄位
        if (!folderProblem.isEmpty()) { p << tr("%1 %2").arg(who, folderProblem); continue; }
        const QString value = rec.value(e.matchField);
        const card::ImageMatch m = in_.imagePool.match(e.imageFolder, value);
        switch (m.status) {
        case card::ImageMatch::NoValue:
            if (!usedFields_.contains(e.matchField)) p << tr("%1 的比對欄位「%2」無資料，無法比對圖片").arg(who, e.matchField);
            break;
        case card::ImageMatch::NotFound:  p << tr("%1 找不到圖片").arg(who); break;
        case card::ImageMatch::Ambiguous: p << tr("%1 有 %2 張圖片符合 (重名)").arg(who).arg(m.candidates.size()); break;
        default: break;
        }
        const int same = matchValueCount_.value(e.id).value(value.trimmed().toLower());
        if (same > 1) p << tr("%1 資料重名 ×%2，無法確定是哪張圖").arg(who).arg(same);
    }
    return p;
}

void PrintDialog::rebuildPlan() {
    if (running_) return;
    resultsShown_ = false;
    items_.clear();
    doneCount_ = 0;

    for (int row : selectedRows()) {
        Item it;
        it.row = row;
        const QStringList problems = problemsOf(row);
        if (!problems.isEmpty() && skipCheck_->isChecked()) {
            it.state = State::Skipped;
            it.message = tr("略過：") + problems.join(QStringLiteral("；"));
        }
        else {
            it.state = State::Pending;
            if (!problems.isEmpty()) it.message = tr("⚠ ") + problems.join(QStringLiteral("；"));
        }
        items_.append(it);
    }

    table_->setUpdatesEnabled(false);
    table_->clearContents();
    table_->setRowCount(items_.size());
    for (int i = 0; i < items_.size(); ++i) updateRow(i);
    table_->setUpdatesEnabled(true);

    progress_->setRange(0, 1);
    progress_->setValue(0);
    currentLabel_->setText(tr("尚未開始"));
    timeLabel_->clear();
    updateSummary();
}

void PrintDialog::updateSummary() {
    int pending = 0, skipped = 0, warn = 0;
    for (const Item& it : items_) {
        if (it.state == State::Pending) { ++pending; if (!it.message.isEmpty()) ++warn; }
        else if (it.state == State::Skipped) ++skipped;
    }
    printableCount_ = pending;
    QString s = tr("將處理 %1 筆：列印 %2 筆、略過 %3 筆").arg(items_.size()).arg(pending).arg(skipped);
    if (warn > 0) s += tr("\n其中 %1 筆有問題但仍會列印 (已取消「略過有問題的資料」)").arg(warn);
    if (skipped > 0) {
        for (const Item& it : items_) {
            if (it.state != State::Skipped) continue;
            QString reason = it.message;
            reason.remove(tr("略過："));
            s += tr("\n例如第 %1 筆被略過：%2 (全部原因請看下方表格「訊息」欄)").arg(it.row + 1).arg(reason);
            break;
        }
    }
    summaryLabel_->setText(s);
    startBtn_->setEnabled(!running_ && printableCount_ > 0);
}

void PrintDialog::updateRow(int i) {
    const Item& it = items_[i];
    const card::Record& rec = in_.data.rows[it.row];

    QString stateText;
    QColor bg;
    switch (it.state) {
    case State::Pending:     stateText = tr("待印");   break;
    case State::Skipped:     stateText = tr("略過");   bg = QColor(235, 235, 235); break;
    case State::Printing:    stateText = tr("列印中"); bg = QColor(210, 230, 255); break;
    case State::Done:        stateText = tr("完成");   bg = QColor(220, 245, 220); break;
    case State::Failed:      stateText = tr("失敗");   bg = QColor(255, 215, 215); break;
    case State::Unprocessed: stateText = tr("未處理"); bg = QColor(245, 245, 225); break;
    }
    if (it.state == State::Pending && !it.message.isEmpty()) bg = QColor(255, 243, 190);

    const QStringList cells{ QString::number(it.row + 1), rec.value(labelFields_.value(0)),
                             rec.value(labelFields_.value(1)), stateText, it.message };
    for (int c = 0; c < cells.size(); ++c) {
        QTableWidgetItem* x = table_->item(i, c);
        if (!x) { x = new QTableWidgetItem; table_->setItem(i, c, x); }
        x->setText(cells[c]);
        x->setToolTip(c == 4 ? it.message : QString());
        if (bg.isValid()) x->setBackground(bg); else x->setBackground(QBrush());
    }
}

// ================================================================== running ===

void PrintDialog::startJob() {
    if (running_) return;
    if (resultsShown_ && !retryMode_) rebuildPlan();       // 上一輪的結果還在表上：重新依目前設定規劃

    QList<int> todo;
    for (int i = 0; i < items_.size(); ++i) if (items_[i].state == State::Pending) todo << i;
    if (todo.isEmpty()) return;

    const bool dry = dryCheck_->isChecked();
    QString outDir;
    if (dry) {
        outDir = dryDirEdit_->text().trimmed();
        if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
            if (interactive_) QMessageBox::warning(this, tr("測試模式"), tr("請先選擇可寫入的輸出資料夾。"));
            return;
        }
    }
    else {
        if (printerCombo_->currentText().isEmpty()) {
            if (interactive_) QMessageBox::warning(this, tr("列印"), tr("請選擇印表機。"));
            return;
        }
        if (interactive_) {
            const auto r = QMessageBox::question(this, tr("確認列印"),
                tr("即將在「%1」列印 %2 張卡片，卡片送出後無法收回。\n確定要開始嗎？")
                .arg(printerCombo_->currentText()).arg(todo.size()));
            if (r != QMessageBox::Yes) return;
        }
        tempDir_ = std::make_unique<QTemporaryDir>();
        if (!tempDir_->isValid()) {
            if (interactive_) QMessageBox::warning(this, tr("列印"), tr("無法建立暫存資料夾。"));
            return;
        }
    }
    saveSettings();

    // ---- 準備執行 ----
    PrintOptions base;
    base.printerName = w(printerCombo_->currentText());
    base.hopperId = w(hopperCombo_->currentText().trimmed().toLower());
    base.cardEjectSide = w(ejectCombo_->currentText());
    base.duplex = false;                                  // 目前卡面模型只有正面
    base.checkHopper = checkHopperCheck_->isChecked();
    base.pollCompletion = pollCheck_->isChecked();
    base.magstripe = false;
    base.fullPageImage = true;
    if (!dry) base.topcoat = topcoatForPrinter();             // 鍍膜 (階段 4)
    const bool continueOnError = continueCheck_->isChecked();
    const int dryDpi = dryDpiCombo_->currentText().toInt();
    const std::wstring printer = base.printerName;

    running_ = true;
    stoppedByError_ = false;
    cancel_ = false;
    doneCount_ = 0;
    setControlsEnabled(false);
    retryBtn_->setEnabled(false);
    progress_->setRange(0, todo.size());
    progress_->setValue(0);
    jobTimer_.start();
    log_->clear();
    log(tr("=== 批次%1開始 ===").arg(dry ? tr("輸出圖片") : tr("列印")));
    for (const QString& line : settingsSummary().split(QLatin1Char('\n'))) log(line);
    log(tr("處理 %1 筆 (範圍共 %2 筆，其中略過 %3 筆)")
        .arg(todo.size()).arg(items_.size()).arg(items_.size() - todo.size()));
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].state == State::Skipped) logItem(i, items_[i].message);

    const int total = todo.size();
    auto* t = QThread::create([this, todo, total, dry, outDir, dryDpi, printer, base, continueOnError] {
        auto post = [this](auto fn) { QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection); };

        // 1) 決定渲染解析度
        int dpi = dryDpi;
        QString fatal;
        if (!dry) {
            try { dpi = CardPrinter::queryResolutionDpi(printer, nullptr); }
            catch (const std::exception& e) { fatal = QString::fromLocal8Bit(e.what()); }
        }
        if (!fatal.isEmpty()) {
            post([this, fatal] {
                stoppedByError_ = true;
                log(tr("無法連線印表機：%1").arg(fatal));
                finishJob();
                });
            return;
        }
        post([this, dpi, dry] { log(tr("渲染解析度：%1 dpi%2").arg(dpi).arg(dry ? tr(" (測試模式)") : tr(" (依印表機)"))); });

        // 2) 逐張處理
        for (int k = 0; k < total; ++k) {
            if (cancel_) break;
            const int i = todo[k];
            post([this, i, k, total] { markPrinting(i, k + 1, total); });

            QElapsedTimer tm;
            tm.start();
            QString path, err;
            // 渲染在 UI 執行緒做 (字型/圖片快取最安全)，工作執行緒等它完成
            QMetaObject::invokeMethod(this, [&] { path = renderItem(i, dpi, dry, outDir, &err); },
                Qt::BlockingQueuedConnection);

            bool ok = false;
            QString msg;
            if (!err.isEmpty()) {
                msg = err;
            }
            else if (dry) {
                ok = true;
                msg = QDir::toNativeSeparators(path);
            }
            else {
                PrintOptions o = base;
                o.frontImagePath = w(path);
                try {
                    CardPrinter::print(o, [this, post, i](const std::wstring& line) {
                        const QString q = QString::fromStdWString(line);
                        post([this, i, q] { logItem(i, q); });
                        });
                    ok = true;
                    msg = base.pollCompletion ? tr("印表機已回報完成") : tr("已送出");
                }
                catch (const std::exception& e) {
                    msg = QString::fromLocal8Bit(e.what());
                }
                QFile::remove(path);
            }
            const double sec = tm.elapsed() / 1000.0;
            post([this, i, ok, msg, sec] { setItemResult(i, ok, msg, sec); });
            if (!ok && !continueOnError) {
                post([this] { stoppedByError_ = true; });
                break;
            }
        }
        post([this] { finishJob(); });
        });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

QString PrintDialog::renderItem(int itemIndex, int dpi, bool dryRun, const QString& outDir, QString* error) {
    const Item& it = items_[itemIndex];
    card::Record rec = in_.data.rows[it.row];
    in_.imagePool.applyTo(rec, in_.layout);                   // 帶入各動態圖片 (依它自己的資料夾與欄位) 比對到的圖片

    card::CardLayout lay = in_.layout;                        // 每一筆資料的內容不同：複製一份版面再依該筆貼合
    card::fitAll(lay, rec);
    QImage img = card::renderCard(lay, rec, dpi);
    if (img.isNull()) { *error = tr("卡面渲染失敗"); return QString(); }

    QString path;
    if (dryRun) {
        path = QDir(outDir).filePath(QStringLiteral("%1_%2.png")
            .arg(it.row + 1, 4, 10, QLatin1Char('0'))
            .arg(sanitizeFileName(rec.value(labelFields_.value(0)))));
    }
    else {
        // 印表機進卡為橫向：直式卡面要旋轉後才送出 (測試模式輸出的是卡面原樣，不旋轉)
        if (in_.layout.orientation == card::Orientation::Portrait) {
            const int angle = rotateCombo_->currentIndex() == 1 ? -90 : 90;
            img = img.transformed(QTransform().rotate(angle), Qt::SmoothTransformation);
        }
        path = tempDir_->filePath(QStringLiteral("card_%1.png").arg(it.row + 1));
    }
    img = img.convertToFormat(QImage::Format_RGB888);          // 不含透明通道，避免驅動端/GDI+ 處理 alpha
    if (!img.save(path, "PNG")) {
        *error = tr("無法寫入圖片：%1").arg(QDir::toNativeSeparators(path));
        return QString();
    }
    return path;
}

void PrintDialog::markPrinting(int itemIndex, int seq, int total) {
    items_[itemIndex].state = State::Printing;
    items_[itemIndex].message.clear();
    updateRow(itemIndex);
    table_->scrollToItem(table_->item(itemIndex, 3));
    const card::Record& rec = in_.data.rows[items_[itemIndex].row];
    currentLabel_->setText(tr("第 %1 / %2 張：%3")
        .arg(seq).arg(total).arg(titleOf(labelFields_, rec)));
    log(tr("[%1/%2] 開始：%3 (資料第 %4 筆)").arg(seq).arg(total)
        .arg(titleOf(labelFields_, rec)).arg(items_[itemIndex].row + 1));
}

void PrintDialog::setItemResult(int itemIndex, bool ok, const QString& message, double seconds) {
    Item& it = items_[itemIndex];
    it.state = ok ? State::Done : State::Failed;
    it.message = message;
    it.seconds = seconds;
    ++doneCount_;
    updateRow(itemIndex);
    log(tr("    -> %1：%2 (%3 秒)").arg(ok ? tr("完成") : tr("失敗"), message).arg(seconds, 0, 'f', 1));
    updateProgress();
}

void PrintDialog::updateProgress() {
    progress_->setValue(doneCount_);
    const qint64 elapsed = jobTimer_.elapsed();
    QString t = tr("已用 %1").arg(fmtDuration(elapsed));
    const int remaining = progress_->maximum() - doneCount_;
    if (doneCount_ > 0 && remaining > 0 && running_)
        t += tr("，預估剩餘 %1").arg(fmtDuration(elapsed / doneCount_ * remaining));
    timeLabel_->setText(t);
}

void PrintDialog::requestCancel() {
    if (!running_ || cancel_) return;
    cancel_ = true;
    cancelBtn_->setEnabled(false);
    log(tr("已要求取消：目前這張卡完成後就會停止 (已送出的卡不會被中斷)。"));
}

void PrintDialog::finishJob() {
    int done = 0, failed = 0, skipped = 0, unprocessed = 0;
    for (Item& it : items_) {
        if (it.state == State::Pending || it.state == State::Printing) {
            it.state = State::Unprocessed;
            it.message = cancel_ ? tr("已取消，未處理") : tr("前一張失敗而停止，未處理");
        }
    }
    for (int i = 0; i < items_.size(); ++i) {
        updateRow(i);
        switch (items_[i].state) {
        case State::Done: ++done; break;
        case State::Failed: ++failed; break;
        case State::Skipped: ++skipped; break;
        case State::Unprocessed: ++unprocessed; break;
        default: break;
        }
    }

    running_ = false;
    resultsShown_ = true;
    retryMode_ = false;
    tempDir_.reset();
    setControlsEnabled(true);
    retryBtn_->setEnabled(failed > 0);
    updateProgress();
    currentLabel_->setText(tr("結束：完成 %1、失敗 %2、略過 %3、未處理 %4").arg(done).arg(failed).arg(skipped).arg(unprocessed));
    log(tr("=== 結束：完成 %1、失敗 %2、略過 %3、未處理 %4；總耗時 %5 ===")
        .arg(done).arg(failed).arg(skipped).arg(unprocessed).arg(fmtDuration(jobTimer_.elapsed())));

    if (interactive_ && (failed > 0 || unprocessed > 0)) {
        QMessageBox::warning(this, tr("批次結束"),
            tr("完成 %1 張；失敗 %2 張；未處理 %3 張。\n詳細原因請看表格「訊息」欄或匯出 Log。")
            .arg(done).arg(failed).arg(unprocessed));
    }
}

void PrintDialog::retryFailed() {
    if (running_) return;
    retryRows_.clear();
    for (const Item& it : items_) if (it.state == State::Failed) retryRows_ << it.row;
    if (retryRows_.isEmpty()) return;
    retryMode_ = true;
    rebuildPlan();
    startJob();
}

void PrintDialog::reject() {
    if (running_) {
        log(tr("列印進行中，無法關閉。請先按「取消」，等目前這張完成後再關閉。"));
        return;
    }
    saveSettings();
    QDialog::reject();
}

// ===================================================================== log ===

void PrintDialog::log(const QString& line) {
    log_->appendPlainText(QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), line));
}

void PrintDialog::logItem(int itemIndex, const QString& line) {
    const card::Record& rec = in_.data.rows[items_[itemIndex].row];
    log(QStringLiteral("  #%1 %2 | %3").arg(items_[itemIndex].row + 1).arg(titleOf(labelFields_, rec), line));
}

// 版面的鍍膜設定 → 印表機座標。直式卡面會先旋轉 90° 才送出，範圍必須用「同一個方向」換算。
TopcoatSpec PrintDialog::topcoatForPrinter() const {
    // 不用 isEnabled()：列印中整個群組會被停用，但設定仍然有效
    if (in_.layout.topcoat.isDefault() || !topcoatCheck_->isChecked() || dryCheck_->isChecked()) return TopcoatSpec();
    TopcoatSpec spec = card::toTopcoatSpec(in_.layout.topcoat);
    if (in_.layout.orientation == card::Orientation::Portrait) {
        const bool clockwise = rotateCombo_->currentIndex() == 0;       // 與 renderItem() 的影像旋轉方向一致
        spec = topcoat::rotatePortraitToLandscape(spec, in_.layout.widthMm(), in_.layout.heightMm(), clockwise);
    }
    return spec;
}

QString PrintDialog::settingsSummary() const {
    QStringList l;
    l << tr("時間：%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    if (dryCheck_->isChecked()) {
        l << tr("模式：測試 (只輸出圖片)；輸出資料夾：%1").arg(dryDirEdit_->text());
    }
    else {
        l << tr("印表機：%1").arg(printerCombo_->currentText());
        l << tr("進卡匣：%1；出卡側：%2；列印前檢查進卡匣：%3；等待完成：%4；失敗時繼續：%5")
            .arg(hopperCombo_->currentText(), ejectCombo_->currentText(),
                checkHopperCheck_->isChecked() ? tr("是") : tr("否"),
                pollCheck_->isChecked() ? tr("是") : tr("否"),
                continueCheck_->isChecked() ? tr("是") : tr("否"));
    }
    const bool portrait = in_.layout.orientation == card::Orientation::Portrait;
    l << tr("卡面：%1 (%2 × %3 mm)；元素 %4 個")
        .arg(portrait ? tr("直式") : tr("橫式")).arg(in_.layout.widthMm(), 0, 'f', 1)
        .arg(in_.layout.heightMm(), 0, 'f', 1).arg(in_.layout.elements.size());
    if (!dryCheck_->isChecked())
        l << tr("鍍膜：%1").arg(topcoatForPrinter().isDefault() ? tr("不送出 (沿用驅動程式設定)")
            : QString::fromStdWString(topcoat::describe(topcoatForPrinter())));
    if (!in_.layout.background.isEmpty())
        l << tr("卡面底圖：%1 (%2)").arg(in_.layout.background.path, in_.layout.background.print ? tr("印出") : tr("不印出"));
    for (const auto& e : in_.layout.elements) {
        if (e.type == card::ElementType::Barcode || e.type == card::ElementType::QRCode) {
            l << tr("條碼「%1」：%2，%3").arg(e.name,
                e.type == card::ElementType::QRCode ? QStringLiteral("QR Code") : card::barcodeTypeNames().value(static_cast<int>(e.barcodeType)),
                e.barcodeSource == card::ImageSource::Fixed ? tr("固定 %1").arg(e.barcodeValue) : tr("動態欄位 %1").arg(e.barcodeField));
            continue;
        }
        if (e.type != card::ElementType::Image) continue;
        if (e.imageSource == card::ImageSource::Fixed)
            l << tr("圖片「%1」：固定 %2").arg(e.name, e.imagePath.isEmpty() ? tr("(未指定)") : e.imagePath);
        else
            l << tr("圖片「%1」：依資料比對，資料夾 %2；比對欄位 %3").arg(e.name, e.imageFolder.isEmpty() ? tr("(未設定)") : e.imageFolder, e.matchField);
    }
    l << tr("略過有問題的資料：%1").arg(skipCheck_->isChecked() ? tr("是") : tr("否"));
    return l.join(QLatin1Char('\n'));
}

bool PrintDialog::exportLog(const QString& path) const {
    QString text = tr("# 批次列印 Log\n") + settingsSummary() + QStringLiteral("\n\n") + log_->toPlainText() + QLatin1Char('\n');
    return writeUtf8Bom(path, text);
}

bool PrintDialog::exportResultCsv(const QString& path) const {
    QStringList lines;
    QStringList head{ tr("序號") };
    head += in_.data.fields;
    head << tr("狀態") << tr("訊息") << tr("耗時(秒)");
    QStringList h;
    for (const QString& c : head) h << csvCell(c);
    lines << h.join(QLatin1Char(','));

    auto stateText = [](State s) {
        switch (s) {
        case State::Pending: return QStringLiteral("待印");
        case State::Skipped: return QStringLiteral("略過");
        case State::Printing: return QStringLiteral("列印中");
        case State::Done: return QStringLiteral("完成");
        case State::Failed: return QStringLiteral("失敗");
        default: return QStringLiteral("未處理");
        }
        };
    for (const Item& it : items_) {
        const card::Record& rec = in_.data.rows[it.row];
        QStringList cells{ csvCell(QString::number(it.row + 1)) };
        for (const QString& f : in_.data.fields) cells << csvCell(rec.value(f));
        cells << csvCell(stateText(it.state)) << csvCell(it.message) << csvCell(QString::number(it.seconds, 'f', 1));
        lines << cells.join(QLatin1Char(','));
    }
    return writeUtf8Bom(path, lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}