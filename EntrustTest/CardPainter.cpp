#include "CardPainter.h"
#include "ImageMatcher.h"
#include "QrCodeGen.h"

#include <QCache>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPen>
#include <QVector>
#include <QtMath>
#include <QHash>

namespace card {

    namespace {
        QMutex g_cacheMutex;
        QCache<QString, QImage>& cache() {
            static QCache<QString, QImage> c(256 * 1024);   // 約 256 MB (單位 KB)
            return c;
        }

        QImage loadImageCached(const QString& path) {
            if (path.isEmpty() || !QFileInfo::exists(path)) return QImage();
            QMutexLocker lock(&g_cacheMutex);
            if (QImage* hit = cache().object(path)) return *hit;
            QImage* img = new QImage(path);
            if (img->isNull()) { delete img; return QImage(); }
            const QImage copy = *img;
            cache().insert(path, img, qMax<qsizetype>(1, img->sizeInBytes() / 1024));
            return copy;
        }

        // 圖片元素實際要用的路徑：固定 = imagePath；動態 = 依這個元素自己的資料夾 / 欄位比對到的圖
        QString resolveImagePath(const Element& e, const Record& rec) {
            if (e.imageSource == ImageSource::Dynamic) return rec.value(autoImageKeyFor(e.id));   // 這個元素自己的比對結果
            return substitute(e.imagePath, rec);
        }

        // Code 39 (ASCII 大寫、數字及 - . $ / + % 空白)。每個字元由 9 個 bar/space 組成。
        QString code39Pattern(QChar ch) {
            static const QHash<QChar, QString> patterns = {
                { QLatin1Char('0'), QStringLiteral("nnnwwnwnn") },{ QLatin1Char('1'), QStringLiteral("wnnwnnnnw") },{ QLatin1Char('2'), QStringLiteral("nnwwnnnnw") },{ QLatin1Char('3'), QStringLiteral("wnwwnnnnn") },
                { QLatin1Char('4'), QStringLiteral("nnnwwnnnw") },{ QLatin1Char('5'), QStringLiteral("wnnwwnnnn") },{ QLatin1Char('6'), QStringLiteral("nnwwwnnnn") },{ QLatin1Char('7'), QStringLiteral("nnnwnnwnw") },
                { QLatin1Char('8'), QStringLiteral("wnnwnnwnn") },{ QLatin1Char('9'), QStringLiteral("nnwwnnwnn") },{ QLatin1Char('A'), QStringLiteral("wnnnnwnnw") },{ QLatin1Char('B'), QStringLiteral("nnwnnwnnw") },
                { QLatin1Char('C'), QStringLiteral("wnwnnwnnn") },{ QLatin1Char('D'), QStringLiteral("nnnnwwnnw") },{ QLatin1Char('E'), QStringLiteral("wnnnwwnnn") },{ QLatin1Char('F'), QStringLiteral("nnwnwwnnn") },
                { QLatin1Char('G'), QStringLiteral("nnnnnwwnw") },{ QLatin1Char('H'), QStringLiteral("wnnnnwwnn") },{ QLatin1Char('I'), QStringLiteral("nnwnnwwnn") },{ QLatin1Char('J'), QStringLiteral("nnnnwwwnn") },
                { QLatin1Char('K'), QStringLiteral("wnnnnnnww") },{ QLatin1Char('L'), QStringLiteral("nnwnnnnww") },{ QLatin1Char('M'), QStringLiteral("wnwnnnnwn") },{ QLatin1Char('N'), QStringLiteral("nnnnwnnww") },
                { QLatin1Char('O'), QStringLiteral("wnnnwnnwn") },{ QLatin1Char('P'), QStringLiteral("nnwnwnnwn") },{ QLatin1Char('Q'), QStringLiteral("nnnnnnwww") },{ QLatin1Char('R'), QStringLiteral("wnnnnnwwn") },
                { QLatin1Char('S'), QStringLiteral("nnwnnnwwn") },{ QLatin1Char('T'), QStringLiteral("nnnnwnwwn") },{ QLatin1Char('U'), QStringLiteral("wwnnnnnnw") },{ QLatin1Char('V'), QStringLiteral("nwwnnnnnw") },
                { QLatin1Char('W'), QStringLiteral("wwwnnnnnn") },{ QLatin1Char('X'), QStringLiteral("nwnnwnnnw") },{ QLatin1Char('Y'), QStringLiteral("wwnnwnnnn") },{ QLatin1Char('Z'), QStringLiteral("nwwnwnnnn") },
                { QLatin1Char('-'), QStringLiteral("nwnnnnwnw") },{ QLatin1Char('.'), QStringLiteral("wwnnnnwnn") },{ QLatin1Char(' '), QStringLiteral("nwwnnnwnn") },{ QLatin1Char('$'), QStringLiteral("nwnwnwnnn") },
                { QLatin1Char('/'), QStringLiteral("nwnwnnnwn") },{ QLatin1Char('+'), QStringLiteral("nwnnnwnwn") },{ QLatin1Char('%'), QStringLiteral("nnnwnwnwn") },{ QLatin1Char('*'), QStringLiteral("nwnnwnwnn") }
            };
            return patterns.value(ch);
        }

        bool isCode39Valid(const QString& value) {
            if (value.isEmpty()) return false;
            for (QChar c : value.toUpper()) if (code39Pattern(c).isEmpty() || c == QLatin1Char('*')) return false;
            return true;
        }

        // ---- 一維條碼：每種編碼器只負責產生「交替的 條/空 寬度 (單位 module，第一個一定是條)」，繪製共用 ----
        using Runs = QVector<int>;

        QString onlyDigitsErr(const QString& v) {
            for (QChar c : v) if (c.unicode() < '0' || c.unicode() > '9') return QStringLiteral("僅支援數字");
            return QString();
        }
        void appendDigits(Runs& out, const char* pat) { for (; *pat; ++pat) out.append(*pat - '0'); }
        void bitsToRuns(const QString& bits, Runs& out) {      // '1' = 條, '0' = 空；相同連續位元合併成一個寬度
            int i = 0;
            while (i < bits.size()) { int j = i; while (j < bits.size() && bits[j] == bits[i]) ++j; out.append(j - i); i = j; }
        }

        QString encCode39(const QString& v, Runs& out) {
            const QString value = v.toUpper();
            if (!isCode39Valid(value)) return QStringLiteral("不支援此字元 (僅 0-9 A-Z 空白 - . $ / + %)");
            const QString encoded = QLatin1Char('*') + value + QLatin1Char('*');
            for (int ci = 0; ci < encoded.size(); ++ci) {
                const QString pat = code39Pattern(encoded.at(ci));
                for (QChar w : pat) out.append(w == QLatin1Char('w') ? 3 : 1);
                if (ci + 1 < encoded.size()) out.append(1);      // 字元間的窄空白
            }
            return QString();
        }

        QString encCode128(const QString& v, Runs& out) {
            static const char* kPat[107] = {
                "212222","222122","222221","121223","121322","131222","122213","122312","132212","221213",
                "221312","231212","112232","122132","122231","113222","123122","123221","223211","221132",
                "221231","213212","223112","312131","311222","321122","321221","312212","322112","322211",
                "212123","212321","232121","111323","131123","131321","112313","132113","132311","211313",
                "231113","231311","112133","112331","132131","113123","113321","133121","313121","211331",
                "231131","213113","213311","213131","311123","311321","331121","312113","312311","332111",
                "314111","221411","431111","111224","111422","121124","121421","141122","141221","112214",
                "112412","122114","122411","142112","142211","241211","221114","413111","241112","134111",
                "111242","121142","121241","114212","124112","124211","411212","421112","421211","212141",
                "214121","412121","111143","111341","131141","114113","114311","411113","411311","113141",
                "114131","311141","411131","211412","211214","211232","2331112" };
            const int n = v.size();
            for (QChar c : v) if (c.unicode() < 32 || c.unicode() > 126) return QStringLiteral("不支援此字元 (僅 ASCII 32~126)");
            auto isD = [&](int i) { return i < n && v[i].unicode() >= '0' && v[i].unicode() <= '9'; };
            auto runLen = [&](int i) { int k = 0; while (isD(i + k)) ++k; return k; };
            QVector<int> codes;
            int mode = 0;                                   // 0 = Code B, 1 = Code C
            const int r0 = runLen(0);
            if (r0 >= 4 && r0 % 2 == 0) { mode = 1; codes << 105; }
            else codes << 104;
            int i = 0;
            while (i < n) {
                if (mode == 0) {
                    const int r = runLen(i);
                    if (r >= 6 || (r >= 4 && (i == 0 || i + r == n))) {      // 數字夠長：切到 Code C 壓縮
                        if (r % 2) { codes << (v[i].unicode() - 32); ++i; }  // 奇數個數字：先用 B 送出第一個
                        codes << 99; mode = 1;
                    }
                    else { codes << (v[i].unicode() - 32); ++i; }
                }
                else {
                    if (isD(i) && isD(i + 1)) { codes << ((v[i].unicode() - '0') * 10 + (v[i + 1].unicode() - '0')); i += 2; }
                    else { codes << 100; mode = 0; }
                }
            }
            int sum = codes[0];
            for (int k = 1; k < codes.size(); ++k) sum += codes[k] * k;
            codes << (sum % 103) << 106;
            for (int c : codes) appendDigits(out, kPat[c]);
            return QString();
        }

        int eanCheckDigit(const QString& d) {               // d 為不含檢查碼的資料；由右至左權重 3,1,3,1...
            int sum = 0, w = 3;
            for (int i = d.size() - 1; i >= 0; --i, w = (w == 3 ? 1 : 3)) sum += (d[i].unicode() - '0') * w;
            return (10 - sum % 10) % 10;
        }
        // 資料長度 = dataLen 時自動補檢查碼；= dataLen+1 時驗證檢查碼
        QString eanComplete(const QString& v, int dataLen, QString& full) {
            QString e = onlyDigitsErr(v);
            if (!e.isEmpty()) return e;
            if (v.size() == dataLen) full = v + QChar('0' + eanCheckDigit(v));
            else if (v.size() == dataLen + 1) {
                full = v;
                if (eanCheckDigit(v.left(dataLen)) != v[dataLen].unicode() - '0') return QStringLiteral("檢查碼錯誤");
            }
            else return QStringLiteral("需 %1 或 %2 位數字").arg(dataLen).arg(dataLen + 1);
            return QString();
        }
        QString eanLCode(int d) {
            static const char* L[10] = { "0001101","0011001","0010011","0111101","0100011","0110001","0101111","0111011","0110111","0001011" };
            return QString::fromLatin1(L[d]);
        }
        QString eanGCode(int d) {
            static const char* G[10] = { "0100111","0110011","0011011","0100001","0011101","0111001","0000101","0010001","0001001","0010111" };
            return QString::fromLatin1(G[d]);
        }
        QString eanRCode(int d) {                            // R = L 的反相
            QString s = eanLCode(d);
            for (QChar& c : s) c = (c == QLatin1Char('0')) ? QLatin1Char('1') : QLatin1Char('0');
            return s;
        }
        QString encEan13Full(const QString& d13, Runs& out) {   // d13：完整 13 碼 (UPC-A 前面補 0 後也走這裡)
            static const char* kParity[10] = { "LLLLLL","LLGLGG","LLGGLG","LLGGGL","LGLLGG","LGGLLG","LGGGLL","LGLGLG","LGLGGL","LGGLGL" };
            QString bits = QStringLiteral("101");
            const char* par = kParity[d13[0].unicode() - '0'];
            for (int i = 1; i <= 6; ++i) { const int d = d13[i].unicode() - '0'; bits += (par[i - 1] == 'L') ? eanLCode(d) : eanGCode(d); }
            bits += QStringLiteral("01010");
            for (int i = 7; i <= 12; ++i) bits += eanRCode(d13[i].unicode() - '0');
            bits += QStringLiteral("101");
            bitsToRuns(bits, out);
            return QString();
        }
        QString encEan13(const QString& v, Runs& out) {
            QString full; const QString e = eanComplete(v, 12, full);
            return e.isEmpty() ? encEan13Full(full, out) : e;
        }
        QString encUpcA(const QString& v, Runs& out) {
            QString full; const QString e = eanComplete(v, 11, full);
            return e.isEmpty() ? encEan13Full(QLatin1Char('0') + full, out) : e;
        }
        QString encEan8(const QString& v, Runs& out) {
            QString full; const QString e = eanComplete(v, 7, full);
            if (!e.isEmpty()) return e;
            QString bits = QStringLiteral("101");
            for (int i = 0; i < 4; ++i) bits += eanLCode(full[i].unicode() - '0');
            bits += QStringLiteral("01010");
            for (int i = 4; i < 8; ++i) bits += eanRCode(full[i].unicode() - '0');
            bits += QStringLiteral("101");
            bitsToRuns(bits, out);
            return QString();
        }

        QString encItf(const QString& v, Runs& out) {
            static const char* kW[10] = { "nnwwn","wnnnw","nwnnw","wwnnn","nnwnw","wnwnn","nwwnn","nnnww","wnnwn","nwnwn" };
            const QString e = onlyDigitsErr(v);
            if (!e.isEmpty()) return e;
            const QString d = (v.size() % 2) ? QLatin1Char('0') + v : v;      // 奇數位數：前面補 0
            out << 1 << 1 << 1 << 1;                                           // 起始 (窄條 窄空 窄條 窄空)
            for (int i = 0; i < d.size(); i += 2) {
                const char* b = kW[d[i].unicode() - '0'];                      // 奇數位：條
                const char* s = kW[d[i + 1].unicode() - '0'];                  // 偶數位：空
                for (int k = 0; k < 5; ++k) { out.append(b[k] == 'w' ? 3 : 1); out.append(s[k] == 'w' ? 3 : 1); }
            }
            out << 3 << 1 << 1;                                                // 結束 (寬條 窄空 窄條)
            return QString();
        }

        QString encCodabar(const QString& v, Runs& out) {
            static const char kChars[] = "0123456789-$:/.+ABCD";
            static const int kBits[20] = { 0x003,0x006,0x009,0x060,0x012,0x042,0x021,0x024,0x030,0x048,0x00c,0x018,0x045,0x051,0x054,0x015,0x01A,0x029,0x00B,0x00E };
            QString s = v.toUpper();
            auto isSS = [](QChar c) { return c == QLatin1Char('A') || c == QLatin1Char('B') || c == QLatin1Char('C') || c == QLatin1Char('D'); };
            if (s.size() < 2 || !isSS(s.at(0)) || !isSS(s.at(s.size() - 1))) s = QLatin1Char('A') + s + QLatin1Char('A');   // 沒指定起訖碼：用 A
            for (int ci = 0; ci < s.size(); ++ci) {
                const int idx = QString::fromLatin1(kChars).indexOf(s[ci]);
                if (idx < 0 || ((ci > 0 && ci + 1 < s.size()) && idx >= 16)) return QStringLiteral("不支援此字元 (僅 0-9 - $ : / . +)");
                for (int b = 6; b >= 0; --b) out.append((kBits[idx] >> b) & 1 ? 3 : 1);
                if (ci + 1 < s.size()) out.append(1);
            }
            return QString();
        }

        // 把資料編成條碼；回傳空字串 = 成功 (runs 為結果)，否則為原因
        QString encodeBarcode(BarcodeType type, const QString& source, Runs& runs) {
            if (source.isEmpty()) return QStringLiteral("資料為空");
            switch (type) {
            case BarcodeType::Code39:   return encCode39(source, runs);
            case BarcodeType::EAN13:    return encEan13(source, runs);
            case BarcodeType::EAN8:     return encEan8(source, runs);
            case BarcodeType::UPCA:     return encUpcA(source, runs);
            case BarcodeType::ITF:      return encItf(source, runs);
            case BarcodeType::Codabar:  return encCodabar(source, runs);
            default:                    return encCode128(source, runs);
            }
        }

        void paintBarcode(QPainter& p, const QRectF& r, BarcodeType type, const QString& source) {
            Runs runs;
            QString err;
            const QString typeName = barcodeTypeNames().value(static_cast<int>(type));
            if (source.isEmpty()) err = QStringLiteral("[條碼資料] [%1]").arg(typeName);
            else {
                const QString e = encodeBarcode(type, source, runs);
                if (!e.isEmpty()) err = QStringLiteral("[%1] %2").arg(typeName, e);
            }
            p.fillRect(r, Qt::white);
            if (!err.isEmpty()) {
                p.setPen(QPen(QColor(180, 0, 0), 0));
                p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
                p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, err);
                return;
            }
            int units = 0;
            for (int w : runs) units += w;
            const int quiet = 10;                                       // 前後各留 10 個 module 的空白區
            const double module = r.width() / (units + 2 * quiet);
            const double top = r.top() + r.height() * 0.06, bottom = r.bottom() - r.height() * 0.06;
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(Qt::NoPen); p.setBrush(Qt::black);
            int acc = quiet;
            for (int i = 0; i < runs.size(); ++i) {
                if ((i % 2) == 0) {                                      // 邊界取整數，避免模糊 / 粗細不均
                    const double x0 = qRound(r.left() + acc * module), x1 = qRound(r.left() + (acc + runs[i]) * module);
                    p.drawRect(QRectF(x0, top, qMax(1.0, x1 - x0), bottom - top));
                }
                acc += runs[i];
            }
        }

        // ---- QR Code ----

        QString encodeQr(const QString& value, int ecc, qrgen::Matrix& m) {

            if (value.isEmpty()) return QStringLiteral("資料為空");

            if (!qrgen::encode(value.toUtf8().toStdString(), qBound(0, ecc, 3), m)) return QStringLiteral("資料太長，放不進 QR Code");

            return QString();

        }



        void paintQr(QPainter& p, const QRectF& r, const QString& value, int ecc) {

            qrgen::Matrix m;

            const QString err = encodeQr(value, ecc, m);

            p.fillRect(r, Qt::white);

            if (!err.isEmpty()) {

                p.setPen(QPen(QColor(180, 0, 0), 0));

                p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));

                p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, value.isEmpty() ? QStringLiteral("[QR Code 資料]") : QStringLiteral("[QR Code] %1").arg(err));

                return;

            }

            const int n = m.size;

            const double side = qMin(r.width(), r.height());

            double mod = side / (n + 8);                         // 四周各留 4 個 module 的空白區 (QR 規格)

            if (mod >= 1.0) mod = qFloor(mod);                   // 盡量取整數像素，邊緣才銳利、列印好掃

            const double total = n * mod;

            const double ox = qRound(r.left() + (r.width() - total) / 2.0), oy = qRound(r.top() + (r.height() - total) / 2.0);

            p.setRenderHint(QPainter::Antialiasing, false);

            p.setPen(Qt::NoPen); p.setBrush(Qt::black);

            for (int y = 0; y < n; ++y) {

                int x = 0;

                while (x < n) {

                    if (!m.at(x, y)) { ++x; continue; }

                    int x1 = x;

                    while (x1 < n && m.at(x1, y)) ++x1;          // 同一列連續的黑格合併成一個矩形

                    const double l = qRound(ox + x * mod), rr = qRound(ox + x1 * mod);

                    const double t = qRound(oy + y * mod), bt = qRound(oy + (y + 1) * mod);

                    p.drawRect(QRectF(l, t, rr - l, bt - t));

                    x = x1;

                }

            }

        }



        // 文字元素的字型；繪製與量測共用，確保兩者一致
        QFont makeFont(const Element& e, double pxPerMm) {
            QFont f(e.fontFamily);
            const double px = e.fontPt * 25.4 / 72.0 * pxPerMm;   // pt -> mm -> px
            f.setPixelSize(qMax(1, qRound(px)));
            f.setBold(e.bold);
            f.setItalic(e.italic);
            // 貼合文字時關閉 hinting，字寬才會隨解析度等比例縮放 (預覽 10 px/mm 與列印 300/600 dpi 的寬度一致)
            if (e.fitContent) f.setHintingPreference(QFont::PreferNoHinting);
            return f;
        }
    }

    void clearImageCache() {
        QMutexLocker lock(&g_cacheMutex);
        cache().clear();
    }

    QString checkCodeElement(const Element& e, const Record& rec) {
        if (e.type != ElementType::Barcode && e.type != ElementType::QRCode) return QString();
        const QString value = e.barcodeSource == ImageSource::Dynamic ? rec.value(e.barcodeField) : e.barcodeValue;
        if (e.type == ElementType::QRCode) { qrgen::Matrix m; return encodeQr(value, e.qrEcc, m); }
        Runs runs;
        return encodeBarcode(e.barcodeType, value, runs);
    }

    void paintElement(QPainter& p, const Element& e, const QRectF& r, double pxPerMm, const Record& rec) {
        p.save();
        const bool fitText = e.fitContent && e.type == ElementType::Text;
        if (!fitText) p.setClipRect(r, Qt::IntersectClip);     // 貼合文字：框已剛好包住文字，不裁切 (避免字尾被切掉)

        if (e.type == ElementType::Text) {
            p.setFont(makeFont(e, pxPerMm));
            p.setPen(e.color);

            int flags = int(Qt::AlignVCenter);
            flags |= fitText ? int(Qt::TextDontClip) : int(Qt::TextWordWrap);   // 貼合文字不自動換行 (只依 Enter 換行)
            switch (e.align) {
            case HAlign::Center: flags |= int(Qt::AlignHCenter); break;
            case HAlign::Right:  flags |= int(Qt::AlignRight);   break;
            default:             flags |= int(Qt::AlignLeft);    break;
            }
            p.drawText(r, flags, substitute(e.text, rec));
        }
        else if (e.type == ElementType::Barcode) {
            const QString value = e.barcodeSource == ImageSource::Dynamic
                ? rec.value(e.barcodeField) : e.barcodeValue;
            paintBarcode(p, r, e.barcodeType, value);
        }
        else if (e.type == ElementType::QRCode) {
            const QString value = e.barcodeSource == ImageSource::Dynamic
                ? rec.value(e.barcodeField) : e.barcodeValue;
            paintQr(p, r, value, e.qrEcc);
        }
        else {
            // 動態圖片：路徑來自 ImageMatcherPool (依這個元素自己的資料夾與比對欄位)
            const bool autoMode = e.imageSource == ImageSource::Dynamic;
            const QString path = resolveImagePath(e, rec);
            const QImage img = loadImageCached(path);
            if (img.isNull()) {
                // 找不到圖片：畫虛線框 + 提示，方便在預覽時發現問題
                QPen pen(QColor(150, 150, 150), qMax(1.0, 0.25 * pxPerMm), Qt::DashLine);
                p.setPen(pen);
                p.setBrush(QColor(245, 245, 245));
                p.drawRect(r.adjusted(1, 1, -1, -1));
                QFont f;
                f.setPixelSize(qMax(1, qRound(3.0 * pxPerMm)));
                p.setFont(f);
                p.setPen(QColor(120, 120, 120));
                p.drawText(r.adjusted(2, 2, -2, -2), int(Qt::AlignCenter) | int(Qt::TextWordWrap),
                    !path.isEmpty() ? path
                    : (autoMode && !rec.value(autoImageNoteKeyFor(e.id)).isEmpty() ? rec.value(autoImageNoteKeyFor(e.id))
                        : QStringLiteral("[圖片]")));
            }
            else {
                p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                // 依「顯示方式」決定圖片在框內的大小與位置；框的大小完全由使用者決定。
                //   縮放倍率 sx / sy：Stretch 各自拉伸；Cover 取較大者 (蓋滿框)；Contain / MatchHeight 取較小者 (整張放進框)。
                //   再乘上額外縮放 %，並依水平 / 垂直位置 (0 ~ 100%) 決定圖片相對於框的位置；超出框的部分被裁掉。
                const double iw = img.width(), ih = img.height();
                double sx, sy;
                if (e.imageFit == ImageFit::Stretch) {
                    sx = r.width() / iw;
                    sy = r.height() / ih;
                }
                else {
                    const double fx = r.width() / iw, fy = r.height() / ih;
                    sx = sy = (e.imageFit == ImageFit::Cover ? qMax(fx, fy) : qMin(fx, fy)) * qBound(0.1, e.imageZoomPct / 100.0, 4.0);
                }
                const double dw = iw * sx, dh = ih * sy;
                const double dx = r.left() + (r.width() - dw) * qBound(0.0, e.imageFocusX, 100.0) / 100.0;
                const double dy = r.top() + (r.height() - dh) * qBound(0.0, e.imageFocusY, 100.0) / 100.0;
                const QRectF vis = QRectF(dx, dy, dw, dh).intersected(r);        // 框內看得到的部分
                if (!vis.isEmpty())
                    p.drawImage(vis, img, QRectF((vis.left() - dx) / sx, (vis.top() - dy) / sy, vis.width() / sx, vis.height() / sy));
            }
        }
        p.restore();
    }

    void paintBackground(QPainter& p, const CardBackground& bg, const QRectF& r, double pxPerMm, const Record& rec) {
        if (bg.isEmpty()) return;
        // 借用圖片元素的繪製邏輯 (含快取、顯示方式、位置)：把底圖當成「鋪滿整張卡的固定圖片」
        Element e;
        e.type = ElementType::Image;
        e.imageSource = ImageSource::Fixed;
        e.imagePath = bg.path;
        e.imageFit = bg.fit == ImageFit::MatchHeight ? ImageFit::Cover : bg.fit;
        e.imageFocusX = bg.focusX;
        e.imageFocusY = bg.focusY;
        if (!QFileInfo::exists(substitute(bg.path, rec))) return;   // 找不到檔案就不畫 (不要在卡面上畫出「找不到圖片」的提示框)
        paintElement(p, e, r, pxPerMm, rec);
    }

    bool fitElement(Element& e, const Record& rec) {
        double nw = e.wMm, nh = e.hMm;

        if (e.type == ElementType::Text) {
            const QString t = substitute(e.text, rec);
            if (t.isEmpty()) return false;                       // 沒有內容就維持原樣
            constexpr double R = 50.0;                           // 量測用解析度 (px/mm)，與實際輸出解析度無關
            const QFontMetricsF fm(makeFont(e, R));
            const QStringList lines = t.split(QLatin1Char('\n'));
            double wPx = 0;
            for (const QString& ln : lines) wPx = qMax(wPx, fm.horizontalAdvance(ln));
            const double hPx = fm.height() + (lines.size() - 1) * fm.lineSpacing();
            nw = qMax(0.5, wPx / R);
            nh = qMax(0.5, hPx / R);
        }
        else if (e.type == ElementType::Image) {
            if (e.imageFit != ImageFit::MatchHeight) return false;   // 只有「框高依圖片比例」才會自動改框的大小
            const QImage img = loadImageCached(resolveImagePath(e, rec));
            if (img.isNull() || img.width() <= 0) return false;  // 找不到圖片：維持原樣
            nh = qMax(0.5, e.wMm * img.height() / double(img.width()));
        }
        else return false;

        if (qAbs(nw - e.wMm) < 1e-6 && qAbs(nh - e.hMm) < 1e-6) return false;

        const double bottom = e.yMm + e.hMm;                     // 底邊固定
        if (e.type == ElementType::Text) {
            switch (e.align) {                                    // 依對齊固定錨點
            case HAlign::Center: e.xMm = (e.xMm + e.wMm / 2.0) - nw / 2.0; break;
            case HAlign::Right:  e.xMm = (e.xMm + e.wMm) - nw; break;
            default: break;                                       // 靠左：左邊不動
            }
            e.wMm = nw;
        }
        e.hMm = nh;
        e.yMm = bottom - nh;
        return true;
    }

    void fitAll(CardLayout& layout, const Record& rec) {
        for (auto& e : layout.elements)
            if (e.fitContent) fitElement(e, rec);
    }

    QImage renderCard(const CardLayout& layout, const Record& rec, int dpi) {
        const double ppm = dpi / 25.4;
        const int w = qRound(layout.widthMm() * ppm);
        const int h = qRound(layout.heightMm() * ppm);

        QImage img(w, h, QImage::Format_RGB32);
        img.fill(Qt::white);
        img.setDotsPerMeterX(qRound(dpi / 0.0254));
        img.setDotsPerMeterY(qRound(dpi / 0.0254));

        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (layout.background.print)                                  // 卡面底圖：只有勾選「列印時印出」才會畫進輸出
            paintBackground(p, layout.background, QRectF(0, 0, w, h), ppm, rec);
        for (const auto& e : layout.elements) {
            paintElement(p, e, QRectF(e.xMm * ppm, e.yMm * ppm, e.wMm * ppm, e.hMm * ppm), ppm, rec);
        }
        p.end();
        return img;
    }

} // namespace card