#pragma once
#include "core_bridge.hpp"
#include "media.hpp"
#include <sstream>
#include <iomanip>
#include <set>
#include <cctype>
#include <charconv>

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
    int max_depth;
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
        if (depth > max_depth) fail();
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
    explicit Parser(const std::string& input,int maximum_depth=70) : s(input),max_depth(maximum_depth) {}
    Json parse() {
        if (s.size() > max_file_bytes) throw std::runtime_error("Fichier trop volumineux (maximum 8 Mio)");
        if (s.size() >= 3 && s.compare(0, 3, "\xef\xbb\xbf") == 0) pos = 3;
        Json result = value(0);
        space();
        if (pos != s.size()) fail();
        return result;
    }
};
// Layouts: tree growing to the right (v1 behaviour), branches balanced on both sides, or 360° rings.
enum Layout { LayoutRight = 0, LayoutBalanced = 1, LayoutRadial = 2 };
// Markers: none, important, learnt, to review, warning, key landmark (date/place).
constexpr int marker_count = 6;
constexpr size_t max_links = 1000, max_link_label = 200;
struct Link {
    long long from = 0, to = 0;
    std::string label;
    bool operator==(const Link&) const = default;
};
struct State {
    std::shared_ptr<MindMap> map = std::make_shared<MindMap>("Mon sujet");
    long long selected = 1;
    double zoom = 1, pan_x = 260, pan_y = 340;
    int layout = LayoutBalanced;
    bool show_notes = false, dark = false, minimap = true;
    // Native extensions stored next to the generated tree, keyed by node id.
    std::vector<Link> links;
    std::map<long long, int> markers;
    std::map<long long, Picture> pictures;
    // Sub-maps: idea id -> path of another .mindmap, relative to this file's folder ('/' separators, UTF-8).
    std::map<long long, std::string> submaps;
};
constexpr size_t max_submap_path = 1024;
// Relative, without '..' or drive letters: a sub-map always lives next to or below its parent map.
inline bool valid_submap_path(const std::string& p) {
    constexpr std::string_view suffix = ".mindmap";
    if (p.empty() || p.size() > max_submap_path || p.front() == '/' || p.find('\\') != std::string::npos
        || p.find('\0') != std::string::npos || p.find(':') != std::string::npos) return false;
    if (p.size() <= suffix.size() || p.compare(p.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    size_t at = 0;
    for (;;) {
        auto end = p.find('/', at);
        if (end == std::string::npos) end = p.size();
        auto part = p.substr(at, end - at);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == p.size()) return true;
        at = end + 1;
    }
}
inline int marker_of(const State& state, long long id) {
    auto it = state.markers.find(id);
    return it == state.markers.end() ? 0 : it->second;
}
// Drops links and markers whose ideas no longer exist (after removal, undo, import...).
inline void prune(State& state) {
    auto& nodes = state.map->nodes;
    std::erase_if(state.links, [&](const Link& l) { return !nodes.count(l.from) || !nodes.count(l.to) || l.from == l.to; });
    std::erase_if(state.markers, [&](const auto& m) { return !nodes.count(m.first) || m.second <= 0 || m.second >= marker_count; });
    std::erase_if(state.pictures, [&](const auto& p) { return !nodes.count(p.first); });
    std::erase_if(state.submaps, [&](const auto& p) { return !nodes.count(p.first); });
}
inline bool add_link(State& state, long long from, long long to, const std::string& label = "") {
    if (from == to) throw std::runtime_error("Une idée ne peut pas être reliée à elle-même");
    if (!state.map->nodes.count(from) || !state.map->nodes.count(to)) throw std::runtime_error("Idée absente");
    for (const auto& l : state.links)
        if ((l.from == from && l.to == to) || (l.from == to && l.to == from)) return false;
    if (state.links.size() >= max_links) throw std::runtime_error("Limite de 1 000 liens atteinte");
    state.links.push_back({from, to, label.substr(0, max_link_label)});
    return true;
}
inline std::string encode(const State& state) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "{\"format\":\"mindmap-portable\",\"version\":" << (state.submaps.empty() ? 3 : 4) << ",\"next_id\":" << state.map->next_id
        << ",\"selected\":" << state.selected << ",\"view\":{\"zoom\":" << state.zoom << ",\"x\":" << state.pan_x << ",\"y\":" << state.pan_y
        << ",\"layout\":" << state.layout << ",\"notes\":" << (state.show_notes ? "true" : "false")
        << ",\"dark\":" << (state.dark ? "true" : "false") << ",\"minimap\":" << (state.minimap ? "true" : "false") << "},\"nodes\":[";
    bool first = true;
    // Depth-first order preserves sibling order, including after reparenting.
    for (long long id : state.map->descendants(1)) {
        auto n = state.map->nodes.at(id);
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << n->id << ",\"parent\":" << n->parent << ",\"title\":" << quote(n->title)
            << ",\"note\":" << quote(n->note) << ",\"x\":" << n->x << ",\"y\":" << n->y
            << ",\"color\":" << n->color << ",\"collapsed\":" << (n->collapsed ? "true" : "false");
        if (int marker = marker_of(state, id)) out << ",\"marker\":" << marker;
        if(auto p=state.pictures.find(id);p!=state.pictures.end())out<<",\"picture\":"<<quote(p->second.data);
        if(auto p=state.submaps.find(id);p!=state.submaps.end())out<<",\"submap\":"<<quote(p->second);
        out << '}';
    }
    out << "],\"links\":[";
    first = true;
    for (const auto& l : state.links) {
        if (!first) out << ',';
        first = false;
        out << "{\"from\":" << l.from << ",\"to\":" << l.to << ",\"label\":" << quote(l.label) << '}';
    }
    auto encoded=out.str() + "]}\n";
    if(encoded.size()>max_file_bytes)throw std::runtime_error("Carte trop volumineuse (8 Mio maximum) : retirez des images ou raccourcissez les notes");
    return encoded;
}
inline State decode(const std::string& input) {
    Json j = Parser(input).parse();
    if (j.at("format").text() != "mindmap-portable")
        throw std::runtime_error("Format ou version non pris en charge");
    // Version 1 files (before links, markers and layouts) still open unchanged.
    const auto version = j.at("version").integer(1, 4);
    auto optional = [](const Json& owner, const char* name) -> const Json* {
        auto it = owner.object.find(name);
        return owner.kind == Json::Object && it != owner.object.end() ? &it->second : nullptr;
    };
    State result;
    if (version == 1) result.layout = LayoutRight;
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
        if (auto marker = optional(item, "marker")) {
            auto value = marker->integer(0, marker_count - 1);
            if (value) result.markers[id] = static_cast<int>(value);
        }
        result.map->nodes[id] = n;
        if(auto picture=optional(item,"picture"))result.pictures[id]=picture_from_png(unbase64(picture->text()));
        if(auto submap=optional(item,"submap")){
            auto path=submap->text();
            if(!valid_submap_path(path))throw std::runtime_error("Chemin de sous-carte invalide");
            result.submaps[id]=path;
        }
        max_id = std::max(max_id, id);
    }
    if (!result.map->nodes.count(1) || result.map->nodes.at(1)->parent != -1) throw std::runtime_error("Sujet central absent");
    for (const auto& item : nodes.array) {
        auto n = result.map->nodes.at(item.at("id").integer(1,2147483646));
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
    if (auto layout = optional(view, "layout")) result.layout = static_cast<int>(layout->integer(0, 2));
    if (auto notes = optional(view, "notes")) result.show_notes = notes->flag();
    if (auto dark = optional(view, "dark")) result.dark = dark->flag();
    if (auto minimap = optional(view, "minimap")) result.minimap = minimap->flag();
    if (auto links = optional(j, "links")) {
        if (links->kind != Json::Array || links->array.size() > max_links) throw std::runtime_error("Liens invalides");
        for (const auto& item : links->array) {
            Link l{item.at("from").integer(1, 2147483646), item.at("to").integer(1, 2147483646), item.at("label").text()};
            if (l.label.size() > max_link_label || l.label.find('\0') != std::string::npos) throw std::runtime_error("Libellé de lien hors limites");
            if (!add_link(result, l.from, l.to, l.label)) throw std::runtime_error("Lien duplique");
        }
    }
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
} // namespace document
