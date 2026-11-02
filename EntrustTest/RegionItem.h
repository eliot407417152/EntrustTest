// RegionItem: 場景中可拖曳、可縮放的「鍍膜範圍」(鍍膜區 / 移除鍍膜 / 列印阻擋)。
// 與 ElementItem 相同：不使用 Q_OBJECT，改用 std::function 回呼，所以不需要 moc 處理。
#pragma once
#include <QGraphicsItem>
#include <functional>
#include "CardLayout.h"
#include "SnapGuides.h"

class RegionItem : public QGraphicsItem {
public:
    enum { Type = UserType + 2 };
    static constexpr double kPxPerMm = 10.0;      // 與 ElementItem 相同：1 mm = 10 px

    using ChangedFn = std::function<void(const QString& id)>;

    RegionItem(card::CardLayout* layout, const QString& id, ChangedFn onChanged, QGraphicsItem* parent = nullptr);

    int type() const override { return Type; }
    QString id() const { return id_; }
    void syncFromModel();
    void setGuideSink(card::GuideSink fn) { guideSink_ = std::move(fn); }

    static QString kindText(card::RegionKind k);
    static QColor kindColor(card::RegionKind k);

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
    double handleSize() const;
    card::SnapContext snapContext() const;
    void sendGuides(const std::vector<card::Guide>& g) const { if (guideSink_) guideSink_(g); }

    card::CardLayout* layout_;
    QString id_;
    ChangedFn onChanged_;
    bool syncing_ = false;
    card::GuideSink guideSink_;
    card::Handle resizing_ = card::Handle::None;
    QRectF origMm_;
};