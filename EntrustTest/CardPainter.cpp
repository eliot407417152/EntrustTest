#include "CardPainter.h"
#include "ImageMatcher.h"

#include <QCache>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPen>

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
        else {
            if (e.imageFit != ImageFit::MatchHeight) return false;   // 只有「框高依圖片比例」才會自動改框的大小
            const QImage img = loadImageCached(resolveImagePath(e, rec));
            if (img.isNull() || img.width() <= 0) return false;  // 找不到圖片：維持原樣
            nh = qMax(0.5, e.wMm * img.height() / double(img.width()));
        }

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