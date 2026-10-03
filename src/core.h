#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

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
enum class Key { Space, Enter, Backspace, Escape, Up, Down, PageUp, PageDown, Toggle };
struct Result { std::wstring output; bool consumed = true; bool changed = true; };
class Engine {
public:
    explicit Engine(std::shared_ptr<const Dictionary> dict) : dictionary(std::move(dict)) {}
    Result type(char value);
    Result key(Key key);
    Result choose(size_t pageIndex);
    Result punctuation(wchar_t value);
    Result finish();
    const Entry* entry() const;
    size_t count() const;
    size_t pageStart() const { return (selected / 5) * 5; }
    void reset();
    std::shared_ptr<const Dictionary> dictionary;
    std::string buffer;
    std::vector<std::string> corrections;
    bool correcting = false, english = false, chinesePunctuation = true;
    size_t selected = 0;
private:
    bool doubleQuote_ = false, singleQuote_ = false;
};
}
