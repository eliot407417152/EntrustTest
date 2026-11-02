#include "RegionItem.h"

#include <QApplication>
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

double RegionItem::handleSize() const {
    return card::handleSizePx(card::viewScale(this), contentRect());
}

card::SnapContext RegionItem::snapContext() const {
    card::SnapContext c = card::makeSnapContext(*layout_, id_, card::viewScale(this), kPxPerMm);
    c.enabled = card::snapEnabledFlag() && !(QApplication::keyboardModifiers() & Qt::AltModifier);
    return c;
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
        card::paintHandles(p, r, handleSize());
    }
    p->restore();
}

QVariant RegionItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange && !syncing_) {
        QPointF pt = value.toPointF();
        const card::TopcoatRegion* reg = layout_->topcoat.find(id_);
        const bool userDrag = reg && resizing_ == card::Handle::None && scene() && scene()->selectedItems().size() <= 1
            && (QApplication::mouseButtons() & Qt::LeftButton);
        if (userDrag) {
            const card::SnapContext ctx = snapContext();
            QRectF r(pt.x() / kPxPerMm, pt.y() / kPxPerMm, reg->wMm, reg->hMm);
            bool sx = false, sy = false;
            card::snapMoveRect(r, ctx, sx, sy);
            std::vector<card::Guide> guides;
            if (ctx.enabled) card::collectGuides(r, ctx, guides);
            sendGuides(guides);
            return QPointF(sx ? r.x() * kPxPerMm : qRound(pt.x()), sy ? r.y() * kPxPerMm : qRound(pt.y()));
        }
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
    if (isSelected()) {
        const card::Handle h = card::hitHandle(ev->pos(), contentRect(), handleSize());
        if (h != card::Handle::None) {
            if (const card::TopcoatRegion* r = layout_->topcoat.find(id_)) {
                resizing_ = h;
                origMm_ = QRectF(r->xMm, r->yMm, r->wMm, r->hMm);
                ev->accept();
                return;
            }
        }
    }
    QGraphicsItem::mousePressEvent(ev);
}

void RegionItem::mouseMoveEvent(QGraphicsSceneMouseEvent* ev) {
    if (resizing_ != card::Handle::None) {
        if (card::TopcoatRegion* r = layout_->topcoat.find(id_)) {
            const QPointF inCard = parentItem() ? parentItem()->mapFromScene(ev->scenePos()) : ev->scenePos();
            const card::SnapContext ctx = snapContext();
            std::vector<card::Guide> guides;
            const QRectF nr = card::dragResize(resizing_, origMm_, inCard / kPxPerMm, 0.5, 0.5,
                ev->modifiers() & Qt::ShiftModifier, &ctx, &guides);
            r->xMm = nr.x(); r->yMm = nr.y(); r->wMm = nr.width(); r->hMm = nr.height();
            syncFromModel();
            sendGuides(guides);
            if (onChanged_) onChanged_(id_);
        }
        ev->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(ev);
}

void RegionItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* ev) {
    resizing_ = card::Handle::None;
    sendGuides({});
    QGraphicsItem::mouseReleaseEvent(ev);
}

void RegionItem::hoverMoveEvent(QGraphicsSceneHoverEvent* ev) {
    const card::Handle h = isSelected() ? card::hitHandle(ev->pos(), contentRect(), handleSize()) : card::Handle::None;
    setCursor(QCursor(card::handleCursor(h)));
    QGraphicsItem::hoverMoveEvent(ev);
}