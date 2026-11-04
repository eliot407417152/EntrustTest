// DesignerWindow: 卡面設計器主視窗 (階段 4：鍍膜範圍)。
// 刻意不使用 Q_OBJECT：所有連線都用 lambda，所以不需要 moc 處理。
#pragma once
#include <QMainWindow>
#include <functional>
#include <utility>
#include <vector>
#include "CardLayout.h"
#include "ExcelReader.h"
#include "ImageMatcher.h"

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QGraphicsItem;
class QFormLayout;
class QFontComboBox;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsView;
class CardView;
class RulerWidget;
class GuideOverlay;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QCloseEvent;
class QTimer;
class QTableWidget;
class ElementItem;
class RegionItem;

class DesignerWindow : public QMainWindow {
public:
    explicit DesignerWindow(QWidget* parent = nullptr);
    ~DesignerWindow() override;

    // 載入 Excel 製卡資料 (成功回傳 true；失敗會跳出訊息視窗)
    bool loadData(const QString& path);

    // 設定圖片資料夾 (空字串 = 不使用)。圖片元素的「圖片路徑」留空時，會依資料欄位自動到此資料夾找圖
    void setImageFolder(const QString& dir);

protected:
    void showEvent(QShowEvent* ev) override;
    void closeEvent(QCloseEvent* ev) override;     // 記住視窗位置、大小與面板配置

private:
    // UI 建立
    void buildUi();
    void buildMenus();
    QWidget* buildPropertyPanel();
    QWidget* buildPreviewPanel();
    QWidget* buildDataPanel();
    QWidget* buildTopcoatPanel();
    QWidget* buildBackgroundPanel();
    void resetDockLayout();
    void applyUiScale();                   // 介面大小改變後，更新所有寫死像素尺寸的元件
    void fitWindowToScreen();              // 視窗適應螢幕 (並還原面板配置)
    void applyDockSizes();
    void updateActionStates();

    // 場景與模型同步
    void rebuildScene(bool refit = true);   // refit = false：保留目前的縮放與捲動位置 (復原/重做用)
    ElementItem* createItem(const QString& id, int z);
    ElementItem* findItem(const QString& id) const;
    ElementItem* selectedItem() const;
    void fitView();
    void updateCursorLabel(const QPointF& scenePos, bool inside);   // 狀態列：卡面尺寸 + 游標位置 (mm)

    // 復原 / 重做 (以版面快照實作，見 DesignerWindow.cpp 的「復原 / 重做」一節)
    void commitUndo();                     // 版面與上次紀錄不同 -> 記成一個復原步驟
    void scheduleCommit();                 // 連續編輯 (拖曳、輸入數字…) 停手後才記成一步
    void undo();
    void redo();
    void restoreLayout(const card::CardLayout& l);
    void updateUndoActions();
    void updateRecordLabel();

    // 動作
    void addElement(card::ElementType type);
    void deleteSelected();
    void moveLayer(int mode);              // 圖層順序：0 = 移到最上層、1 = 上移一層、2 = 下移一層、3 = 移到最下層
    void openLayout();
    void saveLayout();
    void exportPreview();
    void openPrintDialog();                // 階段 3：批次列印
    void setOrientation(card::Orientation o);

    // 鍍膜範圍 (階段 4)
    RegionItem* createRegionItem(const QString& id, int z);
    RegionItem* findRegionItem(const QString& id) const;
    RegionItem* selectedRegion() const;
    QString currentRegionId() const;
    void addRegion();
    void deleteSelectedRegion();
    void refreshTopcoatPanel();                    // 依 layout_ 重新填入「鍍膜」面板
    void refreshRegionList();
    void refreshRegionListLabel(const card::TopcoatRegion& r);
    void syncRegionListSelection(const QString& id);
    void populateRegionEditor(const card::TopcoatRegion* r);
    void editSelectedRegion(const std::function<void(card::TopcoatRegion&)>& fn);
    void onRegionItemChanged(const QString& id);
    void refreshTopcoatPreview();
    void refreshRegionItemsView();                 // 「編輯鍍膜範圍 / 只畫外框」切換後，更新範圍與元素的滑鼠狀態

    // 卡面底圖：匯入卡面的圖當設計參考，可選擇列印時印出或不印出
    void importBackground();
    void clearBackground();
    void editBackground(const std::function<void(card::CardBackground&)>& fn);
    void refreshBackgroundPanel();                 // 依 layout_.background 重新填入「卡面底圖」面板

    // 製卡資料 (階段 2)
    void browseData();
    void applyDataRow(int row);
    void stepRecord(int delta);
    void applyDataFilter(const QString& text);
    void updateDataInfo();
    QStringList currentFields() const;     // 目前可用的欄位：載入 Excel 後 = Excel 的所有欄位；尚未載入 = 預設範例欄位
    QStringList layoutFields() const;      // 版面實際用到的欄位 (文字的 {欄位}、固定圖片路徑的 {欄位}、動態圖片的比對欄位)
    void rebuildFieldWidgets();            // 載入資料後，依 Excel 欄位重建：插入欄位 / 圖片比對欄位 / 預覽資料 / 資料表欄
    void rebuildPreviewForm();             // 重建右側「預覽資料」的欄位輸入框

    // 圖片自動對應 (階段 2)
    void refreshImageMatches(bool force = true);   // 重算「圖片」欄、統計，並更新預覽 (force = false 時，沒變動就略過)
    const card::Element* imageTarget();    // 「圖片」欄 / 統計顯示哪個圖片元素的比對結果
    void onImageSourceChanged();           // 圖片的來源 / 資料夾 / 比對欄位改變後：重新掃描、比對、更新預覽與面板
    void migrateLegacyImageSettings(card::CardLayout& lay);   // 舊版 (全域圖片資料夾) -> 每個圖片元素自己的設定

    // 面板與清單
    void populatePanel(const card::Element* e);
    void refreshList();
    void refreshListName(const card::Element& e);
    void syncListSelection(const QString& id);
    void onSelectionChanged();
    void onItemChanged(const QString& id);
    void editSelected(const std::function<void(card::Element&)>& fn);
    void applyPreviewRecord();
    void refitAll();                       // 預覽資料改變後，重新貼合所有「貼合內容」的元素
    void updateColorButton(const QColor& c);

    card::CardLayout layout_;
    // 復原 / 重做
    std::vector<card::CardLayout> undoStack_;
    std::vector<card::CardLayout> redoStack_;
    card::CardLayout baseline_;            // 上一次記錄時的版面
    QTimer* commitTimer_ = nullptr;
    static constexpr size_t kMaxUndo = 100;

    card::Record previewRecord_;           // 場景內的元素都指向這個物件
    bool updating_ = false;                // 避免「程式更新控制項」又觸發「使用者編輯」
    bool shownOnce_ = false;
    bool restoredState_ = false;           // 已還原上次的面板配置 (就不要再套用預設面板大小)
    QLabel* cornerLabel_ = nullptr;        // 兩條尺規交會處的 "mm"
    QWidget* topcoatPanel_ = nullptr;
    QWidget* backgroundPanel_ = nullptr;
    QGraphicsItem* bgItem_ = nullptr;      // 場景中的卡面底圖 (最底層，不能選取 / 拖曳)
    std::vector<QAction*> scaleActs_;      // 「介面大小」選單的各個選項 (data = 百分比，0 = 自動)

    QGraphicsScene* scene_ = nullptr;
    CardView* view_ = nullptr;
    RulerWidget* rulerH_ = nullptr;        // 卡面上方的 mm 尺規
    RulerWidget* rulerV_ = nullptr;        // 卡面左方的 mm 尺規
    GuideOverlay* guides_ = nullptr;       // 拖曳時的對齊輔助線
    QLabel* recordLabel_ = nullptr;        // 工具列：「第 n / N 筆」
    QLabel* cursorLabel_ = nullptr;        // 狀態列右側：卡面尺寸、游標位置
    QGraphicsRectItem* cardItem_ = nullptr;

    // 四個可開關的面板 (到「檢視」選單可重新開啟)
    QDockWidget* elementDock_ = nullptr;
    QDockWidget* dataDock_ = nullptr;
    QDockWidget* propertyDock_ = nullptr;
    QDockWidget* previewDock_ = nullptr;
    QDockWidget* topcoatDock_ = nullptr;
    QDockWidget* backgroundDock_ = nullptr;

    // 動作 (需要隨狀態啟用/停用的)
    QAction* actUndo_ = nullptr;
    QAction* actRedo_ = nullptr;
    QAction* actDelete_ = nullptr;
    QAction* actLayer_[4] = { nullptr, nullptr, nullptr, nullptr };   // 移到最上層 / 上移一層 / 下移一層 / 移到最下層
    QAction* actPrevRecord_ = nullptr;
    QAction* actNextRecord_ = nullptr;
    QAction* actLandscape_ = nullptr;
    QAction* actPortrait_ = nullptr;
    QAction* actShowRegions_ = nullptr;
    QAction* actRegionOutline_ = nullptr;
    QAction* actRegionEdit_ = nullptr;     // 編輯鍍膜範圍模式 (開啟時只能操作範圍，元素暫時不接受滑鼠)
    QCheckBox* regionEditCheck_ = nullptr;
    QAction* actSnap_ = nullptr;
    QAction* actImportBg_ = nullptr;
    QAction* actClearBg_ = nullptr;

    QListWidget* elementList_ = nullptr;

    // 屬性面板
    QWidget* propertyPanel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLabel* typeLabel_ = nullptr;
    QDoubleSpinBox* xSpin_ = nullptr;
    QDoubleSpinBox* ySpin_ = nullptr;
    QDoubleSpinBox* wSpin_ = nullptr;
    QDoubleSpinBox* hSpin_ = nullptr;
    QCheckBox* fitCheck_ = nullptr;        // 貼合內容
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
    QComboBox* imageSourceCombo_ = nullptr;   // 固定圖片 / 動態圖片
    QWidget* imageFixedBox_ = nullptr;        // 固定圖片的設定 (路徑)
    QWidget* imageDynBox_ = nullptr;          // 動態圖片的設定 (資料夾、比對欄位)
    QLineEdit* imageFolderEdit_ = nullptr;
    QComboBox* matchFieldCombo_ = nullptr;
    QLabel* imageSummary_ = nullptr;
    QComboBox* imageFitCombo_ = nullptr;      // 圖片顯示方式：裁切 / 縮放 / 拉伸 / 框高依圖片比例
    QDoubleSpinBox* imageZoomSpin_ = nullptr;
    QDoubleSpinBox* imageFocusXSpin_ = nullptr;
    QDoubleSpinBox* imageFocusYSpin_ = nullptr;

    // 條碼 (Code 39)：固定輸入內容 / 動態指定 Excel 欄位
    QGroupBox* barcodeGroup_ = nullptr;
    QComboBox* barcodeSourceCombo_ = nullptr;
    QComboBox* barcodeTypeCombo_ = nullptr;
    QWidget* barcodeTypeBox_ = nullptr;
    QWidget* qrEccBox_ = nullptr;
    QComboBox* qrEccCombo_ = nullptr;
    QLabel* barcodeHint_ = nullptr;
    QWidget* barcodeFixedBox_ = nullptr;
    QWidget* barcodeDynamicBox_ = nullptr;
    QLineEdit* barcodeValueEdit_ = nullptr;
    QComboBox* barcodeFieldCombo_ = nullptr;

    // 鍍膜面板
    QComboBox* topBaseCombo_ = nullptr;
    QComboBox* topExcCombo_ = nullptr;
    QListWidget* regionList_ = nullptr;
    QGroupBox* regionEditBox_ = nullptr;
    QComboBox* regionKindCombo_ = nullptr;
    QDoubleSpinBox* rxSpin_ = nullptr;
    QDoubleSpinBox* rySpin_ = nullptr;
    QDoubleSpinBox* rwSpin_ = nullptr;
    QDoubleSpinBox* rhSpin_ = nullptr;
    QPlainTextEdit* topcoatPreview_ = nullptr;

    // 卡面底圖面板
    QLineEdit* bgPathEdit_ = nullptr;
    QLabel* bgInfoLabel_ = nullptr;
    QComboBox* bgFitCombo_ = nullptr;
    QDoubleSpinBox* bgFocusXSpin_ = nullptr;
    QDoubleSpinBox* bgFocusYSpin_ = nullptr;
    QCheckBox* bgShowCheck_ = nullptr;
    QCheckBox* bgPrintCheck_ = nullptr;
    QPushButton* bgClearBtn_ = nullptr;

    // 預覽資料
    QCheckBox* usePreviewCheck_ = nullptr;
    std::vector<std::pair<QString, QLineEdit*>> previewEdits_;
    QFormLayout* previewForm_ = nullptr;

    // 製卡資料
    card::DataTable data_;
    QTableWidget* dataTable_ = nullptr;
    QLabel* dataInfo_ = nullptr;
    QLineEdit* dataFilter_ = nullptr;
    QString lastDataDir_;

    // 圖片自動對應
    card::ImageMatcherPool imagePool_;     // 各動態圖片元素用到的圖片資料夾 (相同資料夾只掃描一次)
    QString lastImageRefreshKey_;          // 上次計算「圖片」欄時的條件 (沒變就不重算)
};