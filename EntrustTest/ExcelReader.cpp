#include "ExcelReader.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QXmlStreamReader>

#include <cmath>
#include <cstdint>
#include <vector>

namespace card {

    namespace {

        // ===================================================== inflate (RFC 1951) ===
        // 以 zlib 作者的 puff.c 為藍本改寫；只做解壓縮，不依賴 zlib。

        struct InflateError {};

        class Inflater {
        public:
            Inflater(const uint8_t* in, size_t n, std::vector<uint8_t>& out) : in_(in), n_(n), out_(out) {}

            void run() {
                int last;
                do {
                    last = bits(1);
                    const int type = bits(2);
                    if (type == 0) stored();
                    else if (type == 1) fixedBlock();
                    else if (type == 2) dynamicBlock();
                    else throw InflateError();
                } while (!last);
            }

        private:
            static constexpr int kMaxBits = 15;
            static constexpr int kMaxLCodes = 286;
            static constexpr int kMaxDCodes = 30;
            static constexpr int kMaxCodes = kMaxLCodes + kMaxDCodes;
            static constexpr int kFixLCodes = 288;

            struct Huffman {
                short count[kMaxBits + 1];
                short symbol[kMaxCodes > kFixLCodes ? kMaxCodes : kFixLCodes];
            };

            int bits(int need) {
                long val = bitbuf_;
                while (bitcnt_ < need) {
                    if (incnt_ >= n_) throw InflateError();
                    val |= static_cast<long>(in_[incnt_++]) << bitcnt_;
                    bitcnt_ += 8;
                }
                bitbuf_ = static_cast<int>(val >> need);
                bitcnt_ -= need;
                return static_cast<int>(val & ((1L << need) - 1));
            }

            void stored() {
                bitbuf_ = 0;
                bitcnt_ = 0;
                if (incnt_ + 4 > n_) throw InflateError();
                unsigned len = in_[incnt_] | (in_[incnt_ + 1] << 8);
                const unsigned nlen = in_[incnt_ + 2] | (in_[incnt_ + 3] << 8);
                incnt_ += 4;
                if (len != (~nlen & 0xFFFFu)) throw InflateError();
                if (incnt_ + len > n_) throw InflateError();
                out_.insert(out_.end(), in_ + incnt_, in_ + incnt_ + len);
                incnt_ += len;
            }

            int decode(const Huffman& h) {
                int code = 0, first = 0, index = 0;
                for (int len = 1; len <= kMaxBits; ++len) {
                    code |= bits(1);
                    const int count = h.count[len];
                    if (code - count < first) return h.symbol[index + (code - first)];
                    index += count;
                    first += count;
                    first <<= 1;
                    code <<= 1;
                }
                throw InflateError();
            }

            // 回傳 0 = 完整；>0 = 不完整；<0 = 過度訂閱
            static int construct(Huffman& h, const short* length, int n) {
                for (int len = 0; len <= kMaxBits; ++len) h.count[len] = 0;
                for (int sym = 0; sym < n; ++sym) h.count[length[sym]]++;
                if (h.count[0] == n) return 0;
                int left = 1;
                for (int len = 1; len <= kMaxBits; ++len) {
                    left <<= 1;
                    left -= h.count[len];
                    if (left < 0) return left;
                }
                short offs[kMaxBits + 1];
                offs[1] = 0;
                for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = offs[len] + h.count[len];
                for (int sym = 0; sym < n; ++sym)
                    if (length[sym] != 0) h.symbol[offs[length[sym]]++] = static_cast<short>(sym);
                return left;
            }

            void codes(const Huffman& lencode, const Huffman& distcode) {
                static const short lens[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
                static const short lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
                static const short dists[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
                static const short dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

                for (;;) {
                    int symbol = decode(lencode);
                    if (symbol < 256) {
                        out_.push_back(static_cast<uint8_t>(symbol));
                    }
                    else if (symbol == 256) {
                        return;
                    }
                    else {
                        symbol -= 257;
                        if (symbol >= 29) throw InflateError();
                        const int len = lens[symbol] + bits(lext[symbol]);
                        const int ds = decode(distcode);
                        if (ds >= 30) throw InflateError();
                        const size_t dist = static_cast<size_t>(dists[ds] + bits(dext[ds]));
                        if (dist > out_.size()) throw InflateError();
                        size_t from = out_.size() - dist;
                        for (int i = 0; i < len; ++i) out_.push_back(out_[from + i]);
                    }
                }
            }

            void fixedBlock() {
                static bool built = false;
                static Huffman lencode, distcode;
                if (!built) {
                    short lengths[kFixLCodes];
                    int s = 0;
                    for (; s < 144; ++s) lengths[s] = 8;
                    for (; s < 256; ++s) lengths[s] = 9;
                    for (; s < 280; ++s) lengths[s] = 7;
                    for (; s < kFixLCodes; ++s) lengths[s] = 8;
                    construct(lencode, lengths, kFixLCodes);
                    for (s = 0; s < kMaxDCodes; ++s) lengths[s] = 5;
                    construct(distcode, lengths, kMaxDCodes);
                    built = true;
                }
                codes(lencode, distcode);
            }

            void dynamicBlock() {
                static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                short lengths[kMaxCodes];
                Huffman lencode, distcode;

                const int nlen = bits(5) + 257;
                const int ndist = bits(5) + 1;
                const int ncode = bits(4) + 4;
                if (nlen > kMaxLCodes || ndist > kMaxDCodes) throw InflateError();

                int index = 0;
                for (; index < ncode; ++index) lengths[order[index]] = static_cast<short>(bits(3));
                for (; index < 19; ++index) lengths[order[index]] = 0;
                if (construct(lencode, lengths, 19) != 0) throw InflateError();

                index = 0;
                while (index < nlen + ndist) {
                    int symbol = decode(lencode);
                    if (symbol < 16) {
                        lengths[index++] = static_cast<short>(symbol);
                    }
                    else {
                        int len = 0;
                        if (symbol == 16) {
                            if (index == 0) throw InflateError();
                            len = lengths[index - 1];
                            symbol = 3 + bits(2);
                        }
                        else if (symbol == 17) symbol = 3 + bits(3);
                        else symbol = 11 + bits(7);
                        if (index + symbol > nlen + ndist) throw InflateError();
                        while (symbol--) lengths[index++] = static_cast<short>(len);
                    }
                }
                if (lengths[256] == 0) throw InflateError();

                int err = construct(lencode, lengths, nlen);
                if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) throw InflateError();
                err = construct(distcode, lengths + nlen, ndist);
                if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) throw InflateError();

                codes(lencode, distcode);
            }

            const uint8_t* in_;
            size_t n_;
            size_t incnt_ = 0;
            int bitbuf_ = 0;
            int bitcnt_ = 0;
            std::vector<uint8_t>& out_;
        };

        // ============================================================== ZIP 容器 ===

        class ZipArchive {
        public:
            bool open(const QByteArray& data, QString* err) {
                data_ = data;
                const int n = data_.size();
                if (n < 22) return fail(err, QStringLiteral("檔案太小，不是有效的 .xlsx"));

                // 由檔尾往回找 End Of Central Directory
                int eocd = -1;
                const int lowest = qMax(0, n - 22 - 65535);
                for (int i = n - 22; i >= lowest; --i) {
                    if (u32(i) == 0x06054b50u) { eocd = i; break; }
                }
                if (eocd < 0) return fail(err, QStringLiteral("不是有效的 .xlsx 檔 (找不到 ZIP 結構)"));

                const int total = u16(eocd + 10);
                const quint32 cdOffset = u32(eocd + 16);
                if (total == 0xFFFF || cdOffset == 0xFFFFFFFFu)
                    return fail(err, QStringLiteral("不支援 ZIP64 格式的檔案"));

                qint64 pos = cdOffset;
                for (int i = 0; i < total; ++i) {
                    if (pos + 46 > n || u32(int(pos)) != 0x02014b50u)
                        return fail(err, QStringLiteral("ZIP 目錄損毀"));
                    Entry e;
                    e.method = u16(int(pos) + 10);
                    e.compSize = u32(int(pos) + 20);
                    e.size = u32(int(pos) + 24);
                    const int nameLen = u16(int(pos) + 28);
                    const int extraLen = u16(int(pos) + 30);
                    const int commentLen = u16(int(pos) + 32);
                    e.localOffset = u32(int(pos) + 42);
                    if (pos + 46 + nameLen > n) return fail(err, QStringLiteral("ZIP 目錄損毀"));
                    const QString name = QString::fromUtf8(data_.constData() + pos + 46, nameLen);
                    entries_.insert(name, e);
                    pos += 46 + nameLen + extraLen + commentLen;
                }
                return true;
            }

            bool has(const QString& name) const { return entries_.contains(name); }

            bool read(const QString& name, QByteArray* out, QString* err) const {
                auto it = entries_.constFind(name);
                if (it == entries_.constEnd()) return fail(err, QStringLiteral("檔案內缺少 %1").arg(name));
                const Entry& e = it.value();
                const qint64 lh = e.localOffset;
                if (lh + 30 > data_.size() || u32(int(lh)) != 0x04034b50u)
                    return fail(err, QStringLiteral("ZIP 內容損毀 (%1)").arg(name));
                const qint64 start = lh + 30 + u16(int(lh) + 26) + u16(int(lh) + 28);
                if (start + e.compSize > data_.size())
                    return fail(err, QStringLiteral("ZIP 內容損毀 (%1)").arg(name));

                if (e.method == 0) {
                    *out = data_.mid(int(start), int(e.compSize));
                    return true;
                }
                if (e.method != 8) return fail(err, QStringLiteral("不支援的壓縮方式 (%1)").arg(e.method));

                std::vector<uint8_t> buf;
                buf.reserve(e.size);
                try {
                    Inflater inf(reinterpret_cast<const uint8_t*>(data_.constData()) + start, e.compSize, buf);
                    inf.run();
                }
                catch (const InflateError&) {
                    return fail(err, QStringLiteral("解壓縮失敗 (%1)").arg(name));
                }
                *out = QByteArray(reinterpret_cast<const char*>(buf.data()), int(buf.size()));
                return true;
            }

        private:
            struct Entry { quint32 localOffset = 0, compSize = 0, size = 0; quint16 method = 0; };

            static bool fail(QString* err, const QString& msg) { if (err) *err = msg; return false; }
            quint16 u16(int off) const {
                if (off < 0 || off + 2 > data_.size()) return 0;
                const auto* p = reinterpret_cast<const uint8_t*>(data_.constData()) + off;
                return quint16(p[0] | (p[1] << 8));
            }
            quint32 u32(int off) const {
                if (off < 0 || off + 4 > data_.size()) return 0;
                const auto* p = reinterpret_cast<const uint8_t*>(data_.constData()) + off;
                return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
            }

            QByteArray data_;
            QHash<QString, Entry> entries_;
        };

        // ============================================================ SpreadsheetML ===

        // 讀 <si>/<is> 內的文字：串接所有 <t>，略過注音 <rPh>
        QString readRichText(QXmlStreamReader& xml, const QLatin1String& endName) {
            QString s;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == endName) break;
                if (!xml.isStartElement()) continue;
                if (xml.name() == QLatin1String("rPh")) xml.skipCurrentElement();
                else if (xml.name() == QLatin1String("t")) s += xml.readElementText();
            }
            return s;
        }

        QStringList parseSharedStrings(const QByteArray& xmlData) {
            QStringList list;
            QXmlStreamReader xml(xmlData);
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QLatin1String("si"))
                    list.append(readRichText(xml, QLatin1String("si")));
            }
            return list;
        }

        int columnFromRef(const QString& ref) {   // "AB12" -> 27 (0 起算)；失敗回 -1
            int col = 0, i = 0;
            for (; i < ref.size(); ++i) {
                const ushort c = ref[i].toUpper().unicode();
                if (c < 'A' || c > 'Z') break;
                col = col * 26 + (c - 'A' + 1);
            }
            return i == 0 ? -1 : col - 1;
        }

        QString normalizeNumber(const QString& v) {
            const QString t = v.trimmed();
            if (t.isEmpty()) return t;
            bool digitsOnly = true;
            for (int i = 0; i < t.size(); ++i) {
                const QChar c = t[i];
                if (!(c.isDigit() || (i == 0 && c == QLatin1Char('-')))) { digitsOnly = false; break; }
            }
            if (digitsOnly) return t;
            bool ok = false;
            const double d = t.toDouble(&ok);
            if (!ok) return t;
            if (d == std::floor(d) && std::fabs(d) < 1e15) return QString::number(static_cast<qlonglong>(d));
            return QString::number(d, 'g', 15);
        }

        QList<QStringList> parseSheet(const QByteArray& xmlData, const QStringList& sst) {
            constexpr int kMaxRow = 1048576, kMaxCol = 16384;
            QList<QStringList> rows;
            QXmlStreamReader xml(xmlData);
            int rowIdx = -1, colIdx = -1;

            while (!xml.atEnd()) {
                xml.readNext();
                if (!xml.isStartElement()) continue;

                if (xml.name() == QLatin1String("row")) {
                    const QString r = xml.attributes().value(QLatin1String("r")).toString();
                    bool ok = false;
                    const int n = r.toInt(&ok);
                    rowIdx = (ok && n > 0) ? n - 1 : rowIdx + 1;
                    colIdx = -1;
                }
                else if (xml.name() == QLatin1String("c")) {
                    const QString ref = xml.attributes().value(QLatin1String("r")).toString();
                    const QString type = xml.attributes().value(QLatin1String("t")).toString();
                    const int c = columnFromRef(ref);
                    colIdx = (c >= 0) ? c : colIdx + 1;

                    QString v, inl;
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isEndElement() && xml.name() == QLatin1String("c")) break;
                        if (!xml.isStartElement()) continue;
                        if (xml.name() == QLatin1String("v")) v = xml.readElementText();
                        else if (xml.name() == QLatin1String("is")) inl = readRichText(xml, QLatin1String("is"));
                    }

                    QString text;
                    if (type == QLatin1String("s")) {
                        bool ok = false;
                        const int idx = v.toInt(&ok);
                        if (ok && idx >= 0 && idx < sst.size()) text = sst[idx];
                    }
                    else if (type == QLatin1String("inlineStr")) text = inl;
                    else if (type == QLatin1String("str")) text = v;
                    else if (type == QLatin1String("b")) text = (v == QLatin1String("1")) ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
                    else if (type == QLatin1String("e")) text.clear();
                    else text = normalizeNumber(v);

                    if (rowIdx < 0 || rowIdx >= kMaxRow || colIdx >= kMaxCol || text.isEmpty()) continue;
                    while (rows.size() <= rowIdx) rows.append(QStringList());
                    QStringList& row = rows[rowIdx];
                    while (row.size() <= colIdx) row.append(QString());
                    row[colIdx] = text;
                }
            }
            return rows;
        }

        // 找第一個工作表的檔案路徑 (xl/worksheets/sheetN.xml)
        QString firstSheetPath(const ZipArchive& zip) {
            const QString fallback = QStringLiteral("xl/worksheets/sheet1.xml");
            QByteArray wb, rels;
            if (!zip.read(QStringLiteral("xl/workbook.xml"), &wb, nullptr)) return fallback;
            if (!zip.read(QStringLiteral("xl/_rels/workbook.xml.rels"), &rels, nullptr)) return fallback;

            QString rid;
            {
                static const QString kRelNs = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
                QXmlStreamReader xml(wb);
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == QLatin1String("sheet")) {
                        rid = xml.attributes().value(kRelNs, QLatin1String("id")).toString();
                        break;
                    }
                }
            }
            if (rid.isEmpty()) return fallback;

            QXmlStreamReader xml(rels);
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QLatin1String("Relationship") &&
                    xml.attributes().value(QLatin1String("Id")) == rid) {
                    QString target = xml.attributes().value(QLatin1String("Target")).toString();
                    if (target.startsWith(QLatin1Char('/'))) return target.mid(1);
                    return QStringLiteral("xl/") + target;
                }
            }
            return fallback;
        }

    } // namespace

    // ================================================================== public ===

    bool readXlsxFirstSheet(const QString& path, QList<QStringList>* rows, QString* error) {
        auto fail = [error](const QString& m) { if (error) *error = m; return false; };

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("無法開啟檔案：%1").arg(f.errorString()));
        const QByteArray data = f.readAll();
        f.close();

        ZipArchive zip;
        QString err;
        if (!zip.open(data, &err))
            return fail(err + QStringLiteral("\n(舊版 .xls 請先用 Excel 另存為 .xlsx)"));

        QStringList sst;
        if (zip.has(QStringLiteral("xl/sharedStrings.xml"))) {
            QByteArray sx;
            if (!zip.read(QStringLiteral("xl/sharedStrings.xml"), &sx, &err)) return fail(err);
            sst = parseSharedStrings(sx);
        }

        QByteArray sheet;
        if (!zip.read(firstSheetPath(zip), &sheet, &err)) return fail(err);
        *rows = parseSheet(sheet, sst);
        return true;
    }

    bool loadExcelTable(const QString& path, const QStringList& requiredFields, DataTable* out, QString* error) {
        auto fail = [error](const QString& m) { if (error) *error = m; return false; };

        QList<QStringList> raw;
        if (!readXlsxFirstSheet(path, &raw, error)) return false;
        if (raw.isEmpty()) return fail(QStringLiteral("工作表是空的"));

        // 找標題列：前 30 列中，欄位最齊全的那一列
        int headerRow = -1, bestHit = -1;
        QList<int> bestCols;
        const int scan = qMin(30, int(raw.size()));
        for (int r = 0; r < scan; ++r) {
            QList<int> cols;
            int hit = 0;
            for (const QString& want : requiredFields) {
                int idx = -1;
                for (int c = 0; c < raw[r].size(); ++c)
                    if (raw[r][c].trimmed() == want) { idx = c; break; }     // 重複標題取第一個
                cols.append(idx);
                if (idx >= 0) ++hit;
            }
            if (hit > bestHit) { bestHit = hit; headerRow = r; bestCols = cols; }
            if (hit == requiredFields.size()) break;
        }

        QStringList missing;
        for (int i = 0; i < requiredFields.size(); ++i)
            if (bestCols.value(i, -1) < 0) missing << requiredFields[i];
        if (!missing.isEmpty()) {
            return fail(QStringLiteral("找不到必要的欄位：%1\n請確認第一列(標題列)的欄位名稱。")
                .arg(missing.join(QStringLiteral("、"))));
        }

        DataTable t;
        t.path = path;
        t.fields = requiredFields;
        for (int r = headerRow + 1; r < raw.size(); ++r) {
            Record rec;
            bool any = false;
            for (int i = 0; i < requiredFields.size(); ++i) {
                const int c = bestCols[i];
                const QString v = (c < raw[r].size()) ? raw[r][c].trimmed() : QString();
                if (!v.isEmpty()) any = true;
                rec.insert(requiredFields[i], v);
            }
            if (any) t.rows.append(rec);
            else ++t.skippedBlankRows;
        }
        *out = t;
        return true;
    }

} // namespace card