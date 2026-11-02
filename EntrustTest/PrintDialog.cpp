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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QTime>
#include <QTransform>
#include <QVBoxLayout>
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

} // namespace

// ================================================================ construction ===

PrintDialog::PrintDialog(const PrintInput& in, QWidget* parent) : QDialog(parent), in_(in) {
    setWindowTitle(tr("批次列印"));
    resize(980, 820);

    // 分析版面：用到哪些欄位、有沒有「自動比對圖片」的圖片元素
    for (const auto& e : in_.layout.elements) {
        const QString& src = (e.type == card::ElementType::Text) ? e.text : e.imagePath;
        if (e.type == card::ElementType::Image && e.imagePath.trimmed().isEmpty()) hasAutoImage_ = true;
        auto it = placeholderRe().globalMatch(src);
        while (it.hasNext()) usedFields_.insert(it.next().captured(1));
    }
    if (hasAutoImage_) {
        for (const auto& rec : in_.data.rows) {
            const QString k = rec.value(in_.matchField).trimmed().toLower();
            if (!k.isEmpty()) matchValueCount_[k]++;
        }
    }

    buildUi();
    loadSettings();
    updateModeUi();
    rebuildPlan();
}

void PrintDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

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
    auto* group = new QButtonGroup(this);
    group->addButton(radioCurrent_);
    group->addButton(radioVisible_);
    group->addButton(radioRange_);
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
    skipCheck_ = new QCheckBox(tr("略過有問題的資料 (版面用到的欄位空白、找不到圖片、重名)"));
    skipCheck_->setChecked(true);
    skipCheck_->setToolTip(tr("取消勾選時，有問題的資料仍會列印 (缺少的欄位/圖片會是空白或提示框)"));
    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);
    rl->addWidget(radioCurrent_);
    rl->addWidget(radioVisible_);
    rl->addWidget(rrow);
    rl->addWidget(skipCheck_);
    rl->addStretch(1);
    rl->addWidget(summaryLabel_);
    top->addWidget(rangeBox_, 2);
    root->addLayout(top);

    if (in_.previewOnly) {
        radioVisible_->setEnabled(false);
        radioRange_->setEnabled(false);
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
    timeLabel_->setMinimumWidth(240);
    prog->addWidget(timeLabel_);
    root->addLayout(prog);
    currentLabel_ = new QLabel(tr("尚未開始"));
    root->addWidget(currentLabel_);

    // ------------------------------------------------ 逐筆進度表 + Log
    auto* split = new QSplitter(Qt::Vertical);
    table_ = new QTableWidget(0, 5);
    table_->setHorizontalHeaderLabels({ tr("序號"), tr("姓名"), tr("學號"), tr("狀態"), tr("訊息") });
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
    root->addLayout(btns);
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
    startBtn_->setText(dry ? tr("輸出圖片") : tr("開始列印"));
}

void PrintDialog::setControlsEnabled(bool on) {
    printerBox_->setEnabled(on);
    dryBox_->setEnabled(on);
    rangeBox_->setEnabled(on);
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
    else {
        const int a = qMin(fromSpin_->value(), toSpin_->value());
        const int b = qMax(fromSpin_->value(), toSpin_->value());
        for (int r = a - 1; r <= b - 1 && r < n; ++r) rows << r;
    }
    return rows;
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

    for (const auto& e : in_.layout.elements) {                       // 指定路徑的圖片
        if (e.type != card::ElementType::Image || e.imagePath.trimmed().isEmpty()) continue;
        const QString path = card::substitute(e.imagePath, rec);
        if (!QFileInfo::exists(path)) p << tr("找不到圖片檔：%1").arg(path);
    }

    if (hasAutoImage_) {                                              // 依資料夾自動比對的圖片
        if (!in_.matcher.hasFolder()) {
            p << tr("尚未設定圖片資料夾");
        }
        else {
            const QString value = rec.value(in_.matchField);
            const card::ImageMatch m = in_.matcher.find(value);
            switch (m.status) {
            case card::ImageMatch::NoValue:
                if (!usedFields_.contains(in_.matchField)) p << tr("「%1」無資料，無法比對圖片").arg(in_.matchField);
                break;
            case card::ImageMatch::NotFound:  p << tr("找不到圖片"); break;
            case card::ImageMatch::Ambiguous: p << tr("%1 張圖片符合 (重名)").arg(m.candidates.size()); break;
            default: break;
            }
            const int same = matchValueCount_.value(value.trimmed().toLower());
            if (same > 1) p << tr("資料重名 ×%1，無法確定是哪張圖").arg(same);
        }
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

    const QStringList cells{ QString::number(it.row + 1), rec.value(QStringLiteral("姓名")),
                             rec.value(QStringLiteral("學號")), stateText, it.message };
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
    in_.matcher.applyTo(rec, in_.matchField);                 // 帶入依資料夾比對到的圖片

    QImage img = card::renderCard(in_.layout, rec, dpi);
    if (img.isNull()) { *error = tr("卡面渲染失敗"); return QString(); }

    QString path;
    if (dryRun) {
        path = QDir(outDir).filePath(QStringLiteral("%1_%2.png")
            .arg(it.row + 1, 4, 10, QLatin1Char('0'))
            .arg(sanitizeFileName(rec.value(QStringLiteral("姓名")))));
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
    currentLabel_->setText(tr("第 %1 / %2 張：%3 (%4)")
        .arg(seq).arg(total).arg(rec.value(QStringLiteral("姓名")), rec.value(QStringLiteral("學號"))));
    log(tr("[%1/%2] 開始：%3 (資料第 %4 筆)").arg(seq).arg(total)
        .arg(rec.value(QStringLiteral("姓名"))).arg(items_[itemIndex].row + 1));
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
    log(QStringLiteral("  #%1 %2 | %3").arg(items_[itemIndex].row + 1).arg(rec.value(QStringLiteral("姓名")), line));
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
    l << tr("圖片比對：資料夾 %1；比對欄位 %2")
        .arg(in_.matcher.hasFolder() ? in_.matcher.folder() : tr("(未設定)"), in_.matchField);
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