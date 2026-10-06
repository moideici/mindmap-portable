#pragma once
#include "core_bridge.hpp"
#include <sstream>
#include <iomanip>
#include <set>
#include <cctype>

namespace document {
constexpr size_t max_file_bytes = 8 * 1024 * 1024;

inline bool valid_utf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i++]);
        if (c < 128) continue;
        unsigned value = 0, count = 0, minimum = 0;
        if (c >= 0xC2 && c <= 0xDF) { count = 1; value = c & 31; minimum = 128; }
        else if (c >= 0xE0 && c <= 0xEF) { count = 2; value = c & 15; minimum = 2048; }
        else if (c >= 0xF0 && c <= 0xF4) { count = 3; value = c & 7; minimum = 65536; }
        else return false;
        if (i + count > s.size()) return false;
        for (unsigned j = 0; j < count; ++j) {
            c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xC0) != 0x80) return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
}
inline void utf8_append(std::string& s, unsigned code) {
    if (code < 128) s += static_cast<char>(code);
    else if (code < 2048) { s += static_cast<char>(192 | (code >> 6)); s += static_cast<char>(128 | (code & 63)); }
    else if (code < 65536) {
        s += static_cast<char>(224 | (code >> 12)); s += static_cast<char>(128 | ((code >> 6) & 63)); s += static_cast<char>(128 | (code & 63));
    } else {
        s += static_cast<char>(240 | (code >> 18)); s += static_cast<char>(128 | ((code >> 12) & 63));
        s += static_cast<char>(128 | ((code >> 6) & 63)); s += static_cast<char>(128 | (code & 63));
    }
}
inline std::string quote(const std::string& s) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + "\"";
}

struct Json {
    enum Kind { Null, Number, Boolean, String, Array, Object } kind = Null;
    double number = 0;
    bool boolean = false;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    const Json& at(const std::string& name) const {
        auto it = object.find(name);
        if (kind != Object || it == object.end()) throw std::runtime_error("Champ manquant : " + name);
        return it->second;
    }
    std::string text() const { if (kind != String) throw std::runtime_error("Texte attendu"); return string; }
    double num() const { if (kind != Number) throw std::runtime_error("Nombre attendu"); return number; }
    bool flag() const { if (kind != Boolean) throw std::runtime_error("Booleen attendu"); return boolean; }
    long long integer(long long low, long long high) const {
        auto n = num();
        if (!std::isfinite(n) || std::floor(n) != n || n < static_cast<double>(low) || n > static_cast<double>(high))
            throw std::runtime_error("Entier hors limites");
        return static_cast<long long>(n);
    }
};
class Parser {
    const std::string& s;
    size_t pos = 0;
    [[noreturn]] void fail() const { throw std::runtime_error("JSON invalide a l'octet " + std::to_string(pos)); }
    void space() { while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) ++pos; }
    bool eat(char c) { space(); if (pos < s.size() && s[pos] == c) { ++pos; return true; } return false; }
    unsigned hex4() {
        unsigned result = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos >= s.size()) fail();
            unsigned char c = static_cast<unsigned char>(s[pos++]);
            unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (digit == 16) fail();
            result = result * 16 + digit;
        }
        return result;
    }
    std::string text() {
        if (!eat('"')) fail();
        std::string out;
        while (pos < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[pos++]);
            if (c == '"') { if (!valid_utf8(out)) fail(); return out; }
            if (c < 32) fail();
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos >= s.size()) fail();
            c = static_cast<unsigned char>(s[pos++]);
            switch (c) {
                case '"': case '\\': case '/': out += static_cast<char>(c); break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned code = hex4();
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (pos + 2 > s.size() || s[pos] != '\\' || s[pos+1] != 'u') fail();
                        pos += 2;
                        unsigned low = hex4();
                        if (low < 0xDC00 || low > 0xDFFF) fail();
                        code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
                    } else if (code >= 0xDC00 && code <= 0xDFFF) fail();
                    utf8_append(out, code);
                    break;
                }
                default: fail();
            }
        }
        fail();
    }
    Json value(int depth) {
        if (depth > 70) fail();
        space();
        if (pos >= s.size()) fail();
        Json j;
        if (s[pos] == '"') { j.kind = Json::String; j.string = text(); return j; }
        if (eat('{')) {
            j.kind = Json::Object;
            if (eat('}')) return j;
            do {
                std::string key = text();
                if (!eat(':')) fail();
                if (!j.object.emplace(key, value(depth + 1)).second) fail();
            } while (eat(','));
            if (!eat('}')) fail();
            return j;
        }
        if (eat('[')) {
            j.kind = Json::Array;
            if (eat(']')) return j;
            do { j.array.push_back(value(depth + 1)); if (j.array.size() > 10000) fail(); } while (eat(','));
            if (!eat(']')) fail();
            return j;
        }
        for (const char* literal : {"true", "false", "null"}) {
            const auto length = std::char_traits<char>::length(literal);
            if (s.compare(pos, length, literal) == 0) {
                pos += length;
                j.kind = literal[0] == 'n' ? Json::Null : Json::Boolean;
                j.boolean = literal[0] == 't';
                return j;
            }
        }
        size_t start = pos;
        if (s[pos] == '-') ++pos;
        if (pos >= s.size()) fail();
        if (s[pos] == '0') ++pos;
        else { if (s[pos] < '1' || s[pos] > '9') fail(); while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos; }
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            const size_t digits = pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
            if (pos == digits) fail();
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            const size_t digits = pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
            if (pos == digits) fail();
        }
        j.kind = Json::Number;
        auto numeric = s.substr(start, pos - start);
        auto converted = std::from_chars(numeric.data(), numeric.data() + numeric.size(), j.number);
        if (converted.ec != std::errc{} || converted.ptr != numeric.data() + numeric.size() || !std::isfinite(j.number)) fail();
        return j;
    }
public:
    explicit Parser(const std::string& input) : s(input) {}
    Json parse() {
        if (s.size() > max_file_bytes) throw std::runtime_error("Fichier trop volumineux (maximum 8 Mio)");
        if (s.size() >= 3 && s.compare(0, 3, "\xef\xbb\xbf") == 0) pos = 3;
        Json result = value(0);
        space();
        if (pos != s.size()) fail();
        return result;
    }
};
struct State {
    std::shared_ptr<MindMap> map = std::make_shared<MindMap>("Mon sujet");
    long long selected = 1;
    double zoom = 1, pan_x = 260, pan_y = 340;
};
inline std::string encode(const State& state) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "{\"format\":\"mindmap-portable\",\"version\":1,\"next_id\":" << state.map->next_id
        << ",\"selected\":" << state.selected << ",\"view\":{\"zoom\":" << state.zoom << ",\"x\":" << state.pan_x << ",\"y\":" << state.pan_y << "},\"nodes\":[";
    bool first = true;
    // Depth-first order preserves sibling order, including after reparenting.
    for (long long id : state.map->descendants(1)) {
        auto n = state.map->nodes.at(id);
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << n->id << ",\"parent\":" << n->parent << ",\"title\":" << quote(n->title)
            << ",\"note\":" << quote(n->note) << ",\"x\":" << n->x << ",\"y\":" << n->y
            << ",\"color\":" << n->color << ",\"collapsed\":" << (n->collapsed ? "true" : "false") << '}';
    }
    auto encoded=out.str() + "]}\n";
    if(encoded.size()>max_file_bytes)throw std::runtime_error("Carte trop volumineuse (8 Mio maximum) : raccourcissez les notes");
    return encoded;
}
inline State decode(const std::string& input) {
    Json j = Parser(input).parse();
    if (j.at("format").text() != "mindmap-portable" || j.at("version").integer(1, 1) != 1)
        throw std::runtime_error("Format ou version non pris en charge");
    State result;
    result.map->nodes.clear();
    const auto& nodes = j.at("nodes");
    if (nodes.kind != Json::Array || nodes.array.empty() || nodes.array.size() > 2000) throw std::runtime_error("Nombre d'idees invalide");
    long long max_id = 1;
    for (const auto& item : nodes.array) {
        long long id = item.at("id").integer(1, 2147483646);
        if (result.map->nodes.count(id)) throw std::runtime_error("Identifiant duplique");
        auto title = item.at("title").text(), note = item.at("note").text();
        if (title.empty() || title.size() > 1024 || note.size() > 16000 || title.find('\0') != std::string::npos || note.find('\0') != std::string::npos)
            throw std::runtime_error("Titre ou note hors limites");
        auto n = std::make_shared<MapNode>(id, item.at("parent").integer(-1, 2147483646), title, note,
            item.at("x").num(), item.at("y").num(), item.at("color").integer(0, 0xFFFFFF), item.at("collapsed").flag(), PyList<long long>());
        if (std::abs(n->x) > 1000000 || std::abs(n->y) > 1000000) throw std::runtime_error("Position hors limites");
        result.map->nodes[id] = n;
        max_id = std::max(max_id, id);
    }
    if (!result.map->nodes.count(1) || result.map->nodes.at(1)->parent != -1) throw std::runtime_error("Sujet central absent");
    for (const auto& pair : result.map->nodes) {
        auto n = pair.second;
        if (n->id == 1) continue;
        if (n->parent == n->id || !result.map->nodes.count(n->parent)) throw std::runtime_error("Parent absent ou circulaire");
        result.map->nodes.at(n->parent)->children.push_back(n->id);
        std::set<long long> visited;
        long long cursor = n->id;
        int depth = 0;
        while (cursor != 1) {
            if (!result.map->nodes.count(cursor) || !visited.insert(cursor).second || ++depth > 63)
                throw std::runtime_error("Cycle, branche orpheline ou profondeur excessive");
            cursor = result.map->nodes.at(cursor)->parent;
        }
    }
    result.map->next_id = j.at("next_id").integer(max_id + 1, 2147483647);
    result.selected = j.at("selected").integer(1, 2147483646);
    if (!result.map->nodes.count(result.selected)) result.selected = 1;
    const auto& view = j.at("view");
    result.zoom = view.at("zoom").num(); result.pan_x = view.at("x").num(); result.pan_y = view.at("y").num();
    if (result.zoom < .2 || result.zoom > 3 || std::abs(result.pan_x) > 10000000 || std::abs(result.pan_y) > 10000000)
        throw std::runtime_error("Vue hors limites");
    return result;
}
inline std::string xml(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) { case '&': result += "&amp;"; break; case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break; case '"': result += "&quot;"; break; case '\'': result += "&apos;"; break;
            default: if (static_cast<unsigned char>(c) >= 32 || c == '\n' || c == '\t') result += c; }
    }
    return result;
}
struct Bounds { double left=0, top=0, right=0, bottom=0; };
inline Bounds bounds(const State& state) {
    Bounds b{1e9, 1e9, -1e9, -1e9};
    for (auto id : state.map->visible()) {
        auto n = state.map->nodes.at(id);
        b.left = std::min(b.left, n->x - 110); b.right = std::max(b.right, n->x + 110);
        b.top = std::min(b.top, n->y - 40); b.bottom = std::max(b.bottom, n->y + 40);
    }
    return b;
}
inline std::string svg(const State& state) {
    auto b = bounds(state);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << b.left-30 << ' ' << b.top-30 << ' '
        << b.right-b.left+60 << ' ' << b.bottom-b.top+60 << "\">\n<rect x=\"" << b.left-30 << "\" y=\"" << b.top-30
        << "\" width=\"100%\" height=\"100%\" fill=\"#f5f7fc\"/>\n";
    for (auto id : state.map->visible()) {
        auto n = state.map->nodes.at(id);
        if (n->parent == -1) continue;
        auto p = state.map->nodes.at(n->parent);
        out << "<path d=\"M " << p->x+100 << ' ' << p->y << " C " << p->x+180 << ' ' << p->y << ' ' << n->x-180 << ' ' << n->y
            << ' ' << n->x-100 << ' ' << n->y << "\" stroke=\"#a5afd0\" stroke-width=\"2\" fill=\"none\"/>\n";
    }
    for (auto id : state.map->visible()) {
        auto n = state.map->nodes.at(id);
        std::ostringstream color; color << '#' << std::hex << std::setfill('0') << std::setw(6) << n->color;
        out << "<g><title>" << xml(n->title + "\n" + n->note) << "</title><rect x=\"" << n->x-100 << "\" y=\"" << n->y-34
            << "\" width=\"200\" height=\"68\" rx=\"14\" fill=\"white\" stroke=\"" << color.str() << "\" stroke-width=\"2\"/>"
            << "<clipPath id=\"clip" << id << "\"><rect x=\"" << n->x-90 << "\" y=\"" << n->y-30 << "\" width=\"180\" height=\"60\"/></clipPath>";
        std::vector<std::string> lines;std::string line;size_t count=0;
        for(size_t pos=0;pos<n->title.size();){
            unsigned char c=static_cast<unsigned char>(n->title[pos]);
            size_t bytes=c<128?1:c<224?2:c<240?3:4;
            if(count>=23 || c=='\n' || c=='\r'){
                lines.push_back(line);line.clear();count=0;
                if(lines.size()>=3){lines.back()+="…";break;}
                if(c=='\n'||c=='\r'){++pos;continue;}
            }
            line+=n->title.substr(pos,bytes);pos+=bytes;++count;
        }
        if(lines.size()<3&&!line.empty())lines.push_back(line);
        out << "<text text-anchor=\"middle\" font-family=\"Segoe UI, sans-serif\" font-size=\"13\" fill=\"#253047\" clip-path=\"url(#clip" << id << ")\">";
        for(size_t i=0;i<lines.size();++i)out << "<tspan x=\"" << n->x << "\" y=\"" << n->y+5-static_cast<double>(lines.size()-1)*8+static_cast<double>(i)*16 << "\">" << xml(lines[i]) << "</tspan>";
        out << "</text></g>\n";
    }
    return out.str() + "</svg>\n";
}
inline std::string markdown(const State& state) {
    std::string out = "# " + state.map->nodes.at(1)->title + "\n\n";
    if (!state.map->nodes.at(1)->note.empty()) out += state.map->nodes.at(1)->note + "\n\n";
    for (auto id : state.map->descendants(1)) {
        if (id == 1) continue;
        auto n = state.map->nodes.at(id);
        size_t depth = static_cast<size_t>(state.map->depth(id));
        std::string title = n->title;
        std::replace(title.begin(), title.end(), '\n', ' '); std::replace(title.begin(), title.end(), '\r', ' ');
        out += std::string((depth-1)*2, ' ') + "- " + title + "\n";
        if (!n->note.empty()) {
            std::istringstream lines(n->note); std::string line;
            while (std::getline(lines, line)) out += std::string(depth*2, ' ') + "> " + line + "\n";
        }
    }
    return out;
}
} // namespace document
