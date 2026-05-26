#include "databasemanager.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <sqlite3.h>

#include <memory>
#include <stdexcept>

namespace {
using StmtPtr = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
constexpr const char* kAppDataSubDir = "CppLocalRAG";
constexpr const char* kDbFileName = "cpplocalrag.sqlite";

QString sqliteError(sqlite3* db, const QString& context) {
    const char* err = db ? sqlite3_errmsg(db) : "Unknown sqlite error";
    return context + ": " + QString::fromUtf8(err);
}
}  // namespace

DatabaseManager::DatabaseManager() : db_(nullptr) {}

DatabaseManager::~DatabaseManager() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

//数据库初始化，创建应用数据目录和数据库文件，并创建 documents 表
void DatabaseManager::initialize() {
    // 创建应用数据目录
    // 在当前设备上，解析出的数据路径为：C:\Users\olord\AppData\Roaming\CppLocalRAG
    const QString appDataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + kAppDataSubDir;
    QDir dir;
    if (!dir.mkpath(appDataDir)) {
        throw std::runtime_error(("Cannot create app data directory: " + appDataDir).toStdString());
    }
    // 最终生成的数据库文件路径
    dbFilePath_ = appDataDir + "/" + kDbFileName;

    // 打开数据库（如果文件不存在会自动创建）
    const QByteArray dbPathUtf8 = dbFilePath_.toUtf8();
    const int openRc = sqlite3_open_v2(dbPathUtf8.constData(), &db_,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);    //这句话调用sqlite3_open_v2打开数据库（三个参数：数据库文件路径、数据库句柄（输出参数，成功打开后db_指向SQLite数据库连接）、打开模式）
    if (openRc != SQLITE_OK) {
        const QString msg = sqliteError(db_, "Failed to open sqlite database");
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error(msg.toStdString());
    }

    // 创建 documents 表
    //  1. 定义 SQL 语句，创建一个名为 documents 的表，如果该表不存在的话。表中包含以下字段：
    //   - id: 一个自增的整数，作为主键。
    //   - title: 一个文本字段，用于存储文档的标题。
    //   - content: 一个文本字段，用于存储文档的内容。
    //   - file_path: 一个文本字段，用于存储文档的文件路径。
    //   - created_at: 一个文本字段，用于存储文档的创建时间。
    const char* sql =
        "CREATE TABLE IF NOT EXISTS documents ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "title TEXT,"
        "content TEXT,"
        "file_path TEXT,"
        "created_at TEXT"
        ");";

    //  2. 执行 SQL 语句，使用 sqlite3_exec 函数执行上述 SQL 语句。如果执行失败，抛出一个运行时错误，包含错误信息。
    char* errMsg = nullptr;
    const int execRc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    if (execRc != SQLITE_OK) {
        QString msg = "Failed to create documents table";
        if (errMsg != nullptr) {
            msg += ": " + QString::fromUtf8(errMsg);
            sqlite3_free(errMsg);
        }
        throw std::runtime_error(msg.toStdString());
    }
}

//插入文档，返回新插入文档的 ID
int DatabaseManager::insertDocument(const QString& title, const QString& content, const QString& filePath) {
    ensureOpen();

    //根据系统时间生成文档创建时间字符串，格式为 ISO 8601（例如：2024-06-01T12:34:56）
    const QString createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    //1. 准备 SQL 插入语句，使用参数占位符（?）来防止 SQL 注入攻击
    const char* sql = "INSERT INTO documents (title, content, file_path, created_at) VALUES (?, ?, ?, ?);";

    // 2. SQL 预编译，也就是把一条 SQL 字符串转换成 SQLite 可以执行的 sqlite3_stmt 对象
    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);   //调用 sqlite3_prepare_v2 函数预编译 SQL 语句，参数包括数据库连接、SQL 字符串、SQL 字符串长度（-1 表示以 null 结尾的字符串）、输出参数（预编译后的 sqlite3_stmt 对象）和一个可选的输出参数（指向未使用部分 SQL 字符串的指针，这里不需要所以传 nullptr）。如果预编译失败，抛出一个运行时错误，包含错误信息。返回值rc为SQLite返回码，表示预编译的结果，如果不是 SQLITE_OK，说明预编译失败。
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare insertDocument statement").toStdString());
    }

    //使用智能指针管理 sqlite3_stmt 对象，确保在函数结束时自动调用 sqlite3_finalize 来释放资源
    StmtPtr stmt(rawStmt, sqlite3_finalize);

    const QByteArray titleUtf8 = title.toUtf8();
    const QByteArray contentUtf8 = content.toUtf8();
    const QByteArray filePathUtf8 = filePath.toUtf8();
    const QByteArray createdAtUtf8 = createdAt.toUtf8();

    // 3. 绑定参数，将文档的标题、内容、文件路径和创建时间绑定到预编译的 SQL 语句中对应的占位符（?）。使用 sqlite3_bind_text 函数绑定文本参数，参数包括 sqlite3_stmt 对象、参数索引（从 1 开始）、参数值的 UTF-8 编码、参数值的长度（-1 表示以 null 结尾的字符串）和一个可选的释放函数（这里使用 SQLITE_TRANSIENT 表示 SQLite 会在需要时复制参数值）。如果绑定失败，抛出一个运行时错误，包含错误信息。
    sqlite3_bind_text(stmt.get(), 1, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, filePathUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, createdAtUtf8.constData(), -1, SQLITE_TRANSIENT);

    //4. 执行 SQL 语句，使用 sqlite3_step 函数执行预编译的 SQL 语句。如果执行失败，抛出一个运行时错误，包含错误信息。执行成功后，使用 sqlite3_last_insert_rowid 函数获取新插入文档的 ID，并返回该 ID。
    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(sqliteError(db_, "Failed to execute insertDocument").toStdString());
    }

    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

//根据关键词搜索文档，如果关键词为空则返回所有文档      
QVector<Document> DatabaseManager::searchDocuments(const QString& keyword) const {
    ensureOpen();

    QVector<Document> docs; //结果容器，用于存储查询结果的文档列表
    sqlite3_stmt* rawStmt = nullptr;

    //1.根据是否提供了关键词，构造不同的 SQL 查询语句。如果关键词为空或仅包含空白字符，则查询所有文档；否则，使用 LIKE 语句进行模糊匹配，查询标题或内容中包含关键词的文档。LIKE 模式使用百分号（%）作为通配符，表示任意字符序列。
    QString sql;
    QByteArray likePattern;
    if (keyword.trimmed().isEmpty()) {
        sql = "SELECT id, title, content, file_path, created_at FROM documents ORDER BY id DESC;";
    } else {
        sql =
            "SELECT id, title, content, file_path, created_at "
            "FROM documents "
            "WHERE title LIKE ? OR content LIKE ? "
            "ORDER BY id DESC;";
        likePattern = ("%" + keyword + "%").toUtf8();
    }

    //2. SQL 预编译，将构造好的 SQL 查询语句预编译成 sqlite3_stmt 对象。如果预编译失败，抛出一个运行时错误，包含错误信息。
    int rc = sqlite3_prepare_v2(db_, sql.toUtf8().constData(), -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare searchDocuments statement").toStdString());
    }

    StmtPtr stmt(rawStmt, sqlite3_finalize);

    //3. 绑定参数，如果提供了关键词，则将 LIKE 模式绑定到预编译的 SQL 语句中对应的占位符（?）。使用 sqlite3_bind_text 函数绑定文本参数，参数包括 sqlite3_stmt 对象、参数索引（从 1 开始）、参数值的 UTF-8 编码、参数值的长度（-1 表示以 null 结尾的字符串）和一个可选的释放函数（这里使用 SQLITE_TRANSIENT 表示 SQLite 会在需要时复制参数值）。如果绑定失败，抛出一个运行时错误，包含错误信息。
    if (!keyword.trimmed().isEmpty()) {
        sqlite3_bind_text(stmt.get(), 1, likePattern.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 2, likePattern.constData(), -1, SQLITE_TRANSIENT);
    }

    //4. 执行 SQL 语句，使用 sqlite3_step 函数执行预编译的 SQL 语句，并迭代结果集。对于每一行结果，创建一个 Document 对象，并将查询到的字段值赋给 Document 对象的成员变量。将 Document 对象添加到结果容器中。迭代完成后，如果 sqlite3_step 返回的不是 SQLITE_DONE，说明在迭代过程中发生了错误，抛出一个运行时错误，包含错误信息。最后返回结果容器。
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        Document doc;
        doc.id = sqlite3_column_int(stmt.get(), 0);
        doc.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1)));
        doc.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2)));
        doc.filePath = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3)));
        doc.createdAt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)));
        docs.push_back(doc);
    }

    if (rc != SQLITE_DONE) {
        throw std::runtime_error(sqliteError(db_, "Failed while iterating searchDocuments result").toStdString());
    }

    return docs;
}

//根据文档 ID 获取文档详情，如果文档不存在则抛出异常
Document DatabaseManager::getDocumentById(int id) const {
    ensureOpen();

    //1. 定义 SQL 查询语句，使用参数占位符（?）来防止 SQL 注入攻击。查询语句从 documents 表中选择 id、title、content、file_path 和 created_at 字段，其中 id 字段匹配指定的参数值，并限制结果为一行。
    const char* sql =
        "SELECT id, title, content, file_path, created_at FROM documents WHERE id = ? LIMIT 1;";
    //2. SQL 预编译，将 SQL 查询语句预编译成 sqlite3_stmt 对象。如果预编译失败，抛出一个运行时错误，包含错误信息。
    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare getDocumentById statement").toStdString());
    }

    StmtPtr stmt(rawStmt, sqlite3_finalize);

    //3. 绑定参数，将文档 ID 绑定到预编译的 SQL 语句中对应的占位符（?）。使用 sqlite3_bind_int 函数绑定整数参数，参数包括 sqlite3_stmt 对象、参数索引（从 1 开始）和参数值。如果绑定失败，抛出一个运行时错误，包含错误信息。
    sqlite3_bind_int(stmt.get(), 1, id);

    //4. 执行 SQL 语句，使用 sqlite3_step 函数执行预编译的 SQL 语句。如果执行成功并返回一行结果，创建一个 Document 对象，并将查询到的字段值赋给 Document 对象的成员变量。返回 Document 对象。如果没有查询到结果，抛出一个运行时错误，说明文档未找到。如果在执行过程中发生错误，抛出一个运行时错误，包含错误信息。
    rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        Document doc;
        doc.id = sqlite3_column_int(stmt.get(), 0);
        doc.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1)));
        doc.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2)));
        doc.filePath = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3)));
        doc.createdAt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4)));
        return doc;
    }

    if (rc == SQLITE_DONE) {
        throw std::runtime_error(("Document not found, id=" + QString::number(id)).toStdString());
    }

    throw std::runtime_error(sqliteError(db_, "Failed to execute getDocumentById").toStdString());
}

//获取文档总数
int DatabaseManager::getDocumentsCount() const {
    ensureOpen();

    const char* sql = "SELECT COUNT(*) FROM documents;";
    sqlite3_stmt* rawStmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &rawStmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(sqliteError(db_, "Failed to prepare getDocumentsCount statement").toStdString());
    }

    StmtPtr stmt(rawStmt, sqlite3_finalize);
    rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        return sqlite3_column_int(stmt.get(), 0);
    }
    throw std::runtime_error(sqliteError(db_, "Failed to execute getDocumentsCount").toStdString());
}

void DatabaseManager::ensureOpen() const {
    if (db_ == nullptr) {
        throw std::runtime_error("Database is not initialized.");
    }
}