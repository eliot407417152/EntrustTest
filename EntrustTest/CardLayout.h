// CardLayout: 卡面「資料模型」(與 UI、列印完全無關)
// 預覽、匯出、列印都吃同一份模型，確保所見即所得。
#pragma once
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

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

    struct CardLayout {
        Orientation orientation = Orientation::Landscape;
        QList<Element> elements;          // 順序 = 疊放順序 (後面的在上層)

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

    // 把 "{姓名}" 之類的欄位換成 rec 內的值；找不到的欄位保持原樣
    QString substitute(const QString& tmpl, const Record& rec);

    // 可用的動態欄位 (之後載入製卡檔案時會以實際欄位取代)
    const QStringList& fieldNames();

} // namespace card