#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace etype {
struct Candidate { std::wstring text, pos; };
struct Entry {
    std::string word;
    std::wstring phonetic, root, form;
    int rank = 1000000;
    std::vector<Candidate> candidates;
};
std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);
std::string lower(std::string value);
class Dictionary {
public:
    bool load(const std::wstring& path);
    const Entry* find(const std::string& word) const;
    std::vector<std::string> correct(const std::string& word) const;
    size_t size() const { return entries_.size(); }
private:
    std::unordered_map<std::string, Entry> entries_;
};
enum class Key { Space, Enter, CommitEnglish, Backspace, Escape, Up, Down, PageUp, PageDown, Toggle, Left, Right, Home, End, Delete };
struct Result { std::wstring output; bool consumed = true; bool changed = true; };
class Engine {
public:
    explicit Engine(std::shared_ptr<const Dictionary> dict) : dictionary(std::move(dict)) {}
    Result type(char value);
    Result key(Key key, bool extendSelection=false, bool byWord=false);
    Result choose(size_t pageIndex);
    Result punctuation(wchar_t value);
    Result finish();
    const Entry* entry() const;
    size_t count() const;
    size_t pageStart() const { return (selected / 5) * 5; }
    void reset();
    void invalidate();
    void setSelection(size_t active, size_t anchor);
    bool hasSelection() const { return caret != selectionAnchor; }
    uint64_t beginTranslation();
    bool completeTranslation(uint64_t revision, const std::string& original,
                             std::vector<Candidate> values, std::wstring error = {});
    uint64_t beginWordLookup(bool correction=false);
    bool completeWordLookup(uint64_t revision, const std::string& original, Entry entry,
                            std::vector<std::string> suggestions={}, std::wstring error={});
    bool onlineWords=false, wordPending=false, correctionLookup=false, wordMissing=false;
    std::wstring wordStatus;
    std::shared_ptr<const Dictionary> dictionary;
    std::string buffer;
    std::vector<std::string> corrections;
    bool correcting = false, english = false, chinesePunctuation = true;
    bool sentenceMode = false, translating = false;
    uint64_t revision = 0;
    std::vector<Candidate> sentences;
    std::wstring sentenceStatus;
    size_t selected = 0;
    size_t caret = 0, selectionAnchor = 0;
private:
    Entry onlineEntry_;
    size_t wordLeft() const;
    size_t wordRight() const;
    void eraseSelection();
    bool doubleQuote_ = false, singleQuote_ = false;
};
}
