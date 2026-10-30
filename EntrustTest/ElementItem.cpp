#include "ElementItem.h"
#include "CardPainter.h"

#include <QCursor>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

ElementItem::ElementItem(card::CardLayout* layout, const card::Record* rec, const QString& id,
    ChangedFn onChanged, QGraphicsItem* parent)
    : QGraphicsItem(parent), layout_(layout), rec_(rec), id_(id), onChanged_(std::move(onChanged)) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    syncFromModel();
}

void ElementItem::syncFromModel() {
    const card::Element* e = layout_->find(id_);
    if (!e) return;
    syncing_ = true;
    prepareGeometryChange();
    setPos(e->xMm * kPxPerMm, e->yMm * kPxPerMm);
    syncing_ = false;
    update();
}

QRectF ElementItem::contentRect() const {
    const card::Element* e = layout_->find(id_);
    return e ? QRectF(0, 0, e->wMm * kPxPerMm, e->hMm * kPxPerMm) : QRectF();
}

QRectF ElementItem::handleRect() const {
    const QRectF r = contentRect();
    return QRectF(r.right() - 10, r.bottom() - 10, 10, 10);   // 右下角 (在元素內側)
}

QRectF ElementItem::boundingRect() const {
    return contentRect().adjusted(-1, -1, 1, 1);
}

QPainterPath ElementItem::shape() const {
    QPainterPath path;
    path.addRect(contentRect());
    return path;
}

void ElementItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    const card::Element* e = layout_->find(id_);
    if (!e) return;
    const QRectF r = contentRect();

    card::paintElement(*p, *e, r, kPxPerMm, *rec_);

    p->save();
    if (isSelected()) {
        QPen pen(QColor(0, 120, 215), 1, Qt::DashLine);
        pen.setCosmetic(true);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(r);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 120, 215));
        p->drawRect(handleRect());
    }
    else {
        QPen pen(QColor(190, 190, 190), 1, Qt::DotLine);   // 未選取時畫淡淡的邊界，方便看到範圍
        pen.setCosmetic(true);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(r);
    }
    p->restore();
}

QVariant ElementItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange && !syncing_) {
        const QPointF pt = value.toPointF();
        return QPointF(qRound(pt.x()), qRound(pt.y()));          // 以 0.1 mm 為單位吸附
    }
    if (change == ItemPositionHasChanged && !syncing_) {
        if (card::Element* e = layout_->find(id_)) {
            e->xMm = pos().x() / kPxPerMm;
            e->yMm = pos().y() / kPxPerMm;
        }
        if (onChanged_) onChanged_(id_);
    }
    return QGraphicsItem::itemChange(change, value);
}

void ElementItem::mousePressEvent(QGraphicsSceneMouseEvent* ev) {
    if (isSelected() && handleRect().contains(ev->pos())) {
        resizing_ = true;
        ev->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(ev);
}

void ElementItem::mouseMoveEvent(QGraphicsSceneMouseEvent* ev) {
    if (resizing_) {
        if (card::Element* e = layout_->find(id_)) {
            const double wPx = qMax(20.0, double(qRound(ev->pos().x())));   // 最小 2 mm
            const double hPx = qMax(20.0, double(qRound(ev->pos().y())));
            e->wMm = wPx / kPxPerMm;
            e->hMm = hPx / kPxPerMm;
            prepareGeometryChange();
            update();
            if (onChanged_) onChanged_(id_);
        }
        ev->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(ev);
}

void ElementItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* ev) {
    resizing_ = false;
    QGraphicsItem::mouseReleaseEvent(ev);
}

void ElementItem::hoverMoveEvent(QGraphicsSceneHoverEvent* ev) {
    const bool onHandle = isSelected() && handleRect().contains(ev->pos());
    setCursor(onHandle ? QCursor(Qt::SizeFDiagCursor) : QCursor(Qt::ArrowCursor));
    QGraphicsItem::hoverMoveEvent(ev);
}