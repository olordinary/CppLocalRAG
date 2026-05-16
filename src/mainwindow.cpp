#include "mainwindow.h"
#include "document.h"
#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QVBoxLayout>
#include <exception>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),

        //成员变量初始化列表
      searchLineEdit_(nullptr),
      searchButton_(nullptr),
      documentListWidget_(nullptr),
      previewTextEdit_(nullptr),
      searchEngine_(nullptr),
      tokenizer_(nullptr),
      embeddingEngine_(nullptr),
      vectorIndex_(nullptr) {

    //设置窗口标题、大小和接受拖拽
    setWindowTitle(QString::fromUtf8("离线知识库系统"));
    resize(1100, 700);
    setAcceptDrops(true);

    // 初始化数据库管理器，捕获异常并显示错误信息
    try {
        databaseManager_.initialize();
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QString::fromUtf8("数据库错误"), QString::fromUtf8(ex.what()));
    }

    //UI布局和菜单设置
    setupUi();  //设置UI布局
    setupMenus();   //设置菜单栏
    refreshDocumentList();  //刷新文档列表显示

    try {
        // ====================== 搜索引擎初始化 ======================
        //初始化词典路径
        const QString appDir = QCoreApplication::applicationDirPath();  //获取可执行文件所在目录
        const QString dictDir = QDir(appDir).absoluteFilePath("../resources/dict"); //基于 exe 目录推导词典目录
        const std::string dictDirStd = QDir::toNativeSeparators(dictDir).toStdString(); //转换为适合系统的路径格式并转为 std::string

        //初始化分词器和关键词搜索引擎

        tokenizer_ = std::make_unique<Tokenizer>(dictDirStd);
        searchEngine_ = std::make_unique<SearchEngine>(dictDirStd);

        // 从数据库加载所有文档构建BM25索引
        //1.调用 databaseManager_ 的 searchDocuments 方法获取所有文档（传入空字符串表示不使用关键词过滤），将结果存储在 docs 变量中。
        QVector<Document> docs = databaseManager_.searchDocuments(QString());
        //2.将 QVector<Document> 转换为 std::vector<Document>，以便传递给 searchEngine_ 的 buildIndex 方法。预先调用 reserve 方法为 std::vector 分配足够的内存，以提高效率。
        std::vector<Document> allDocs;
        allDocs.reserve(docs.size());   //reserve预分配内存以提高效率
        for (const auto& doc : docs) {
            allDocs.push_back(doc);
        }
        //3.调用 searchEngine_ 的 buildIndex 方法，传入包含所有文档的 std::vector<Document>，以构建 BM25 搜索索引。这样后续的搜索请求就可以基于这个索引进行快速检索了。
        searchEngine_->buildIndex(allDocs);

        // ====================== 向量模块初始化 ======================
        embeddingEngine_ = std::make_unique<EmbeddingEngine>();
        embeddingEngine_->init(tokenizer_.get());   //初始化文本向量化引擎，传入分词器指针以便在编码时使用相同的分词逻辑

        vectorIndex_ = std::make_unique<VectorIndex>();   //初始化HNSW向量索引，参数：向量维度1024，最大文档数10万
        vectorIndex_->init(1024, 100000);

        for (const Document& doc : docs) {
            std::string content = doc.content.toStdString();
            std::vector<float> vec = embeddingEngine_->encode(content); //创建文档级向量，不是chunk级向量
            vectorIndex_->addVectors(vec, doc.id);
        }
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QString::fromUtf8("引擎初始化失败"), QString::fromUtf8(ex.what()));
    }
}

MainWindow::~MainWindow()=default;

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);

    auto* searchLayout = new QHBoxLayout();
    searchLineEdit_ = new QLineEdit(centralWidget);
    searchLineEdit_->setPlaceholderText(QString::fromUtf8("输入关键词或语义查询"));
    searchButton_ = new QPushButton(QString::fromUtf8("关键词搜索"), centralWidget);
    QPushButton* semanticBtn = new QPushButton(QString::fromUtf8("语义搜索"), centralWidget);

    searchLayout->addWidget(searchLineEdit_);
    searchLayout->addWidget(searchButton_);
    searchLayout->addWidget(semanticBtn);

    auto* splitter = new QSplitter(Qt::Horizontal, centralWidget);
    documentListWidget_ = new QListWidget(splitter);
    previewTextEdit_ = new QTextEdit(splitter);
    previewTextEdit_->setReadOnly(true);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    mainLayout->addLayout(searchLayout);
    mainLayout->addWidget(splitter);
    setCentralWidget(centralWidget);

    statusBar()->showMessage(QString::fromUtf8("就绪"));

    connect(searchButton_, &QPushButton::clicked, this, &MainWindow::onSearch);
    connect(semanticBtn, &QPushButton::clicked, this, &MainWindow::onSemanticSearch);
    connect(searchLineEdit_, &QLineEdit::returnPressed, this, &MainWindow::onSearch);
    connect(documentListWidget_, &QListWidget::itemClicked, this, &MainWindow::onDocumentClicked);
}

void MainWindow::setupMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    QAction* importAction = fileMenu->addAction(QString::fromUtf8("导入文档"));
    QAction* exitAction = fileMenu->addAction(QString::fromUtf8("退出"));

    QMenu* helpMenu = menuBar()->addMenu(QString::fromUtf8("帮助"));
    QAction* aboutAction = helpMenu->addAction(QString::fromUtf8("关于"));

    connect(importAction, &QAction::triggered, this, &MainWindow::onImportDocuments);
    connect(exitAction, &QAction::triggered, this, &QCoreApplication::quit);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAbout);
}

//打开文件对话框选择文档进行导入
void MainWindow::onImportDocuments() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, QString::fromUtf8("导入文档"), QString(),
        QString::fromUtf8("文本文档 (*.txt *.md* .cpp *.h *.hpp *.py *.java)")
    );
    if (!files.isEmpty()) importFiles(files);
}

void MainWindow::onSearch() {
    QString kw = searchLineEdit_->text().trimmed();
    if (kw.isEmpty()) {
        refreshDocumentList();
        return;
    }

    if (!searchEngine_) {
        QMessageBox::warning(this,
                             QString::fromUtf8("错误"),
                             QString::fromUtf8("搜索引擎未初始化"));
        return;
    }
    
    try {
        std::string query = kw.toStdString();
        auto res = searchEngine_->search(query);
        refreshSearchResults(res);
    } catch (const std::exception& ex) {
        qWarning() << "搜索失败:" << ex.what();
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("搜索失败"));
    }
}

void MainWindow::onSemanticSearch() {
    QString text = searchLineEdit_->text().trimmed();

    if (text.isEmpty()) {
        refreshDocumentList();
        return;
    }

    if (!embeddingEngine_ || !vectorIndex_) {
        QMessageBox::warning(this,
                             QString::fromUtf8("错误"),
                             QString::fromUtf8("语义搜索引擎未初始化"));
        return;
    }

    try {
        std::string q = text.toStdString();
        std::vector<float> vec = embeddingEngine_->encode(q);
        auto results = vectorIndex_->search(vec, 10);
        refreshSemanticResults(results);
    } catch (const std::exception& ex) {
        qWarning() << "语义搜索失败:" << ex.what();
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("语义搜索失败"));
    }
}

void MainWindow::onDocumentClicked(QListWidgetItem* item) {
    if (!item) return;
    int id = item->data(Qt::UserRole).toInt();
    try {
        Document doc = databaseManager_.getDocumentById(id);
        previewTextEdit_->setPlainText(doc.content);
    } catch (const std::exception& ex) {
        qWarning() << "加载文档失败:" << ex.what();
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("加载文档失败"));
    }
}

void MainWindow::onShowAbout() {
    QMessageBox::about(this, QString::fromUtf8("关于"),
                       QString::fromUtf8("离线知识库系统\n基于 Qt6 + SQLite + BM25 + 向量语义检索"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) files << url.toLocalFile();
    }
    importFiles(files);
    event->acceptProposedAction();
}

void MainWindow::refreshDocumentList(const QString& keyword) {
    documentListWidget_->clear();
    previewTextEdit_->clear();
    QVector<Document> docs = databaseManager_.searchDocuments(keyword);
    for (const Document& doc : docs) {
        QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
        auto* item = new QListWidgetItem(title, documentListWidget_);
        item->setData(Qt::UserRole, doc.id);
    }
    updateStatusBarCount();
}

void MainWindow::refreshSearchResults(const std::vector<std::pair<int, double>>& results) {
    documentListWidget_->clear();
    for (const auto& [id, score] : results) {
        try {
            Document doc = databaseManager_.getDocumentById(id);
            QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
            title += QString(" (BM25: %1)").arg(score, 0, 'f', 2);
            auto* item = new QListWidgetItem(title, documentListWidget_);
            item->setData(Qt::UserRole, id);
        } catch (const std::exception& ex) {
            qWarning() << "刷新搜索结果失败:" << ex.what();
        }
    }
}

void MainWindow::refreshSemanticResults(const std::vector<std::pair<int, float>>& results) {
    documentListWidget_->clear();
    for (const auto& [id, sim] : results) {
        try {
            Document doc = databaseManager_.getDocumentById(id);
            QString title = doc.title.isEmpty() ? QFileInfo(doc.filePath).fileName() : doc.title;
            title += QString(" (相似度: %1)").arg(sim, 0, 'f', 2);
            auto* item = new QListWidgetItem(title, documentListWidget_);
            item->setData(Qt::UserRole, id);
        } catch (const std::exception& ex) {
            qWarning() << "刷新语义搜索结果失败:" << ex.what();
        }
    }
}

void MainWindow::updateStatusBarCount() {
    int cnt = databaseManager_.getDocumentsCount();
    statusBar()->showMessage(QString::fromUtf8("文档总数：%1").arg(cnt));
}

//导入文件列表，逐个处理，支持txt和md格式，读取内容并存入数据库，同时更新搜索引擎和向量索引，最后刷新文档列表显示并弹出导入结果提示框
void MainWindow::importFiles(const QStringList& filePaths) {
    int ok = 0;
    for (const QString& path : filePaths) {
        if (!isSupportedDocument(path)) continue;

        //读取文件内容，尝试使用UTF-8编码，如果包含替换字符则退回到本地编码
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        QByteArray bytes = file.readAll();
        QString content = QString::fromUtf8(bytes);
        if (content.contains(QChar::ReplacementCharacter)) {
            content = QString::fromLocal8Bit(bytes);
        }

        //生成文档标题，优先使用文件名（不带扩展名），如果文件名为空则使用默认标题
        QString title = QFileInfo(path).completeBaseName();

        //，并更新搜索引擎和向量索引，捕获异常以保证导入过程的健壮性
        try {
            //将文档信息插入数据库
            int id = databaseManager_.insertDocument(title, content, path);

            //更新BM25搜索引擎索引
            if (searchEngine_) {
                Document d;
                d.id = id;
                d.title = title;
                d.content = content;
                d.filePath = path;
                searchEngine_->addDocument(d);
            }

            //更新向量索引，生成文档级向量（不是chunk级），并添加到HNSW索引
            if (embeddingEngine_ && vectorIndex_) {
                std::string c = content.toStdString();
                std::vector<float> vec = embeddingEngine_->encode(c);
                vectorIndex_->addVectors(vec, id);
            }
            ok++;
        } catch (const std::exception& ex) {
            qWarning() << "导入文件失败:" << ex.what();
        }
    }

    refreshDocumentList(searchLineEdit_->text().trimmed());
    QMessageBox::information(this, QString::fromUtf8("导入完成"),
                             QString::fromUtf8("成功导入 %1 个文档").arg(ok));
}

bool MainWindow::isSupportedDocument(const QString& filePath) const {
    QString s = QFileInfo(filePath).suffix().toLower();
    return s == "txt" || s == "md" || s=="cpp" || s=="h" || s=="py" || s=="java" || s=="hpp";
}