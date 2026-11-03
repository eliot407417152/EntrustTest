// CardView.h: 卡面檢視 (Ctrl+滾輪縮放) 與 mm 尺規。
// header-only，而且刻意不使用 Q_OBJECT (改用 std::function 回呼)，所以不需要 moc 處理。
#pragma once
#include <QEvent>
#include <QFont>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QWheelEvent>
#include <QWidget>
#include <cmath>
#include <functional>
#include "UiScale.h"

// 卡面檢視：Ctrl + 滾輪縮放。縮放 / 捲動 / 改變大小 / 滑鼠移動時，用回呼通知尺規更新。
class CardView : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;

    std::function<void()> onViewChanged;                                   // 縮放、捲動、視窗大小改變
    std::function<void(const QPointF& scenePos, bool inside)> onCursorMoved; // 滑鼠在卡面檢視內的位置 (場景座標)

protected:
    void wheelEvent(QWheelEvent* e) override {
        if (e->modifiers() & Qt::ControlModifier) {
            const double f = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
            scale(f, f);
            e->accept();
            notifyChanged();
        }
        else {
            QGraphicsView::wheelEvent(e);
        }
    }
    void scrollContentsBy(int dx, int dy) override {
        QGraphicsView::scrollContentsBy(dx, dy);
        notifyChanged();
    }
    void resizeEvent(QResizeEvent* e) override {
        QGraphicsView::resizeEvent(e);
        notifyChanged();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        QGraphicsView::mouseMoveEvent(e);
        if (onCursorMoved) onCursorMoved(mapToScene(e->pos()), true);
    }
    bool viewportEvent(QEvent* e) override {
        if (e->type() == QEvent::Leave && onCursorMoved) onCursorMoved(QPointF(), false);
        return QGraphicsView::viewportEvent(e);
    }

private:
    void notifyChanged() { if (onViewChanged) onViewChanged(); }
};

// 尺規：單位 mm，原點 = 卡面左上角。刻度會跟著 CardView 的縮放與捲動自動調整，
// 卡面範圍內的底色為白色，範圍外為灰色；紅線是目前滑鼠位置。
class RulerWidget : public QWidget {
public:
    static constexpr int kThickness = 24;                       // 100% 時的厚度 (px)
    int thickness() const { return uiscale::px(kThickness); }   // 依介面大小縮放後的厚度
    // 介面大小改變後呼叫：更新厚度與字型
    void applyScale() {
        if (orient_ == Qt::Horizontal) setFixedHeight(thickness());
        else setFixedWidth(thickness());
        QFont f = font();
        f.setPixelSize(uiscale::px(10));
        setFont(f);
        updateGeometry();
        update();
    }

    // pxPerMm：場景單位與 mm 的換算 (ElementItem::kPxPerMm)
    RulerWidget(Qt::Orientation o, QGraphicsView* view, double pxPerMm, QWidget* parent = nullptr)
        : QWidget(parent), orient_(o), view_(view), pxPerMm_(pxPerMm) {
        applyScale();
    }

    std::function<QRectF()> cardRect;       // 卡面在場景中的範圍 (用來畫白色底)
    std::function<double()> flipHeightMm;   // 垂直尺規：卡面高度 (mm)。有設定 = 原點在卡面左下角、刻度往上增加

    void setCursorScenePos(const QPointF& p, bool visible) {
        cursor_ = p;
        cursorVisible_ = visible;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const bool horiz = orient_ == Qt::Horizontal;
        const int T = thickness();             // 依介面大小縮放後的尺規厚度
        p.fillRect(rect(), QColor(222, 222, 222));

        const QTransform t = view_->viewportTransform();
        bool ok = false;
        const QTransform inv = t.inverted(&ok);
        const double scale = std::abs(horiz ? t.m11() : t.m22()) * pxPerMm_;   // 螢幕 px / mm
        if (!ok || scale <= 0) return;

        auto axisPx = [&](double scenePos) {   // 場景座標 -> 尺規上的位置 (px)
            const QPointF q = t.map(QPointF(scenePos, scenePos));
            return horiz ? q.x() : q.y();
            };

        // 卡面範圍 = 白色底
        const QRectF card = cardRect ? cardRect() : QRectF();
        if (card.isValid()) {
            const double a = horiz ? axisPx(card.left()) : axisPx(card.top());
            const double b = horiz ? axisPx(card.right()) : axisPx(card.bottom());
            p.fillRect(horiz ? QRectF(a, 0, b - a, T) : QRectF(0, a, T, b - a), QColor(250, 250, 250));
        }

        // 目前看得到的 mm 範圍
        const QPointF o = inv.map(QPointF(0, 0));
        const QPointF e = inv.map(QPointF(width(), height()));
        double lo = (horiz ? o.x() : o.y()) / pxPerMm_;
        double hi = (horiz ? e.x() : e.y()) / pxPerMm_;
        if (lo > hi) std::swap(lo, hi);
        const double flipH = (!horiz && flipHeightMm) ? flipHeightMm() : 0.0;   // > 0 = 垂直尺規以卡面底邊為 0
        if (flipH > 0) { const double l2 = flipH - hi, h2 = flipH - lo; lo = l2; hi = h2; }

        // 依縮放程度挑「主刻度」間距：至少相隔 55 px
        static const double kSteps[] = { 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500 };
        double major = kSteps[11];
        for (double s : kSteps) if (s * scale >= uiscale::px(55)) { major = s; break; }
        const double mant = major / std::pow(10.0, std::floor(std::log10(major) + 1e-9));   // 1 / 2 / 5
        int sub = mant < 1.5 ? 10 : (mant < 3 ? 4 : 5);                                     // 每個主刻度切幾格
        if (major / sub * scale < uiscale::px(4)) sub = mant < 1.5 ? 2 : 1;                            // 太密就少畫

        const double minor = major / sub;
        const long long i0 = static_cast<long long>(std::floor(lo / minor)) - 1;
        const long long i1 = static_cast<long long>(std::ceil(hi / minor)) + 1;
        if (i1 - i0 <= 4000) {
            p.setPen(QColor(90, 90, 90));
            for (long long i = i0; i <= i1; ++i) {
                const double mm = i * minor;
                const double px = axisPx((flipH > 0 ? flipH - mm : mm) * pxPerMm_);
                const long long k = ((i % sub) + sub) % sub;
                const bool isMajor = (k == 0);
                const bool isMid = (sub == 10 && k == 5);
                const int tl = isMajor ? uiscale::px(14) : (isMid ? uiscale::px(9) : uiscale::px(5));
                if (horiz) p.drawLine(QPointF(px, T - tl), QPointF(px, T));
                else p.drawLine(QPointF(T - tl, px), QPointF(T, px));

                if (isMajor) {
                    const QString txt = QString::number(std::abs(mm) < 1e-9 ? 0.0 : mm, 'f', major < 1 ? 1 : 0);
                    if (horiz) {
                        p.drawText(QPointF(px + 3, uiscale::px(9)), txt);
                    }
                    else {
                        p.save();
                        p.translate(1, px + 3);
                        p.rotate(90);                 // 由上往下閱讀
                        p.drawText(QPointF(0, 0), txt);
                        p.restore();
                    }
                }
            }
        }

        // 滑鼠位置 (紅線)
        if (cursorVisible_) {
            const double px = axisPx(horiz ? cursor_.x() : cursor_.y());
            p.setPen(QPen(QColor(220, 40, 40), 1));
            if (horiz) p.drawLine(QPointF(px, 0), QPointF(px, T));
            else p.drawLine(QPointF(0, px), QPointF(T, px));
        }

        // 與卡面檢視相接的那條邊界線
        p.setPen(QColor(160, 160, 160));
        if (horiz) p.drawLine(0, T - 1, width(), T - 1);
        else p.drawLine(T - 1, 0, T - 1, height());
    }

private:
    Qt::Orientation orient_;
    QGraphicsView* view_;
    double pxPerMm_;
    QPointF cursor_;
    bool cursorVisible_ = false;
};