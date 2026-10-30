#include "DesignerWindow.h"
#include "CardPainter.h"
#include "ElementItem.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

    // Ctrl + 滾輪縮放
    class CardView : public QGraphicsView {
    public:
        using QGraphicsView::QGraphicsView;
    protected:
        void wheelEvent(QWheelEvent* e) override {
            if (e->modifiers() & Qt::ControlModifier) {
                const double f = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
                scale(f, f);
                e->accept();
            }
            else {
                QGraphicsView::wheelEvent(e);
            }
        }
    };

    QDoubleSpinBox* makeMmSpin(double minV, double maxV) {
        auto* s = new QDoubleSpinBox;
        s->setRange(minV, maxV);
        s->setDecimals(1);
        s->setSingleStep(0.5);
        s->setSuffix(QStringLiteral(" mm"));
        return s;
    }

    QString typeText(const card::Element& e) {
        return e.type == card::ElementType::Text ? QStringLiteral("文字") : QStringLiteral("圖片");
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
        if (!tip.isEmpty()) a->setToolTip(tip);
        if (fn) QObject::connect(a, &QAction::triggered, parent, [fn] { fn(); });
        return a;
    }

} // namespace

// ============================================================ construction ===

DesignerWindow::DesignerWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("卡面設計器 (階段 2)"));
    resize(1360, 840);

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
    applyPreviewRecord();
    rebuildScene();
}

void DesignerWindow::showEvent(QShowEvent* ev) {
    QMainWindow::showEvent(ev);
    if (!shownOnce_) {
        shownOnce_ = true;
        // 等視窗版面算好之後再調整面板大小與縮放
        QTimer::singleShot(0, this, [this] {
            applyDockSizes();
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
    setCentralWidget(view_);
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] { onSelectionChanged(); });

    // ---- 面板 (都放進 QDockWidget，可由「檢視」選單重新開啟) ----
    elementDock_ = new QDockWidget(tr("元素清單"), this);
    elementDock_->setObjectName(QStringLiteral("dockElements"));
    elementList_ = new QListWidget;
    elementDock_->setWidget(elementList_);
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

    previewDock_ = new QDockWidget(tr("預覽資料"), this);
    previewDock_->setObjectName(QStringLiteral("dockPreview"));
    previewDock_->setWidget(buildPreviewPanel());

    resetDockLayout();
    buildMenus();
    updateActionStates();

    statusBar()->showMessage(tr("Ctrl+滾輪縮放；拖曳元素移動；選取後拖曳右下角方塊調整大小；Delete / Backspace 刪除選取元素"));
}

// 面板配置：左=元素清單+製卡資料、右=屬性、下=預覽資料
void DesignerWindow::resetDockLayout() {
    for (QDockWidget* d : { elementDock_, dataDock_, propertyDock_, previewDock_ }) {
        d->setFloating(false);
        removeDockWidget(d);
    }
    addDockWidget(Qt::LeftDockWidgetArea, elementDock_);
    splitDockWidget(elementDock_, dataDock_, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, propertyDock_);
    addDockWidget(Qt::BottomDockWidgetArea, previewDock_);
    for (QDockWidget* d : { elementDock_, dataDock_, propertyDock_, previewDock_ }) d->show();
    applyDockSizes();
}

void DesignerWindow::applyDockSizes() {
    resizeDocks({ elementDock_, dataDock_ }, { 220, 420 }, Qt::Vertical);
    resizeDocks({ elementDock_, propertyDock_ }, { 390, 350 }, Qt::Horizontal);
}

// ---- 選單列 + 精簡工具列 ----
void DesignerWindow::buildMenus() {
    // 動作
    auto* actOpenLayout = newAction(this, tr("開啟版面…"), QKeySequence::Open, tr("開啟已儲存的版面檔 (.json)"), [this] { openLayout(); });
    auto* actSaveLayout = newAction(this, tr("儲存版面…"), QKeySequence::Save, tr("把目前的版面存成 .json"), [this] { saveLayout(); });
    auto* actExport = newAction(this, tr("輸出預覽圖 (300dpi)…"), QKeySequence(Qt::CTRL | Qt::Key_E), tr("把目前預覽的卡面輸出成 PNG"), [this] { exportPreview(); });
    auto* actQuit = newAction(this, tr("結束"), QKeySequence::Quit, QString(), [this] { close(); });

    auto* actAddText = newAction(this, tr("新增文字"), QKeySequence(Qt::CTRL | Qt::Key_T), tr("在卡面新增一個文字元素"), [this] { addElement(card::ElementType::Text); });
    auto* actAddImage = newAction(this, tr("新增圖片"), QKeySequence(Qt::CTRL | Qt::Key_I), tr("在卡面新增一個圖片元素"), [this] { addElement(card::ElementType::Image); });

    // 刪除：Delete 與 Backspace 都可以；只在焦點位於「卡面」或「元素清單」時生效，
    // 避免在屬性面板輸入文字、按倒退鍵時誤刪元素。
    actDelete_ = newAction(this, tr("刪除元素"), QKeySequence(), tr("刪除選取的元素 (Delete / Backspace)"), [this] { deleteSelected(); });
    actDelete_->setShortcuts({ QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace) });
    actDelete_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    view_->addAction(actDelete_);
    elementList_->addAction(actDelete_);

    auto* orientGroup = new QActionGroup(this);
    actLandscape_ = orientGroup->addAction(tr("橫式 (85.6 × 54 mm)"));
    actPortrait_ = orientGroup->addAction(tr("直式 (54 × 85.6 mm)"));
    actLandscape_->setCheckable(true);
    actPortrait_->setCheckable(true);
    actLandscape_->setChecked(true);
    connect(actLandscape_, &QAction::triggered, this, [this] { setOrientation(card::Orientation::Landscape); });
    connect(actPortrait_, &QAction::triggered, this, [this] { setOrientation(card::Orientation::Portrait); });

    auto* actLoadData = newAction(this, tr("載入 Excel 製卡資料…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), tr("載入 .xlsx：姓名、學號、系所、內碼、外碼"), [this] { browseData(); });
    actPrevRecord_ = newAction(this, tr("上一筆"), QKeySequence(Qt::ALT | Qt::Key_Left), tr("預覽上一筆資料 (Alt+←)"), [this] { stepRecord(-1); });
    actNextRecord_ = newAction(this, tr("下一筆"), QKeySequence(Qt::ALT | Qt::Key_Right), tr("預覽下一筆資料 (Alt+→)"), [this] { stepRecord(+1); });

    auto* actFit = newAction(this, tr("符合視窗"), QKeySequence(Qt::CTRL | Qt::Key_0), tr("縮放到整張卡剛好放進視窗"), [this] { fitView(); });
    auto* actResetDocks = newAction(this, tr("還原預設面板配置"), QKeySequence(), tr("把所有面板重新開啟並放回預設位置"), [this] { resetDockLayout(); });

    // 選單
    QMenu* mFile = menuBar()->addMenu(tr("檔案(&F)"));
    mFile->addAction(actOpenLayout);
    mFile->addAction(actSaveLayout);
    mFile->addSeparator();
    mFile->addAction(actLoadData);
    mFile->addSeparator();
    mFile->addAction(actExport);
    mFile->addSeparator();
    mFile->addAction(actQuit);

    QMenu* mEdit = menuBar()->addMenu(tr("編輯(&E)"));
    mEdit->addAction(actAddText);
    mEdit->addAction(actAddImage);
    mEdit->addSeparator();
    mEdit->addAction(actDelete_);

    QMenu* mCard = menuBar()->addMenu(tr("卡面(&C)"));
    mCard->addAction(actLandscape_);
    mCard->addAction(actPortrait_);

    QMenu* mData = menuBar()->addMenu(tr("資料(&D)"));
    mData->addAction(actLoadData);
    mData->addSeparator();
    mData->addAction(actPrevRecord_);
    mData->addAction(actNextRecord_);

    QMenu* mView = menuBar()->addMenu(tr("檢視(&V)"));
    mView->addAction(actFit);
    mView->addSeparator();
    QMenu* mPanels = mView->addMenu(tr("面板"));
    for (QDockWidget* d : { elementDock_, dataDock_, propertyDock_, previewDock_ })
        mPanels->addAction(d->toggleViewAction());       // 面板被關掉後，從這裡重新開啟
    mView->addAction(actResetDocks);

    // 工具列：只放最常用的，並用分隔線分組
    QToolBar* tb = addToolBar(tr("常用"));
    tb->setObjectName(QStringLiteral("toolbarMain"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->addAction(actOpenLayout);
    tb->addAction(actSaveLayout);
    tb->addSeparator();
    tb->addAction(actAddText);
    tb->addAction(actAddImage);
    tb->addAction(actDelete_);
    tb->addSeparator();
    tb->addAction(actLoadData);
    tb->addAction(actPrevRecord_);
    tb->addAction(actNextRecord_);
    tb->addSeparator();
    tb->addAction(actFit);
}

void DesignerWindow::updateActionStates() {
    if (actDelete_) actDelete_->setEnabled(selectedItem() != nullptr);
    const bool hasData = !data_.rows.isEmpty();
    if (actPrevRecord_) actPrevRecord_->setEnabled(hasData);
    if (actNextRecord_) actNextRecord_->setEnabled(hasData);
}

QWidget* DesignerWindow::buildPropertyPanel() {
    propertyPanel_ = new QWidget;
    propertyPanel_->setMinimumWidth(300);
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
    basic->addRow(tr("X"), xSpin_);
    basic->addRow(tr("Y"), ySpin_);
    basic->addRow(tr("寬"), wSpin_);
    basic->addRow(tr("高"), hSpin_);
    root->addLayout(basic);

    // --- 文字 ---
    textGroup_ = new QGroupBox(tr("文字"));
    auto* tf = new QFormLayout(textGroup_);
    textEdit_ = new QPlainTextEdit;
    textEdit_->setFixedHeight(64);
    textEdit_->setPlaceholderText(tr("固定文字，或用 {姓名} 這類欄位當動態內容"));
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

    // --- 圖片 ---
    imageGroup_ = new QGroupBox(tr("圖片"));
    auto* imf = new QFormLayout(imageGroup_);
    imageEdit_ = new QLineEdit;
    imageEdit_->setPlaceholderText(tr("圖片檔路徑，可含 {欄位}，例如 D:\\photos\\{學號}.jpg"));
    auto* browse = new QPushButton(tr("選擇…"));
    auto* pathRow = new QWidget;
    auto* pl = new QHBoxLayout(pathRow);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(imageEdit_, 1);
    pl->addWidget(browse);
    imf->addRow(tr("路徑"), pathRow);
    keepAspectCheck_ = new QCheckBox(tr("保持長寬比"));
    imf->addRow(keepAspectCheck_);
    root->addWidget(imageGroup_);

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
    hookSpin(ySpin_, [](card::Element& e, double v) { e.yMm = v; });
    hookSpin(wSpin_, [](card::Element& e, double v) { e.wMm = v; });
    hookSpin(hSpin_, [](card::Element& e, double v) { e.hMm = v; });
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
    connect(keepAspectCheck_, &QCheckBox::toggled, this, [this](bool on) {
        editSelected([&](card::Element& e) { e.keepAspect = on; });
        });

    populatePanel(nullptr);
    return propertyPanel_;
}

QWidget* DesignerWindow::buildPreviewPanel() {
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);

    usePreviewCheck_ = new QCheckBox(tr("以此資料預覽"));
    usePreviewCheck_->setToolTip(tr("取消勾選時，動態欄位 {姓名} 等會直接顯示原樣"));
    usePreviewCheck_->setChecked(true);
    l->addWidget(usePreviewCheck_);

    // 尚未載入 Excel 時的範例值；載入後，選取某一筆資料會把該筆資料填進這些欄位
    const QMap<QString, QString> samples{
        { QStringLiteral("姓名"), QStringLiteral("王小明") },
        { QStringLiteral("學號"), QStringLiteral("315130106") },
        { QStringLiteral("系所"), QStringLiteral("職業安全衛生系") },
        { QStringLiteral("內碼"), QStringLiteral("1234567890") },
        { QStringLiteral("外碼"), QStringLiteral("9876543210") } };
    for (const QString& name : card::fieldNames()) {
        auto* edit = new QLineEdit(samples.value(name));
        edit->setMinimumWidth(70);
        l->addWidget(new QLabel(name + QStringLiteral("："), w));
        l->addWidget(edit, 1);
        previewEdits_.emplace_back(name, edit);
        connect(edit, &QLineEdit::textChanged, this, [this] { applyPreviewRecord(); });
    }
    connect(usePreviewCheck_, &QCheckBox::toggled, this, [this] { applyPreviewRecord(); });
    return w;
}

QWidget* DesignerWindow::buildDataPanel() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);

    auto* top = new QHBoxLayout;
    auto* loadBtn = new QPushButton(tr("載入 Excel…"));
    dataInfo_ = new QLabel(tr("尚未載入資料\n需要欄位：%1").arg(card::fieldNames().join(QStringLiteral("、"))));
    dataInfo_->setWordWrap(true);
    top->addWidget(loadBtn);
    top->addWidget(dataInfo_, 1);
    v->addLayout(top);

    dataFilter_ = new QLineEdit;
    dataFilter_->setPlaceholderText(tr("搜尋 (姓名、學號、系所…)"));
    dataFilter_->setClearButtonEnabled(true);
    v->addWidget(dataFilter_);

    const QStringList& fields = card::fieldNames();
    dataTable_ = new QTableWidget(0, fields.size());
    dataTable_->setHorizontalHeaderLabels(fields);
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
    return w;
}

// ========================================================== scene / model ===

void DesignerWindow::applyPreviewRecord() {
    previewRecord_.clear();
    if (usePreviewCheck_ && usePreviewCheck_->isChecked()) {
        for (const auto& pr : previewEdits_) previewRecord_.insert(pr.first, pr.second->text());
    }
    if (scene_) scene_->update();
}

ElementItem* DesignerWindow::createItem(const QString& id, int z) {
    auto* it = new ElementItem(&layout_, &previewRecord_, id,
        [this](const QString& changedId) { onItemChanged(changedId); }, cardItem_);
    it->setZValue(z);
    return it;
}

void DesignerWindow::rebuildScene() {
    updating_ = true;
    scene_->clear();                       // 會刪除舊的卡片與所有元素
    cardItem_ = nullptr;

    const double w = layout_.widthMm() * ElementItem::kPxPerMm;
    const double h = layout_.heightMm() * ElementItem::kPxPerMm;
    cardItem_ = scene_->addRect(0, 0, w, h, QPen(QColor(110, 110, 110)), QBrush(Qt::white));
    cardItem_->setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);   // 超出卡片的部分不會被印出來
    scene_->setSceneRect(QRectF(-60, -60, w + 120, h + 120));

    for (int i = 0; i < layout_.elements.size(); ++i) createItem(layout_.elements[i].id, i);

    const bool portrait = layout_.orientation == card::Orientation::Portrait;
    actLandscape_->setChecked(!portrait);
    actPortrait_->setChecked(portrait);
    updating_ = false;

    refreshList();
    populatePanel(nullptr);
    updateActionStates();
    fitView();
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

void DesignerWindow::fitView() {
    if (!cardItem_) return;
    view_->resetTransform();
    view_->fitInView(cardItem_->sceneBoundingRect().adjusted(-25, -25, 25, 25), Qt::KeepAspectRatio);
}

// ================================================================= actions ===

void DesignerWindow::addElement(card::ElementType type) {
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
    else {
        e.name = tr("圖片 %1").arg(count);
        e.xMm = 5; e.yMm = 5; e.wMm = 25; e.hMm = 25;
    }
    layout_.elements.append(e);
    createItem(e.id, layout_.elements.size() - 1);
    refreshList();

    scene_->clearSelection();
    if (ElementItem* it = findItem(e.id)) it->setSelected(true);
}

void DesignerWindow::deleteSelected() {
    ElementItem* it = selectedItem();
    if (!it) return;
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
}

void DesignerWindow::setOrientation(card::Orientation o) {
    if (layout_.orientation == o) return;
    layout_.orientation = o;
    rebuildScene();                        // 元素座標維持不變，需要時請自行重新排版
}

void DesignerWindow::openLayout() {
    const QString path = QFileDialog::getOpenFileName(this, tr("開啟版面"), QString(), tr("版面檔 (*.json)"));
    if (path.isEmpty()) return;
    card::CardLayout loaded;
    QString err;
    if (!card::CardLayout::loadFromFile(path, &loaded, &err)) {
        QMessageBox::warning(this, tr("開啟失敗"), err);
        return;
    }
    layout_ = loaded;
    rebuildScene();
    statusBar()->showMessage(tr("已開啟 %1").arg(path), 5000);
}

void DesignerWindow::saveLayout() {
    QString path = QFileDialog::getSaveFileName(this, tr("儲存版面"), QStringLiteral("layout.json"), tr("版面檔 (*.json)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".json");
    QString err;
    if (!layout_.saveToFile(path, &err)) {
        QMessageBox::warning(this, tr("儲存失敗"), err);
        return;
    }
    statusBar()->showMessage(tr("已儲存 %1").arg(path), 5000);
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

// ========================================================= 製卡資料 (階段 2) ===

void DesignerWindow::browseData() {
    const QString path = QFileDialog::getOpenFileName(this, tr("載入製卡資料"), lastDataDir_,
        tr("Excel 活頁簿 (*.xlsx)"));
    if (path.isEmpty()) return;
    loadData(path);
}

bool DesignerWindow::loadData(const QString& path) {
    card::DataTable t;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = card::loadExcelTable(path, card::fieldNames(), &t, &err);
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
    dataTable_->setUpdatesEnabled(false);
    dataTable_->clearContents();
    dataTable_->setRowCount(int(data_.rows.size()));
    for (int r = 0; r < data_.rows.size(); ++r) {
        for (int c = 0; c < data_.fields.size(); ++c) {
            const QString v = data_.rows[r].value(data_.fields[c]);
            auto* item = new QTableWidgetItem(v);
            if (v.isEmpty()) {                       // 缺資料 (含 Excel 的 #N/A 等錯誤值) 以淡紅色標示
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

    updateDataInfo();
    updateActionStates();
    dataDock_->show();
    dataDock_->raise();

    if (data_.rows.isEmpty()) {
        QMessageBox::information(this, tr("沒有資料"), tr("已讀取欄位，但標題列下面沒有任何資料列。"));
        return true;
    }
    dataTable_->setCurrentCell(0, 0);
    applyDataRow(0);                       // 重新載入時選取列可能沒變，所以明確套用第一筆
    statusBar()->showMessage(tr("已載入 %1：共 %2 筆").arg(QFileInfo(path).fileName()).arg(data_.rows.size()), 6000);
    return true;
}

void DesignerWindow::applyDataRow(int row) {
    if (row < 0 || row >= data_.rows.size()) return;
    const card::Record& rec = data_.rows[row];

    usePreviewCheck_->setChecked(true);
    for (const auto& pr : previewEdits_) pr.second->setText(rec.value(pr.first));   // 每次變動都會自動重畫
    statusBar()->showMessage(tr("預覽第 %1 / %2 筆：%3")
        .arg(row + 1).arg(data_.rows.size()).arg(rec.value(QStringLiteral("姓名"))), 8000);
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
    for (const auto& rec : data_.rows) {
        for (const QString& f : data_.fields) if (rec.value(f).isEmpty()) { ++incomplete; break; }
    }
    if (incomplete > 0) s += tr("\n⚠ %1 筆有欄位缺資料 (標紅)").arg(incomplete);
    dataInfo_->setText(s);
}

// ============================================================ panel / list ===

void DesignerWindow::updateColorButton(const QColor& c) {
    colorBtn_->setStyleSheet(
        QStringLiteral("background-color:%1; border:1px solid #888; min-height:22px;").arg(c.name()));
}

void DesignerWindow::populatePanel(const card::Element* e) {
    const bool prev = updating_;
    updating_ = true;

    propertyPanel_->setEnabled(e != nullptr);
    if (e) {
        nameEdit_->setText(e->name);
        typeLabel_->setText(typeText(*e));
        xSpin_->setValue(e->xMm);
        ySpin_->setValue(e->yMm);
        wSpin_->setValue(e->wMm);
        hSpin_->setValue(e->hMm);

        textGroup_->setVisible(e->type == card::ElementType::Text);
        imageGroup_->setVisible(e->type == card::ElementType::Image);

        if (textEdit_->toPlainText() != e->text) textEdit_->setPlainText(e->text);
        fontCombo_->setCurrentFont(QFont(e->fontFamily));
        sizeSpin_->setValue(e->fontPt);
        boldCheck_->setChecked(e->bold);
        italicCheck_->setChecked(e->italic);
        alignCombo_->setCurrentIndex(static_cast<int>(e->align));
        updateColorButton(e->color);

        if (imageEdit_->text() != e->imagePath) imageEdit_->setText(e->imagePath);
        keepAspectCheck_->setChecked(e->keepAspect);
    }
    else {
        nameEdit_->clear();
        typeLabel_->clear();
        textGroup_->setVisible(false);
        imageGroup_->setVisible(false);
    }
    updating_ = prev;
}

void DesignerWindow::refreshList() {
    const bool prev = updating_;
    updating_ = true;
    elementList_->clear();
    for (const auto& e : layout_.elements) {
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
    syncListSelection(it ? it->id() : QString());
    populatePanel(it ? layout_.find(it->id()) : nullptr);
    updateActionStates();
}

void DesignerWindow::onItemChanged(const QString& id) {
    if (updating_) return;
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
    it->syncFromModel();
    it->update();
    refreshListName(*e);
}