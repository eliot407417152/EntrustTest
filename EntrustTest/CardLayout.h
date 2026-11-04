// CardLayout: 卡面「資料模型」(與 UI、列印完全無關)
// 預覽、匯出、列印都吃同一份模型，確保所見即所得。
#pragma once
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include "TopcoatSpec.h"

namespace card {

    constexpr double kCardLongMm = 85.6;    // CR80
    constexpr double kCardShortMm = 53.98;

    enum class Orientation { Landscape, Portrait };
    enum class ElementType { Text, Image, Barcode, QRCode };
    enum class HAlign { Left = 0, Center = 1, Right = 2 };
    // 條碼種類 (數值會存進版面檔的字串 key，順序可調整，但 key 不要改)
    enum class BarcodeType { Code128 = 0, Code39, EAN13, EAN8, UPCA, ITF, Codabar };
    const QStringList& barcodeTypeNames();                 // 下拉選單顯示名稱 (順序 = BarcodeType 數值)
    QString barcodeTypeKey(BarcodeType t);                 // 版面檔用的 key
    BarcodeType barcodeTypeFromKey(const QString& k);      // 找不到 = Code39 (相容舊版面檔)

    // ------------------------------------------------------------ 座標系 ---
    // 模型 (JSON 版面檔、渲染、鍍膜 Escape) 一律以「卡面左上角」為原點、Y 往下，單位 mm。
    // 操作介面 (屬性面板 / 尺規 / 游標座標) 則以「卡面左下角」為原點、Y 往上顯示與輸入，
    // 元素的 (X, Y) = 該元素「左下角」到卡面左下角的距離。兩者只在介面邊界換算，
    // 所以舊的版面檔與列印流程完全不受影響。
    //   uiY = cardH - (yMm + hMm)        yMm = cardH - uiY - hMm
    inline double toUiY(double yMm, double hMm, double cardHMm) { return cardHMm - (yMm + hMm); }
    inline double fromUiY(double uiY, double hMm, double cardHMm) { return cardHMm - uiY - hMm; }
    // 只有 Y 座標的點 (游標 / 尺規) 用：
    inline double flipY(double yMm, double cardHMm) { return cardHMm - yMm; }

    // 一筆製卡資料：欄位名稱 -> 值，例如 {"姓名":"王小明"}
    using Record = QMap<QString, QString>;

    // 圖片來源：固定 = 每張卡都用同一張 (imagePath)；動態 = 依每一筆資料在 imageFolder 比對 (見 ImageMatcher.h)
    enum class ImageSource { Fixed = 0, Dynamic = 1 };
    // 圖片放進框裡的方式 (框的大小由使用者決定，不會被圖片比例鎖死)：
    //   Cover       裁切填滿：維持比例放大到蓋滿整個框，超出框的部分裁掉
    //   Contain     縮放適應：維持比例縮到整張放進框裡 (比例不同時會留白)
    //   Stretch     拉伸：直接拉成框的大小 (比例會改變)
    //   MatchHeight 框高依圖片比例：寬度不變，框的高度自動等於 寬 x 圖片長寬比
    enum class ImageFit { Cover = 0, Contain = 1, Stretch = 2, MatchHeight = 3 };

    struct Element {
        QString id;                       // 唯一，例如 e_3
        QString name;                     // 清單顯示名稱
        ElementType type = ElementType::Text;

        // 位置與大小 (單位 mm，原點為卡片左上角)
        double xMm = 5.0, yMm = 5.0, wMm = 40.0, hMm = 8.0;

        // 文字 (內容可含 {姓名} 之類的動態欄位)
        QString text = QStringLiteral("文字");
        QString fontFamily = QStringLiteral("Microsoft JhengHei");
        double fontPt = 10.0;
        bool bold = false;
        bool italic = false;
        QColor color = Qt::black;
        HAlign align = HAlign::Left;

        // 圖片：每個圖片元素各自有一組設定，所以不同的圖片可以用不同的來源 / 資料夾 / 比對欄位。
        ImageSource imageSource = ImageSource::Dynamic;
        QString imagePath;                // 固定圖片：路徑 (可含 {欄位})
        QString imageFolder;              // 動態圖片：圖片資料夾
        QString matchField;               // 動態圖片：用哪個欄位的值去比對檔名
        ImageFit imageFit = ImageFit::Cover;
        double imageZoomPct = 100.0;      // 額外縮放 % (Cover / Contain 才有作用)
        double imageFocusX = 50.0;        // 圖片在框內的水平位置：0 = 靠左 (裁切時保留左邊) … 100 = 靠右
        double imageFocusY = 50.0;        // 垂直位置：0 = 靠上 … 100 = 靠下
        bool keepAspect = true;           // 舊欄位，只為了相容舊版面檔；實際以 imageFit 為準

        // 貼合內容：文字框大小跟隨文字、圖片框高度跟隨圖片比例 (見 CardPainter.h 的 fitElement)。
        // 預設 false，所以舊版面檔不受影響。
        bool fitContent = false;

        // 條碼：固定內容，或依每筆製卡資料指定欄位產生；barcodeType 為條碼種類。
        BarcodeType barcodeType = BarcodeType::Code128;
        // barcodeSource = Fixed 時使用 barcodeValue；Dynamic 時使用 barcodeField。
        ImageSource barcodeSource = ImageSource::Fixed;
        QString barcodeValue;             // 固定條碼內容
        QString barcodeField;             // 動態條碼：對應 Excel / Record 欄位名稱
        // QR Code (type == QRCode)：資料來源沿用 barcodeSource / barcodeValue / barcodeField，
        // 另外多一個容錯等級 qrEcc：0 = L (7%)、1 = M (15%)、2 = Q (25%)、3 = H (30%)。
        int qrEcc = 1;

        bool isDynamic() const;           // 內容是否含 {欄位}
        QJsonObject toJson() const;
        static Element fromJson(const QJsonObject& o);
    };

    // ---------------------------------------------------------------- 鍍膜 (階段 4) ---
    // 基本設定走 PrintTicket；額外的「範圍」走 Escape (~TA / ~TR / ~PB)。詳見 TopcoatSpec.h。
    enum class TopcoatBase { DriverDefault = 0, All = 1, Except = 2 };
    enum class TopcoatException { Chip7816 = 0, MagJis = 1, MagIso2 = 2, MagIso3 = 3 };
    enum class RegionKind {
        Add = 0,       // 鍍膜區 (~TA)：整面先關閉鍍膜，只在範圍內鍍膜
        Remove = 1,    // 移除鍍膜 (~TR)：範圍內不鍍膜
        Block = 2      // 列印阻擋 (~PB)：範圍內不列印
    };

    struct TopcoatRegion {
        QString id;                       // r_1, r_2 ...
        RegionKind kind = RegionKind::Remove;
        // 位置與大小 (單位 mm，原點為「設計中卡面」的左上角，與元素相同)
        double xMm = 10.0, yMm = 10.0, wMm = 20.0, hMm = 15.0;

        QJsonObject toJson() const;
        static TopcoatRegion fromJson(const QJsonObject& o);
    };

    struct TopcoatSettings {
        TopcoatBase base = TopcoatBase::DriverDefault;
        TopcoatException exception = TopcoatException::Chip7816;   // base == Except 時才有作用
        QList<TopcoatRegion> regions;

        bool isDefault() const { return base == TopcoatBase::DriverDefault && regions.isEmpty(); }
        TopcoatRegion* find(const QString& id);
        const TopcoatRegion* find(const QString& id) const;
        QString nextId() const;

        QJsonObject toJson() const;
        static TopcoatSettings fromJson(const QJsonObject& o);
    };

    // ------------------------------------------------------------ 卡面底圖 ---
    // 匯入「卡面的圖」(例如已設計好的卡面樣板)，設計時墊在所有元素的最底層，方便對位文字與圖片。
    //   print = false (預設)：只在設計畫面顯示當參考，預覽輸出 / 列印時不會印出。
    //   print = true        ：渲染卡面時會先畫這張底圖，再疊上各元素 (預覽輸出 / 批次列印都會印出)。
    // 一律鋪滿整張卡面；路徑可含 {欄位}，這樣不同資料可以用不同的卡面。
    struct CardBackground {
        QString path;                     // 圖片檔路徑 (空字串 = 沒有底圖)
        ImageFit fit = ImageFit::Cover;   // Cover / Contain / Stretch (不使用 MatchHeight)
        double focusX = 50.0;             // 裁切 / 留白時圖片的水平位置 0 ~ 100 %
        double focusY = 50.0;             // 垂直位置 0 ~ 100 %
        bool showInDesigner = true;       // 設計畫面是否顯示 (只影響編輯畫面)
        bool print = false;               // 是否印出

        bool isEmpty() const { return path.trimmed().isEmpty(); }
        QJsonObject toJson() const;
        static CardBackground fromJson(const QJsonObject& o);
    };

    struct CardLayout {
        Orientation orientation = Orientation::Landscape;
        CardBackground background;        // 卡面底圖 (舊版面檔沒有這一項 = 無底圖)
        QList<Element> elements;          // 順序 = 疊放順序 (後面的在上層)
        TopcoatSettings topcoat;          // 鍍膜設定 (舊版面檔沒有這一項 = 沿用驅動程式設定)

        double widthMm() const;
        double heightMm() const;

        Element* find(const QString& id);
        const Element* find(const QString& id) const;
        QString nextId() const;

        QJsonObject toJson() const;
        static CardLayout fromJson(const QJsonObject& o);
        bool saveToFile(const QString& path, QString* error = nullptr) const;
        static bool loadFromFile(const QString& path, CardLayout* out, QString* error = nullptr);
    };

    // 把鍍膜設定換成 Qt-free 的 TopcoatSpec (座標仍是「設計中卡面」的座標：top = y、left = x)。
    // 直式卡面要列印時，另外呼叫 topcoat::rotatePortraitToLandscape() 依旋轉方向換算。
    ::TopcoatSpec toTopcoatSpec(const TopcoatSettings& t);

    // 把 "{姓名}" 之類的欄位換成 rec 內的值；找不到的欄位保持原樣
    QString substitute(const QString& tmpl, const Record& rec);

    // 可用的動態欄位 (之後載入製卡檔案時會以實際欄位取代)
    const QStringList& fieldNames();

} // namespace card