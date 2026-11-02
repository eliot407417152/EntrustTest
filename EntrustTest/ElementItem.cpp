#include "ElementItem.h"
#include "CardPainter.h"

#include <QApplication>
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

double ElementItem::handleSize() const {
    return card::handleSizePx(card::viewScale(this), contentRect());
}

card::SnapContext ElementItem::snapContext() const {
    card::SnapContext c = card::makeSnapContext(*layout_, id_, card::viewScale(this), kPxPerMm);
    c.enabled = card::snapEnabledFlag() && !(QApplication::keyboardModifiers() & Qt::AltModifier);   // 按住 Alt = 暫時不吸附
    return c;
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
        card::paintHandles(p, r, handleSize());          // 四個角 + 四個邊中點，共 8 個把手
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
        QPointF pt = value.toPointF();
        const card::Element* e = layout_->find(id_);
        const bool userDrag = e && resizing_ == card::Handle::None && scene() && scene()->selectedItems().size() <= 1
            && (QApplication::mouseButtons() & Qt::LeftButton);
        if (userDrag) {
            // 使用者拖曳中：貼齊其他元素 / 卡面的邊與中心線，並畫出輔助線
            const card::SnapContext ctx = snapContext();
            QRectF r(pt.x() / kPxPerMm, pt.y() / kPxPerMm, e->wMm, e->hMm);
            bool sx = false, sy = false;
            card::snapMoveRect(r, ctx, sx, sy);
            std::vector<card::Guide> guides;
            if (ctx.enabled) card::collectGuides(r, ctx, guides);
            sendGuides(guides);
            return QPointF(sx ? r.x() * kPxPerMm : qRound(pt.x()), sy ? r.y() * kPxPerMm : qRound(pt.y()));
        }
        return QPointF(qRound(pt.x()), qRound(pt.y()));          // 以 0.1 mm 為單位
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
    if (isSelected()) {
        const card::Handle h = card::hitHandle(ev->pos(), contentRect(), handleSize());
        if (h != card::Handle::None) {
            if (const card::Element* e = layout_->find(id_)) {
                resizing_ = h;
                origMm_ = QRectF(e->xMm, e->yMm, e->wMm, e->hMm);
                ev->accept();
                return;
            }
        }
    }
    QGraphicsItem::mousePressEvent(ev);
}

void ElementItem::mouseMoveEvent(QGraphicsSceneMouseEvent* ev) {
    if (resizing_ != card::Handle::None) {
        if (card::Element* e = layout_->find(id_)) {
            // 滑鼠在卡面上的位置 (mm)
            const QPointF inCard = parentItem() ? parentItem()->mapFromScene(ev->scenePos()) : ev->scenePos();
            const card::SnapContext ctx = snapContext();
            std::vector<card::Guide> guides;
            const QRectF nr = card::dragResize(resizing_, origMm_, inCard / kPxPerMm, 2.0, 2.0,
                ev->modifiers() & Qt::ShiftModifier, &ctx, &guides);     // Shift：角落等比例縮放
            e->xMm = nr.x(); e->yMm = nr.y(); e->wMm = nr.width(); e->hMm = nr.height();
            syncFromModel();                 // 上 / 左邊拖曳時位置也會變
            sendGuides(guides);
            if (onChanged_) onChanged_(id_);
        }
        ev->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(ev);
}

void ElementItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* ev) {
    resizing_ = card::Handle::None;
    sendGuides({});                          // 放開滑鼠後清掉輔助線
    QGraphicsItem::mouseReleaseEvent(ev);
}

void ElementItem::hoverMoveEvent(QGraphicsSceneHoverEvent* ev) {
    const card::Handle h = isSelected() ? card::hitHandle(ev->pos(), contentRect(), handleSize()) : card::Handle::None;
    setCursor(QCursor(card::handleCursor(h)));
    QGraphicsItem::hoverMoveEvent(ev);
}