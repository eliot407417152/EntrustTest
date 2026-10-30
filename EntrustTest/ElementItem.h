// ElementItem: 場景中可拖曳、可縮放的卡面元素。
// 刻意不使用 Q_OBJECT (改用 std::function 回呼)，所以不需要 moc 處理。
#pragma once
#include <QGraphicsItem>
#include <functional>
#include "CardLayout.h"

class ElementItem : public QGraphicsItem {
public:
    enum { Type = UserType + 1 };
    static constexpr double kPxPerMm = 10.0;      // 場景單位：1 mm = 10 px

    using ChangedFn = std::function<void(const QString& id)>;

    ElementItem(card::CardLayout* layout, const card::Record* rec, const QString& id,
        ChangedFn onChanged, QGraphicsItem* parent = nullptr);

    int type() const override { return Type; }
    QString id() const { return id_; }

    // 模型 (mm) 改變後，呼叫它把圖形位置/大小更新過來
    void syncFromModel();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* ev) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* ev) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* ev) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* ev) override;

private:
    QRectF contentRect() const;
    QRectF handleRect() const;

    card::CardLayout* layout_;
    const card::Record* rec_;
    QString id_;
    ChangedFn onChanged_;
    bool syncing_ = false;
    bool resizing_ = false;
};