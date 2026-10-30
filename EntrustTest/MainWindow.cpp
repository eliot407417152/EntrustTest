#include "MainWindow.h"
#include "CardPrinter.h"

#include <QCheckBox>
#include <QDir>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <exception>

namespace {
    std::wstring w(const QString& s) { return s.toStdWString(); }

    QWidget* withBrowse(QLineEdit* edit, QPushButton* btn) {
        auto* box = new QWidget;
        auto* l = new QHBoxLayout(box);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(edit, 1);
        l->addWidget(btn);
        return box;
    }
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Datacard 證卡列印"));

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);

    // ---- 印表機 ----
    auto* printerBox = new QGroupBox(tr("印表機與進卡設定"));
    auto* pf = new QFormLayout(printerBox);

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
    pf->addRow(tr("進卡匣"), hopperCombo_);

    ejectCombo_ = new QComboBox;
    ejectCombo_->addItems({ "Default", "Front", "Back" });
    pf->addRow(tr("出卡側"), ejectCombo_);

    duplexCheck_ = new QCheckBox(tr("雙面列印"));
    checkHopperCheck_ = new QCheckBox(tr("列印前檢查進卡匣是否為空"));
    pollCheck_ = new QCheckBox(tr("等待印表機回報工作完成"));
    pollCheck_->setChecked(true);
    pf->addRow(duplexCheck_);
    pf->addRow(checkHopperCheck_);
    pf->addRow(pollCheck_);
    root->addWidget(printerBox);

    // ---- 內容 ----
    auto* contentBox = new QGroupBox(tr("卡面內容"));
    auto* cf = new QFormLayout(contentBox);
    frontText_ = new QLineEdit(tr("王小明"));
    frontImage_ = new QLineEdit;
    auto* frontBrowse = new QPushButton(tr("選擇…"));
    backText_ = new QLineEdit;
    backImage_ = new QLineEdit;
    auto* backBrowse = new QPushButton(tr("選擇…"));
    cf->addRow(tr("正面文字"), frontText_);
    cf->addRow(tr("正面圖片"), withBrowse(frontImage_, frontBrowse));
    cf->addRow(tr("背面文字"), backText_);
    cf->addRow(tr("背面圖片"), withBrowse(backImage_, backBrowse));
    root->addWidget(contentBox);

    // ---- 磁條 ----
    auto* magBox = new QGroupBox(tr("磁條編碼 (需 ISO 磁條模組)"));
    auto* mf = new QFormLayout(magBox);
    magCheck_ = new QCheckBox(tr("寫入磁條"));
    track1_ = new QLineEdit("ABC 123");
    track2_ = new QLineEdit("456");
    track3_ = new QLineEdit("789");
    mf->addRow(magCheck_);
    mf->addRow("Track 1", track1_);
    mf->addRow("Track 2", track2_);
    mf->addRow("Track 3", track3_);
    for (auto* e : { track1_, track2_, track3_ }) e->setEnabled(false);
    connect(magCheck_, &QCheckBox::toggled, this, [this](bool on) {
        for (auto* e : { track1_, track2_, track3_ }) e->setEnabled(on);
        });
    root->addWidget(magBox);

    // ---- 動作與 log ----
    printBtn_ = new QPushButton(tr("列印"));
    printBtn_->setDefault(true);
    root->addWidget(printBtn_);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMinimumHeight(140);
    root->addWidget(log_, 1);

    setCentralWidget(central);
    resize(640, 780);

    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshPrinters);
    connect(statusBtn_, &QPushButton::clicked, this, &MainWindow::checkStatus);
    connect(printBtn_, &QPushButton::clicked, this, &MainWindow::startPrint);
    connect(frontBrowse, &QPushButton::clicked, this, [this] { browseImage(frontImage_); });
    connect(backBrowse, &QPushButton::clicked, this, [this] { browseImage(backImage_); });

    refreshPrinters();
}

void MainWindow::refreshPrinters() {
    printerCombo_->clear();
    int preferred = -1;
    for (const auto& n : CardPrinter::listPrinters()) {
        const QString name = QString::fromStdWString(n);
        printerCombo_->addItem(name);
        // 預設選名稱含 Card / XPS 的印表機
        if (preferred < 0 && (name.contains("Card", Qt::CaseInsensitive) || name.contains("XPS", Qt::CaseInsensitive)))
            preferred = printerCombo_->count() - 1;
    }
    if (preferred >= 0) printerCombo_->setCurrentIndex(preferred);
}

void MainWindow::browseImage(QLineEdit* target) {
    const QString f = QFileDialog::getOpenFileName(this, tr("選擇圖片"), QString(),
        tr("圖片 (*.bmp *.png *.jpg *.jpeg *.gif *.tif)"));
    if (!f.isEmpty()) target->setText(QDir::toNativeSeparators(f));
}

void MainWindow::appendLog(const QString& text) { log_->appendPlainText(text); }

void MainWindow::setBusy(bool busy) {
    printBtn_->setEnabled(!busy);
    statusBtn_->setEnabled(!busy);
    setCursor(busy ? Qt::BusyCursor : Qt::ArrowCursor);
}

// SDK 的呼叫都是同步且可能等很久 (輪詢工作完成)，所以一律放到背景執行緒，
// 並用 queued 方式把 log 送回 UI 執行緒。
void MainWindow::checkStatus() {
    const std::wstring printer = w(printerCombo_->currentText());
    setBusy(true);
    auto* t = QThread::create([this, printer] {
        QString result;
        try {
            result = QString::fromStdWString(CardPrinter::queryStatus(printer, nullptr));
        }
        catch (const std::exception& e) {
            result = tr("查詢失敗: ") + QString::fromLocal8Bit(e.what());
        }
        QMetaObject::invokeMethod(this, [this, result] { appendLog(result); setBusy(false); },
            Qt::QueuedConnection);
        });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void MainWindow::startPrint() {
    PrintOptions o;
    o.printerName = w(printerCombo_->currentText());
    o.hopperId = w(hopperCombo_->currentText().trimmed().toLower());
    o.cardEjectSide = w(ejectCombo_->currentText());
    o.duplex = duplexCheck_->isChecked();
    o.checkHopper = checkHopperCheck_->isChecked();
    o.pollCompletion = pollCheck_->isChecked();
    o.magstripe = magCheck_->isChecked();
    o.track1 = w(track1_->text());
    o.track2 = w(track2_->text());
    o.track3 = w(track3_->text());
    o.frontText = w(frontText_->text());
    o.backText = w(backText_->text());
    o.frontImagePath = w(frontImage_->text().trimmed());
    o.backImagePath = w(backImage_->text().trimmed());

    setBusy(true);
    appendLog(tr("--- 開始列印: %1 ---").arg(printerCombo_->currentText()));

    auto* t = QThread::create([this, o] {
        const CardPrinter::LogFn log = [this](const std::wstring& line) {
            const QString q = QString::fromStdWString(line);
            QMetaObject::invokeMethod(this, [this, q] { appendLog(q); }, Qt::QueuedConnection);
            };
        QString error;
        try {
            CardPrinter::print(o, log);
        }
        catch (const std::exception& e) {
            error = QString::fromLocal8Bit(e.what());
        }
        QMetaObject::invokeMethod(this, [this, error] {
            if (error.isEmpty()) appendLog(tr("--- 列印流程結束 ---"));
            else {
                appendLog(tr("錯誤: ") + error);
                QMessageBox::warning(this, tr("列印失敗"), error);
            }
            setBusy(false);
            }, Qt::QueuedConnection);
        });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}