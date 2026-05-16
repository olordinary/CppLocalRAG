#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QPushButton>
#include <QTextEdit>
#include <QMainWindow>
#include <QListWidgetItem>
#include <memory>
#include "databasemanager.h"
#include "searchengine.h"
#include "tokenizer.h"
#include "embeddingengine.h"
#include "vectorindex.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    //构造和析构
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    //拖拽事件：主窗口支持拖拽文件导入
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    //导入文档、搜索、语义搜索、点击文档列表项、显示关于信息等槽函数
    void onImportDocuments();
    void onSearch();
    void onSemanticSearch();
    void onDocumentClicked(QListWidgetItem *item);
    void onShowAbout();

private:
    void setupUi();
    void setupMenus();
    void refreshDocumentList(const QString &keyword = QString());
    void refreshSearchResults(const std::vector<std::pair<int, double>> &results);
    void refreshSemanticResults(const std::vector<std::pair<int, float>> &results);
    void updateStatusBarCount();
    void importFiles(const QStringList &filePaths);
    bool isSupportedDocument(const QString &filePath) const;

    //UI控件
    QLineEdit *searchLineEdit_;
    QPushButton *searchButton_;
    QListWidget *documentListWidget_;
    QTextEdit *previewTextEdit_;

    DatabaseManager databaseManager_;   //数据库管理器
    std::unique_ptr<SearchEngine> searchEngine_; //基于BM25的搜索引擎智能指针
    std::unique_ptr<Tokenizer> tokenizer_;   //分词器智能指针
    std::unique_ptr<EmbeddingEngine> embeddingEngine_;   //文本向量化引擎智能指针
    std::unique_ptr<VectorIndex> vectorIndex_;   //HNSW向量索引智能指针

};

#endif // MAINWINDOW_H