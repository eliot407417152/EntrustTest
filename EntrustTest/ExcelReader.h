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

    // ---- 載入「所有欄位」：標題列下的每一欄都匯入，文字與圖片可任選其中的欄位當參考 ----

    // Excel 欄號：0 -> "A"、27 -> "AB"
    QString excelColumnName(int index);

    // 自動猜標題列：前 30 列中，第一個「非空白儲存格數達最大值 7 成以上」的列
    // (標題列有幾格空白時，不會被下面資料較滿的列搶走；上方只有標題文字的說明列也不會被選到)。0 起算；空表回傳 -1。
    int detectHeaderRow(const QList<QStringList>& raw);

    // 以 headerRow 為標題列，回傳要匯入的所有欄位名稱。
    //  - 標題空白、但底下有資料的欄位，命名為「欄C」(C = Excel 欄號)；整欄空白的略過
    //  - 標題重複時，後面的加上 (2)、(3)
    //  - 標題裡的 { } 換成 ( )，避免和 {欄位} 的語法衝突
    // columnIndexes (可省略) 會填入各欄位對應的 Excel 欄索引 (0 起算)。
    QStringList importFields(const QList<QStringList>& raw, int headerRow, QList<int>* columnIndexes = nullptr);

    // 以 headerRow 為標題列，匯入所有欄位建立資料表。
    bool buildDataTableAll(const QList<QStringList>& raw, int headerRow, const QString& path,
        DataTable* out, QString* error = nullptr);

} // namespace card