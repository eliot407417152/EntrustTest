// ImageMatcher: 依「資料欄位的值」到圖片資料夾中，找出檔名包含該值的圖片 (階段 2)。
//
// 規則：
//   1. 只看檔名 (不含副檔名)，不分大小寫。
//   2. 先找「檔名完全等於該值」的圖片；沒有的話，再找「檔名包含該值」的圖片。
//   3. 剛好 1 張 -> 採用；0 張 -> 找不到；2 張以上 -> 重名 (不自動挑選，避免貼錯照片)。
//   4. 欄位值是空白時一律視為「無資料」，不會比對 (否則空字串會符合所有檔案)。
#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include "CardLayout.h"

namespace card {

    // 圖片元素「圖片路徑」留空時，繪製會改用 Record 裡這兩個特殊鍵 (由 ImageMatcher::applyTo 填入)
    const QString& autoImageKey();       // 比對到的圖片完整路徑
    const QString& autoImageNoteKey();   // 找不到/重名時，要顯示在圖片框裡的提示文字

    // 每個「動態圖片」元素各自的結果：key = "@image:<元素id>" / "@imageNote:<元素id>"
    QString autoImageKeyFor(const QString& elementId);
    QString autoImageNoteKeyFor(const QString& elementId);

    struct ImageMatch {
        enum Status { NoValue, NotFound, Unique, Ambiguous };
        Status status = NoValue;
        QString path;                    // Unique 時才有值
        QStringList candidates;          // 符合的檔名 (Unique / Ambiguous)
        QString note() const;            // 給人看的提示 (Unique 時為空)
    };

    class ImageMatcher {
    public:
        // 設定並掃描資料夾 (不含子資料夾)。資料夾不存在回傳 false，並視為未設定。
        bool setFolder(const QString& dir);
        void rescan();                   // 檔案有增減時重新掃描
        QString folder() const { return folder_; }
        bool hasFolder() const { return !folder_.isEmpty(); }
        int fileCount() const { return files_.size(); }

        ImageMatch find(const QString& value) const;

        // 把 rec[field] 的比對結果寫進 rec 的特殊鍵；未設定資料夾時什麼都不做。
        // 階段 3 列印每一筆之前，也請呼叫它。
        void applyTo(Record& rec, const QString& field) const;

    private:
        struct File { QString name; QString path; QString baseLower; };
        QString folder_;
        QList<File> files_;
    };

    // 多個圖片資料夾的掃描結果 (key = 資料夾)。每個動態圖片元素有自己的資料夾與比對欄位，
    // 相同的資料夾只掃描一次。可以直接複製 (列印時交給對話框一份)。
    class ImageMatcherPool {
    public:
        bool ensure(const QString& folder);              // 還沒掃描就掃描；資料夾不存在回傳 false
        void ensureAll(const CardLayout& layout);        // 掃描版面中所有動態圖片用到的資料夾
        void rescan(const QString& folder);              // 資料夾內的圖片有增減時
        const ImageMatcher* find(const QString& folder) const;     // 沒掃描過 / 不存在 = nullptr
        QString folderProblem(const QString& folder) const;        // 資料夾有問題時的說明 (沒問題 = 空字串)
        ImageMatch match(const QString& folder, const QString& value) const;
        // 對版面中每個動態圖片元素，依「它自己的」資料夾與比對欄位，把結果寫進 rec 的 @image:<id> / @imageNote:<id>
        void applyTo(Record& rec, const CardLayout& layout) const;

    private:
        static QString normalize(const QString& folder);
        QHash<QString, ImageMatcher> byFolder_;
    };

} // namespace card