// ExcelReader: 讀取 .xlsx 製卡資料 (階段 2)。
// 不依賴任何第三方函式庫：內含簡易 ZIP 讀取 + inflate 解壓縮 + SpreadsheetML 解析。
#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include "CardLayout.h"

namespace card {

    // 載入後的製卡資料表
    struct DataTable {
        QString path;                 // 來源檔案
        QStringList fields;           // 欄位名稱 (= 載入時要求的欄位)
        QList<Record> rows;           // 每一筆：欄位名稱 -> 值 (已去除前後空白)
        int skippedBlankRows = 0;     // 被略過的全空白列數量
    };

    // 讀取活頁簿「第一個工作表」的所有儲存格 (文字)。rows[列][欄]，缺的儲存格為空字串。
    bool readXlsxFirstSheet(const QString& path, QList<QStringList>* rows, QString* error = nullptr);

    // 讀取 xlsx，依「標題列的欄位名稱」找出 requiredFields 各欄。
    // 標題列會在前 30 列內自動尋找；缺少欄位時回傳 false 並在 error 列出缺少的欄位。
    bool loadExcelTable(const QString& path, const QStringList& requiredFields,
        DataTable* out, QString* error = nullptr);

} // namespace card