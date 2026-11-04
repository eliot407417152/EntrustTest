#include "CardLayout.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace card {

    namespace {
        QString typeToStr(ElementType t) {
            if (t == ElementType::Image) return QStringLiteral("image");
            if (t == ElementType::Barcode) return QStringLiteral("barcode");
            if (t == ElementType::QRCode) return QStringLiteral("qrcode");
            return QStringLiteral("text");
        }
        ElementType strToType(const QString& s) {
            if (s == QLatin1String("image")) return ElementType::Image;
            if (s == QLatin1String("barcode")) return ElementType::Barcode;
            if (s == QLatin1String("qrcode")) return ElementType::QRCode;
            return ElementType::Text;
        }

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

    namespace {
        QString fitToStr(ImageFit f) {
            switch (f) {
            case ImageFit::Contain: return QStringLiteral("contain");
            case ImageFit::Stretch: return QStringLiteral("stretch");
            case ImageFit::MatchHeight: return QStringLiteral("matchHeight");
            default: return QStringLiteral("cover");
            }
        }
        ImageFit strToFit(const QString& s) {
            if (s == QLatin1String("contain")) return ImageFit::Contain;
            if (s == QLatin1String("stretch")) return ImageFit::Stretch;
            if (s == QLatin1String("matchHeight")) return ImageFit::MatchHeight;
            return ImageFit::Cover;
        }
    }

    // ------------------------------------------------------------ BarcodeType ---
    namespace {
        struct BcInfo { const char* key; const char* name; };
        const BcInfo kBc[] = {
            {"code128", "Code 128"}, {"code39", "Code 39"}, {"ean13", "EAN-13"}, {"ean8", "EAN-8"},
            {"upca", "UPC-A"}, {"itf", "ITF (Interleaved 2 of 5)"}, {"codabar", "Codabar"} };
    }
    const QStringList& barcodeTypeNames() {
        static const QStringList l = [] { QStringList r; for (const auto& b : kBc) r << QString::fromUtf8(b.name); return r; }();
        return l;
    }
    QString barcodeTypeKey(BarcodeType t) { return QString::fromLatin1(kBc[static_cast<int>(t)].key); }
    BarcodeType barcodeTypeFromKey(const QString& k) {
        for (int i = 0; i < int(sizeof(kBc) / sizeof(kBc[0])); ++i) if (k == QLatin1String(kBc[i].key)) return static_cast<BarcodeType>(i);
        return BarcodeType::Code39;
    }

    // ------------------------------------------------------------------ Element ---

    bool Element::isDynamic() const {
        if (type == ElementType::Image && imageSource == ImageSource::Dynamic) return true;
        if ((type == ElementType::Barcode || type == ElementType::QRCode) && barcodeSource == ImageSource::Dynamic) return true;
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
        o["barcodeSource"] = barcodeSource == ImageSource::Fixed ? QStringLiteral("fixed") : QStringLiteral("dynamic");
        o["barcodeType"] = barcodeTypeKey(barcodeType);
        o["barcodeValue"] = barcodeValue;
        o["qrEcc"] = qrEcc;
        o["barcodeField"] = barcodeField;
        o["imagePath"] = imagePath;
        o["keepAspect"] = keepAspect;
        o["fitContent"] = fitContent;
        o["imageSource"] = imageSource == ImageSource::Fixed ? QStringLiteral("fixed") : QStringLiteral("dynamic");
        o["imageFolder"] = imageFolder;
        o["matchField"] = matchField;
        o["imageFit"] = fitToStr(imageFit);
        o["imageZoomPct"] = imageZoomPct;
        o["imageFocusX"] = imageFocusX;
        o["imageFocusY"] = imageFocusY;
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
        e.barcodeSource = o.value("barcodeSource").toString() == QLatin1String("dynamic") ? ImageSource::Dynamic : ImageSource::Fixed;
        e.barcodeType = barcodeTypeFromKey(o.value("barcodeType").toString());   // 舊版面檔沒有此欄 = Code 39
        e.barcodeValue = o.value("barcodeValue").toString(e.barcodeValue);
        e.qrEcc = qBound(0, o.value("qrEcc").toInt(1), 3);
        e.barcodeField = o.value("barcodeField").toString();
        e.imagePath = o.value("imagePath").toString();
        e.keepAspect = o.value("keepAspect").toBool(true);
        e.fitContent = o.value("fitContent").toBool(false);
        // 圖片來源：舊版面檔沒有這個欄位 -> 路徑留空 = 動態 (依資料夾比對)，有路徑 = 固定
        if (o.contains("imageSource")) e.imageSource = o.value("imageSource").toString() == QLatin1String("fixed") ? ImageSource::Fixed : ImageSource::Dynamic;
        else e.imageSource = e.imagePath.trimmed().isEmpty() ? ImageSource::Dynamic : ImageSource::Fixed;
        e.imageFolder = o.value("imageFolder").toString();
        e.matchField = o.value("matchField").toString();
        // 顯示方式：舊版面檔 -> 貼合內容 = 框高依圖片比例；保持長寬比 = 縮放適應；否則拉伸
        if (o.contains("imageFit")) e.imageFit = strToFit(o.value("imageFit").toString());
        else e.imageFit = e.fitContent ? ImageFit::MatchHeight : (e.keepAspect ? ImageFit::Contain : ImageFit::Stretch);
        e.imageZoomPct = qBound(10.0, o.value("imageZoomPct").toDouble(100.0), 400.0);
        e.imageFocusX = qBound(0.0, o.value("imageFocusX").toDouble(50.0), 100.0);
        e.imageFocusY = qBound(0.0, o.value("imageFocusY").toDouble(50.0), 100.0);
        if (e.type == ElementType::Image) e.fitContent = (e.imageFit == ImageFit::MatchHeight);   // 圖片的 fitContent 與「框高依圖片比例」同步
        return e;
    }

    // ------------------------------------------------------------------ 鍍膜 ---

    namespace {
        QString kindToStr(RegionKind k) {
            switch (k) {
            case RegionKind::Add: return QStringLiteral("add");
            case RegionKind::Block: return QStringLiteral("block");
            default: return QStringLiteral("remove");
            }
        }
        RegionKind strToKind(const QString& s) {
            if (s == QLatin1String("add")) return RegionKind::Add;
            if (s == QLatin1String("block")) return RegionKind::Block;
            return RegionKind::Remove;
        }
        QString baseToStr(TopcoatBase b) {
            switch (b) {
            case TopcoatBase::All: return QStringLiteral("all");
            case TopcoatBase::Except: return QStringLiteral("except");
            case TopcoatBase::None: return QStringLiteral("none");
            default: return QStringLiteral("driver");
            }
        }
        TopcoatBase strToBase(const QString& s) {
            if (s == QLatin1String("all")) return TopcoatBase::All;
            if (s == QLatin1String("except")) return TopcoatBase::Except;
            if (s == QLatin1String("none")) return TopcoatBase::None;
            return TopcoatBase::DriverDefault;
        }
        QString excToStr(TopcoatException x) {
            switch (x) {
            case TopcoatException::MagJis: return QStringLiteral("magJis");
            case TopcoatException::MagIso2: return QStringLiteral("magIso2");
            case TopcoatException::MagIso3: return QStringLiteral("magIso3");
            default: return QStringLiteral("chip7816");
            }
        }
        TopcoatException strToExc(const QString& s) {
            if (s == QLatin1String("magJis")) return TopcoatException::MagJis;
            if (s == QLatin1String("magIso2")) return TopcoatException::MagIso2;
            if (s == QLatin1String("magIso3")) return TopcoatException::MagIso3;
            return TopcoatException::Chip7816;
        }
    }

    QJsonObject TopcoatRegion::toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["kind"] = kindToStr(kind);
        o["x"] = xMm; o["y"] = yMm; o["w"] = wMm; o["h"] = hMm;
        return o;
    }

    TopcoatRegion TopcoatRegion::fromJson(const QJsonObject& o) {
        TopcoatRegion r;
        r.id = o.value("id").toString();
        r.kind = strToKind(o.value("kind").toString());
        r.xMm = o.value("x").toDouble(r.xMm);
        r.yMm = o.value("y").toDouble(r.yMm);
        r.wMm = qMax(0.5, o.value("w").toDouble(r.wMm));
        r.hMm = qMax(0.5, o.value("h").toDouble(r.hMm));
        return r;
    }

    TopcoatRegion* TopcoatSettings::find(const QString& id) {
        for (auto& r : regions) if (r.id == id) return &r;
        return nullptr;
    }
    const TopcoatRegion* TopcoatSettings::find(const QString& id) const {
        for (const auto& r : regions) if (r.id == id) return &r;
        return nullptr;
    }
    QString TopcoatSettings::nextId() const {
        int maxN = 0;
        for (const auto& r : regions)
            if (r.id.startsWith(QLatin1String("r_"))) maxN = qMax(maxN, r.id.mid(2).toInt());
        return QStringLiteral("r_%1").arg(maxN + 1);
    }

    QJsonObject TopcoatSettings::toJson() const {
        QJsonObject o;
        o["base"] = baseToStr(base);
        o["exception"] = excToStr(exception);
        QJsonArray arr;
        for (const auto& r : regions) arr.append(r.toJson());
        o["regions"] = arr;
        return o;
    }

    TopcoatSettings TopcoatSettings::fromJson(const QJsonObject& o) {
        TopcoatSettings t;
        t.base = strToBase(o.value("base").toString());
        t.exception = strToExc(o.value("exception").toString());
        for (const auto& v : o.value("regions").toArray()) {
            TopcoatRegion r = TopcoatRegion::fromJson(v.toObject());
            if (r.id.isEmpty() || t.find(r.id)) r.id = t.nextId();
            t.regions.append(r);
        }
        return t;
    }

    // ------------------------------------------------------------- 卡面底圖 ---

    QJsonObject CardBackground::toJson() const {
        QJsonObject o;
        o["path"] = path;
        o["fit"] = fitToStr(fit);
        o["focusX"] = focusX;
        o["focusY"] = focusY;
        o["showInDesigner"] = showInDesigner;
        o["print"] = print;
        return o;
    }

    CardBackground CardBackground::fromJson(const QJsonObject& o) {
        CardBackground b;
        b.path = o.value("path").toString();
        b.fit = strToFit(o.value("fit").toString());
        if (b.fit == ImageFit::MatchHeight) b.fit = ImageFit::Cover;
        b.focusX = qBound(0.0, o.value("focusX").toDouble(50.0), 100.0);
        b.focusY = qBound(0.0, o.value("focusY").toDouble(50.0), 100.0);
        b.showInDesigner = o.value("showInDesigner").toBool(true);
        b.print = o.value("print").toBool(false);
        return b;
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
        if (!background.isEmpty()) o["background"] = background.toJson();   // 沒有底圖就不寫出，版面檔維持精簡
        if (!topcoat.isDefault()) o["topcoat"] = topcoat.toJson();       // 預設值不寫出，版面檔維持精簡
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
        l.background = CardBackground::fromJson(o.value("background").toObject());   // 舊版面檔沒有 = 無底圖
        l.topcoat = TopcoatSettings::fromJson(o.value("topcoat").toObject());   // 舊版面檔沒有 = 預設
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

    ::TopcoatSpec toTopcoatSpec(const TopcoatSettings& t) {
        ::TopcoatSpec s;
        switch (t.base) {
        case TopcoatBase::All: s.base = ::TopcoatSpec::Base::All; break;
        case TopcoatBase::Except: s.base = ::TopcoatSpec::Base::Except; break;
        case TopcoatBase::None: s.base = ::TopcoatSpec::Base::None; break;
        default: s.base = ::TopcoatSpec::Base::DriverDefault; break;
        }
        switch (t.exception) {
        case TopcoatException::MagJis: s.exception = ::TopcoatSpec::Exception::MagJis; break;
        case TopcoatException::MagIso2: s.exception = ::TopcoatSpec::Exception::MagIso2; break;
        case TopcoatException::MagIso3: s.exception = ::TopcoatSpec::Exception::MagIso3; break;
        default: s.exception = ::TopcoatSpec::Exception::Chip7816; break;
        }
        for (const auto& r : t.regions) {
            const TopcoatRect rc{ r.yMm, r.xMm, r.wMm, r.hMm };          // 上 左 寬 高
            switch (r.kind) {
            case RegionKind::Add: s.addAreas.push_back(rc); break;
            case RegionKind::Block: s.blockAreas.push_back(rc); break;
            default: s.removeAreas.push_back(rc); break;
            }
        }
        return s;
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