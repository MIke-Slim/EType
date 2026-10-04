#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "core.h"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <cctype>

namespace etype {
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n) return {};
    std::wstring r(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n); return r;
}
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr); return r;
}
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> r; size_t a = 0;
    for (;;) { auto b = s.find(delimiter, a); r.push_back(s.substr(a, b == std::string::npos ? b : b-a)); if (b == std::string::npos) break; a=b+1; }
    return r;
}
bool Dictionary::load(const std::wstring& path) {
    std::ifstream f{std::filesystem::path(path)};
    if (!f) return false;
    std::unordered_map<std::string, Entry> next;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto fields = split(line, '\t'); if (fields.size() != 7) continue;
        auto name = lower(fields[0]);
        auto& e = next[name]; e.word = name; e.phonetic = wide(fields[1]); e.root=wide(fields[2]); e.form=wide(fields[3]);
        try { e.rank = std::stoi(fields[4]); } catch (...) { e.rank=1000000; }
        auto text = wide(fields[5]); auto pos = wide(fields[6]);
        if (!text.empty() && std::none_of(e.candidates.begin(), e.candidates.end(), [&](const Candidate& c) {return c.text == text && c.pos == pos;})) e.candidates.push_back({text,pos});
    }
    for (auto i = next.begin(); i != next.end();) { if (i->second.candidates.empty()) i = next.erase(i); else ++i; }
    entries_.swap(next); return !entries_.empty();
}
const Entry* Dictionary::find(const std::string& word) const { auto i = entries_.find(lower(word)); return i == entries_.end() ? nullptr : &i->second; }
// Optimal string alignment distance includes one adjacent transposition.
static int distance(const std::string& a, const std::string& b, int limit) {
    if (std::abs((int)a.size()-(int)b.size()) > limit) return limit+1;
    std::vector<int> older(b.size()+1), previous(b.size()+1), current(b.size()+1);
    for (size_t j=0;j<=b.size();++j) previous[j]=(int)j;
    for (size_t i=1;i<=a.size();++i) {
        current[0]=(int)i; int minimum=current[0];
        for (size_t j=1;j<=b.size();++j) {
            current[j]=std::min({previous[j]+1,current[j-1]+1,previous[j-1]+(a[i-1]!=b[j-1])});
            if (i>1 && j>1 && a[i-1]==b[j-2] && a[i-2]==b[j-1]) current[j]=std::min(current[j],older[j-2]+1);
            minimum=std::min(minimum,current[j]);
        }
        if (minimum>limit) return limit+1;
        older.swap(previous); previous.swap(current);
    }
    return previous[b.size()];
}
std::vector<std::string> Dictionary::correct(const std::string& value) const {
    std::vector<std::string> r;
    if (value.size()<2 || value.size()>48 || find(value)) return r;
    struct Hit { int distance, rank; std::string word; };
    std::vector<Hit> hits; auto word=lower(value); int limit=word.size()<5?1:2;
    for (const auto& pair : entries_) {
        const auto& e=pair.second;
        int d=distance(word,e.word,limit); if (d<=limit) hits.push_back({d,e.rank,e.word});
    }
    std::sort(hits.begin(),hits.end(),[](const Hit& a,const Hit& b){ if(a.distance!=b.distance)return a.distance<b.distance; if(a.rank!=b.rank)return a.rank<b.rank; return a.word<b.word; });
    for(size_t i=0;i<std::min<size_t>(5,hits.size());++i)r.push_back(hits[i].word);
    return r;
}
const Entry* Engine::entry() const { return dictionary->find(buffer); }
size_t Engine::count() const { if(correcting)return corrections.size(); auto e=entry(); return e?e->candidates.size():0; }
void Engine::reset() { buffer.clear(); corrections.clear(); correcting=false; selected=0; }
Result Engine::type(char c) {
    if(english)return {wide(std::string(1,c)),false,false};
    buffer.push_back(c); correcting=false; corrections.clear(); selected=0; return {};
}
Result Engine::finish() { Result r{wide(buffer)}; reset(); return r; }
Result Engine::choose(size_t index) {
    size_t at=pageStart()+index; if(at>=count())return {};
    if(correcting) { auto fixed=corrections[at]; buffer=fixed; correcting=false; corrections.clear(); selected=0; return {}; }
    auto e=entry(); if(!e)return {};
    Result r{e->candidates[at].text}; reset(); return r;
}
Result Engine::key(Key k) {
    if(k==Key::Toggle) { auto r=finish(); english=!english; return r; }
    if(buffer.empty())return {{},false,false};
    switch(k) {
    case Key::Enter: return finish();
    case Key::Escape: reset(); return {};
    case Key::Backspace: buffer.pop_back(); correcting=false; corrections.clear(); selected=0; return {};
    case Key::Space:
        if(count())return choose(selected-pageStart());
        correcting=true; corrections=dictionary->correct(buffer); selected=0; return {};
    case Key::Up: if(count())selected=(selected+count()-1)%count(); return {};
    case Key::Down: if(count())selected=(selected+1)%count(); return {};
    case Key::PageUp: if(count()&&pageStart()>=5)selected=pageStart()-5; return {};
    case Key::PageDown: if(pageStart()+5<count())selected=pageStart()+5; return {};
    default:return {};
    }
}
Result Engine::punctuation(wchar_t c) {
    Result r;
    // Punctuation never silently accepts a spelling correction.
    auto e=entry();
    if(!correcting && e && !e->candidates.empty())r.output=e->candidates[std::min(selected,e->candidates.size()-1)].text;
    else r.output=wide(buffer);
    reset();
    if(chinesePunctuation && !english) {
        switch(c) {
        case L',':c=L'，';break; case L'.':c=L'。';break; case L'?':c=L'？';break; case L'!':c=L'！';break;
        case L';':c=L'；';break; case L':':c=L'：';break; case L'(':c=L'（';break; case L')':c=L'）';break;
        case L'[':c=L'【';break; case L']':c=L'】';break;
        case L'"':c=doubleQuote_?L'”':L'“';doubleQuote_=!doubleQuote_;break;
        case L'\'':c=singleQuote_?L'’':L'‘';singleQuote_=!singleQuote_;break;
        default:break;
        }
    }
    r.output.push_back(c);return r;
}
}
