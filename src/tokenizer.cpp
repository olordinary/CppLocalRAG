// Created by 何宇轩 on 2024/6/20.
// 文本清洗 + 分词
// 1.优化英文分词正则表达式，使其更适配C++知识库
// 2.完善了停用词过滤功能，加载停用词文件和设置内置停用词同时进行，以避免分词结果中包含常见但无意义的词汇
// 3.修正中文分词前对纯英文/数字词的过滤，避免重复分词
#include "tokenizer.h"

#include "cppjieba/Jieba.hpp"

#include <QRegularExpression>
#include <QString>
#include <fstream>

#include <algorithm>
#include <cctype>
#include <stdexcept>

// 加载停用词文件
void Tokenizer::loadStopWords(const std::string&stopWordPath){
    std::ifstream file(stopWordPath);
    if(!file.is_open()){
        std::cerr << "[Tokenizer] 警告：无法打开停用词文件: "<< stopWordPath<< "，将仅使用内置停用词表。\n";
        return;
    }
    std::string word;
    while (std::getline(file, word)) {
        if(!word.empty() &&word.back() == '\r'){
            word.pop_back();
        }

        std::transform(word.begin(), word.end(), word.begin(),[](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if(!word.empty()){
            stopWords_.insert(word);
        }
    }
}

Tokenizer::Tokenizer(const std::string& dictDir) {
    // 初始化 cppjieba 分词器，需要提供词典路径。这里假设词典文件位于 dictDir 目录下，并且包含以下文件：
    // - jieba.dict.utf8: 基础词典
    // - hmm_model.utf8: HMM 模型文件
    // - user.dict.utf8: 用户自定义词典（可选）
    // - idf.utf8: IDF 文件（可选）
    // - stop_words.utf8: 停用词文件（可选）
    const std::string dictPath = dictDir + "/jieba.dict.utf8";
    const std::string hmmPath = dictDir + "/hmm_model.utf8";
    const std::string userDictPath = dictDir + "/user.dict.utf8";
    const std::string idfPath = dictDir + "/idf.utf8";
    const std::string stopWordPath = dictDir + "/stop_words.utf8";  //注意：此处的 stopWordPath 是传递给 cppjieba 用于关键词抽取的，不会在分词的时候过滤该路径文件中的停用词。

    try {
        jieba_ = std::make_unique<cppjieba::Jieba>(dictPath, hmmPath, userDictPath, idfPath, stopWordPath);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("初始化 cppjieba 失败: ") + ex.what());
    }

    //初始化停用词表
    loadStopWords(stopWordPath);
    stopWords_.insert({"的", "了", "在", "是", "我", "有", "和", "就", "不", "人", "都", "一", "一个",
                       "上", "也", "很", "到", "说", "要", "去", "你", "会", "着", "没有", "看", "好", "自己", "这"});
    stopWords_.insert({
        "the", "is", "are", "was", "were",
        "and", "or", "to", "of", "in", "on",
        "for", "with", "a", "an", "this", "that"
    });
}

Tokenizer::~Tokenizer() = default;

// 分词函数：這裡的 tokenize 方法會先提取英文和數字詞，然後使用 cppjieba 分詞中文內容，最後過濾掉停用詞。
std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    if (text.empty()) {
        return {};
    }

    if (!jieba_) {
        throw std::runtime_error("分词器尚未初始化");
    }

    std::vector<std::string> filtered;
    const QString input = QString::fromUtf8(text.c_str());

    // 先提取英文與數字詞（如 CMakeLists、Qt6），並轉小寫
    // 这里使用了一个简单的正则表达式来匹配英文和数字词。对于更复杂的情况，可能需要更复杂的正则表达式或其他方法来提取英文和数字词。
    //QRegularExpression Qt正则表达式：
    //const QRegularExpression enWordRegex("\\b[A-Za-z0-9]+\\b");
    const QRegularExpression enWordRegex(
        R"((C\+\+|C#|[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*(?:\.[A-Za-z0-9_]+)?|[A-Za-z]+[0-9]+|[0-9]+))"
    );
    QRegularExpressionMatchIterator it = enWordRegex.globalMatch(input);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        
        // 提取匹配的英文或数字词，并转为 std::string
        std::string token = match.captured(0).toStdString();

        // 转小写
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (token.empty()) {
            continue;
        }

        // 保留停用詞過濾
        if (stopWords_.find(token) != stopWords_.end()) {
            continue;
        }

        filtered.push_back(token);
    }

    // 中文內容使用 cppjieba 分詞
    std::vector<std::string> words;
    jieba_->CutForSearch(text, words);

 
    for (const auto& w : words) {
        if (w.empty()) {
            continue;
        }

        // 純英文/數字詞已在上面提取，這裡跳過避免重複
        const QString qw = QString::fromUtf8(w.c_str());

        const QRegularExpression pureEnWordRegex("^[A-Za-z0-9]+$");
        if (pureEnWordRegex.match(qw).hasMatch()) {
            continue;
        }

        // 保留停用詞過濾
        if (stopWords_.find(w) != stopWords_.end()) {
            continue;
        }

        filtered.push_back(w);
    }

    return filtered;
}