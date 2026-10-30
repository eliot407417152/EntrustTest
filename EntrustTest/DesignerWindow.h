// DesignerWindow: 卡面設計器主視窗 (階段 2：載入 Excel 製卡資料)。
// 刻意不使用 Q_OBJECT：所有連線都用 lambda，所以不需要 moc 處理。
#pragma once
#include <QMainWindow>
#include <functional>
#include <utility>
#include <vector>
#include "CardLayout.h"
#include "ExcelReader.h"

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QFontComboBox;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsView;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QTableWidget;
class ElementItem;

class DesignerWindow : public QMainWindow {
public:
    explicit DesignerWindow(QWidget* parent = nullptr);

    // 載入 Excel 製卡資料 (成功回傳 true；失敗會跳出訊息視窗)
    bool loadData(const QString& path);

protected:
    void showEvent(QShowEvent* ev) override;

private:
    // UI 建立
    void buildUi();
    void buildMenus();
    QWidget* buildPropertyPanel();
    QWidget* buildPreviewPanel();
    QWidget* buildDataPanel();
    void resetDockLayout();
    void applyDockSizes();
    void updateActionStates();

    // 場景與模型同步
    void rebuildScene();
    ElementItem* createItem(const QString& id, int z);
    ElementItem* findItem(const QString& id) const;
    ElementItem* selectedItem() const;
    void fitView();

    // 動作
    void addElement(card::ElementType type);
    void deleteSelected();
    void openLayout();
    void saveLayout();
    void exportPreview();
    void setOrientation(card::Orientation o);

    // 製卡資料 (階段 2)
    void browseData();
    void applyDataRow(int row);
    void stepRecord(int delta);
    void applyDataFilter(const QString& text);
    void updateDataInfo();

    // 面板與清單
    void populatePanel(const card::Element* e);
    void refreshList();
    void refreshListName(const card::Element& e);
    void syncListSelection(const QString& id);
    void onSelectionChanged();
    void onItemChanged(const QString& id);
    void editSelected(const std::function<void(card::Element&)>& fn);
    void applyPreviewRecord();
    void updateColorButton(const QColor& c);

    card::CardLayout layout_;
    card::Record previewRecord_;           // 場景內的元素都指向這個物件
    bool updating_ = false;                // 避免「程式更新控制項」又觸發「使用者編輯」
    bool shownOnce_ = false;

    QGraphicsScene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    QGraphicsRectItem* cardItem_ = nullptr;

    // 四個可開關的面板 (到「檢視」選單可重新開啟)
    QDockWidget* elementDock_ = nullptr;
    QDockWidget* dataDock_ = nullptr;
    QDockWidget* propertyDock_ = nullptr;
    QDockWidget* previewDock_ = nullptr;

    // 動作 (需要隨狀態啟用/停用的)
    QAction* actDelete_ = nullptr;
    QAction* actPrevRecord_ = nullptr;
    QAction* actNextRecord_ = nullptr;
    QAction* actLandscape_ = nullptr;
    QAction* actPortrait_ = nullptr;

    QListWidget* elementList_ = nullptr;

    // 屬性面板
    QWidget* propertyPanel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLabel* typeLabel_ = nullptr;
    QDoubleSpinBox* xSpin_ = nullptr;
    QDoubleSpinBox* ySpin_ = nullptr;
    QDoubleSpinBox* wSpin_ = nullptr;
    QDoubleSpinBox* hSpin_ = nullptr;
    QGroupBox* textGroup_ = nullptr;
    QPlainTextEdit* textEdit_ = nullptr;
    QComboBox* fieldCombo_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QDoubleSpinBox* sizeSpin_ = nullptr;
    QCheckBox* boldCheck_ = nullptr;
    QCheckBox* italicCheck_ = nullptr;
    QComboBox* alignCombo_ = nullptr;
    QPushButton* colorBtn_ = nullptr;
    QGroupBox* imageGroup_ = nullptr;
    QLineEdit* imageEdit_ = nullptr;
    QCheckBox* keepAspectCheck_ = nullptr;

    // 預覽資料
    QCheckBox* usePreviewCheck_ = nullptr;
    std::vector<std::pair<QString, QLineEdit*>> previewEdits_;

    // 製卡資料
    card::DataTable data_;
    QTableWidget* dataTable_ = nullptr;
    QLabel* dataInfo_ = nullptr;
    QLineEdit* dataFilter_ = nullptr;
    QString lastDataDir_;
};