#include "RegionItem.h"

#include <QCursor>
#include <QFont>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

using card::RegionKind;

RegionItem::RegionItem(card::CardLayout* layout, const QString& id, ChangedFn onChanged, QGraphicsItem* parent)
    : QGraphicsItem(parent), layout_(layout), id_(id), onChanged_(std::move(onChanged)) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    syncFromModel();
}

QString RegionItem::kindText(RegionKind k) {
    switch (k) {
    case RegionKind::Add: return QStringLiteral("鍍膜區");
    case RegionKind::Block: return QStringLiteral("列印阻擋");
    default: return QStringLiteral("移除鍍膜");
    }
}

QColor RegionItem::kindColor(RegionKind k) {
    switch (k) {
    case RegionKind::Add: return QColor(0, 150, 70);
    case RegionKind::Block: return QColor(210, 40, 40);
    default: return QColor(235, 130, 0);
    }
}

void RegionItem::syncFromModel() {
    const card::TopcoatRegion* r = layout_->topcoat.find(id_);
    if (!r) return;
    syncing_ = true;
    prepareGeometryChange();
    setPos(r->xMm * kPxPerMm, r->yMm * kPxPerMm);
    syncing_ = false;
    update();
}

QRectF RegionItem::contentRect() const {
    const card::TopcoatRegion* r = layout_->topcoat.find(id_);
    return r ? QRectF(0, 0, r->wMm * kPxPerMm, r->hMm * kPxPerMm) : QRectF();
}

QRectF RegionItem::handleRect() const {
    const QRectF r = contentRect();
    return QRectF(r.right() - 10, r.bottom() - 10, 10, 10);
}

QRectF RegionItem::boundingRect() const { return contentRect().adjusted(-2, -2, 2, 2); }

QPainterPath RegionItem::shape() const {
    QPainterPath p;
    p.addRect(contentRect());
    return p;
}

void RegionItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    const card::TopcoatRegion* reg = layout_->topcoat.find(id_);
    if (!reg) return;
    const QRectF r = contentRect();
    const QColor c = kindColor(reg->kind);

    p->save();
    QColor fill = c;
    fill.setAlpha(reg->kind == RegionKind::Add ? 55 : 80);
    p->setBrush(fill);
    QPen pen(c, 1.5, Qt::DashLine);
    pen.setCosmetic(true);
    p->setPen(pen);
    p->drawRect(r);

    p->setClipRect(r);
    QFont f;
    f.setPixelSize(26);
    f.setBold(true);
    p->setFont(f);
    p->setPen(c.darker(130));
    p->drawText(r.adjusted(4, 2, -4, -2), int(Qt::AlignLeft) | int(Qt::AlignTop), kindText(reg->kind));
    p->setClipping(false);

    if (isSelected()) {
        QPen sel(QColor(0, 120, 215), 1.5, Qt::SolidLine);
        sel.setCosmetic(true);
        p->setPen(sel);
        p->setBrush(Qt::NoBrush);
        p->drawRect(r);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 120, 215));
        p->drawRect(handleRect());
    }
    p->restore();
}

QVariant RegionItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange && !syncing_) {
        const QPointF pt = value.toPointF();
        return QPointF(qRound(pt.x()), qRound(pt.y()));          // 以 0.1 mm 吸附
    }
    if (change == ItemPositionHasChanged && !syncing_) {
        if (card::TopcoatRegion* r = layout_->topcoat.find(id_)) {
            r->xMm = pos().x() / kPxPerMm;
            r->yMm = pos().y() / kPxPerMm;
        }
        if (onChanged_) onChanged_(id_);
    }
    return QGraphicsItem::itemChange(change, value);
}

void RegionItem::mousePressEvent(QGraphicsSceneMouseEvent* ev) {
    if (isSelected() && handleRect().contains(ev->pos())) {
        resizing_ = true;
        ev->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(ev);
}

void RegionItem::mouseMoveEvent(QGraphicsSceneMouseEvent* ev) {
    if (resizing_) {
        if (card::TopcoatRegion* r = layout_->topcoat.find(id_)) {
            r->wMm = qMax(5.0, double(qRound(ev->pos().x()))) / kPxPerMm;    // 最小 0.5 mm
            r->hMm = qMax(5.0, double(qRound(ev->pos().y()))) / kPxPerMm;
            prepareGeometryChange();
            update();
            if (onChanged_) onChanged_(id_);
        }
        ev->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(ev);
}

void RegionItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* ev) {
    resizing_ = false;
    QGraphicsItem::mouseReleaseEvent(ev);
}

void RegionItem::hoverMoveEvent(QGraphicsSceneHoverEvent* ev) {
    const bool onHandle = isSelected() && handleRect().contains(ev->pos());
    setCursor(onHandle ? QCursor(Qt::SizeFDiagCursor) : QCursor(Qt::ArrowCursor));
    QGraphicsItem::hoverMoveEvent(ev);
}