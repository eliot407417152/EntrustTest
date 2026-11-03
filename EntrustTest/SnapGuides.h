// SnapGuides.h: 對齊輔助線 (吸附) + 八方向縮放把手的共用邏輯，ElementItem / RegionItem 共用。
// header-only，不使用 Q_OBJECT。座標單位除非特別註明，都是 mm (原點 = 卡面左上角)。
#pragma once
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include "CardLayout.h"
#include "UiScale.h"

namespace card {

    // ---------------------------------------------------------------- 對齊輔助線 ---

    struct Guide {                    // 一條輔助線 (mm)
        bool vertical;                // true：垂直線 x = pos，從 y = from 畫到 to；false：水平線
        double pos, from, to;
    };
    using GuideSink = std::function<void(const std::vector<Guide>&)>;

    // 全域開關 (檢視選單的「對齊輔助線」)。拖曳時按住 Alt 可暫時停用。
    inline bool& snapEnabledFlag() { static bool on = true; return on; }

    struct SnapContext {
        std::vector<QRectF> others;   // 其他元素 / 鍍膜範圍
        QRectF cardRect;              // 卡面 (0,0,w,h)
        double thr = 0.5;             // 吸附距離 (mm)
        bool enabled = true;

        void lines(std::vector<double>& xs, std::vector<double>& ys) const {
            auto add = [&](const QRectF& r) {
                xs.push_back(r.left()); xs.push_back(r.center().x()); xs.push_back(r.right());
                ys.push_back(r.top());  ys.push_back(r.center().y()); ys.push_back(r.bottom());
                };
            for (const QRectF& o : others) add(o);
            add(cardRect);            // 卡面的左 / 中 / 右、上 / 中 / 下
        }
    };

    // 目前檢視的縮放倍率 (螢幕 px / 場景 px)
    inline double viewScale(const QGraphicsItem* it) {
        if (it && it->scene() && !it->scene()->views().isEmpty()) {
            const double s = std::abs(it->scene()->views().first()->transform().m11());
            if (s > 0) return s;
        }
        return 1.0;
    }

    // excludeId：自己 (元素或範圍的 id)。吸附距離固定約 6 個螢幕像素，不管縮放多少。
    inline SnapContext makeSnapContext(const CardLayout& l, const QString& excludeId, double scale, double pxPerMm) {
        SnapContext c;
        for (const Element& e : l.elements)
            if (e.id != excludeId) c.others.push_back(QRectF(e.xMm, e.yMm, e.wMm, e.hMm));
        for (const TopcoatRegion& r : l.topcoat.regions)
            if (r.id != excludeId) c.others.push_back(QRectF(r.xMm, r.yMm, r.wMm, r.hMm));
        c.cardRect = QRectF(0, 0, l.widthMm(), l.heightMm());
        c.thr = 6.0 / (std::max(scale, 0.05) * pxPerMm);
        return c;
    }

    inline bool snapNearest(double v, const std::vector<double>& lines, double thr, double& out) {
        double best = thr;
        bool hit = false;
        for (double l : lines) {
            const double d = std::abs(l - v);
            if (d <= best) { best = d; out = l; hit = true; }
        }
        return hit;
    }

    inline double round1(double v) { return std::round(v * 10.0) / 10.0; }   // 0.1 mm

    // 移動：用 r 的 左/中/右 與 上/中/下 去貼齊其他元素與卡面。回傳是否有吸附 (x, y)。
    inline void snapMoveRect(QRectF& r, const SnapContext& ctx, bool& snappedX, bool& snappedY) {
        snappedX = snappedY = false;
        if (!ctx.enabled) return;
        std::vector<double> xs, ys;
        ctx.lines(xs, ys);
        double bx = 1e9, by = 1e9, t = 0;
        for (double m : { r.left(), r.center().x(), r.right() })
            if (snapNearest(m, xs, ctx.thr, t) && std::abs(t - m) < std::abs(bx)) bx = t - m;
        for (double m : { r.top(), r.center().y(), r.bottom() })
            if (snapNearest(m, ys, ctx.thr, t) && std::abs(t - m) < std::abs(by)) by = t - m;
        if (std::abs(bx) < 1e8) { r.translate(bx, 0); snappedX = true; }
        if (std::abs(by) < 1e8) { r.translate(0, by); snappedY = true; }
    }

    // 找出 r 與其他物件「對齊」的線 (邊或中心線重合)，供畫輔助線
    inline void collectGuides(const QRectF& r, const SnapContext& ctx, std::vector<Guide>& out) {
        const double eps = 0.01;
        auto scan = [&](const QRectF& o) {
            for (double m : { r.left(), r.center().x(), r.right() })
                for (double t : { o.left(), o.center().x(), o.right() })
                    if (std::abs(m - t) < eps)
                        out.push_back({ true, t, std::min(r.top(), o.top()), std::max(r.bottom(), o.bottom()) });
            for (double m : { r.top(), r.center().y(), r.bottom() })
                for (double t : { o.top(), o.center().y(), o.bottom() })
                    if (std::abs(m - t) < eps)
                        out.push_back({ false, t, std::min(r.left(), o.left()), std::max(r.right(), o.right()) });
            };
        for (const QRectF& o : ctx.others) scan(o);
        scan(ctx.cardRect);
    }

    // ---------------------------------------------------------------- 縮放把手 ---

    enum class Handle { None, TL, T, TR, L, R, BL, B, BR };

    // 把手大小 (場景 px)：約 9 個螢幕像素，小元素會自動縮小，免得把手互相蓋住
    inline double handleSizePx(double viewScale, const QRectF& r) {
        double s = 9.0 * uiscale::factor() / std::max(viewScale, 0.05);   // 約 9 個螢幕像素 (隨介面大小縮放)
        s = std::min(s, std::min(r.width(), r.height()) / 3.5);
        return std::max(s, 2.0);
    }

    // 把手畫在元素「內側」(元素貼著卡面邊緣時也點得到，不會被卡面裁掉)
    inline QRectF handleRect(Handle h, const QRectF& r, double s) {
        const double cx = r.center().x() - s / 2, cy = r.center().y() - s / 2;
        switch (h) {
        case Handle::TL: return QRectF(r.left(), r.top(), s, s);
        case Handle::TR: return QRectF(r.right() - s, r.top(), s, s);
        case Handle::BL: return QRectF(r.left(), r.bottom() - s, s, s);
        case Handle::BR: return QRectF(r.right() - s, r.bottom() - s, s, s);
        case Handle::T:  return QRectF(cx, r.top(), s, s);
        case Handle::B:  return QRectF(cx, r.bottom() - s, s, s);
        case Handle::L:  return QRectF(r.left(), cy, s, s);
        case Handle::R:  return QRectF(r.right() - s, cy, s, s);
        default: return QRectF();
        }
    }

    inline Handle hitHandle(const QPointF& p, const QRectF& r, double s) {
        // 角落優先，再來才是邊的中點
        for (Handle h : { Handle::TL, Handle::TR, Handle::BL, Handle::BR, Handle::T, Handle::B, Handle::L, Handle::R })
            if (handleRect(h, r, s).adjusted(-1, -1, 1, 1).contains(p)) return h;
        return Handle::None;
    }

    inline Qt::CursorShape handleCursor(Handle h) {
        switch (h) {
        case Handle::TL: case Handle::BR: return Qt::SizeFDiagCursor;
        case Handle::TR: case Handle::BL: return Qt::SizeBDiagCursor;
        case Handle::T: case Handle::B: return Qt::SizeVerCursor;
        case Handle::L: case Handle::R: return Qt::SizeHorCursor;
        default: return Qt::ArrowCursor;
        }
    }

    inline void paintHandles(QPainter* p, const QRectF& r, double s) {
        p->save();
        QPen pen(QColor(0, 120, 215), 1);
        pen.setCosmetic(true);
        p->setPen(pen);
        p->setBrush(Qt::white);
        for (Handle h : { Handle::TL, Handle::T, Handle::TR, Handle::L, Handle::R, Handle::BL, Handle::B, Handle::BR })
            p->drawRect(handleRect(h, r, s));
        p->restore();
    }

    // 拖曳把手：mouse = 滑鼠在卡面上的位置 (mm)，orig = 開始拖曳時的範圍 (mm)。
    //  - 邊的把手只改一個方向 (橫向 / 直向)，角落把手兩個方向一起改。
    //  - keepRatio (按住 Shift) + 角落：等比例縮放。
    //  - 會吸附到其他元素 / 卡面的邊與中心線；沒吸附到時以 0.1 mm 為單位。
    inline QRectF dragResize(Handle h, const QRectF& orig, QPointF mouse, double minW, double minH,
        bool keepRatio, const SnapContext* ctx, std::vector<Guide>* guides) {
        const bool mL = h == Handle::TL || h == Handle::L || h == Handle::BL;
        const bool mR = h == Handle::TR || h == Handle::R || h == Handle::BR;
        const bool mT = h == Handle::TL || h == Handle::T || h == Handle::TR;
        const bool mB = h == Handle::BL || h == Handle::B || h == Handle::BR;
        const bool ratio = keepRatio && (mL || mR) && (mT || mB);

        double mx = mouse.x(), my = mouse.y();
        std::vector<double> xs, ys;
        const bool snap = ctx && ctx->enabled && !ratio;
        if (snap) ctx->lines(xs, ys);
        double t = 0;
        if (mL || mR) mx = (snap && snapNearest(mx, xs, ctx->thr, t)) ? t : round1(mx);
        if (mT || mB) my = (snap && snapNearest(my, ys, ctx->thr, t)) ? t : round1(my);

        double l = orig.left(), tp = orig.top(), r = orig.right(), b = orig.bottom();
        if (mL) l = std::min(mx, r - minW);
        if (mR) r = std::max(mx, l + minW);
        if (mT) tp = std::min(my, b - minH);
        if (mB) b = std::max(my, tp + minH);

        if (ratio && orig.width() > 0 && orig.height() > 0) {
            const double s = std::max((r - l) / orig.width(), (b - tp) / orig.height());
            const double w = std::max(orig.width() * s, minW), hh = std::max(orig.height() * s, minH);
            if (mL) l = r - w; else r = l + w;
            if (mT) tp = b - hh; else b = tp + hh;
        }

        const QRectF out(l, tp, r - l, b - tp);
        if (guides && ctx && ctx->enabled) collectGuides(out, *ctx, *guides);
        return out;
    }

} // namespace card

// 場景中的輔助線圖層 (洋紅色細線)；不接受滑鼠，只負責畫
class GuideOverlay : public QGraphicsItem {
public:
    enum { Type = UserType + 3 };

    GuideOverlay(const QRectF& cardPx, double pxPerMm) : area_(cardPx.adjusted(-150, -150, 150, 150)), k_(pxPerMm) {
        setAcceptedMouseButtons(Qt::NoButton);
        setZValue(20000);                                  // 蓋在所有元素與鍍膜範圍上面
    }
    int type() const override { return Type; }

    void setGuides(const std::vector<card::Guide>& g) { guides_ = g; update(); }

    QRectF boundingRect() const override { return area_; }
    QPainterPath shape() const override { return QPainterPath(); }   // 不參與點選
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        if (guides_.empty()) return;
        QPen pen(QColor(255, 0, 140), 1);
        pen.setCosmetic(true);
        p->setPen(pen);
        for (const card::Guide& g : guides_) {
            if (g.vertical) p->drawLine(QPointF(g.pos * k_, g.from * k_), QPointF(g.pos * k_, g.to * k_));
            else p->drawLine(QPointF(g.from * k_, g.pos * k_), QPointF(g.to * k_, g.pos * k_));
        }
    }

private:
    QRectF area_;
    double k_;
    std::vector<card::Guide> guides_;
};