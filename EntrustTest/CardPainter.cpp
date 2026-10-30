#include "CardPainter.h"
#include "ImageMatcher.h"

#include <QCache>
#include <QFileInfo>
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
    }

    void clearImageCache() {
        QMutexLocker lock(&g_cacheMutex);
        cache().clear();
    }

    void paintElement(QPainter& p, const Element& e, const QRectF& r, double pxPerMm, const Record& rec) {
        p.save();
        p.setClipRect(r, Qt::IntersectClip);

        if (e.type == ElementType::Text) {
            QFont f(e.fontFamily);
            const double px = e.fontPt * 25.4 / 72.0 * pxPerMm;   // pt -> mm -> px
            f.setPixelSize(qMax(1, qRound(px)));
            f.setBold(e.bold);
            f.setItalic(e.italic);
            p.setFont(f);
            p.setPen(e.color);

            int flags = int(Qt::AlignVCenter) | int(Qt::TextWordWrap);
            switch (e.align) {
            case HAlign::Center: flags |= int(Qt::AlignHCenter); break;
            case HAlign::Right:  flags |= int(Qt::AlignRight);   break;
            default:             flags |= int(Qt::AlignLeft);    break;
            }
            p.drawText(r, flags, substitute(e.text, rec));
        }
        else {
            // 圖片路徑留空 = 自動比對：使用 ImageMatcher 依資料欄位在圖片資料夾找到的圖
            const bool autoMode = e.imagePath.trimmed().isEmpty();
            const QString path = autoMode ? rec.value(autoImageKey()) : substitute(e.imagePath, rec);
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
                    : (autoMode && !rec.value(autoImageNoteKey()).isEmpty() ? rec.value(autoImageNoteKey())
                        : QStringLiteral("[圖片]")));
            }
            else {
                p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                QRectF target = r;
                if (e.keepAspect) {
                    const QSizeF s = QSizeF(img.size()).scaled(r.size(), Qt::KeepAspectRatio);
                    target = QRectF(r.left() + (r.width() - s.width()) / 2.0,
                        r.top() + (r.height() - s.height()) / 2.0,
                        s.width(), s.height());
                }
                p.drawImage(target, img);
            }
        }
        p.restore();
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
        for (const auto& e : layout.elements) {
            paintElement(p, e, QRectF(e.xMm * ppm, e.yMm * ppm, e.wMm * ppm, e.hMm * ppm), ppm, rec);
        }
        p.end();
        return img;
    }

} // namespace card