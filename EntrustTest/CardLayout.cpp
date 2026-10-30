#include "CardLayout.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace card {

    namespace {
        QString typeToStr(ElementType t) { return t == ElementType::Image ? QStringLiteral("image") : QStringLiteral("text"); }
        ElementType strToType(const QString& s) { return s == QLatin1String("image") ? ElementType::Image : ElementType::Text; }

        QString alignToStr(HAlign a) {
            switch (a) {
            case HAlign::Center: return QStringLiteral("center");
            case HAlign::Right: return QStringLiteral("right");
            default: return QStringLiteral("left");
            }
        }
        HAlign strToAlign(const QString& s) {
            if (s == QLatin1String("center")) return HAlign::Center;
            if (s == QLatin1String("right")) return HAlign::Right;
            return HAlign::Left;
        }
    }

    // ------------------------------------------------------------------ Element ---

    bool Element::isDynamic() const {
        const QString& s = (type == ElementType::Text) ? text : imagePath;
        const int a = s.indexOf(QLatin1Char('{'));
        return a >= 0 && s.indexOf(QLatin1Char('}'), a) > a;
    }

    QJsonObject Element::toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["name"] = name;
        o["type"] = typeToStr(type);
        o["x"] = xMm;
        o["y"] = yMm;
        o["w"] = wMm;
        o["h"] = hMm;
        o["text"] = text;
        o["fontFamily"] = fontFamily;
        o["fontPt"] = fontPt;
        o["bold"] = bold;
        o["italic"] = italic;
        o["color"] = color.name(QColor::HexArgb);
        o["align"] = alignToStr(align);
        o["imagePath"] = imagePath;
        o["keepAspect"] = keepAspect;
        return o;
    }

    Element Element::fromJson(const QJsonObject& o) {
        Element e;
        e.id = o.value("id").toString();
        e.name = o.value("name").toString();
        e.type = strToType(o.value("type").toString());
        e.xMm = o.value("x").toDouble(e.xMm);
        e.yMm = o.value("y").toDouble(e.yMm);
        e.wMm = qMax(1.0, o.value("w").toDouble(e.wMm));
        e.hMm = qMax(1.0, o.value("h").toDouble(e.hMm));
        e.text = o.value("text").toString(e.text);
        e.fontFamily = o.value("fontFamily").toString(e.fontFamily);
        e.fontPt = qMax(1.0, o.value("fontPt").toDouble(e.fontPt));
        e.bold = o.value("bold").toBool(false);
        e.italic = o.value("italic").toBool(false);
        const QColor c(o.value("color").toString());
        if (c.isValid()) e.color = c;
        e.align = strToAlign(o.value("align").toString());
        e.imagePath = o.value("imagePath").toString();
        e.keepAspect = o.value("keepAspect").toBool(true);
        return e;
    }

    // --------------------------------------------------------------- CardLayout ---

    double CardLayout::widthMm() const {
        return orientation == Orientation::Landscape ? kCardLongMm : kCardShortMm;
    }
    double CardLayout::heightMm() const {
        return orientation == Orientation::Landscape ? kCardShortMm : kCardLongMm;
    }

    Element* CardLayout::find(const QString& id) {
        for (auto& e : elements) if (e.id == id) return &e;
        return nullptr;
    }
    const Element* CardLayout::find(const QString& id) const {
        for (const auto& e : elements) if (e.id == id) return &e;
        return nullptr;
    }

    QString CardLayout::nextId() const {
        int maxN = 0;
        for (const auto& e : elements)
            if (e.id.startsWith(QLatin1String("e_"))) maxN = qMax(maxN, e.id.mid(2).toInt());
        return QStringLiteral("e_%1").arg(maxN + 1);
    }

    QJsonObject CardLayout::toJson() const {
        QJsonObject o;
        o["version"] = 1;
        o["orientation"] = orientation == Orientation::Portrait ? QStringLiteral("portrait") : QStringLiteral("landscape");
        QJsonArray arr;
        for (const auto& e : elements) arr.append(e.toJson());
        o["elements"] = arr;
        return o;
    }

    CardLayout CardLayout::fromJson(const QJsonObject& o) {
        CardLayout l;
        l.orientation = o.value("orientation").toString() == QLatin1String("portrait")
            ? Orientation::Portrait : Orientation::Landscape;
        const QJsonArray arr = o.value("elements").toArray();
        for (const auto& v : arr) {
            Element e = Element::fromJson(v.toObject());
            if (e.id.isEmpty() || l.find(e.id)) e.id = l.nextId();   // 避免缺 id 或重複
            l.elements.append(e);
        }
        return l;
    }

    bool CardLayout::saveToFile(const QString& path, QString* error) const {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = f.errorString();
            return false;
        }
        f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
        return true;
    }

    bool CardLayout::loadFromFile(const QString& path, CardLayout* out, QString* error) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            if (error) *error = f.errorString();
            return false;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error) *error = pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("不是有效的版面檔");
            return false;
        }
        *out = fromJson(doc.object());
        return true;
    }

    // --------------------------------------------------------------------- misc ---

    QString substitute(const QString& tmpl, const Record& rec) {
        QString s = tmpl;
        for (auto it = rec.constBegin(); it != rec.constEnd(); ++it)
            s.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value());
        return s;
    }

    const QStringList& fieldNames() {
        // 階段 2：欄位固定為製卡資料 (Excel) 的這五欄，標題列名稱必須一致
        static const QStringList names{
            QStringLiteral("姓名"), QStringLiteral("學號"), QStringLiteral("系所"),
            QStringLiteral("內碼"), QStringLiteral("外碼") };
        return names;
    }

} // namespace card