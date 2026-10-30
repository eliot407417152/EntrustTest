#pragma once
#include <QMainWindow>

class QComboBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void refreshPrinters();
    void browseImage(QLineEdit* target);
    void checkStatus();
    void startPrint();
    void appendLog(const QString& text);
    void setBusy(bool busy);

    QComboBox* printerCombo_;
    QComboBox* hopperCombo_;
    QComboBox* ejectCombo_;
    QCheckBox* duplexCheck_;
    QCheckBox* checkHopperCheck_;
    QCheckBox* pollCheck_;
    QCheckBox* magCheck_;
    QLineEdit* track1_;
    QLineEdit* track2_;
    QLineEdit* track3_;
    QLineEdit* frontText_;
    QLineEdit* frontImage_;
    QLineEdit* backText_;
    QLineEdit* backImage_;
    QPushButton* statusBtn_;
    QPushButton* printBtn_;
    QPlainTextEdit* log_;
};