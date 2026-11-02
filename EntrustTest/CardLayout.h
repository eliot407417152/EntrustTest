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
    enum class ElementType { Text, Image };
    enum class HAlign { Left = 0, Center = 1, Right = 2 };

    // 一筆製卡資料：欄位名稱 -> 值，例如 {"姓名":"王小明"}
    using Record = QMap<QString, QString>;

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

        // 圖片 (路徑可含 {欄位}，階段 2 會接上照片資料夾)
        QString imagePath;
        bool keepAspect = true;

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

    struct CardLayout {
        Orientation orientation = Orientation::Landscape;
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