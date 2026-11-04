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

bool& RegionItem::outlineOnly() { static bool v = false; return v; }
bool& RegionItem::locked() { static bool v = true; return v; }

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

// 平常 (不在「編輯鍍膜範圍」模式) 完全不接受滑鼠，點擊會穿透到下面的卡面元素，所以不會擋到設計。
// 進入「編輯鍍膜範圍」模式後 (勾選面板上的核取方塊、新增範圍、或從清單選取範圍)，才可以拖曳 / 縮放範圍；
// 此時元素暫時不接受滑鼠。
QPainterPath RegionItem::shape() const {
    QPainterPath p;
    if (locked()) return p;
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
    fill.setAlpha(reg->kind == RegionKind::Add ? 30 : 45);      // 淡一點，比較不會遮住設計
    // 平常只畫外框 (不填色，完全不遮住設計)；編輯模式才填淡淡的顏色 (可再用「只畫外框」關掉)
    p->setBrush((locked() || outlineOnly()) ? QBrush(Qt::NoBrush) : QBrush(fill));
    QPen pen(c, 1.5, Qt::DashLine);
    pen.setCosmetic(true);
    p->setPen(pen);
    p->drawRect(r);

    p->setClipRect(r);
    QFont f;
    f.setPixelSize(20);
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
    if (locked()) { ev->ignore(); return; }
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
    setCursor(QCursor(h == card::Handle::None ? Qt::SizeAllCursor : card::handleCursor(h)));   // 外框上 = 可拖曳移動
    QGraphicsItem::hoverMoveEvent(ev);
}