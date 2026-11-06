#include "DesignerWindow.h"
#include "CardPainter.h"
#include "CardView.h"
#include "SnapGuides.h"
#include "ElementItem.h"
#include "PrintDialog.h"
#include "RegionItem.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QImageReader>
#include <QHeaderView>
#include <QLabel>
#include <QSizePolicy>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QCloseEvent>
#include <QPointer>
#include <QToolButton>
#include "UiScale.h"
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

// ================================================== 載入 Excel：確認標題列 ===
// 每個客戶的 Excel 欄位都可能不同：標題列下所有欄位都會匯入，
// 之後文字、圖片可以任選其中的欄位當參考。這裡只需確認「哪一列是欄位標題」。
namespace {

    class HeaderRowDialog : public QDialog {
    public:
        HeaderRowDialog(const QString& path, const QList<QStringList>& raw, QWidget* parent)
            : QDialog(parent), raw_(raw) {
            setWindowTitle(tr("載入製卡資料"));

            auto* v = new QVBoxLayout(this);
            auto* info = new QLabel(tr("檔案：%1\n會讀入此 Excel 的所有欄位，之後文字與圖片都可以任選其中的欄位當參考。\n請確認哪一列是欄位標題：")
                .arg(QFileInfo(path).fileName()));
            info->setWordWrap(true);
            v->addWidget(info);

            auto* rowForm = new QFormLayout;
            headerSpin_ = new QSpinBox;
            headerSpin_->setRange(1, qMax(1, qMin(30, int(raw_.size()))));
            headerSpin_->setValue(qMax(0, card::detectHeaderRow(raw_)) + 1);
            rowForm->addRow(tr("標題列 (第幾列)："), headerSpin_);
            v->addLayout(rowForm);

            summary_ = new QLabel;
            summary_->setWordWrap(true);
            v->addWidget(summary_);

            v->addWidget(new QLabel(tr("資料預覽 (標題列之後的前 8 列)：")));
            preview_ = new QTableWidget;
            preview_->setEditTriggers(QAbstractItemView::NoEditTriggers);
            preview_->setSelectionMode(QAbstractItemView::NoSelection);
            preview_->setMinimumHeight(180);
            v->addWidget(preview_, 1);

            auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            okBtn_ = bb->button(QDialogButtonBox::Ok);
            v->addWidget(bb);
            connect(bb, &QDialogButtonBox::accepted, this, [this] { accept(); });
            connect(bb, &QDialogButtonBox::rejected, this, [this] { reject(); });
            connect(headerSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { refresh(); });

            refresh();
            resize(720, 520);
        }

        int headerRow() const { return headerSpin_->value() - 1; }      // 0 起算

    private:
        void refresh() {
            const int hr = headerRow();
            QList<int> cols;
            const QStringList fields = card::importFields(raw_, hr, &cols);
            okBtn_->setEnabled(!fields.isEmpty());
            summary_->setText(fields.isEmpty()
                ? tr("這一列沒有任何欄位標題，請換一列。")
                : tr("將讀入 %1 個欄位：%2").arg(fields.size()).arg(fields.join(QStringLiteral("、"))));

            const int nRows = qMax(0, qMin(8, int(raw_.size()) - hr - 1));
            preview_->clear();
            preview_->setColumnCount(int(fields.size()));
            preview_->setRowCount(nRows);
            preview_->setHorizontalHeaderLabels(fields);
            for (int r = 0; r < nRows; ++r) {
                const QStringList& row = raw_[hr + 1 + r];
                for (int i = 0; i < fields.size(); ++i) {
                    const int c = cols[i];
                    preview_->setItem(r, i, new QTableWidgetItem(c < row.size() ? row[c].trimmed() : QString()));
                }
            }
            preview_->resizeColumnsToContents();
        }

        QList<QStringList> raw_;
        QSpinBox* headerSpin_ = nullptr;
        QLabel* summary_ = nullptr;
        QTableWidget* preview_ = nullptr;
        QPushButton* okBtn_ = nullptr;
    };

} // namespace

namespace {

    QDoubleSpinBox* makeMmSpin(double minV, double maxV) {
        auto* s = new QDoubleSpinBox;
        s->setRange(minV, maxV);
        s->setDecimals(1);
        s->setSingleStep(0.5);
        s->setSuffix(QStringLiteral(" mm"));
        return s;
    }

    // 工具列樣式：寫死的像素尺寸依「介面大小」縮放
    QString toolbarStyle() {
        using uiscale::px;
        return QStringLiteral("QToolBar { spacing: %1px; padding: %2px; } QToolButton { padding: %3px %4px; }")
            .arg(px(4)).arg(px(3)).arg(px(4)).arg(px(10));
    }
    QString printButtonStyle() {
        using uiscale::px;
        return QStringLiteral("QToolButton { background:#1f7a3d; color:white; font-weight:bold; font-size:%1px;"
            " padding:%2px %3px; border-radius:%4px; } QToolButton:hover { background:#26994c; }")
            .arg(px(13)).arg(px(6)).arg(px(22)).arg(px(4));
    }
    constexpr int kWindowStateVersion = 2;       // 2：新增「卡面底圖」面板 (舊的面板配置不相容，會回到預設配置)

    // 卡面底圖：卡片的最底層，只負責顯示 (滑鼠點擊會穿透到下面，不會干擾元素的選取與拖曳)。
    // 是否印出由 CardBackground::print 決定，與這裡的顯示無關 (見 renderCard)。
    class BackgroundItem : public QGraphicsItem {
    public:
        BackgroundItem(const card::CardLayout* layout, const card::Record* rec, QGraphicsItem* parent)
            : QGraphicsItem(parent), layout_(layout), rec_(rec) {
            setAcceptedMouseButtons(Qt::NoButton);
            setZValue(-1000);
        }
        QRectF boundingRect() const override {
            return QRectF(0, 0, layout_->widthMm() * ElementItem::kPxPerMm, layout_->heightMm() * ElementItem::kPxPerMm);
        }
        void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
            const card::CardBackground& bg = layout_->background;
            if (bg.isEmpty() || !bg.showInDesigner) return;
            card::paintBackground(*p, bg, boundingRect(), ElementItem::kPxPerMm, *rec_);
        }
    private:
        const card::CardLayout* layout_;
        const card::Record* rec_;
    };

    // 元素清單：可用滑鼠拖曳項目調整圖層順序 (拖放完成後通知視窗依清單順序更新)
    class ElementListWidget : public QListWidget {
    public:
        std::function<void()> onReordered;
    protected:
        void dropEvent(QDropEvent* e) override {
            QListWidget::dropEvent(e);
            if (onReordered) onReordered();
        }
    };

    QString typeText(const card::Element& e) {
        if (e.type == card::ElementType::Text) return QStringLiteral("文字");
        if (e.type == card::ElementType::Image) return QStringLiteral("圖片");
        if (e.type == card::ElementType::QRCode) return QStringLiteral("QR Code");
        return QStringLiteral("條碼");
    }

    QString listLabel(const card::Element& e) {
        return QStringLiteral("%1  [%2%3]")
            .arg(e.name, typeText(e), e.isDynamic() ? QStringLiteral("・動態") : QString());
    }

    // 建立一個動作：text / 快捷鍵 / 提示 / 觸發時要做的事
    QAction* newAction(QObject* parent, const QString& text, const QKeySequence& key,
        const QString& tip, std::function<void()> fn) {
        auto* a = new QAction(text, parent);
        if (!key.isEmpty()) a->setShortcut(key);
        QString t = tip;
        // 提示文字沒有自己寫快捷鍵 (含括號) 時自動補上，滑過按鈕就知道快捷鍵
        if (!t.isEmpty() && !key.isEmpty() && !t.contains(QLatin1Char('(')))
            t += QStringLiteral(" (%1)").arg(key.toString(QKeySequence::NativeText));
        if (!t.isEmpty()) a->setToolTip(t);
        if (fn) QObject::connect(a, &QAction::triggered, parent, [fn] { fn(); });
        return a;
    }

} // namespace

// ============================================================ construction ===

DesignerWindow::DesignerWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Entrust Card Tool"));
    uiscale::fitWindowToScreen(this, 1360, 840);     // 依螢幕可用範圍決定視窗大小 (不會超出螢幕)

    // 預設版面：照片、姓名、學號、系所
    layout_.orientation = card::Orientation::Landscape;
    {
        card::Element name;
        name.id = layout_.nextId();
        name.name = tr("姓名");
        name.text = QStringLiteral("{姓名}");
        name.xMm = 32; name.yMm = 10; name.wMm = 48; name.hMm = 10;
        name.fontPt = 16; name.bold = true;
        layout_.elements.append(name);

        card::Element sid;
        sid.id = layout_.nextId();
        sid.name = tr("學號");
        sid.text = QStringLiteral("{學號}");
        sid.xMm = 32; sid.yMm = 22; sid.wMm = 48; sid.hMm = 8;
        sid.fontPt = 11;
        layout_.elements.append(sid);

        card::Element dept;
        dept.id = layout_.nextId();
        dept.name = tr("系所");
        dept.text = QStringLiteral("{系所}");
        dept.xMm = 32; dept.yMm = 32; dept.wMm = 48; dept.hMm = 8;
        dept.fontPt = 10;
        layout_.elements.append(dept);

        card::Element photo;
        photo.id = layout_.nextId();
        photo.name = tr("照片");
        photo.type = card::ElementType::Image;
        photo.xMm = 5; photo.yMm = 10; photo.wMm = 22; photo.hMm = 30;
        layout_.elements.append(photo);
    }

    buildUi();
    migrateLegacyImageSettings(layout_);   // 舊版的全域圖片資料夾 / 比對欄位 -> 預設照片元素自己的設定
    applyPreviewRecord();
    rebuildScene();
    baseline_ = layout_;                   // 復原的起點 = 剛建好的預設版面
    savedJson_ = layout_.toJson();         // 「有沒有未儲存的修改」的比較基準
    updateWindowTitle();
    updateUndoActions();

    // 介面大小改變時 (選單 / 快捷鍵) 更新介面
    uiscale::addListener([w = QPointer<DesignerWindow>(this)] { if (w) w->applyUiScale(); });

    // 還原上次的視窗位置 / 大小 / 面板配置；換過螢幕或解析度時，保證視窗仍在螢幕內
    {
        QSettings st(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner"));
        const QByteArray g = st.value(QStringLiteral("window/geometry")).toByteArray();
        const QByteArray s = st.value(QStringLiteral("window/state")).toByteArray();
        if (!g.isEmpty()) restoreGeometry(g);
        if (!s.isEmpty()) restoredState_ = restoreState(s, kWindowStateVersion);
        uiscale::clampToScreen(this);
    }
}

DesignerWindow::~DesignerWindow() {
    // 視窗結束時，QGraphicsScene (QObject 子物件) 會在成員 (layout_ 等) 已經解構「之後」才被刪除，
    // 刪除元素時它會發出 selectionChanged；先切斷連線，避免處理函式去存取已解構的成員。
    if (scene_) scene_->disconnect(this);
}

void DesignerWindow::closeEvent(QCloseEvent* ev) {
    if (!maybeSave()) {                    // 有未儲存的修改，而且使用者按了「取消」
        ev->ignore();
        return;
    }
    QSettings st(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner"));
    st.setValue(QStringLiteral("window/geometry"), saveGeometry());
    st.setValue(QStringLiteral("window/state"), saveState(kWindowStateVersion));
    QMainWindow::closeEvent(ev);
}

void DesignerWindow::showEvent(QShowEvent* ev) {
    QMainWindow::showEvent(ev);
    if (!shownOnce_) {
        shownOnce_ = true;
        // 等視窗版面算好之後再調整面板大小與縮放
        QTimer::singleShot(0, this, [this] {
            if (!restoredState_) applyDockSizes();           // 有還原上次的面板配置就不要覆蓋
            fitView();
            });
    }
}

void DesignerWindow::buildUi() {
    // ---- 中央：卡面檢視 ----
    scene_ = new QGraphicsScene(this);
    view_ = new CardView(scene_, this);
    view_->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    view_->setBackgroundBrush(QColor(225, 225, 225));
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setFrameShape(QFrame::NoFrame);            // 沒有邊框，尺規的刻度才會和卡面對齊
    view_->viewport()->setMouseTracking(true);        // 沒按滑鼠也要更新游標位置

    // 卡面檢視 + 上方 / 左方 mm 尺規
    rulerH_ = new RulerWidget(Qt::Horizontal, view_, ElementItem::kPxPerMm);
    rulerV_ = new RulerWidget(Qt::Vertical, view_, ElementItem::kPxPerMm);
    rulerH_->cardRect = rulerV_->cardRect = [this] { return cardItem_ ? cardItem_->sceneBoundingRect() : QRectF(); };
    rulerV_->flipHeightMm = [this] { return layout_.heightMm(); };   // 原點在卡面左下角
    auto* corner = new QLabel(tr("mm"));
    cornerLabel_ = corner;                  // 大小與字型在 applyUiScale() 設定
    corner->setAlignment(Qt::AlignCenter);
    auto* centerBox = new QWidget;
    auto* grid = new QGridLayout(centerBox);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(corner, 0, 0);
    grid->addWidget(rulerH_, 0, 1);
    grid->addWidget(rulerV_, 1, 0);
    grid->addWidget(view_, 1, 1);
    setCentralWidget(centerBox);

    view_->onViewChanged = [this] { rulerH_->update(); rulerV_->update(); };
    view_->onCursorMoved = [this](const QPointF& sp, bool inside) {
        rulerH_->setCursorScenePos(sp, inside);
        rulerV_->setCursorScenePos(sp, inside);
        updateCursorLabel(sp, inside);
        };

    // 復原用的計時器：連續編輯停手 0.5 秒 (且滑鼠已放開) 才記成一個步驟
    commitTimer_ = new QTimer(this);
    commitTimer_->setSingleShot(true);
    commitTimer_->setInterval(500);
    connect(commitTimer_, &QTimer::timeout, this, [this] {
        if (QApplication::mouseButtons() != Qt::NoButton) { commitTimer_->start(); return; }
        commitUndo();
        });
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] { onSelectionChanged(); });

    // ---- 面板 (都放進 QDockWidget，可由「檢視」選單重新開啟) ----
    elementDock_ = new QDockWidget(tr("元素清單 (上方 = 最上層)"), this);
    elementDock_->setObjectName(QStringLiteral("dockElements"));
    {
        auto* list = new ElementListWidget;
        elementList_ = list;
        list->setDragDropMode(QAbstractItemView::InternalMove);       // 拖曳項目 = 調整圖層順序
        list->setDefaultDropAction(Qt::MoveAction);
        list->setDragEnabled(true);
        list->setToolTip(tr("拖曳項目，或按右邊的 ▲ ▼，可調整元素的覆蓋順序 (清單最上面 = 蓋在最上層)"));
        list->onReordered = [this] { QTimer::singleShot(0, this, [this] { reorderFromList(); }); };

        layerUpBtn_ = new QToolButton;
        layerUpBtn_->setText(QStringLiteral("▲"));
        layerUpBtn_->setToolTip(tr("上移一層：往清單上方移動，蓋住更多元素 (Ctrl+])"));
        layerDownBtn_ = new QToolButton;
        layerDownBtn_->setText(QStringLiteral("▼"));
        layerDownBtn_->setToolTip(tr("下移一層：往清單下方移動，被其他元素蓋住 (Ctrl+[)"));
        layerUpBtn_->setEnabled(false);
        layerDownBtn_->setEnabled(false);
        connect(layerUpBtn_, &QToolButton::clicked, this, [this] { moveLayer(1); });
        connect(layerDownBtn_, &QToolButton::clicked, this, [this] { moveLayer(2); });

        auto* box = new QWidget;
        auto* hl = new QHBoxLayout(box);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(uiscale::px(4));
        hl->addWidget(list, 1);
        auto* col = new QVBoxLayout;
        col->addStretch(1);
        col->addWidget(layerUpBtn_);
        col->addWidget(layerDownBtn_);
        col->addStretch(1);
        hl->addLayout(col);
        elementDock_->setWidget(box);
    }
    connect(elementList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (updating_ || row < 0) return;
        const QString id = elementList_->item(row)->data(Qt::UserRole).toString();
        if (ElementItem* it = findItem(id)) {
            scene_->clearSelection();
            it->setSelected(true);
        }
        });

    dataDock_ = new QDockWidget(tr("製卡資料"), this);
    dataDock_->setObjectName(QStringLiteral("dockData"));
    dataDock_->setWidget(buildDataPanel());

    propertyDock_ = new QDockWidget(tr("屬性"), this);
    propertyDock_->setObjectName(QStringLiteral("dockProperties"));
    {
        auto* sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setWidget(buildPropertyPanel());
        propertyDock_->setWidget(sa);
    }

    topcoatDock_ = new QDockWidget(tr("鍍膜範圍"), this);
    topcoatDock_->setObjectName(QStringLiteral("dockTopcoat"));
    {
        auto* sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setWidget(buildTopcoatPanel());
        topcoatDock_->setWidget(sa);
    }

    backgroundDock_ = new QDockWidget(tr("卡面底圖"), this);
    backgroundDock_->setObjectName(QStringLiteral("dockBackground"));
    {
        auto* sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setWidget(buildBackgroundPanel());
        backgroundDock_->setWidget(sa);
    }

    previewDock_ = new QDockWidget(tr("預覽資料"), this);
    previewDock_->setObjectName(QStringLiteral("dockPreview"));
    {
        auto* sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setWidget(buildPreviewPanel());
        previewDock_->setWidget(sa);
    }

    resetDockLayout();
    buildMenus();
    updateActionStates();

    cursorLabel_ = new QLabel;
    cursorLabel_->setMinimumWidth(uiscale::px(260));
    cursorLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(cursorLabel_);
    updateCursorLabel(QPointF(), false);

    // 左側：隨選取狀態改變的操作提示 (其他訊息出現時會暫時蓋住它，幾秒後自動回來)
    hintLabel_ = new QLabel;
    hintLabel_->setStyleSheet(QStringLiteral("color:#555; padding:0 6px;"));
    hintLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // 文字太長時截斷，不會把視窗撐大
    statusBar()->addWidget(hintLabel_, 1);
    updateHints();

    applyUiScale();                        // 依目前「介面大小」設定尺寸 (尺規、最小寬度、工具列…)
}

// 面板配置：左=製卡資料；右(由上到下)=元素清單、屬性/鍍膜範圍(分頁)、預覽資料
void DesignerWindow::resetDockLayout() {
    for (QDockWidget* d : { elementDock_, dataDock_, propertyDock_, topcoatDock_, backgroundDock_, previewDock_ }) {
        d->setFloating(false);
        removeDockWidget(d);
    }
    addDockWidget(Qt::LeftDockWidgetArea, dataDock_);

    addDockWidget(Qt::RightDockWidgetArea, elementDock_);
    splitDockWidget(elementDock_, propertyDock_, Qt::Vertical);
    splitDockWidget(propertyDock_, previewDock_, Qt::Vertical);
    tabifyDockWidget(propertyDock_, topcoatDock_);           // 「屬性」、「鍍膜範圍」、「卡面底圖」疊成分頁
    tabifyDockWidget(topcoatDock_, backgroundDock_);

    for (QDockWidget* d : { elementDock_, dataDock_, propertyDock_, topcoatDock_, backgroundDock_, previewDock_ }) d->show();
    propertyDock_->raise();
    applyDockSizes();
}

void DesignerWindow::applyDockSizes() {
    using uiscale::px;
    resizeDocks({ elementDock_, propertyDock_, previewDock_ }, { px(140), px(390), px(200) }, Qt::Vertical);
    resizeDocks({ dataDock_, propertyDock_ }, { px(430), px(360) }, Qt::Horizontal);
}

// ---- 選單列 + 精簡工具列 ----
void DesignerWindow::buildMenus() {
    // 動作
    auto* actOpenLayout = newAction(this, tr("開啟版面"), QKeySequence::Open, tr("開啟已儲存的版面檔 (Ctrl+O)"), [this] { openLayout(); });
    auto* actSaveLayout = newAction(this, tr("儲存版面"), QKeySequence::Save, tr("儲存目前的版面；第一次儲存會詢問檔名 (Ctrl+S)"), [this] { saveLayout(); });
    actSaveAs_ = newAction(this, tr("另存版面…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), tr("把目前的版面另存成新檔 (Ctrl+Shift+S)"), [this] { saveLayoutAs(); });
    auto* actExport = newAction(this, tr("輸出預覽圖 (300dpi)…"), QKeySequence(Qt::CTRL | Qt::Key_E), tr("把目前預覽的卡面輸出成 PNG"), [this] { exportPreview(); });
    auto* actQuit = newAction(this, tr("結束"), QKeySequence::Quit, QString(), [this] { close(); });

    actUndo_ = newAction(this, tr("復原"), QKeySequence::Undo, tr("復原上一個動作 (Ctrl+Z)"), [this] { undo(); });
    actRedo_ = newAction(this, tr("重做"), QKeySequence(), tr("重做 (Ctrl+Y / Ctrl+Shift+Z)"), [this] { redo(); });
    actRedo_->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z) });

    auto* actAddText = newAction(this, tr("新增文字"), QKeySequence(Qt::CTRL | Qt::Key_T), tr("在卡面新增一個文字元素"), [this] { addElement(card::ElementType::Text); });
    auto* actAddImage = newAction(this, tr("新增圖片"), QKeySequence(Qt::CTRL | Qt::Key_I), tr("在卡面新增一個圖片元素"), [this] { addElement(card::ElementType::Image); });
    auto* actAddQr = newAction(this, tr("新增 QR Code"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), tr("在卡面新增一個 QR Code 元素"), [this] { addElement(card::ElementType::QRCode); });
    auto* actAddBarcode = newAction(this, tr("新增條碼"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), tr("在卡面新增一個 Code 39 條碼元素"), [this] { addElement(card::ElementType::Barcode); });

    // 刪除：Delete 與 Backspace 都可以；只在焦點位於「卡面」或「元素清單」時生效，
    // 避免在屬性面板輸入文字、按倒退鍵時誤刪元素。
    actDelete_ = newAction(this, tr("刪除"), QKeySequence(), tr("刪除選取的元素 (Delete / Backspace)"), [this] { deleteSelected(); });
    actDelete_->setShortcuts({ QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace) });
    actDelete_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    view_->addAction(actDelete_);
    elementList_->addAction(actDelete_);
    regionList_->addAction(actDelete_);      // 鍍膜範圍清單內按 Delete / Backspace 也能刪除範圍

    auto* orientGroup = new QActionGroup(this);
    actLandscape_ = orientGroup->addAction(tr("橫式 (85.6 × 54 mm)"));
    actPortrait_ = orientGroup->addAction(tr("直式 (54 × 85.6 mm)"));
    actLandscape_->setCheckable(true);
    actPortrait_->setCheckable(true);
    actLandscape_->setChecked(true);
    connect(actLandscape_, &QAction::triggered, this, [this] { setOrientation(card::Orientation::Landscape); });
    connect(actPortrait_, &QAction::triggered, this, [this] { setOrientation(card::Orientation::Portrait); });

    auto* actLoadData = newAction(this, tr("載入 Excel 資料"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), tr("載入 .xlsx：讀入所有欄位，文字與圖片可任選其中的欄位"), [this] { browseData(); });
    actPrevRecord_ = newAction(this, tr("◀ 上一筆"), QKeySequence(Qt::ALT | Qt::Key_Left), tr("預覽上一筆資料 (Alt+←)"), [this] { stepRecord(-1); });
    actNextRecord_ = newAction(this, tr("下一筆 ▶"), QKeySequence(Qt::ALT | Qt::Key_Right), tr("預覽下一筆資料 (Alt+→)"), [this] { stepRecord(+1); });

    auto* actPrint = newAction(this, tr("列印…"), QKeySequence::Print, tr("列印目前這一筆或整批資料 (Ctrl+P)"), [this] { openPrintDialog(); });

    auto* actFit = newAction(this, tr("符合視窗"), QKeySequence(Qt::CTRL | Qt::Key_0), tr("縮放到整張卡剛好放進視窗"), [this] { fitView(); });
    auto* actResetDocks = newAction(this, tr("還原預設面板配置"), QKeySequence(), tr("把所有面板重新開啟並放回預設位置"), [this] { resetDockLayout(); });

    // 選單
    QMenu* mFile = menuBar()->addMenu(tr("檔案(&F)"));
    mFile->addAction(actOpenLayout);
    mFile->addAction(actSaveLayout);
    mFile->addAction(actSaveAs_);
    mFile->addSeparator();
    mFile->addAction(actLoadData);
    mFile->addSeparator();
    mFile->addAction(actPrint);
    mFile->addAction(actExport);
    mFile->addSeparator();
    mFile->addAction(actQuit);

    QMenu* mEdit = menuBar()->addMenu(tr("編輯(&E)"));
    mEdit->addAction(actUndo_);
    mEdit->addAction(actRedo_);
    mEdit->addSeparator();
    mEdit->addAction(actAddText);
    mEdit->addAction(actAddImage);
    mEdit->addAction(actAddBarcode);
    mEdit->addAction(actAddQr);
    mEdit->addSeparator();
    mEdit->addAction(actDelete_);
    mEdit->addSeparator();
    {
        const QString names[4] = { tr("移到最上層"), tr("上移一層"), tr("下移一層"), tr("移到最下層") };
        const QKeySequence keys[4] = { QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight), QKeySequence(Qt::CTRL | Qt::Key_BracketRight),
            QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft) };
        for (int k = 0; k < 4; ++k) {
            actLayer_[k] = newAction(this, names[k], keys[k], tr("調整選取元素的覆蓋順序：%1").arg(names[k]), [this, k] { moveLayer(k); });
            mEdit->addAction(actLayer_[k]);
            elementList_->addAction(actLayer_[k]);
            view_->addAction(actLayer_[k]);
            actLayer_[k]->setEnabled(false);
        }
        elementList_->setContextMenuPolicy(Qt::ActionsContextMenu);   // 元素清單按右鍵也能調整
    }

    actImportBg_ = newAction(this, tr("匯入卡面底圖…"), QKeySequence(), tr("匯入卡面的圖，墊在最底層方便對位文字與圖片；可選擇列印時印出或不印出"), [this] { importBackground(); });
    actClearBg_ = newAction(this, tr("移除卡面底圖"), QKeySequence(), tr("移除目前的卡面底圖"), [this] { clearBackground(); });

    QMenu* mCard = menuBar()->addMenu(tr("卡面(&C)"));
    mCard->addAction(actLandscape_);
    mCard->addAction(actPortrait_);
    mCard->addSeparator();
    mCard->addAction(actImportBg_);
    mCard->addAction(actClearBg_);

    QMenu* mData = menuBar()->addMenu(tr("資料(&D)"));
    mData->addAction(actLoadData);
    mData->addSeparator();
    mData->addAction(actPrevRecord_);
    mData->addAction(actNextRecord_);

    QMenu* mPrint = menuBar()->addMenu(tr("列印(&P)"));
    mPrint->addAction(actPrint);

    QMenu* mView = menuBar()->addMenu(tr("檢視(&V)"));
    mView->addAction(actFit);
    actShowRegions_ = newAction(this, tr("顯示鍍膜範圍"), QKeySequence(), tr("在卡面上顯示/隱藏鍍膜範圍 (只影響編輯畫面，不影響列印)"), nullptr);
    actShowRegions_->setCheckable(true);
    actShowRegions_->setChecked(true);
    connect(actShowRegions_, &QAction::toggled, this, [this](bool on) {
        if (!cardItem_) return;
        for (QGraphicsItem* gi : cardItem_->childItems())
            if (gi->type() == RegionItem::Type) gi->setVisible(on);
        });
    mView->addAction(actShowRegions_);
    actRegionOutline_ = newAction(this, tr("鍍膜範圍只畫外框"), QKeySequence(), tr("鍍膜範圍只畫虛線外框、不填色，不會遮住卡面設計 (只影響編輯畫面，不影響列印)"), nullptr);
    actRegionOutline_->setCheckable(true);
    connect(actRegionOutline_, &QAction::toggled, this, [this](bool on) { RegionItem::outlineOnly() = on; refreshRegionItemsView(); });
    mView->addAction(actRegionOutline_);
    actRegionEdit_ = newAction(this, tr("編輯鍍膜範圍"), QKeySequence(), tr("開啟時只能操作鍍膜範圍 (元素暫時不接受滑鼠)；關閉時鍍膜範圍不接受滑鼠，不會擋住卡面設計 (只影響編輯畫面)"), nullptr);
    actRegionEdit_->setCheckable(true);
    connect(actRegionEdit_, &QAction::toggled, this, [this](bool on) {
        RegionItem::locked() = !on;
        if (regionEditCheck_ && regionEditCheck_->isChecked() != on) {
            const QSignalBlocker b(regionEditCheck_);
            regionEditCheck_->setChecked(on);
        }
        refreshRegionItemsView();
        updateHints();
        });
    mView->addAction(actRegionEdit_);
    actSnap_ = newAction(this, tr("對齊輔助線"), QKeySequence(), tr("拖曳 / 縮放時自動貼齊其他元素與卡面的邊、中心線 (拖曳時按住 Alt 可暫時停用)"), nullptr);
    actSnap_->setCheckable(true);
    actSnap_->setChecked(true);
    connect(actSnap_, &QAction::toggled, this, [](bool on) { card::snapEnabledFlag() = on; });
    mView->addAction(actSnap_);
    mView->addSeparator();
    QMenu* mPanels = mView->addMenu(tr("面板"));
    for (QDockWidget* d : { elementDock_, dataDock_, propertyDock_, topcoatDock_, backgroundDock_, previewDock_ })
        mPanels->addAction(d->toggleViewAction());       // 面板被關掉後，從這裡重新開啟
    mView->addAction(actResetDocks);

    // ---- 介面大小 / 視窗：不同電腦的螢幕解析度不同，可依需要調整 ----
    mView->addSeparator();
    QMenu* mScale = mView->addMenu(tr("介面大小"));
    auto* scaleGroup = new QActionGroup(this);
    auto addScale = [&](const QString& text, int percent) {
        QAction* a = mScale->addAction(text);
        a->setCheckable(true);
        a->setData(percent);
        scaleGroup->addAction(a);
        scaleActs_.push_back(a);
        connect(a, &QAction::triggered, this, [percent] { uiscale::setStored(percent); });
        };
    addScale(tr("自動 (依螢幕大小)"), 0);
    for (int p : uiscale::kPresets) addScale(QStringLiteral("%1%").arg(p), p);
    mScale->addSeparator();
    auto* actBigger = newAction(this, tr("放大介面"), QKeySequence(), tr("介面放大一級 (Ctrl+Shift++)"), [] { uiscale::step(+1); });
    actBigger->setShortcuts({ QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal) });
    auto* actSmaller = newAction(this, tr("縮小介面"), QKeySequence(), tr("介面縮小一級 (Ctrl+Shift+-)"), [] { uiscale::step(-1); });
    actSmaller->setShortcuts({ QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Underscore) });
    auto* actScaleReset = newAction(this, tr("介面還原 100%"), QKeySequence(), tr("介面大小還原為 100% (Ctrl+Shift+0)"), [] { uiscale::setStored(100); });
    actScaleReset->setShortcuts({ QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_0), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_ParenRight) });
    mScale->addAction(actBigger);
    mScale->addAction(actSmaller);
    mScale->addAction(actScaleReset);
    auto* actFitWindow = newAction(this, tr("視窗適應螢幕"), QKeySequence(), tr("把視窗調成適合目前螢幕的大小並置中，同時還原面板配置 (視窗跑出螢幕或面板被擠壞時使用)"), [this] { fitWindowToScreen(); });
    mView->addAction(actFitWindow);
    auto* actFull = newAction(this, tr("全螢幕"), QKeySequence(Qt::Key_F11), tr("切換全螢幕 (F11)"), [this] { if (isFullScreen()) showNormal(); else showFullScreen(); });
    mView->addAction(actFull);

    auto* actHelp = newAction(this, tr("操作說明"), QKeySequence(Qt::Key_F1), tr("基本流程、卡面操作與快捷鍵一覽"), [this] { showHelp(); });
    QMenu* mHelp = menuBar()->addMenu(tr("說明(&H)"));
    mHelp->addAction(actHelp);

    // 工具列：依「使用流程」分成兩排
    //   第 1 排 (檔案 / 資料 / 列印)：開啟 → 載入資料 → ……… → 列印 (靠右、醒目)
    //   第 2 排 (設計)：復原重做 | 新增 / 刪除元素 | 檢視 | 預覽資料換筆
    const QString tbStyle = toolbarStyle();
    auto caption = [this](const QString& text) {
        auto* l = new QLabel(text);
        l->setStyleSheet(QStringLiteral("color:#666; padding:0 4px 0 8px;"));
        return l;
        };

    QToolBar* tb1 = new QToolBar(tr("檔案與列印"), this);
    tb1->setObjectName(QStringLiteral("toolbarFile"));
    tb1->setMovable(false);
    tb1->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb1->setStyleSheet(tbStyle);
    tb1->addWidget(caption(tr("版面")));
    tb1->addAction(actOpenLayout);
    tb1->addAction(actSaveLayout);
    tb1->addSeparator();
    tb1->addWidget(caption(tr("製卡資料")));
    tb1->addAction(actLoadData);
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb1->addWidget(spacer);                                  // 把「列印」推到最右邊
    tb1->addAction(actPrint);
    if (QWidget* pb = tb1->widgetForAction(actPrint)) {
        pb->setObjectName(QStringLiteral("btnPrint"));
        pb->setStyleSheet(printButtonStyle());
    }
    addToolBar(Qt::TopToolBarArea, tb1);

    addToolBarBreak(Qt::TopToolBarArea);                     // 第 2 排

    QToolBar* tb2 = new QToolBar(tr("設計"), this);
    tb2->setObjectName(QStringLiteral("toolbarDesign"));
    tb2->setMovable(false);
    tb2->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb2->setStyleSheet(tbStyle);
    tb2->addAction(actUndo_);
    tb2->addAction(actRedo_);
    tb2->addSeparator();
    tb2->addWidget(caption(tr("元素")));
    tb2->addAction(actAddText);
    tb2->addAction(actAddImage);
    tb2->addAction(actAddQr);
    tb2->addAction(actAddBarcode);   // 新增條碼按鈕：放在「元素」工具列，與文字、圖片並列
    tb2->addAction(actDelete_);
    tb2->addSeparator();
    // 圖層順序不放在工具列：請用「元素清單」旁的 ▲ ▼ 或直接拖曳清單項目 (編輯選單 / 快捷鍵仍可用)
    tb2->addWidget(caption(tr("檢視")));
    tb2->addAction(actFit);
    tb2->addSeparator();
    tb2->addWidget(caption(tr("預覽資料")));
    tb2->addAction(actPrevRecord_);
    recordLabel_ = new QLabel;
    recordLabel_->setMinimumWidth(uiscale::px(110));
    recordLabel_->setAlignment(Qt::AlignCenter);
    recordLabel_->setToolTip(tr("目前預覽的是第幾筆資料。載入 Excel 後，也可以直接在左側資料表點選。"));
    tb2->addWidget(recordLabel_);
    tb2->addAction(actNextRecord_);
    addToolBar(Qt::TopToolBarArea, tb2);

    updateRecordLabel();
    updateUndoActions();
}

void DesignerWindow::updateRecordLabel() {
    if (!recordLabel_) return;
    if (data_.rows.isEmpty()) {
        recordLabel_->setText(tr("尚未載入資料"));
        return;
    }
    const int row = dataTable_ ? dataTable_->currentRow() : -1;
    recordLabel_->setText(row >= 0 ? tr("第 %1 / %2 筆").arg(row + 1).arg(data_.rows.size())
        : tr("共 %1 筆").arg(data_.rows.size()));
}

void DesignerWindow::updateActionStates() {
    {
        ElementItem* sel = selectedItem();
        int idx = -1;
        if (sel) for (int i = 0; i < layout_.elements.size(); ++i) if (layout_.elements[i].id == sel->id()) { idx = i; break; }
        const int last = layout_.elements.size() - 1;
        if (actLayer_[0]) actLayer_[0]->setEnabled(idx >= 0 && idx < last);     // 已在最上層就不能再往上
        if (actLayer_[1]) actLayer_[1]->setEnabled(idx >= 0 && idx < last);
        if (actLayer_[2]) actLayer_[2]->setEnabled(idx > 0);
        if (actLayer_[3]) actLayer_[3]->setEnabled(idx > 0);
        if (layerUpBtn_) layerUpBtn_->setEnabled(idx >= 0 && idx < last);
        if (layerDownBtn_) layerDownBtn_->setEnabled(idx > 0);
    }
    if (actDelete_) actDelete_->setEnabled(selectedItem() != nullptr || selectedRegion() != nullptr || !currentRegionId().isEmpty());
    const bool hasData = !data_.rows.isEmpty();
    if (actPrevRecord_) actPrevRecord_->setEnabled(hasData);
    if (actNextRecord_) actNextRecord_->setEnabled(hasData);
}

QWidget* DesignerWindow::buildPropertyPanel() {
    propertyPanel_ = new QWidget;
    propertyPanel_->setMinimumWidth(uiscale::px(300));
    auto* root = new QVBoxLayout(propertyPanel_);

    // --- 基本 ---
    auto* basic = new QFormLayout;
    nameEdit_ = new QLineEdit;
    typeLabel_ = new QLabel;
    xSpin_ = makeMmSpin(-100, 300);
    ySpin_ = makeMmSpin(-100, 300);
    wSpin_ = makeMmSpin(1, 300);
    hSpin_ = makeMmSpin(1, 300);
    basic->addRow(tr("名稱"), nameEdit_);
    basic->addRow(tr("類型"), typeLabel_);
    basic->addRow(tr("X 位置"), xSpin_);
    basic->addRow(tr("Y 位置"), ySpin_);
    xSpin_->setToolTip(tr("元素左下角到卡面左邊的距離 (原點 = 卡面左下角)"));
    ySpin_->setToolTip(tr("元素左下角到卡面底邊的距離，往上增加 (原點 = 卡面左下角)"));
    hSpin_->setToolTip(tr("修改高度時，元素的底邊位置不變，往上長高 / 縮短"));
    basic->addRow(tr("寬度"), wSpin_);
    basic->addRow(tr("高度"), hSpin_);
    fitCheck_ = new QCheckBox;
    fitCheck_->setToolTip(tr("文字：框的寬、高自動等於文字的實際大小 (不換行)，X / Y 即為文字的位置。\n"
        "圖片：寬度不變，高度依圖片長寬比自動調整 (底邊不動，往上長高)。\n"
        "內容隨每一筆資料改變時，預覽與列印都會逐筆重新貼合；靠左 / 置中 / 靠右會分別固定左邊 / 中心 / 右邊。"));
    basic->addRow(fitCheck_);
    root->addLayout(basic);

    // --- 文字 ---
    textGroup_ = new QGroupBox(tr("文字"));
    auto* tf = new QFormLayout(textGroup_);
    textEdit_ = new QPlainTextEdit;
    textEdit_->setFixedHeight(uiscale::px(64));
    textEdit_->setPlaceholderText(tr("固定文字，或用 {欄位名稱} 當動態內容 (可按「插入欄位」選 Excel 的欄位)"));
    tf->addRow(textEdit_);

    fieldCombo_ = new QComboBox;
    fieldCombo_->addItems(card::fieldNames());
    auto* insertBtn = new QPushButton(tr("插入欄位"));
    auto* fieldRow = new QWidget;
    auto* fl = new QHBoxLayout(fieldRow);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->addWidget(fieldCombo_, 1);
    fl->addWidget(insertBtn);
    tf->addRow(tr("動態欄位"), fieldRow);

    fontCombo_ = new QFontComboBox;
    sizeSpin_ = new QDoubleSpinBox;
    sizeSpin_->setRange(1, 200);
    sizeSpin_->setDecimals(1);
    sizeSpin_->setSuffix(QStringLiteral(" pt"));
    boldCheck_ = new QCheckBox(tr("粗體"));
    italicCheck_ = new QCheckBox(tr("斜體"));
    auto* styleRow = new QWidget;
    auto* sl = new QHBoxLayout(styleRow);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->addWidget(boldCheck_);
    sl->addWidget(italicCheck_);
    sl->addStretch(1);
    alignCombo_ = new QComboBox;
    alignCombo_->addItems({ tr("靠左"), tr("置中"), tr("靠右") });
    colorBtn_ = new QPushButton;
    tf->addRow(tr("字型"), fontCombo_);
    tf->addRow(tr("大小"), sizeSpin_);
    tf->addRow(tr("樣式"), styleRow);
    tf->addRow(tr("對齊"), alignCombo_);
    tf->addRow(tr("顏色"), colorBtn_);
    root->addWidget(textGroup_);

    // --- 圖片：每個圖片元素各自一組設定 (固定 / 動態)，不同的圖片可以用不同的資料夾與比對欄位 ---
    imageGroup_ = new QGroupBox(tr("圖片"));
    auto* imf = new QFormLayout(imageGroup_);
    imageSourceCombo_ = new QComboBox;
    imageSourceCombo_->addItem(tr("固定圖片 (每張卡都用同一張)"));
    imageSourceCombo_->addItem(tr("動態圖片 (依每一筆資料帶入)"));
    imageSourceCombo_->setToolTip(tr("固定：例如公司 Logo，每張卡都一樣。\n動態：例如照片，依每一筆資料的某個欄位 (學號、姓名…) 到資料夾找對應的檔案。"));
    imf->addRow(tr("來源"), imageSourceCombo_);

    // 固定圖片
    imageFixedBox_ = new QWidget;
    auto* browse = new QPushButton(tr("選擇…"));
    {
        auto* fl = new QFormLayout(imageFixedBox_);
        fl->setContentsMargins(0, 0, 0, 0);
        imageEdit_ = new QLineEdit;
        imageEdit_->setPlaceholderText(tr("圖片檔路徑"));
        imageEdit_->setToolTip(tr("固定使用這張圖片 (例如公司 Logo)。\n路徑也可以含 {欄位}，例如 D:\\photos\\{學號}.jpg。"));
        auto* pathRow = new QWidget;
        auto* pl = new QHBoxLayout(pathRow);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->addWidget(imageEdit_, 1);
        pl->addWidget(browse);
        fl->addRow(tr("路徑"), pathRow);
    }
    imf->addRow(imageFixedBox_);

    // 動態圖片
    imageDynBox_ = new QWidget;
    auto* folderBtn = new QPushButton(tr("選擇…"));
    auto* rescanBtn = new QPushButton(tr("重新掃描"));
    {
        auto* dl = new QFormLayout(imageDynBox_);
        dl->setContentsMargins(0, 0, 0, 0);
        imageFolderEdit_ = new QLineEdit;
        imageFolderEdit_->setPlaceholderText(tr("圖片資料夾路徑"));
        auto* folderRow = new QWidget;
        auto* fr = new QHBoxLayout(folderRow);
        fr->setContentsMargins(0, 0, 0, 0);
        fr->addWidget(imageFolderEdit_, 1);
        fr->addWidget(folderBtn);
        dl->addRow(tr("資料夾"), folderRow);

        matchFieldCombo_ = new QComboBox;
        matchFieldCombo_->addItems(card::fieldNames());
        matchFieldCombo_->setToolTip(tr("用這個欄位的值，去找「檔名等於 / 包含該值」的圖片"));
        rescanBtn->setToolTip(tr("資料夾內的圖片有增減時按這裡"));
        auto* matchRow = new QWidget;
        auto* mr = new QHBoxLayout(matchRow);
        mr->setContentsMargins(0, 0, 0, 0);
        mr->addWidget(matchFieldCombo_, 1);
        mr->addWidget(rescanBtn);
        dl->addRow(tr("比對欄位"), matchRow);

        imageSummary_ = new QLabel;
        imageSummary_->setWordWrap(true);
        dl->addRow(imageSummary_);
    }
    imf->addRow(imageDynBox_);

    // 尺寸處理：框的大小自己決定，圖片依下面的方式放進框裡
    imageFitCombo_ = new QComboBox;
    imageFitCombo_->addItem(tr("裁切填滿 (蓋滿框，超出的裁掉)"));
    imageFitCombo_->addItem(tr("縮放適應 (整張放進框，可能留白)"));
    imageFitCombo_->addItem(tr("拉伸 (填滿框，比例會改變)"));
    imageFitCombo_->addItem(tr("框高依圖片比例 (寬度不變，自動算高度)"));
    imageFitCombo_->setToolTip(tr("框的寬、高可以自己設定，圖片依這裡的方式放進框裡。\n"
        "裁切填滿：維持比例放大到蓋滿框，超出的部分裁掉 (適合證件照)。\n"
        "縮放適應：維持比例縮到整張放進框裡。\n拉伸：直接拉成框的大小。\n"
        "框高依圖片比例：框的高度自動跟著圖片比例，這時高度不能手動改。"));
    imf->addRow(tr("顯示方式"), imageFitCombo_);
    auto makePctSpin = [](double lo, double hi, double step, double v) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(0);
        s->setSingleStep(step);
        s->setSuffix(QStringLiteral(" %"));
        s->setValue(v);
        return s;
        };
    imageZoomSpin_ = makePctSpin(10, 400, 5, 100);
    imageZoomSpin_->setToolTip(tr("在顯示方式的基礎上再放大 / 縮小。放大後超出框的部分會被裁掉。"));
    imageFocusXSpin_ = makePctSpin(0, 100, 5, 50);
    imageFocusXSpin_->setToolTip(tr("圖片在框內的水平位置：0 = 靠左 (裁切時保留左邊)，50 = 置中，100 = 靠右"));
    imageFocusYSpin_ = makePctSpin(0, 100, 5, 50);
    imageFocusYSpin_->setToolTip(tr("圖片在框內的垂直位置：0 = 靠上 (裁切時保留上方)，50 = 置中，100 = 靠下"));
    imf->addRow(tr("縮放"), imageZoomSpin_);
    imf->addRow(tr("水平位置"), imageFocusXSpin_);
    imf->addRow(tr("垂直位置"), imageFocusYSpin_);
    root->addWidget(imageGroup_);

    // --- 條碼：Code 39，固定內容或依 Excel 欄位動態帶入 ---
    barcodeGroup_ = new QGroupBox(tr("條碼"));
    auto* bcf = new QFormLayout(barcodeGroup_);
    barcodeSourceCombo_ = new QComboBox;
    barcodeSourceCombo_->addItem(tr("固定條碼 (每張卡都相同)"));
    barcodeSourceCombo_->addItem(tr("動態條碼 (依每筆資料欄位)"));
    bcf->addRow(tr("來源"), barcodeSourceCombo_);
    barcodeTypeBox_ = new QWidget;
    {
        auto* f = new QFormLayout(barcodeTypeBox_);
        f->setContentsMargins(0, 0, 0, 0);
        barcodeTypeCombo_ = new QComboBox;
        barcodeTypeCombo_->addItems(card::barcodeTypeNames());
        f->addRow(tr("條碼類型"), barcodeTypeCombo_);
    }
    bcf->addRow(barcodeTypeBox_);
    qrEccBox_ = new QWidget;                       // 只有 QR Code 元素才會顯示
    {
        auto* f = new QFormLayout(qrEccBox_);
        f->setContentsMargins(0, 0, 0, 0);
        qrEccCombo_ = new QComboBox;
        qrEccCombo_->addItems({ tr("L (約 7%)"), tr("M (約 15%)"), tr("Q (約 25%)"), tr("H (約 30%)") });
        qrEccCombo_->setToolTip(tr("容錯等級越高，卡面髒汙 / 刮損時越容易掃出來，但同樣內容的 QR Code 會變得更密。"));
        f->addRow(tr("容錯等級"), qrEccCombo_);
    }
    bcf->addRow(qrEccBox_);
    barcodeFixedBox_ = new QWidget;
    {
        auto* f = new QFormLayout(barcodeFixedBox_);
        f->setContentsMargins(0, 0, 0, 0);
        barcodeValueEdit_ = new QLineEdit;
        barcodeValueEdit_->setPlaceholderText(tr("輸入條碼內容，例如 1234567890"));
        f->addRow(tr("條碼內容"), barcodeValueEdit_);
    }
    bcf->addRow(barcodeFixedBox_);
    barcodeDynamicBox_ = new QWidget;
    {
        auto* f = new QFormLayout(barcodeDynamicBox_);
        f->setContentsMargins(0, 0, 0, 0);
        barcodeFieldCombo_ = new QComboBox;
        barcodeFieldCombo_->addItems(card::fieldNames());
        f->addRow(tr("資料欄位"), barcodeFieldCombo_);
    }
    bcf->addRow(barcodeDynamicBox_);
    barcodeHint_ = new QLabel(tr("Code 128：可含英數與符號 (純數字自動壓縮)；Code 39：數字、大寫英文、空白及 - . $ / + %；EAN-13 / UPC-A / EAN-8：純數字 (檢查碼可省略，自動補)；ITF：純數字 (奇數位數前面補 0)；Codabar：數字及 - $ : / . +。"));
    barcodeHint_->setWordWrap(true);
    bcf->addRow(barcodeHint_);
    root->addWidget(barcodeGroup_);

    root->addStretch(1);

    // ---- 連線：使用者編輯 -> 模型 ----
    connect(nameEdit_, &QLineEdit::textEdited, this, [this](const QString& v) {
        editSelected([&](card::Element& e) { e.name = v; });
        });

    auto hookSpin = [this](QDoubleSpinBox* s, std::function<void(card::Element&, double)> fn) {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, fn](double v) {
            editSelected([&](card::Element& e) { fn(e, v); });
            });
        };
    hookSpin(xSpin_, [](card::Element& e, double v) { e.xMm = v; });
    hookSpin(ySpin_, [this](card::Element& e, double v) { e.yMm = card::fromUiY(v, e.hMm, layout_.heightMm()); });
    hookSpin(wSpin_, [](card::Element& e, double v) { e.wMm = v; });
    hookSpin(hSpin_, [this](card::Element& e, double v) {      // 以底邊為基準：底邊不動，往上長高
        const double bottom = layout_.heightMm() - (e.yMm + e.hMm);
        e.hMm = v;
        e.yMm = layout_.heightMm() - bottom - v;
        });
    hookSpin(sizeSpin_, [](card::Element& e, double v) { e.fontPt = v; });

    connect(textEdit_, &QPlainTextEdit::textChanged, this, [this] {
        const QString v = textEdit_->toPlainText();
        editSelected([&](card::Element& e) { e.text = v; });
        });
    connect(insertBtn, &QPushButton::clicked, this, [this] {
        textEdit_->insertPlainText(QStringLiteral("{%1}").arg(fieldCombo_->currentText()));
        textEdit_->setFocus();
        });
    connect(fontCombo_, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        editSelected([&](card::Element& e) { e.fontFamily = f.family(); });
        });
    connect(boldCheck_, &QCheckBox::toggled, this, [this](bool on) {
        editSelected([&](card::Element& e) { e.bold = on; });
        });
    connect(italicCheck_, &QCheckBox::toggled, this, [this](bool on) {
        editSelected([&](card::Element& e) { e.italic = on; });
        });
    connect(alignCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        editSelected([&](card::Element& e) { e.align = static_cast<card::HAlign>(i); });
        });
    connect(colorBtn_, &QPushButton::clicked, this, [this] {
        ElementItem* it = selectedItem();
        const card::Element* cur = it ? layout_.find(it->id()) : nullptr;
        if (!cur) return;
        const QColor c = QColorDialog::getColor(cur->color, this, tr("選擇文字顏色"));
        if (!c.isValid()) return;
        editSelected([&](card::Element& e) { e.color = c; });
        updateColorButton(c);
        });
    connect(imageEdit_, &QLineEdit::textEdited, this, [this](const QString& v) {
        editSelected([&](card::Element& e) { e.imagePath = v; });
        });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("選擇圖片"), QString(),
            tr("圖片 (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff)"));
        if (f.isEmpty()) return;
        editSelected([&](card::Element& e) { e.imagePath = QDir::toNativeSeparators(f); });
        const bool prev = updating_;
        updating_ = true;
        imageEdit_->setText(QDir::toNativeSeparators(f));
        updating_ = prev;
        });
    // 圖片：來源 / 資料夾 / 比對欄位 / 顯示方式 (每個圖片元素各自一組)
    connect(imageSourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_) return;
        editSelected([&](card::Element& e) { e.imageSource = i == 0 ? card::ImageSource::Fixed : card::ImageSource::Dynamic; });
        onImageSourceChanged();
        });
    connect(imageFolderEdit_, &QLineEdit::editingFinished, this, [this] {
        if (updating_) return;
        ElementItem* it = selectedItem();
        const card::Element* e = it ? layout_.find(it->id()) : nullptr;
        if (e && imageFolderEdit_->text().trimmed() != e->imageFolder.trimmed()) setImageFolder(imageFolderEdit_->text());
        });
    connect(folderBtn, &QPushButton::clicked, this, [this] {
        ElementItem* it = selectedItem();
        const card::Element* e = it ? layout_.find(it->id()) : nullptr;
        if (!e) return;
        const QString start = QDir(e->imageFolder).exists() ? e->imageFolder : lastDataDir_;
        const QString dir = QFileDialog::getExistingDirectory(this, tr("選擇圖片資料夾"), start);
        if (!dir.isEmpty()) setImageFolder(dir);
        });
    connect(matchFieldCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        const QString f = matchFieldCombo_->currentText();
        editSelected([&](card::Element& e) { e.matchField = f; });
        QSettings(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner")).setValue(QStringLiteral("lastMatchField"), f);
        onImageSourceChanged();
        });
    connect(rescanBtn, &QPushButton::clicked, this, [this] {
        ElementItem* it = selectedItem();
        const card::Element* e = it ? layout_.find(it->id()) : nullptr;
        if (!e) return;
        imagePool_.rescan(e->imageFolder);
        onImageSourceChanged();
        });
    connect(imageFitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_) return;
        editSelected([&](card::Element& e) {
            e.imageFit = static_cast<card::ImageFit>(i);
            e.fitContent = (e.imageFit == card::ImageFit::MatchHeight);     // 「框高依圖片比例」= 貼合圖片
            });
        if (ElementItem* it = selectedItem()) populatePanel(layout_.find(it->id()));   // 更新高度欄位 / 縮放欄位的啟用狀態
        });
    connect(imageZoomSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        editSelected([&](card::Element& e) { e.imageZoomPct = v; });
        });
    connect(imageFocusXSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        editSelected([&](card::Element& e) { e.imageFocusX = v; });
        });
    connect(imageFocusYSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        editSelected([&](card::Element& e) { e.imageFocusY = v; });
        });
    connect(barcodeValueEdit_, &QLineEdit::textEdited, this, [this](const QString& v) {
        editSelected([&](card::Element& e) { e.barcodeValue = v; });
        });
    connect(barcodeSourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_) return;
        editSelected([&](card::Element& e) { e.barcodeSource = i == 0 ? card::ImageSource::Fixed : card::ImageSource::Dynamic; });
        if (ElementItem* it = selectedItem()) populatePanel(layout_.find(it->id()));
        });
    connect(barcodeTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_ || i < 0) return;
        editSelected([&](card::Element& e) { e.barcodeType = static_cast<card::BarcodeType>(i); });
        });
    connect(qrEccCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_ || i < 0) return;
        editSelected([&](card::Element& e) { e.qrEcc = i; });
        });
    connect(barcodeFieldCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        const QString field = barcodeFieldCombo_->currentText();
        editSelected([&](card::Element& e) { e.barcodeField = field; });
        });

    connect(fitCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_) return;
        editSelected([&](card::Element& e) { e.fitContent = on; });      // 開啟時 editSelected 會立刻貼合
        if (ElementItem* it = selectedItem()) populatePanel(layout_.find(it->id()));   // 更新數字與寬 / 高欄位的啟用狀態
        });

    // 尚未選取元素時，不顯示一堆灰掉的欄位，改顯示說明
    propHint_ = new QLabel(tr("尚未選取元素\n\n"
        "• 在卡面上點一下要編輯的元素\n"
        "• 或在上方「元素清單」選取\n"
        "• 還沒有元素？用工具列的「新增文字 / 圖片 / QR Code / 條碼」加入\n\n"
        "選取後，這裡會出現該元素的位置、大小與內容設定。"));
    propHint_->setWordWrap(true);
    propHint_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    propHint_->setStyleSheet(QStringLiteral("color:#666; padding:%1px;").arg(uiscale::px(10)));

    auto* outer = new QWidget;
    auto* ol = new QVBoxLayout(outer);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addWidget(propHint_);
    ol->addWidget(propertyPanel_, 1);

    populatePanel(nullptr);
    return outer;
}

QWidget* DesignerWindow::buildPreviewPanel() {
    // 放在右側面板，所以欄位由上往下排
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);

    auto* previewHint = new QLabel(tr("這裡是卡面預覽用的內容，不會改到 Excel。\n"
        "載入 Excel 後，在左側點選一筆就會自動帶入；也可以手動改來試排版。"));
    previewHint->setWordWrap(true);
    previewHint->setStyleSheet(QStringLiteral("color:#666;"));
    v->addWidget(previewHint);

    usePreviewCheck_ = new QCheckBox(tr("以此資料預覽"));
    usePreviewCheck_->setToolTip(tr("取消勾選時，動態欄位 {欄位名稱} 等會直接顯示原樣"));
    usePreviewCheck_->setChecked(true);
    v->addWidget(usePreviewCheck_);

    previewForm_ = new QFormLayout;
    previewForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    v->addLayout(previewForm_);
    rebuildPreviewForm();                     // 欄位依目前可用的欄位建立 (載入 Excel 後會重建)
    v->addStretch(1);

    connect(usePreviewCheck_, &QCheckBox::toggled, this, [this] { applyPreviewRecord(); });
    return w;
}

QWidget* DesignerWindow::buildDataPanel() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);

    auto* top = new QHBoxLayout;
    auto* loadBtn = new QPushButton(tr("載入 Excel…"));
    dataInfo_ = new QLabel(tr("尚未載入資料"));
    dataInfo_->setWordWrap(true);
    top->addWidget(loadBtn);
    top->addWidget(dataInfo_, 1);
    v->addLayout(top);


    dataFilter_ = new QLineEdit;
    dataFilter_->setPlaceholderText(tr("搜尋 (任一欄位)"));
    dataFilter_->setClearButtonEnabled(true);
    v->addWidget(dataFilter_);

    dataEmptyLabel_ = new QLabel(tr("尚未載入製卡資料\n\n"
        "按上方「載入 Excel…」選擇 .xlsx 檔。\n"
        "載入後點選任一筆，卡面就會顯示該筆資料；\n"
        "Excel 的所有欄位都能用在文字、圖片與條碼。\n\n"
        "還沒有資料也沒關係：\n可以先在右側「預覽資料」輸入範例內容來排版。"));
    dataEmptyLabel_->setAlignment(Qt::AlignCenter);
    dataEmptyLabel_->setWordWrap(true);
    dataEmptyLabel_->setStyleSheet(QStringLiteral("color:#666;"));
    v->addWidget(dataEmptyLabel_, 1);

    const QStringList& fields = card::fieldNames();
    dataTable_ = new QTableWidget(0, fields.size() + 1);          // 最後一欄是「圖片」比對結果
    dataTable_->setHorizontalHeaderLabels(QStringList(fields) << tr("圖片"));
    if (auto* h = dataTable_->horizontalHeaderItem(fields.size()))
        h->setToolTip(tr("目前選取的「動態圖片」元素的比對結果 (沒有選取時顯示版面中第一個動態圖片)。\n圖片的資料夾與比對欄位在右邊「屬性」面板設定，每個圖片各自設定。"));
    dataTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    dataTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    dataTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dataTable_->setAlternatingRowColors(true);
    dataTable_->verticalHeader()->setDefaultSectionSize(22);
    dataTable_->horizontalHeader()->setStretchLastSection(true);
    v->addWidget(dataTable_, 1);

    connect(loadBtn, &QPushButton::clicked, this, [this] { browseData(); });
    connect(dataFilter_, &QLineEdit::textChanged, this, [this](const QString& t) { applyDataFilter(t); });
    connect(dataTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0) applyDataRow(row);
        });

    refreshImageMatches();
    updateDataEmptyState();
    return w;
}

// ========================================================== scene / model ===

void DesignerWindow::applyPreviewRecord() {
    previewRecord_.clear();
    if (usePreviewCheck_ && usePreviewCheck_->isChecked()) {
        for (const auto& pr : previewEdits_) previewRecord_.insert(pr.first, pr.second->text());
        imagePool_.applyTo(previewRecord_, layout_);              // 各動態圖片依自己的資料夾與比對欄位帶入圖片
    }
    refitAll();
    if (layout_.background.path.contains(QLatin1Char('{'))) refreshBackgroundPanel();   // 底圖路徑含 {欄位}：換資料後重新檢查檔案
    if (scene_) scene_->update();
}

void DesignerWindow::refitAll() {
    if (!scene_) return;
    bool any = false;
    for (auto& e : layout_.elements) {
        if (!e.fitContent || !card::fitElement(e, previewRecord_)) continue;
        any = true;
        if (ElementItem* it = findItem(e.id)) it->syncFromModel();
        if (card::Element* b = baseline_.find(e.id))       // 因預覽資料而改變的大小，不算使用者的編輯 (不產生復原步驟)
            if (b->fitContent) { b->xMm = e.xMm; b->yMm = e.yMm; b->wMm = e.wMm; b->hMm = e.hMm; }
    }
    if (!any) return;
    if (ElementItem* sel = selectedItem()) populatePanel(layout_.find(sel->id()));
}

ElementItem* DesignerWindow::createItem(const QString& id, int z) {
    auto* it = new ElementItem(&layout_, &previewRecord_, id,
        [this](const QString& changedId) { onItemChanged(changedId); }, cardItem_);
    it->setZValue(z);
    it->setGuideSink([this](const std::vector<card::Guide>& g) { if (guides_) guides_->setGuides(g); });
    return it;
}

void DesignerWindow::rebuildScene(bool refit) {
    updating_ = true;
    imagePool_.ensureAll(layout_);         // 開啟版面 / 復原後，動態圖片用到的資料夾要掃描
    card::fitAll(layout_, previewRecord_); // 貼合內容的元素依目前預覽資料重新貼合
    scene_->clear();                       // 會刪除舊的卡片與所有元素
    cardItem_ = nullptr;
    bgItem_ = nullptr;
    guides_ = nullptr;

    const double w = layout_.widthMm() * ElementItem::kPxPerMm;
    const double h = layout_.heightMm() * ElementItem::kPxPerMm;
    cardItem_ = scene_->addRect(0, 0, w, h, QPen(QColor(110, 110, 110)), QBrush(Qt::white));
    cardItem_->setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);   // 超出卡片的部分不會被印出來
    scene_->setSceneRect(QRectF(-60, -60, w + 120, h + 120));
    guides_ = new GuideOverlay(QRectF(0, 0, w, h), ElementItem::kPxPerMm);   // 輔助線圖層 (最上層)
    scene_->addItem(guides_);

    bgItem_ = new BackgroundItem(&layout_, &previewRecord_, cardItem_);       // 卡面底圖：最底層
    for (int i = 0; i < layout_.elements.size(); ++i) createItem(layout_.elements[i].id, i);
    for (int i = 0; i < layout_.topcoat.regions.size(); ++i) createRegionItem(layout_.topcoat.regions[i].id, 10000 + i);   // 鍍膜範圍疊在最上層

    const bool portrait = layout_.orientation == card::Orientation::Portrait;
    actLandscape_->setChecked(!portrait);
    actPortrait_->setChecked(portrait);
    updating_ = false;

    refreshRegionItemsView();              // 重建場景後，依「編輯鍍膜範圍」模式設定滑鼠狀態
    refreshList();
    populatePanel(nullptr);
    refreshTopcoatPanel();
    refreshBackgroundPanel();
    updateActionStates();
    if (refit) fitView();
    else if (view_->onViewChanged) view_->onViewChanged();
    updateCursorLabel(QPointF(), false);
}

ElementItem* DesignerWindow::findItem(const QString& id) const {
    if (!cardItem_) return nullptr;
    for (QGraphicsItem* gi : cardItem_->childItems()) {
        if (gi->type() == ElementItem::Type) {
            auto* it = static_cast<ElementItem*>(gi);
            if (it->id() == id) return it;
        }
    }
    return nullptr;
}

ElementItem* DesignerWindow::selectedItem() const {
    for (QGraphicsItem* gi : scene_->selectedItems())
        if (gi->type() == ElementItem::Type) return static_cast<ElementItem*>(gi);
    return nullptr;
}

RegionItem* DesignerWindow::selectedRegion() const {
    for (QGraphicsItem* gi : scene_->selectedItems())
        if (gi->type() == RegionItem::Type) return static_cast<RegionItem*>(gi);
    return nullptr;
}

void DesignerWindow::fitView() {
    if (!cardItem_) return;
    view_->resetTransform();
    view_->fitInView(cardItem_->sceneBoundingRect().adjusted(-25, -25, 25, 25), Qt::KeepAspectRatio);
    if (view_->onViewChanged) view_->onViewChanged();      // 尺規跟著更新
}

// ================================================================= actions ===

void DesignerWindow::addElement(card::ElementType type) {
    commitUndo();                          // 先把還沒記錄的編輯收成一步
    card::Element e;
    e.id = layout_.nextId();
    e.type = type;
    int count = 1;
    for (const auto& x : layout_.elements) if (x.type == type) ++count;

    if (type == card::ElementType::Text) {
        e.name = tr("文字 %1").arg(count);
        e.text = tr("新文字");
        e.xMm = 5; e.yMm = 5; e.wMm = 50; e.hMm = 8;
    }
    else if (type == card::ElementType::Image) {
        e.name = tr("圖片 %1").arg(count);
        e.xMm = 5; e.yMm = 5; e.wMm = 25; e.hMm = 25;
        QSettings st(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner"));        // 沿用上次使用的資料夾 / 比對欄位
        e.imageSource = card::ImageSource::Dynamic;
        e.imageFolder = st.value(QStringLiteral("lastImageFolder"), st.value(QStringLiteral("imageFolder"))).toString();
        QString mf = st.value(QStringLiteral("lastMatchField"), st.value(QStringLiteral("matchField"), QStringLiteral("姓名"))).toString();
        const QStringList avail = currentFields();
        if (!avail.contains(mf) && !avail.isEmpty()) mf = avail.first();             // 上次的欄位不在這份資料裡：先用第一個欄位，請使用者再選
        e.matchField = mf;
        e.imageFit = card::ImageFit::Cover;
    }
    else if (type == card::ElementType::QRCode) {
        e.name = tr("QR Code %1").arg(count);
        e.barcodeSource = card::ImageSource::Dynamic;
        e.qrEcc = 1;
        const QStringList avail = currentFields();
        e.barcodeField = avail.contains(QStringLiteral("學號")) ? QStringLiteral("學號") : (avail.isEmpty() ? QString() : avail.first());
        e.xMm = 5; e.yMm = 5; e.wMm = 20; e.hMm = 20;
    }
    else {
        e.name = tr("條碼 %1").arg(count);
        e.barcodeSource = card::ImageSource::Dynamic;
        e.barcodeType = card::BarcodeType::Code128;
        const QStringList avail = currentFields();
        e.barcodeField = avail.contains(QStringLiteral("學號")) ? QStringLiteral("學號") : (avail.isEmpty() ? QString() : avail.first());
        e.xMm = 5; e.yMm = 5; e.wMm = 45; e.hMm = 14;
    }
    layout_.elements.append(e);
    imagePool_.ensureAll(layout_);
    createItem(e.id, layout_.elements.size() - 1);
    refreshList();

    scene_->clearSelection();
    if (ElementItem* it = findItem(e.id)) it->setSelected(true);
    commitUndo();
}

// 覆蓋順序：layout_.elements 的順序 = 疊放順序 (越後面越上層)，所以調整圖層就是移動它在清單中的位置。
void DesignerWindow::moveLayer(int mode) {
    ElementItem* it = selectedItem();
    if (!it) return;
    const QString id = it->id();
    int from = -1;
    for (int i = 0; i < layout_.elements.size(); ++i) if (layout_.elements[i].id == id) { from = i; break; }
    if (from < 0) return;
    const int last = layout_.elements.size() - 1;
    int to = from;
    switch (mode) {
    case 0: to = last; break;
    case 1: to = qMin(last, from + 1); break;
    case 2: to = qMax(0, from - 1); break;
    default: to = 0; break;
    }
    if (to == from) return;

    commitUndo();
    layout_.elements.move(from, to);
    for (int i = 0; i < layout_.elements.size(); ++i)
        if (ElementItem* e = findItem(layout_.elements[i].id)) e->setZValue(i);   // z 值 = 在清單中的順序
    refreshList();
    syncListSelection(id);
    updateActionStates();
    scene_->update();
    commitUndo();
}

// 清單由上到下 = 最上層到最下層；拖曳完成後，依清單順序重排 layout_.elements (越後面越上層)
void DesignerWindow::reorderFromList() {
    QStringList ids;
    for (int i = elementList_->count() - 1; i >= 0; --i)           // 反過來 = 由下到上
        ids << elementList_->item(i)->data(Qt::UserRole).toString();
    if (ids.size() != layout_.elements.size()) { refreshList(); return; }
    bool same = true;
    for (int i = 0; i < ids.size(); ++i) if (layout_.elements[i].id != ids[i]) { same = false; break; }
    if (same) return;

    QString selId;
    if (ElementItem* it = selectedItem()) selId = it->id();

    commitUndo();
    decltype(layout_.elements) reordered;
    for (const QString& id : ids) {
        const card::Element* e = layout_.find(id);
        if (!e) { refreshList(); return; }
        reordered.append(*e);
    }
    layout_.elements = reordered;
    for (int i = 0; i < layout_.elements.size(); ++i)
        if (ElementItem* e = findItem(layout_.elements[i].id)) e->setZValue(i);
    refreshList();
    if (!selId.isEmpty()) syncListSelection(selId);
    updateActionStates();
    scene_->update();
    commitUndo();
    statusBar()->showMessage(tr("已調整圖層順序"), 3000);
}

void DesignerWindow::deleteSelected() {
    // 焦點在「鍍膜範圍」清單，或選取的是鍍膜範圍 (沒有選元素)：刪除範圍
    if (regionList_->hasFocus() || (!selectedItem() && selectedRegion())) {
        deleteSelectedRegion();
        return;
    }
    ElementItem* it = selectedItem();
    if (!it) return;
    commitUndo();
    const QString id = it->id();

    updating_ = true;
    delete it;                             // 從場景移除
    for (int i = 0; i < layout_.elements.size(); ++i) {
        if (layout_.elements[i].id == id) { layout_.elements.removeAt(i); break; }
    }
    updating_ = false;

    refreshList();
    populatePanel(nullptr);
    updateActionStates();
    commitUndo();
}

void DesignerWindow::setOrientation(card::Orientation o) {
    if (layout_.orientation == o) return;
    commitUndo();
    layout_.orientation = o;
    rebuildScene();                        // 元素座標維持不變，需要時請自行重新排版
    commitUndo();
}

void DesignerWindow::openLayout() {
    const QString startDir = QSettings(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner")).value(QStringLiteral("lastLayoutDir")).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("開啟版面"), startDir, tr("版面檔 (*.json)"));
    if (path.isEmpty()) return;
    card::CardLayout loaded;
    QString err;
    if (!card::CardLayout::loadFromFile(path, &loaded, &err)) {
        QMessageBox::warning(this, tr("開啟失敗"), err);
        return;
    }
    commitUndo();
    migrateLegacyImageSettings(loaded);    // 舊版面檔沒有「每個圖片自己的資料夾」：沿用上次使用的設定
    layout_ = loaded;
    rebuildScene();
    commitUndo();                          // 開啟版面也可以用 Ctrl+Z 退回開啟前
    layoutPath_ = path;
    savedJson_ = layout_.toJson();         // 剛開啟 = 沒有未儲存的修改
    QSettings(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner")).setValue(QStringLiteral("lastLayoutDir"), QFileInfo(path).absolutePath());
    updateWindowTitle();
    statusBar()->showMessage(tr("已開啟 %1").arg(QDir::toNativeSeparators(path)), 5000);
}

bool DesignerWindow::saveLayout() {
    if (layoutPath_.isEmpty()) return saveLayoutAs();     // 還沒有檔名：問一次
    return writeLayout(layoutPath_);                      // 已有檔名：直接存，不再跳視窗
}

bool DesignerWindow::saveLayoutAs() {
    const QString dir = QSettings(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner")).value(QStringLiteral("lastLayoutDir")).toString();
    const QString start = !layoutPath_.isEmpty() ? layoutPath_
        : (dir.isEmpty() ? QStringLiteral("layout.json") : QDir(dir).filePath(QStringLiteral("layout.json")));
    QString path = QFileDialog::getSaveFileName(this, tr("另存版面"), start, tr("版面檔 (*.json)"));
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".json");
    return writeLayout(path);
}

bool DesignerWindow::writeLayout(const QString& path) {
    commitUndo();
    QString err;
    if (!layout_.saveToFile(path, &err)) {
        QMessageBox::warning(this, tr("儲存失敗"), err);
        return false;
    }
    layoutPath_ = path;
    savedJson_ = layout_.toJson();
    QSettings(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner")).setValue(QStringLiteral("lastLayoutDir"), QFileInfo(path).absolutePath());
    updateWindowTitle();
    statusBar()->showMessage(tr("已儲存 %1").arg(QDir::toNativeSeparators(path)), 5000);
    return true;
}

bool DesignerWindow::isDirty() const {
    return layout_.toJson() != savedJson_;
}

void DesignerWindow::updateWindowTitle() {
    const QString name = layoutPath_.isEmpty() ? tr("未命名版面") : QFileInfo(layoutPath_).fileName();
    setWindowTitle(QStringLiteral("%1[*] - Entrust Card Tool").arg(name));     // [*] 由 setWindowModified 決定要不要顯示 *
    setWindowModified(isDirty());
}

// 關閉前：版面有改動就先問。回傳 false = 使用者按了「取消」(或存檔失敗)，不要關閉。
bool DesignerWindow::maybeSave() {
    commitUndo();
    if (!isDirty()) return true;
    const QString name = layoutPath_.isEmpty() ? tr("未命名版面") : QFileInfo(layoutPath_).fileName();
    QMessageBox box(QMessageBox::Question, tr("尚未儲存"),
        tr("「%1」有尚未儲存的修改。\n要先儲存再離開嗎？").arg(name), QMessageBox::NoButton, this);
    QPushButton* saveBtn = box.addButton(tr("儲存"), QMessageBox::AcceptRole);
    QPushButton* discardBtn = box.addButton(tr("不儲存"), QMessageBox::DestructiveRole);
    box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.exec();
    if (box.clickedButton() == saveBtn) return saveLayout();
    return box.clickedButton() == discardBtn;                 // 取消 / 按 X / Esc -> false
}

// 狀態列左側：依目前狀態，告訴使用者「現在可以做什麼」
void DesignerWindow::updateHints() {
    if (!hintLabel_) return;
    QString s;
    if (actRegionEdit_ && actRegionEdit_->isChecked()) {
        s = tr("編輯鍍膜範圍中：拖曳範圍移動、拖右下角方塊調整大小；點選卡面上的元素即可回到一般設計");
    }
    else if (ElementItem* it = selectedItem()) {
        const card::Element* e = layout_.find(it->id());
        s = tr("已選取「%1」：拖曳移動、拖右下角方塊調整大小 (Shift 等比例)、Delete 刪除；精確數值在右側「屬性」輸入")
            .arg(e ? e->name : QString());
    }
    else {
        s = tr("點選卡面上的元素即可編輯，或用工具列「新增文字 / 圖片…」加入元素；Ctrl+滾輪縮放卡面；F1 = 操作說明");
    }
    hintLabel_->setText(s);
    hintLabel_->setToolTip(s);
}

void DesignerWindow::updateDataEmptyState() {
    if (!dataEmptyLabel_) return;
    const bool empty = data_.rows.isEmpty() && data_.path.isEmpty();
    dataEmptyLabel_->setVisible(empty);
    if (dataFilter_) dataFilter_->setVisible(!empty);
    if (dataTable_) dataTable_->setVisible(!empty);
}

void DesignerWindow::showHelp() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("操作說明"));
    auto* v = new QVBoxLayout(&dlg);
    auto* tb = new QTextBrowser;
    tb->setOpenLinks(false);
    QString html;
    html += tr("<h3>基本流程</h3><ol>"
        "<li><b>載入製卡資料</b>：工具列「載入 Excel 資料」，選 .xlsx 並確認標題列。</li>"
        "<li><b>設計卡面</b>：用「新增文字 / 圖片 / QR Code / 條碼」加入元素，在右側「屬性」設定內容。"
        "文字可用 {欄位名稱} 帶入 Excel 資料；圖片選「動態圖片」會依資料欄位自動找檔案。</li>"
        "<li><b>預覽</b>：在左側資料表點選一筆，卡面立刻顯示該筆；也可用工具列「上一筆 / 下一筆」。</li>"
        "<li><b>儲存版面</b>：存成 .json，下次用「開啟版面」直接載入。標題列出現 * 表示有尚未儲存的修改。</li>"
        "<li><b>列印</b>：工具列最右邊綠色「列印…」。可印目前這一筆、指定範圍或自訂清單；"
        "建議先勾「測試模式」輸出 PNG 檢查版面。</li></ol>");
    html += tr("<h3>卡面操作</h3><ul>"
        "<li>拖曳元素移動；選取後拖右下角方塊調整大小；Shift + 拖曳角落 = 等比例縮放。</li>"
        "<li>拖曳時會自動對齊其他元素與卡面邊緣，按住 Alt 可暫時停用。</li>"
        "<li>Ctrl + 滾輪縮放卡面；「符合視窗」讓整張卡剛好放進視窗。</li>"
        "<li>座標與尺規的原點在<b>卡面左下角</b>：X 往右增加、Y 往上增加，單位 mm。</li>"
        "<li>元素重疊時，在右上角「元素清單」拖曳項目，或按清單旁的 ▲ ▼，決定誰蓋在誰上面；清單最上面 = 最上層。</li>"
        "<li>誤操作可用復原 (Ctrl+Z)；重做 Ctrl+Y。</li></ul>");
    html += tr("<h3>快捷鍵</h3><table cellpadding='3'>"
        "<tr><td>Ctrl+O / Ctrl+S / Ctrl+Shift+S</td><td>開啟 / 儲存 / 另存版面</td></tr>"
        "<tr><td>Ctrl+Shift+O</td><td>載入 Excel 資料</td></tr>"
        "<tr><td>Ctrl+P</td><td>列印</td></tr>"
        "<tr><td>Ctrl+E</td><td>輸出預覽圖 (PNG)</td></tr>"
        "<tr><td>Ctrl+T / Ctrl+I</td><td>新增文字 / 圖片</td></tr>"
        "<tr><td>Ctrl+Shift+R / Ctrl+Shift+B</td><td>新增 QR Code / 條碼</td></tr>"
        "<tr><td>Delete 或 Backspace</td><td>刪除選取的元素 (焦點在卡面或清單時)</td></tr>"
        "<tr><td>Ctrl+] / Ctrl+[</td><td>上移 / 下移一層 (加 Shift = 最上 / 最下層)</td></tr>"
        "<tr><td>Alt+← / Alt+→</td><td>預覽上一筆 / 下一筆資料</td></tr>"
        "<tr><td>Ctrl+0</td><td>卡面符合視窗</td></tr>"
        "<tr><td>Ctrl+Shift++ / Ctrl+Shift+-</td><td>放大 / 縮小介面</td></tr>"
        "<tr><td>F11</td><td>全螢幕</td></tr>"
        "<tr><td>F1</td><td>開啟這份說明</td></tr></table>");
    html += tr("<h3>找不到面板？</h3><p>到「檢視 → 面板」重新開啟；版面亂掉時用「檢視 → 還原預設面板配置」或「視窗適應螢幕」。</p>");
    tb->setHtml(html);
    v->addWidget(tb, 1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    uiscale::fitSize(&dlg, 660, 680);
    dlg.exec();
}

void DesignerWindow::exportPreview() {
    QString path = QFileDialog::getSaveFileName(this, tr("輸出預覽圖"), QStringLiteral("card.png"), tr("PNG (*.png)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".png");
    const QImage img = card::renderCard(layout_, previewRecord_, 300);
    if (!img.save(path)) {
        QMessageBox::warning(this, tr("輸出失敗"), tr("無法寫入 %1").arg(path));
        return;
    }
    statusBar()->showMessage(tr("已輸出 %1 (%2 x %3 px)").arg(path).arg(img.width()).arg(img.height()), 5000);
}

// ============================================================ 批次列印 (階段 3) ===

void DesignerWindow::openPrintDialog() {
    if (layout_.elements.isEmpty()) {
        QMessageBox::information(this, tr("列印"), tr("版面是空的，請先新增元素。"));
        return;
    }

    PrintInput in;
    in.layout = layout_;
    imagePool_.ensureAll(layout_);
    in.imagePool = imagePool_;

    if (data_.rows.isEmpty()) {
        // 還沒載入 Excel：用「預覽資料」欄位的內容印 1 張 (方便先用單張測試印表機與版面)
        in.previewOnly = true;
        in.data.fields = currentFields();
        card::Record rec;
        for (const auto& pr : previewEdits_) rec.insert(pr.first, pr.second->text());
        in.data.rows.append(rec);
        in.currentRow = 0;
        in.visibleRows = { 0 };
    }
    else {
        in.data = data_;
        in.currentRow = dataTable_->currentRow();
        for (int r = 0; r < dataTable_->rowCount(); ++r)
            if (!dataTable_->isRowHidden(r)) in.visibleRows.append(r);
    }

    PrintDialog dlg(in, this);
    dlg.exec();
}

// ================================================================ 卡面底圖 ===

QWidget* DesignerWindow::buildBackgroundPanel() {
    auto* w = new QWidget;
    w->setMinimumWidth(uiscale::px(300));
    backgroundPanel_ = w;
    auto* root = new QVBoxLayout(w);

    auto* hint = new QLabel(tr("匯入卡面的圖，墊在所有元素的最底層，讓你設計文字與圖片時可以對位。\n"
        "預設只在設計畫面顯示當參考，不會印出；勾選「列印時印出」後，預覽輸出與批次列印才會把它印在卡面上。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#666;"));
    root->addWidget(hint);

    auto* box = new QGroupBox(tr("底圖"));
    auto* f = new QFormLayout(box);

    bgPathEdit_ = new QLineEdit;
    bgPathEdit_->setPlaceholderText(tr("卡面圖片檔路徑 (留空 = 沒有底圖)"));
    bgPathEdit_->setToolTip(tr("路徑也可以含 {欄位}，例如 D:\\faces\\{系所}.png，不同資料就能用不同的卡面。"));
    auto* browse = new QPushButton(tr("匯入…"));
    bgClearBtn_ = new QPushButton(tr("移除"));
    auto* pathRow = new QWidget;
    auto* pl = new QHBoxLayout(pathRow);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(bgPathEdit_, 1);
    pl->addWidget(browse);
    pl->addWidget(bgClearBtn_);
    f->addRow(tr("圖片"), pathRow);

    bgInfoLabel_ = new QLabel;
    bgInfoLabel_->setWordWrap(true);
    f->addRow(bgInfoLabel_);

    bgFitCombo_ = new QComboBox;
    bgFitCombo_->addItem(tr("裁切填滿 (蓋滿卡面，超出的裁掉)"));
    bgFitCombo_->addItem(tr("縮放適應 (整張放進卡面，可能留白)"));
    bgFitCombo_->addItem(tr("拉伸 (填滿卡面，比例會改變)"));
    bgFitCombo_->setToolTip(tr("卡面圖的比例與卡面一致 (85.6 × 54 mm) 時三種結果相同；比例不同時才有差別。"));
    f->addRow(tr("顯示方式"), bgFitCombo_);

    auto makePctSpin = [] {
        auto* s = new QDoubleSpinBox;
        s->setRange(0, 100);
        s->setDecimals(0);
        s->setSingleStep(5);
        s->setSuffix(QStringLiteral(" %"));
        s->setValue(50);
        return s;
        };
    bgFocusXSpin_ = makePctSpin();
    bgFocusYSpin_ = makePctSpin();
    f->addRow(tr("水平位置"), bgFocusXSpin_);
    f->addRow(tr("垂直位置"), bgFocusYSpin_);
    root->addWidget(box);

    auto* outBox = new QGroupBox(tr("顯示與列印"));
    auto* ol = new QVBoxLayout(outBox);
    bgShowCheck_ = new QCheckBox(tr("設計時顯示"));
    bgShowCheck_->setToolTip(tr("只影響編輯畫面，不影響列印。"));
    bgPrintCheck_ = new QCheckBox(tr("列印時印出"));
    bgPrintCheck_->setToolTip(tr("不勾選：底圖只是設計時的參考，輸出預覽圖與列印都不含底圖。\n"
        "勾選：渲染卡面時先畫底圖，再疊上文字與圖片。"));
    ol->addWidget(bgShowCheck_);
    ol->addWidget(bgPrintCheck_);
    root->addWidget(outBox);
    root->addStretch(1);

    // ---- 使用者編輯 -> 模型 ----
    connect(browse, &QPushButton::clicked, this, [this] { importBackground(); });
    connect(bgClearBtn_, &QPushButton::clicked, this, [this] { clearBackground(); });
    connect(bgPathEdit_, &QLineEdit::editingFinished, this, [this] {
        if (updating_) return;
        const QString v = bgPathEdit_->text().trimmed();
        if (v == layout_.background.path.trimmed()) return;
        commitUndo();
        editBackground([&](card::CardBackground& b) { b.path = v; });
        commitUndo();
        refreshBackgroundPanel();
        });
    connect(bgFitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_) return;
        editBackground([&](card::CardBackground& b) { b.fit = static_cast<card::ImageFit>(i); });
        refreshBackgroundPanel();
        });
    connect(bgFocusXSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!updating_) editBackground([&](card::CardBackground& b) { b.focusX = v; });
        });
    connect(bgFocusYSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!updating_) editBackground([&](card::CardBackground& b) { b.focusY = v; });
        });
    connect(bgShowCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (!updating_) editBackground([&](card::CardBackground& b) { b.showInDesigner = on; });
        });
    connect(bgPrintCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (updating_) return;
        editBackground([&](card::CardBackground& b) { b.print = on; });
        refreshBackgroundPanel();
        statusBar()->showMessage(on ? tr("卡面底圖：列印時會印出") : tr("卡面底圖：僅設計時參考，不會印出"), 4000);
        });

    return w;
}

void DesignerWindow::editBackground(const std::function<void(card::CardBackground&)>& fn) {
    if (updating_) return;
    fn(layout_.background);
    if (bgItem_) bgItem_->update();
    updateActionStates();
    scheduleCommit();
}

void DesignerWindow::importBackground() {
    const card::CardBackground& cur = layout_.background;
    const QString start = cur.isEmpty() ? lastDataDir_ : QFileInfo(cur.path).absolutePath();
    const QString f = QFileDialog::getOpenFileName(this, tr("匯入卡面底圖"), start,
        tr("圖片 (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff)"));
    if (f.isEmpty()) return;
    commitUndo();
    card::clearImageCache();                // 同一個檔名被改過內容時，重新讀取
    layout_.background.path = QDir::toNativeSeparators(f);
    if (bgItem_) bgItem_->update();
    refreshBackgroundPanel();
    updateActionStates();
    commitUndo();
    backgroundDock_->show();
    backgroundDock_->raise();
    statusBar()->showMessage(tr("已匯入卡面底圖 (預設只在設計時顯示，不會印出；要印出請勾選「列印時印出」)"), 6000);
}

void DesignerWindow::clearBackground() {
    if (layout_.background.isEmpty()) return;
    commitUndo();
    layout_.background.path.clear();
    if (bgItem_) bgItem_->update();
    refreshBackgroundPanel();
    updateActionStates();
    commitUndo();
}

void DesignerWindow::refreshBackgroundPanel() {
    if (!bgPathEdit_) return;
    const bool prev = updating_;
    updating_ = true;
    const card::CardBackground& b = layout_.background;
    if (bgPathEdit_->text() != b.path) bgPathEdit_->setText(b.path);
    bgFitCombo_->setCurrentIndex(qBound(0, int(b.fit), 2));
    bgFocusXSpin_->setValue(b.focusX);
    bgFocusYSpin_->setValue(b.focusY);
    bgShowCheck_->setChecked(b.showInDesigner);
    bgPrintCheck_->setChecked(b.print);

    const bool has = !b.isEmpty();
    const bool crop = b.fit == card::ImageFit::Cover || b.fit == card::ImageFit::Contain;
    bgClearBtn_->setEnabled(has);
    bgFitCombo_->setEnabled(has);
    bgFocusXSpin_->setEnabled(has && crop);
    bgFocusYSpin_->setEnabled(has && crop);
    bgShowCheck_->setEnabled(has);
    bgPrintCheck_->setEnabled(has);

    // 狀態說明：圖片大小、比例是否與卡面一致、會不會印出
    QString info;
    if (!has) {
        info = tr("尚未匯入卡面底圖。");
    }
    else {
        const QString real = card::substitute(b.path, previewRecord_);
        const QSize sz = QImageReader(real).size();
        if (!QFileInfo::exists(real)) {
            info = tr("<span style='color:#c62828;'>找不到圖片檔：%1</span>").arg(real.toHtmlEscaped());
        }
        else if (!sz.isValid()) {
            info = tr("<span style='color:#c62828;'>無法讀取這個圖片檔。</span>");
        }
        else {
            const double imgRatio = double(sz.width()) / sz.height();
            const double cardRatio = layout_.widthMm() / layout_.heightMm();
            info = tr("圖片 %1 × %2 px (比例 %3)；卡面比例 %4").arg(sz.width()).arg(sz.height())
                .arg(imgRatio, 0, 'f', 3).arg(cardRatio, 0, 'f', 3);
            if (qAbs(imgRatio / cardRatio - 1.0) > 0.02)
                info += tr("<br><span style='color:#e65100;'>圖片比例與卡面不同，請確認「顯示方式」。</span>");
        }
        info += b.print ? tr("<br><span style='color:#1f7a3d;'>列印時<b>會印出</b>。</span>")
            : tr("<br><span style='color:#666;'>僅設計時參考，<b>不會印出</b>。</span>");
    }
    bgInfoLabel_->setTextFormat(Qt::RichText);
    bgInfoLabel_->setText(info);
    if (actClearBg_) actClearBg_->setEnabled(has);
    updating_ = prev;
}

// ========================================================= 製卡資料 (階段 2) ===

// ----------------------------------------------------------- 欄位 (依 Excel 而定) ---

QStringList DesignerWindow::currentFields() const {
    return data_.fields.isEmpty() ? card::fieldNames() : data_.fields;
}

QStringList DesignerWindow::layoutFields() const {
    static const QRegularExpression re(QStringLiteral("\\{([^{}]+)\\}"));
    QStringList out;
    auto add = [&out](const QString& f) { if (!f.isEmpty() && !out.contains(f)) out << f; };
    auto addFrom = [&](const QString& src) {
        auto it = re.globalMatch(src);
        while (it.hasNext()) add(it.next().captured(1));
        };
    addFrom(layout_.background.path);      // 卡面底圖的路徑也可以含 {欄位}
    for (const auto& e : layout_.elements) {
        if (e.type == card::ElementType::Text) addFrom(e.text);
        else if (e.type == card::ElementType::Image) {
            if (e.imageSource == card::ImageSource::Dynamic) add(e.matchField.trimmed());
            else addFrom(e.imagePath);
        }
        else if ((e.type == card::ElementType::Barcode || e.type == card::ElementType::QRCode) && e.barcodeSource == card::ImageSource::Dynamic) add(e.barcodeField.trimmed());
    }
    return out;
}

void DesignerWindow::rebuildPreviewForm() {
    if (!previewForm_) return;
    previewEdits_.clear();
    while (previewForm_->rowCount() > 0) previewForm_->removeRow(0);

    // 尚未載入 Excel 時的範例值；載入後，選取某一筆資料會把該筆資料填進這些欄位
    static const QMap<QString, QString> samples{
        { QStringLiteral("姓名"), QStringLiteral("王小明") },
        { QStringLiteral("學號"), QStringLiteral("315130106") },
        { QStringLiteral("系所"), QStringLiteral("職業安全衛生系") },
        { QStringLiteral("內碼"), QStringLiteral("1234567890") },
        { QStringLiteral("外碼"), QStringLiteral("9876543210") } };
    const bool noData = data_.fields.isEmpty();
    for (const QString& name : currentFields()) {
        auto* edit = new QLineEdit(noData ? samples.value(name) : QString());
        previewForm_->addRow(name + QStringLiteral("："), edit);
        previewEdits_.emplace_back(name, edit);
        connect(edit, &QLineEdit::textChanged, this, [this] { applyPreviewRecord(); });
    }
}

void DesignerWindow::rebuildFieldWidgets() {
    const QStringList fields = currentFields();

    if (fieldCombo_) {                                           // 文字：插入欄位
        const QString keep = fieldCombo_->currentText();
        QSignalBlocker b(fieldCombo_);
        fieldCombo_->clear();
        fieldCombo_->addItems(fields);
        fieldCombo_->setCurrentIndex(qMax(0, fieldCombo_->findText(keep)));
    }
    if (matchFieldCombo_) {                                      // 圖片：比對欄位
        QSignalBlocker b(matchFieldCombo_);
        matchFieldCombo_->clear();
        matchFieldCombo_->addItems(fields);
        ElementItem* it = selectedItem();
        const card::Element* e = it ? layout_.find(it->id()) : nullptr;
        matchFieldCombo_->setCurrentIndex(e ? matchFieldCombo_->findText(e->matchField) : -1);   // 找不到 (-1) = 空白，請使用者選
    }
    if (barcodeFieldCombo_) {                                    // 條碼：動態資料欄位
        QSignalBlocker b(barcodeFieldCombo_);
        barcodeFieldCombo_->clear();
        barcodeFieldCombo_->addItems(fields);
        ElementItem* it = selectedItem();
        const card::Element* e = it ? layout_.find(it->id()) : nullptr;
        barcodeFieldCombo_->setCurrentIndex(e ? barcodeFieldCombo_->findText(e->barcodeField) : -1);
    }
    rebuildPreviewForm();                                        // 右側預覽資料

    // 左邊資料表：Excel 的所有欄位 + 最後一欄「圖片」
    dataTable_->setColumnCount(int(fields.size()) + 1);
    dataTable_->setHorizontalHeaderLabels(QStringList(fields) << tr("圖片"));
    if (auto* h = dataTable_->horizontalHeaderItem(int(fields.size())))
        h->setToolTip(tr("目前選取的「動態圖片」元素的比對結果 (沒有選取時顯示版面中第一個動態圖片)。\n圖片的資料夾與比對欄位在右邊「屬性」面板設定，每個圖片各自設定。"));
}

void DesignerWindow::browseData() {
    const QString path = QFileDialog::getOpenFileName(this, tr("載入製卡資料"), lastDataDir_,
        tr("Excel 活頁簿 (*.xlsx)"));
    if (path.isEmpty()) return;
    loadData(path);
}

bool DesignerWindow::loadData(const QString& path) {
    // 1) 先把整張工作表讀出來 (只讀一次)
    QList<QStringList> raw;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool readOk = card::readXlsxFirstSheet(path, &raw, &err);
    QApplication::restoreOverrideCursor();
    if (!readOk) {
        QMessageBox::warning(this, tr("載入失敗"), err);
        return false;
    }
    if (raw.isEmpty()) {
        QMessageBox::warning(this, tr("載入失敗"), tr("工作表是空的"));
        return false;
    }

    // 2) 確認標題列 (欄位名稱所在的那一列)
    HeaderRowDialog dlg(path, raw, this);
    if (dlg.exec() != QDialog::Accepted) return false;

    // 3) 匯入所有欄位
    card::DataTable t;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = card::buildDataTableAll(raw, dlg.headerRow(), path, &t, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("載入失敗"), err);
        return false;
    }
    lastDataDir_ = QFileInfo(path).absolutePath();
    data_ = t;

    // 填表 (先暫停訊號，最後再一次選第一筆)
    dataTable_->blockSignals(true);
    dataFilter_->blockSignals(true);
    dataFilter_->clear();
    rebuildFieldWidgets();                         // 依 Excel 的欄位，重建「插入欄位」、圖片比對欄位、預覽資料、資料表欄
    const QStringList usedFields = layoutFields();
    dataTable_->setUpdatesEnabled(false);
    dataTable_->clearContents();
    dataTable_->setRowCount(int(data_.rows.size()));
    for (int r = 0; r < data_.rows.size(); ++r) {
        for (int c = 0; c < data_.fields.size(); ++c) {
            const QString v = data_.rows[r].value(data_.fields[c]);
            auto* item = new QTableWidgetItem(v);
            if (v.isEmpty() && usedFields.contains(data_.fields[c])) {   // 版面用到的欄位缺資料 (含 Excel 的 #N/A 等錯誤值) 以淡紅色標示
                item->setBackground(QColor(255, 215, 215));
                item->setToolTip(tr("此欄位沒有資料 (空白或 Excel 錯誤值如 #N/A)"));
            }
            dataTable_->setItem(r, c, item);
        }
        dataTable_->setRowHidden(r, false);
    }
    dataTable_->resizeColumnsToContents();
    dataTable_->setUpdatesEnabled(true);
    dataFilter_->blockSignals(false);
    dataTable_->blockSignals(false);

    refreshImageMatches();
    updateDataInfo();
    updateDataEmptyState();
    updateActionStates();
    updateRecordLabel();
    dataDock_->show();
    dataDock_->raise();

    // 版面 (文字的 {欄位}、圖片的比對欄位) 用到、但這份 Excel 沒有的欄位 -> 提醒使用者改選
    QStringList missing;
    for (const QString& f : layoutFields())
        if (!data_.fields.contains(f)) missing << QStringLiteral("「%1」").arg(f);
    if (!missing.isEmpty()) {
        QMessageBox::information(this, tr("版面用到的欄位不在這份 Excel"),
            tr("目前版面用到、但這份 Excel 沒有的欄位：\n%1\n\n這些內容會維持原樣顯示。\n"
                "請把文字內的 {欄位} 改成 Excel 有的欄位 (可用「插入欄位」)，或重新選擇圖片的「比對欄位」。")
            .arg(missing.join(QStringLiteral("、"))));
    }

    if (data_.rows.isEmpty()) {
        QMessageBox::information(this, tr("沒有資料"), tr("已讀取欄位，但標題列下面沒有任何資料列。"));
        return true;
    }
    dataTable_->setCurrentCell(0, 0);
    applyDataRow(0);                       // 重新載入時選取列可能沒變，所以明確套用第一筆
    statusBar()->showMessage(tr("已載入 %1：共 %2 筆").arg(QFileInfo(path).fileName()).arg(data_.rows.size()), 6000);
    return true;
}

// ================================================== 圖片 (每個圖片元素各自設定) ===

// 左邊資料表的「圖片」欄 / 右邊的統計文字，顯示哪個圖片元素的比對結果：
// 選取的動態圖片；沒有選取動態圖片時，用版面中第一個動態圖片。
const card::Element* DesignerWindow::imageTarget() {
    if (ElementItem* it = selectedItem()) {
        const card::Element* e = layout_.find(it->id());
        if (e && e->type == card::ElementType::Image && e->imageSource == card::ImageSource::Dynamic) return e;
    }
    for (const auto& e : layout_.elements)
        if (e.type == card::ElementType::Image && e.imageSource == card::ImageSource::Dynamic) return &e;
    return nullptr;
}

// 把「選取的圖片元素」的資料夾設成 dir
void DesignerWindow::setImageFolder(const QString& dir) {
    ElementItem* it = selectedItem();
    const card::Element* cur = it ? layout_.find(it->id()) : nullptr;
    if (!cur || cur->type != card::ElementType::Image) return;
    const QString d = dir.trimmed().isEmpty() ? QString() : QDir::toNativeSeparators(dir.trimmed());
    if (!d.isEmpty() && !QDir(d).exists()) {
        QMessageBox::warning(this, tr("資料夾不存在"), d);
        populatePanel(cur);                // 輸入框還原成目前的值
        return;
    }
    editSelected([&](card::Element& e) { e.imageFolder = d; });
    if (!d.isEmpty())
        QSettings(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner")).setValue(QStringLiteral("lastImageFolder"), d);
    onImageSourceChanged();
}

// 來源 / 資料夾 / 比對欄位改變後：掃描新的資料夾、重新比對、更新預覽與面板
void DesignerWindow::onImageSourceChanged() {
    imagePool_.ensureAll(layout_);
    refreshImageMatches();                 // 內含 applyPreviewRecord()
    if (ElementItem* it = selectedItem()) populatePanel(layout_.find(it->id()));
}

// 舊版是整個程式共用一個圖片資料夾 / 比對欄位 (存在設定檔)；現在每個圖片元素各自設定。
// 資料夾或欄位是空的動態圖片，沿用上次使用的設定，舊版面檔 / 舊設定就不需要重新設定。
void DesignerWindow::migrateLegacyImageSettings(card::CardLayout& lay) {
    QSettings st(QStringLiteral("EntrustTest"), QStringLiteral("CardDesigner"));
    const QString folder = st.value(QStringLiteral("lastImageFolder"), st.value(QStringLiteral("imageFolder"))).toString();
    const QString field = st.value(QStringLiteral("lastMatchField"), st.value(QStringLiteral("matchField"), QStringLiteral("姓名"))).toString();
    for (auto& e : lay.elements) {
        if (e.type != card::ElementType::Image || e.imageSource != card::ImageSource::Dynamic) continue;
        if (e.imageFolder.trimmed().isEmpty()) e.imageFolder = folder;
        if (e.matchField.trimmed().isEmpty()) e.matchField = field;
    }
}

// 依「目標圖片元素」的資料夾與比對欄位，重算每一筆資料的圖片對應結果，更新表格「圖片」欄、統計文字與預覽
void DesignerWindow::refreshImageMatches(bool force) {
    const card::Element* target = imageTarget();
    const int imgCol = dataTable_->columnCount() - 1;
    const QString folder = target ? target->imageFolder : QString();
    const QString field = target ? target->matchField : QString();
    const card::ImageMatcher* matcher = target ? imagePool_.find(folder) : nullptr;
    const bool hasFolder = matcher != nullptr;

    const QString key = QStringLiteral("%1|%2|%3|%4|%5").arg(target ? target->id : QString(), folder, field)
        .arg(data_.rows.size()).arg(matcher ? matcher->fileCount() : -1);
    if (!force && key == lastImageRefreshKey_) return;       // 條件沒變 (例如只是換選取的元素)：不用重算
    lastImageRefreshKey_ = key;

    // 資料本身重名 (比對欄位內相同的值出現多次)：就算圖片只有一張，也不知道該給誰
    QHash<QString, int> valueCount;
    if (target)
        for (const auto& rec : data_.rows) {
            const QString k = rec.value(field).trimmed().toLower();
            if (!k.isEmpty()) valueCount[k]++;
        }

    int ok = 0, notFound = 0, imgDup = 0, dataDup = 0, noValue = 0;
    dataTable_->setUpdatesEnabled(false);
    for (int r = 0; r < data_.rows.size(); ++r) {
        auto* item = new QTableWidgetItem;
        if (!hasFolder) {
            item->setText(QStringLiteral("—"));
            item->setForeground(QColor(150, 150, 150));
        }
        else {
            const QString value = data_.rows[r].value(field);
            const card::ImageMatch m = matcher->find(value);
            const int same = valueCount.value(value.trimmed().toLower());
            if (m.status == card::ImageMatch::NoValue) {
                item->setText(tr("－ %1 無資料").arg(field));
                item->setBackground(QColor(255, 215, 215));
                ++noValue;
            }
            else if (same > 1) {
                item->setText(tr("⚠ 資料重名 ×%1").arg(same));
                item->setBackground(QColor(255, 215, 170));
                item->setToolTip(tr("比對欄位「%1」有 %2 筆資料的值都是「%3」，無法確定該用哪張圖。").arg(field).arg(same).arg(value));
                ++dataDup;
            }
            else if (m.status == card::ImageMatch::Ambiguous) {
                item->setText(tr("⚠ %1 張符合").arg(m.candidates.size()));
                item->setBackground(QColor(255, 215, 170));
                item->setToolTip(tr("符合的檔案：\n") + m.candidates.join(QLatin1Char('\n')));
                ++imgDup;
            }
            else if (m.status == card::ImageMatch::NotFound) {
                item->setText(tr("✘ 找不到"));
                item->setBackground(QColor(255, 243, 190));
                ++notFound;
            }
            else {
                item->setText(QStringLiteral("✔ ") + m.candidates.value(0));
                item->setToolTip(m.path);
                ++ok;
            }
        }
        dataTable_->setItem(r, imgCol, item);
    }
    dataTable_->setUpdatesEnabled(true);

    // 統計文字 (顯示在右邊屬性面板的圖片區塊)
    QString s;
    if (!target) {
        s.clear();
    }
    else if (folder.trimmed().isEmpty()) {
        s = tr("尚未設定圖片資料夾。");
    }
    else if (!hasFolder) {
        s = tr("找不到資料夾：%1").arg(folder.trimmed());
    }
    else {
        s = tr("資料夾內有 %1 張圖片。").arg(matcher->fileCount());
        if (data_.rows.isEmpty()) {
            s += tr("載入資料後會顯示每一筆的比對結果。");
        }
        else {
            s += tr("\n以「%1」比對 %2 筆：✔ 可用 %3").arg(field).arg(data_.rows.size()).arg(ok);
            if (notFound) s += tr("、✘ 找不到 %1").arg(notFound);
            if (imgDup) s += tr("、⚠ 圖片重名 %1").arg(imgDup);
            if (dataDup) s += tr("、⚠ 資料重名 %1").arg(dataDup);
            if (noValue) s += tr("、欄位無資料 %1").arg(noValue);
            if (imgDup || dataDup) s += tr("\n重名的資料不會被當成「可用」，建議改用較不會重複的欄位 (例如學號)。");
        }
    }
    if (imageSummary_) imageSummary_->setText(s);

    applyPreviewRecord();                  // 目前預覽的那一筆也要換圖
}

void DesignerWindow::applyDataRow(int row) {
    if (row < 0 || row >= data_.rows.size()) return;
    const card::Record& rec = data_.rows[row];

    usePreviewCheck_->setChecked(true);
    for (const auto& pr : previewEdits_) {                                           // 欄位很多時，不要每個欄位都重畫一次
        QSignalBlocker b(pr.second);
        pr.second->setText(rec.value(pr.first));
    }
    applyPreviewRecord();
    updateRecordLabel();

    QString label;                                                                   // 狀態列辨識用：版面有用到、且有值的第一個欄位
    const QStringList used = layoutFields();
    for (const QString& f : data_.fields)
        if (used.contains(f) && !rec.value(f).isEmpty()) { label = rec.value(f); break; }
    if (label.isEmpty()) label = rec.value(data_.fields.value(0));
    statusBar()->showMessage(tr("預覽第 %1 / %2 筆：%3")
        .arg(row + 1).arg(data_.rows.size()).arg(label), 8000);
}

void DesignerWindow::stepRecord(int delta) {
    const int n = dataTable_->rowCount();
    if (n == 0) return;
    int i = dataTable_->currentRow();
    if (i < 0) i = delta > 0 ? -1 : n;
    for (i += delta; i >= 0 && i < n; i += delta) {
        if (!dataTable_->isRowHidden(i)) {          // 略過被搜尋條件濾掉的列
            dataTable_->setCurrentCell(i, 0);
            return;
        }
    }
}

void DesignerWindow::applyDataFilter(const QString& text) {
    const QString key = text.trimmed();
    for (int r = 0; r < dataTable_->rowCount(); ++r) {
        bool match = key.isEmpty();
        for (int c = 0; !match && c < dataTable_->columnCount(); ++c) {
            const QTableWidgetItem* it = dataTable_->item(r, c);
            if (it && it->text().contains(key, Qt::CaseInsensitive)) match = true;
        }
        dataTable_->setRowHidden(r, !match);
    }
    updateDataInfo();
}

void DesignerWindow::updateDataInfo() {
    if (data_.rows.isEmpty() && data_.path.isEmpty()) return;
    int visible = 0;
    for (int r = 0; r < dataTable_->rowCount(); ++r) if (!dataTable_->isRowHidden(r)) ++visible;
    QString s = tr("%1\n共 %2 筆").arg(QFileInfo(data_.path).fileName()).arg(data_.rows.size());
    if (visible != data_.rows.size()) s += tr("，符合 %1 筆").arg(visible);
    if (data_.skippedBlankRows > 0) s += tr("（略過 %1 列空白）").arg(data_.skippedBlankRows);
    int incomplete = 0;
    const QStringList used = layoutFields();                                  // 只檢查版面真的用到的欄位
    for (const auto& rec : data_.rows) {
        for (const QString& f : used)
            if (data_.fields.contains(f) && rec.value(f).isEmpty()) { ++incomplete; break; }
    }
    if (incomplete > 0) s += tr("\n⚠ %1 筆有版面用到的欄位缺資料 (標紅)").arg(incomplete);
    dataInfo_->setText(s);
}

// ============================================================ 鍍膜範圍 (階段 4) ===

namespace {
    QString regionLabel(const card::TopcoatRegion& r, double cardHMm) {   // 座標以卡面左下角為原點
        return QStringLiteral("%1  (%2, %3)  %4 × %5 mm")
            .arg(RegionItem::kindText(r.kind))
            .arg(r.xMm, 0, 'f', 1).arg(card::toUiY(r.yMm, r.hMm, cardHMm), 0, 'f', 1).arg(r.wMm, 0, 'f', 1).arg(r.hMm, 0, 'f', 1);
    }
}

QWidget* DesignerWindow::buildTopcoatPanel() {
    auto* w = new QWidget;
    w->setMinimumWidth(uiscale::px(300));
    topcoatPanel_ = w;
    auto* root = new QVBoxLayout(w);

    // --- 基本設定：PrintTicket ---
    auto* baseBox = new QGroupBox(tr("鍍膜基本設定 (PrintTicket)"));
    auto* bf = new QFormLayout(baseBox);
    topBaseCombo_ = new QComboBox;
    topBaseCombo_->addItems({ tr("沿用驅動程式設定"), tr("全卡鍍膜"), tr("全卡鍍膜，但避開…"), tr("完全不鍍膜") });   // 順序 = card::TopcoatBase 的值
    topExcCombo_ = new QComboBox;
    topExcCombo_->addItems({ tr("晶片 (ISO 7816)"), tr("磁條 JIS"), tr("磁條 ISO 2 軌"), tr("磁條 ISO 3 軌") });
    bf->addRow(tr("鍍膜"), topBaseCombo_);
    bf->addRow(tr("避開"), topExcCombo_);
    root->addWidget(baseBox);

    // --- 額外範圍：Escape ---
    auto* regBox = new QGroupBox(tr("鍍膜額外範圍 (Escape)"));
    auto* rl = new QVBoxLayout(regBox);
    auto* hint = new QLabel(tr("三種範圍：\n"
        "• 鍍膜區：只在範圍內鍍膜 (範圍外不鍍)\n"
        "• 移除鍍膜：範圍內不鍍膜\n"
        "• 列印阻擋：範圍內不列印\n"
        "上方基本設定選「完全不鍍膜」時，鍍膜區與移除鍍膜會被忽略，只有列印阻擋有效。\n\n"
        "怎麼調整：按「新增範圍」或在下方清單選取，會自動進入編輯模式，之後在卡面拖曳移動、"
        "拖右下角方塊調整大小 (座標以卡面左下角為原點，單位 mm)。\n"
        "點選卡面上的元素就會離開編輯模式，回到一般設計。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#666;"));
    rl->addWidget(hint);

    regionEditCheck_ = new QCheckBox(tr("編輯鍍膜範圍 (開啟時只能操作範圍；關閉時範圍不會擋到卡面設計)"));
    regionEditCheck_->setToolTip(tr("平常請保持關閉：鍍膜範圍只顯示虛線外框，滑鼠會直接穿透到卡面元素。\n新增範圍或從下方清單選取範圍時會自動開啟。"));
    rl->addWidget(regionEditCheck_);
    connect(regionEditCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (actRegionEdit_ && actRegionEdit_->isChecked() != on) actRegionEdit_->setChecked(on);
        });

    regionList_ = new QListWidget;
    regionList_->setMinimumHeight(uiscale::px(90));
    rl->addWidget(regionList_);
    auto* btnRow = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("新增範圍"));
    auto* delBtn = new QPushButton(tr("刪除範圍"));
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch(1);
    rl->addLayout(btnRow);

    regionEditBox_ = new QGroupBox(tr("選取的範圍"));
    auto* ef = new QFormLayout(regionEditBox_);
    regionKindCombo_ = new QComboBox;
    regionKindCombo_->addItems({ tr("鍍膜區"), tr("移除鍍膜"), tr("列印阻擋") });   // 順序 = RegionKind 的值
    rxSpin_ = makeMmSpin(-100, 300);
    rySpin_ = makeMmSpin(-100, 300);
    rwSpin_ = makeMmSpin(0.5, 300);
    rhSpin_ = makeMmSpin(0.5, 300);
    ef->addRow(tr("種類"), regionKindCombo_);
    ef->addRow(tr("X 位置"), rxSpin_);
    ef->addRow(tr("Y 位置"), rySpin_);
    ef->addRow(tr("寬度"), rwSpin_);
    ef->addRow(tr("高度"), rhSpin_);
    rl->addWidget(regionEditBox_);
    root->addWidget(regBox);

    // --- 將送出的指令 ---
    root->addWidget(new QLabel(tr("將送給印表機的設定")));
    topcoatPreview_ = new QPlainTextEdit;
    topcoatPreview_->setReadOnly(true);
    topcoatPreview_->setMaximumHeight(150);
    topcoatPreview_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    root->addWidget(topcoatPreview_);
    root->addStretch(1);

    // ---- 連線 ----
    connect(topBaseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_) return;
        layout_.topcoat.base = static_cast<card::TopcoatBase>(i);
        topExcCombo_->setEnabled(i == int(card::TopcoatBase::Except));
        refreshTopcoatPreview();
        scheduleCommit();
        });
    connect(topExcCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_) return;
        layout_.topcoat.exception = static_cast<card::TopcoatException>(i);
        refreshTopcoatPreview();
        scheduleCommit();
        });
    connect(addBtn, &QPushButton::clicked, this, [this] { addRegion(); });
    connect(delBtn, &QPushButton::clicked, this, [this] { deleteSelectedRegion(); });
    connect(regionList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (updating_) return;
        const QString id = row >= 0 ? regionList_->item(row)->data(Qt::UserRole).toString() : QString();
        populateRegionEditor(layout_.topcoat.find(id));
        if (!id.isEmpty() && actRegionEdit_ && !actRegionEdit_->isChecked()) actRegionEdit_->setChecked(true);   // 從清單選範圍 = 要編輯它
        if (RegionItem* it = findRegionItem(id)) {
            scene_->clearSelection();
            it->setSelected(true);                 // 範圍被隱藏時無法選取，編輯欄位仍可使用
        }
        updateActionStates();
        });
    connect(regionKindCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        editSelectedRegion([&](card::TopcoatRegion& r) { r.kind = static_cast<card::RegionKind>(i); });
        });
    auto hook = [this](QDoubleSpinBox* sp, std::function<void(card::TopcoatRegion&, double)> fn) {
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, fn](double v) {
            editSelectedRegion([&](card::TopcoatRegion& r) { fn(r, v); });
            });
        };
    hook(rxSpin_, [](card::TopcoatRegion& r, double v) { r.xMm = v; });
    hook(rySpin_, [this](card::TopcoatRegion& r, double v) { r.yMm = card::fromUiY(v, r.hMm, layout_.heightMm()); });
    hook(rwSpin_, [](card::TopcoatRegion& r, double v) { r.wMm = v; });
    hook(rhSpin_, [this](card::TopcoatRegion& r, double v) {   // 底邊不動，往上長高
        const double bottom = layout_.heightMm() - (r.yMm + r.hMm);
        r.hMm = v;
        r.yMm = layout_.heightMm() - bottom - v;
        });

    populateRegionEditor(nullptr);
    return w;
}

RegionItem* DesignerWindow::createRegionItem(const QString& id, int z) {
    auto* it = new RegionItem(&layout_, id, [this](const QString& changedId) { onRegionItemChanged(changedId); }, cardItem_);
    it->setZValue(z);
    it->setGuideSink([this](const std::vector<card::Guide>& g) { if (guides_) guides_->setGuides(g); });
    it->setVisible(actShowRegions_ ? actShowRegions_->isChecked() : true);
    return it;
}

RegionItem* DesignerWindow::findRegionItem(const QString& id) const {
    if (!cardItem_) return nullptr;
    for (QGraphicsItem* gi : cardItem_->childItems()) {
        if (gi->type() == RegionItem::Type) {
            auto* it = static_cast<RegionItem*>(gi);
            if (it->id() == id) return it;
        }
    }
    return nullptr;
}

QString DesignerWindow::currentRegionId() const {
    if (!regionList_ || !regionList_->currentItem()) return QString();
    return regionList_->currentItem()->data(Qt::UserRole).toString();
}

void DesignerWindow::addRegion() {
    if (actRegionEdit_ && !actRegionEdit_->isChecked()) actRegionEdit_->setChecked(true);   // 新增範圍 = 要編輯它，自動進入編輯模式
    commitUndo();
    card::TopcoatRegion r;
    r.id = layout_.topcoat.nextId();
    r.kind = card::RegionKind::Remove;
    r.wMm = qMin(r.wMm, layout_.widthMm());
    r.hMm = qMin(r.hMm, layout_.heightMm());
    r.xMm = qMax(0.0, (layout_.widthMm() - r.wMm) / 2.0);
    r.yMm = qMax(0.0, (layout_.heightMm() - r.hMm) / 2.0);
    layout_.topcoat.regions.append(r);
    createRegionItem(r.id, 10000 + int(layout_.topcoat.regions.size()));
    refreshRegionList();
    syncRegionListSelection(r.id);
    populateRegionEditor(&layout_.topcoat.regions.last());
    if (RegionItem* it = findRegionItem(r.id)) { scene_->clearSelection(); it->setSelected(true); }
    refreshTopcoatPreview();
    updateActionStates();
    commitUndo();
}

void DesignerWindow::deleteSelectedRegion() {
    QString id;
    if (RegionItem* it = selectedRegion()) id = it->id();
    if (id.isEmpty()) id = currentRegionId();
    if (id.isEmpty()) return;
    commitUndo();

    updating_ = true;
    if (RegionItem* it = findRegionItem(id)) delete it;
    for (int i = 0; i < layout_.topcoat.regions.size(); ++i)
        if (layout_.topcoat.regions[i].id == id) { layout_.topcoat.regions.removeAt(i); break; }
    updating_ = false;

    refreshRegionList();
    populateRegionEditor(nullptr);
    refreshTopcoatPreview();
    updateActionStates();
    commitUndo();
}

void DesignerWindow::refreshTopcoatPanel() {
    const bool prev = updating_;
    updating_ = true;
    topBaseCombo_->setCurrentIndex(int(layout_.topcoat.base));
    topExcCombo_->setCurrentIndex(int(layout_.topcoat.exception));
    topExcCombo_->setEnabled(layout_.topcoat.base == card::TopcoatBase::Except);
    updating_ = prev;
    refreshRegionList();
    populateRegionEditor(nullptr);
    refreshTopcoatPreview();
}

void DesignerWindow::refreshRegionList() {
    const bool prev = updating_;
    updating_ = true;
    regionList_->clear();
    for (const auto& r : layout_.topcoat.regions) {
        auto* li = new QListWidgetItem(regionLabel(r, layout_.heightMm()));
        li->setData(Qt::UserRole, r.id);
        li->setForeground(RegionItem::kindColor(r.kind).darker(130));
        regionList_->addItem(li);
    }
    updating_ = prev;
}

void DesignerWindow::refreshRegionListLabel(const card::TopcoatRegion& r) {
    for (int i = 0; i < regionList_->count(); ++i) {
        QListWidgetItem* li = regionList_->item(i);
        if (li->data(Qt::UserRole).toString() == r.id) {
            li->setText(regionLabel(r, layout_.heightMm()));
            li->setForeground(RegionItem::kindColor(r.kind).darker(130));
            break;
        }
    }
}

void DesignerWindow::syncRegionListSelection(const QString& id) {
    const bool prev = updating_;
    updating_ = true;
    regionList_->setCurrentRow(-1);
    for (int i = 0; i < regionList_->count(); ++i)
        if (regionList_->item(i)->data(Qt::UserRole).toString() == id) { regionList_->setCurrentRow(i); break; }
    updating_ = prev;
}

void DesignerWindow::populateRegionEditor(const card::TopcoatRegion* r) {
    const bool prev = updating_;
    updating_ = true;
    regionEditBox_->setEnabled(r != nullptr);
    if (r) {
        regionKindCombo_->setCurrentIndex(int(r->kind));
        rxSpin_->setValue(r->xMm);
        rySpin_->setValue(card::toUiY(r->yMm, r->hMm, layout_.heightMm()));
        rwSpin_->setValue(r->wMm);
        rhSpin_->setValue(r->hMm);
    }
    updating_ = prev;
}

void DesignerWindow::editSelectedRegion(const std::function<void(card::TopcoatRegion&)>& fn) {
    if (updating_) return;
    QString id;
    if (RegionItem* it = selectedRegion()) id = it->id();
    if (id.isEmpty()) id = currentRegionId();
    card::TopcoatRegion* r = layout_.topcoat.find(id);
    if (!r) return;
    fn(*r);
    if (RegionItem* it = findRegionItem(id)) { it->syncFromModel(); it->update(); }
    refreshRegionListLabel(*r);
    refreshTopcoatPreview();
    scheduleCommit();
}

void DesignerWindow::onRegionItemChanged(const QString& id) {
    if (updating_) return;
    scheduleCommit();
    if (const card::TopcoatRegion* r = layout_.topcoat.find(id)) {
        refreshRegionListLabel(*r);
        populateRegionEditor(r);               // 拖曳 / 縮放時同步更新數字
    }
    refreshTopcoatPreview();
}

void DesignerWindow::refreshRegionItemsView() {
    if (!cardItem_) return;
    const bool editing = !RegionItem::locked();
    for (QGraphicsItem* gi : cardItem_->childItems()) {
        if (gi->type() == RegionItem::Type) {
            if (!editing && gi->isSelected()) gi->setSelected(false);          // 離開編輯模式：不留範圍的選取框
            gi->update();
        }
        else if (gi->type() == ElementItem::Type) {
            gi->setAcceptedMouseButtons(editing ? Qt::NoButton : Qt::AllButtons);   // 編輯範圍時，元素不接受滑鼠
            if (editing && gi->isSelected()) gi->setSelected(false);
        }
    }
}

void DesignerWindow::refreshTopcoatPreview() {
    const ::TopcoatSpec spec = card::toTopcoatSpec(layout_.topcoat);
    QString s;
    if (spec.isDefault()) {
        s = tr("不送出任何鍍膜設定 (完全沿用印表機驅動程式目前的設定)。");
    }
    else {
        s += tr("PrintTicket：") + QString::fromStdWString(topcoat::describeBase(spec));
        const auto esc = topcoat::escapes(spec);
        if (esc.empty()) s += tr("\nEscape：(無)");
        for (const auto& e : esc) s += QStringLiteral("\n") + QString::fromStdWString(e);
    }
    if (spec.base == TopcoatSpec::Base::None) {
        s += tr("\n\n完全不鍍膜：PrintTicket 不送出，改以上面的 ~TA 指令關閉整面鍍膜。");
        if (!spec.addAreas.empty() || !spec.removeAreas.empty())
            s += tr("\n⚠ 鍍膜區 / 移除鍍膜在此模式下不會送出 (只有列印阻擋有效)。");
    }
    if (layout_.orientation == card::Orientation::Portrait && !spec.isDefault())
        s += tr("\n\n直式卡面：以上為設計座標，列印時會依旋轉方向換算成橫式座標。");

    bool outside = false;
    for (const auto& r : layout_.topcoat.regions)
        if (r.xMm < 0 || r.yMm < 0 || r.xMm + r.wMm > layout_.widthMm() + 0.01 || r.yMm + r.hMm > layout_.heightMm() + 0.01) outside = true;
    if (outside) s += tr("\n\n⚠ 有範圍超出卡面。");
    topcoatPreview_->setPlainText(s);
}

// ============================================================ panel / list ===

void DesignerWindow::updateColorButton(const QColor& c) {
    colorBtn_->setStyleSheet(
        QStringLiteral("background-color:%1; border:1px solid #888; min-height:%2px;").arg(c.name()).arg(uiscale::px(22)));
}

// ============================================================ 介面大小 / 視窗 ===

void DesignerWindow::applyUiScale() {
    using uiscale::px;
    if (rulerH_) rulerH_->applyScale();
    if (rulerV_) rulerV_->applyScale();
    if (cornerLabel_ && rulerH_) {
        cornerLabel_->setFixedSize(rulerH_->thickness(), rulerH_->thickness());
        cornerLabel_->setStyleSheet(QStringLiteral("background:#dedede; color:#555; font-size:%1px; border-right:1px solid #a0a0a0; border-bottom:1px solid #a0a0a0;").arg(px(9)));
    }
    for (QToolBar* tb : findChildren<QToolBar*>()) {
        tb->setStyleSheet(toolbarStyle());
        if (QToolButton* pb = tb->findChild<QToolButton*>(QStringLiteral("btnPrint"))) pb->setStyleSheet(printButtonStyle());
    }
    for (QToolButton* b : { layerUpBtn_, layerDownBtn_ }) if (b) b->setFixedWidth(px(30));
    if (cursorLabel_) cursorLabel_->setMinimumWidth(px(260));
    if (recordLabel_) recordLabel_->setMinimumWidth(px(110));
    if (propertyPanel_) propertyPanel_->setMinimumWidth(px(300));
    if (topcoatPanel_) topcoatPanel_->setMinimumWidth(px(300));
    if (backgroundPanel_) backgroundPanel_->setMinimumWidth(px(300));
    if (textEdit_) textEdit_->setFixedHeight(px(64));
    if (regionList_) regionList_->setMinimumHeight(px(90));
    if (colorBtn_) {
        if (ElementItem* it = selectedItem()) if (const card::Element* e = layout_.find(it->id())) updateColorButton(e->color);
    }
    // 「介面大小」選單：勾選目前的設定；「自動」旁邊顯示目前實際套用的比例
    for (QAction* a : scaleActs_) {
        const int p = a->data().toInt();
        a->setChecked(p == uiscale::state().stored);
        if (p == 0) a->setText(tr("自動 (依螢幕大小，目前 %1%)").arg(uiscale::autoPercent()));
    }
    uiscale::clampToScreen(this);
    applyDockSizes();
    if (isVisible()) QTimer::singleShot(0, this, [this] { fitView(); });
    if (statusBar() && isVisible()) statusBar()->showMessage(tr("介面大小：%1%").arg(uiscale::state().percent), 4000);
}

void DesignerWindow::fitWindowToScreen() {
    if (isFullScreen() || isMaximized()) showNormal();
    resetDockLayout();
    uiscale::fitWindowToScreen(this, 1360, 840);
    QTimer::singleShot(0, this, [this] { applyDockSizes(); fitView(); });
}

void DesignerWindow::populatePanel(const card::Element* e) {
    const bool prev = updating_;
    updating_ = true;

    propertyPanel_->setEnabled(e != nullptr);
    propertyPanel_->setVisible(e != nullptr);          // 沒選取時隱藏欄位，改顯示說明
    if (propHint_) propHint_->setVisible(e == nullptr);
    if (e) {
        nameEdit_->setText(e->name);
        typeLabel_->setText(typeText(*e));
        xSpin_->setValue(e->xMm);
        ySpin_->setValue(card::toUiY(e->yMm, e->hMm, layout_.heightMm()));
        wSpin_->setValue(e->wMm);
        hSpin_->setValue(e->hMm);
        const bool isText = e->type == card::ElementType::Text;
        fitCheck_->setText(tr("貼合文字 (框的大小跟隨文字)"));
        fitCheck_->setVisible(isText);                      // 圖片的「框高依圖片比例」放在圖片區塊的「顯示方式」
        fitCheck_->setChecked(isText && e->fitContent);
        wSpin_->setEnabled(!(e->fitContent && isText));     // 貼合時，由內容決定的尺寸不能手動改
        hSpin_->setEnabled(!e->fitContent);                 // 文字貼合 / 圖片「框高依圖片比例」時，高度由內容決定

        textGroup_->setVisible(e->type == card::ElementType::Text);
        imageGroup_->setVisible(e->type == card::ElementType::Image);
        barcodeGroup_->setVisible(e->type == card::ElementType::Barcode || e->type == card::ElementType::QRCode);

        if (textEdit_->toPlainText() != e->text) textEdit_->setPlainText(e->text);
        fontCombo_->setCurrentFont(QFont(e->fontFamily));
        sizeSpin_->setValue(e->fontPt);
        boldCheck_->setChecked(e->bold);
        italicCheck_->setChecked(e->italic);
        alignCombo_->setCurrentIndex(static_cast<int>(e->align));
        updateColorButton(e->color);

        if (e->type == card::ElementType::Barcode || e->type == card::ElementType::QRCode) {
            const bool qr = e->type == card::ElementType::QRCode;
            barcodeGroup_->setTitle(qr ? tr("QR Code") : tr("條碼"));
            barcodeTypeBox_->setVisible(!qr);
            barcodeHint_->setVisible(!qr);
            qrEccBox_->setVisible(qr);
            qrEccCombo_->setCurrentIndex(qBound(0, e->qrEcc, 3));
            const bool dyn = e->barcodeSource == card::ImageSource::Dynamic;
            barcodeSourceCombo_->setCurrentIndex(dyn ? 1 : 0);
            barcodeTypeCombo_->setCurrentIndex(static_cast<int>(e->barcodeType));
            barcodeFixedBox_->setVisible(!dyn);
            barcodeDynamicBox_->setVisible(dyn);
            barcodeValueEdit_->setText(e->barcodeValue);
            int fi = barcodeFieldCombo_->findText(e->barcodeField);
            barcodeFieldCombo_->setCurrentIndex(fi);
        }
        if (e->type == card::ElementType::Image) {
            const bool dyn = e->imageSource == card::ImageSource::Dynamic;
            imageSourceCombo_->setCurrentIndex(dyn ? 1 : 0);
            imageFixedBox_->setVisible(!dyn);
            imageDynBox_->setVisible(dyn);
            if (imageEdit_->text() != e->imagePath) imageEdit_->setText(e->imagePath);
            if (imageFolderEdit_->text() != e->imageFolder) imageFolderEdit_->setText(e->imageFolder);
            matchFieldCombo_->setCurrentIndex(matchFieldCombo_->findText(e->matchField));   // 找不到 (-1) = 空白，請使用者選
            imageFitCombo_->setCurrentIndex(static_cast<int>(e->imageFit));
            imageZoomSpin_->setValue(e->imageZoomPct);
            imageFocusXSpin_->setValue(e->imageFocusX);
            imageFocusYSpin_->setValue(e->imageFocusY);
            const bool crop = e->imageFit == card::ImageFit::Cover || e->imageFit == card::ImageFit::Contain;
            imageZoomSpin_->setEnabled(crop);               // 拉伸 / 框高依圖片比例時，縮放與位置沒有作用
            imageFocusXSpin_->setEnabled(crop);
            imageFocusYSpin_->setEnabled(crop);
        }
    }
    else {
        nameEdit_->clear();
        typeLabel_->clear();
        textGroup_->setVisible(false);
        imageGroup_->setVisible(false);
        barcodeGroup_->setVisible(false);
    }
    updating_ = prev;
}

void DesignerWindow::refreshList() {
    const bool prev = updating_;
    updating_ = true;
    elementList_->clear();
    for (int i = layout_.elements.size() - 1; i >= 0; --i) {      // 清單由上到下 = 最上層到最下層 (與卡面疊放順序一致)
        const auto& e = layout_.elements[i];
        auto* li = new QListWidgetItem(listLabel(e));
        li->setData(Qt::UserRole, e.id);
        elementList_->addItem(li);
    }
    updating_ = prev;
}

void DesignerWindow::refreshListName(const card::Element& e) {
    for (int i = 0; i < elementList_->count(); ++i) {
        QListWidgetItem* li = elementList_->item(i);
        if (li->data(Qt::UserRole).toString() == e.id) { li->setText(listLabel(e)); break; }
    }
}

void DesignerWindow::syncListSelection(const QString& id) {
    const bool prev = updating_;
    updating_ = true;
    elementList_->setCurrentRow(-1);
    for (int i = 0; i < elementList_->count(); ++i) {
        if (elementList_->item(i)->data(Qt::UserRole).toString() == id) { elementList_->setCurrentRow(i); break; }
    }
    updating_ = prev;
}

void DesignerWindow::onSelectionChanged() {
    if (updating_) return;
    ElementItem* it = selectedItem();
    RegionItem* rg = selectedRegion();
    syncListSelection(it ? it->id() : QString());
    populatePanel(it ? layout_.find(it->id()) : nullptr);
    refreshImageMatches(false);            // 「圖片」欄顯示目前選取的動態圖片的比對結果 (條件沒變就不重算)
    if (it && propertyDock_->isVisible()) propertyDock_->raise();
    if (rg) {
        if (actRegionEdit_ && !actRegionEdit_->isChecked()) actRegionEdit_->setChecked(true);
        syncRegionListSelection(rg->id());
        populateRegionEditor(layout_.topcoat.find(rg->id()));
        topcoatDock_->show();
        topcoatDock_->raise();
    }
    else if (it) {
        if (actRegionEdit_ && actRegionEdit_->isChecked()) actRegionEdit_->setChecked(false);   // 選了元素 = 回到設計模式
        syncRegionListSelection(QString());
        populateRegionEditor(nullptr);
    }
    updateActionStates();
    updateHints();
}

void DesignerWindow::onItemChanged(const QString& id) {
    if (updating_) return;
    scheduleCommit();
    if (card::Element* fe = layout_.find(id)) {                 // 拖曳 / 縮放後，貼合內容的元素把大小拉回貼合值
        if (fe->fitContent && card::fitElement(*fe, previewRecord_))
            if (ElementItem* fi = findItem(id)) fi->syncFromModel();
    }
    ElementItem* it = selectedItem();
    if (it && it->id() == id) populatePanel(layout_.find(id));    // 拖曳/縮放時，同步更新面板數字
}

void DesignerWindow::editSelected(const std::function<void(card::Element&)>& fn) {
    if (updating_) return;
    ElementItem* it = selectedItem();
    if (!it) return;
    card::Element* e = layout_.find(it->id());
    if (!e) return;
    fn(*e);
    const bool fitted = e->fitContent && card::fitElement(*e, previewRecord_);   // 文字 / 字型 / 圖片等改變後立即貼合
    it->syncFromModel();
    it->update();
    refreshListName(*e);
    if (fitted) populatePanel(e);                                // 同步面板上的 X / Y / 寬 / 高
    scheduleCommit();
}

// ============================================================ 游標位置 / 卡面尺寸 ===

void DesignerWindow::updateCursorLabel(const QPointF& scenePos, bool inside) {
    if (!cursorLabel_) return;
    QString s = tr("卡面 %1 × %2 mm").arg(layout_.widthMm(), 0, 'f', 1).arg(layout_.heightMm(), 0, 'f', 1);
    if (inside)
        s += tr("　｜　游標 X %1  Y %2 mm")
        .arg(scenePos.x() / ElementItem::kPxPerMm, 0, 'f', 1)
        .arg(card::flipY(scenePos.y() / ElementItem::kPxPerMm, layout_.heightMm()), 0, 'f', 1);   // 原點在左下角
    cursorLabel_->setText(s);
}

// ============================================================ 復原 / 重做 ===
// 做法：保留「上一次記錄時的整份版面」(baseline_)。每次操作結束後，若目前版面與 baseline_ 不同，
// 就把 baseline_ 放進復原堆疊。
//  - 新增 / 刪除 / 改方向 / 開啟版面：動作前後各 commitUndo() 一次，各自成為一步。
//  - 拖曳、縮放、輸入數字、打字：每次變動只呼叫 scheduleCommit()，停手 0.5 秒 (且滑鼠已放開)
//    才記成一步，所以一次拖曳 = 一步，不會變成幾百步。

void DesignerWindow::commitUndo() {
    if (commitTimer_) commitTimer_->stop();
    if (layout_.toJson() == baseline_.toJson()) { updateWindowTitle(); return; }
    undoStack_.push_back(baseline_);
    if (undoStack_.size() > kMaxUndo) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
    baseline_ = layout_;
    updateUndoActions();
    updateWindowTitle();
}

void DesignerWindow::scheduleCommit() {
    if (updating_ || !commitTimer_) return;
    commitTimer_->start();                 // 重新計時
    if (actUndo_) actUndo_->setEnabled(true);
    setWindowModified(true);               // 先標示「有修改」，停手後 commitUndo 會再精確比對一次
}

void DesignerWindow::updateUndoActions() {
    if (actUndo_) actUndo_->setEnabled(!undoStack_.empty());
    if (actRedo_) actRedo_->setEnabled(!redoStack_.empty());
}

void DesignerWindow::undo() {
    commitUndo();                          // 先把還沒記錄的編輯收成一步，Ctrl+Z 才會退回「剛剛那個動作」
    if (undoStack_.empty()) return;
    redoStack_.push_back(layout_);
    const card::CardLayout prev = undoStack_.back();
    undoStack_.pop_back();
    restoreLayout(prev);
    baseline_ = layout_;
    updateUndoActions();
    updateWindowTitle();
    statusBar()->showMessage(tr("已復原 (還可復原 %1 步)").arg(undoStack_.size()), 3000);
}

void DesignerWindow::redo() {
    commitUndo();
    if (redoStack_.empty()) return;
    undoStack_.push_back(layout_);
    const card::CardLayout next = redoStack_.back();
    redoStack_.pop_back();
    restoreLayout(next);
    baseline_ = layout_;
    updateUndoActions();
    updateWindowTitle();
    statusBar()->showMessage(tr("已重做 (還可重做 %1 步)").arg(redoStack_.size()), 3000);
}

void DesignerWindow::restoreLayout(const card::CardLayout& l) {
    // 記住目前選取的元素 / 範圍，還原後選回來
    QString selElement, selRegion;
    if (ElementItem* it = selectedItem()) selElement = it->id();
    else if (RegionItem* rg = selectedRegion()) selRegion = rg->id();

    const bool orientationChanged = l.orientation != layout_.orientation;
    layout_ = l;
    rebuildScene(orientationChanged);      // 沒換方向就不要重設縮放與捲動位置

    if (!selElement.isEmpty()) {
        if (ElementItem* it = findItem(selElement)) it->setSelected(true);
    }
    else if (!selRegion.isEmpty()) {
        if (RegionItem* rg = findRegionItem(selRegion)) rg->setSelected(true);
    }
}