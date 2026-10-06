#pragma once
// Exports (SVG, Markdown), outline import and branch operations that also carry links and markers.
#include "geometry.hpp"

namespace document {
struct MarkerInfo { const char* glyph; const char* name; const char* markdown; long long color; };
inline const MarkerInfo& marker_info(int marker) {
    static const MarkerInfo info[marker_count] = {
        {"", "Aucun repère", "", 0x8892A8},
        {"★", "Important", "★ ", 0xE0A21B},
        {"✓", "Acquis", "✓ ", 0x2E9E5B},
        {"?", "À revoir", "[?] ", 0xE07B24},
        {"!", "Attention", "[!] ", 0xD64545},
        {"⚑", "Repère clé (date, lieu)", "⚑ ", 0x3D7BD9},
    };
    return info[std::clamp(marker, 0, marker_count - 1)];
}
inline const char* link_presets[] = {"cause de", "conséquence", "s’oppose à", "exemple de", "influence", "même période", "voir aussi"};

// Copies a branch with its markers and with the links that stay inside the branch.
inline long long duplicate_branch(State& state, long long id) {
    auto original = state.map->descendants(id);
    long long copy = state.map->duplicate(id);
    auto copied = state.map->descendants(copy);
    std::map<long long, long long> remap;
    for (size_t i = 0; i < original.size() && i < copied.size(); ++i) {
        remap[original[i]] = copied[i];
        if (int marker = marker_of(state, original[i])) state.markers[copied[i]] = marker;
    }
    auto links = state.links;
    for (const auto& l : links)
        if (remap.count(l.from) && remap.count(l.to) && state.links.size() < max_links) add_link(state, remap[l.from], remap[l.to], l.label);
    return copy;
}

inline std::string single_line(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' '); std::replace(text.begin(), text.end(), '\r', ' ');
    return text;
}
inline void markdown_bullets(const State& state, long long id, size_t depth, std::string& out) {
    auto n = state.map->nodes.at(id);
    out += std::string(depth * 2, ' ') + "- " + marker_info(marker_of(state, id)).markdown + single_line(n->title) + "\n";
    if (!n->note.empty()) {
        std::istringstream lines(n->note); std::string line;
        while (std::getline(lines, line)) out += std::string(depth * 2 + 2, ' ') + "> " + line + "\n";
    }
    for (auto child : n->children) markdown_bullets(state, child, depth + 1, out);
}
inline std::string markdown(const State& state) {
    auto root = state.map->nodes.at(1);
    std::string out = "# " + std::string(marker_info(marker_of(state, 1)).markdown) + single_line(root->title) + "\n\n";
    if (!root->note.empty()) out += root->note + "\n\n";
    for (auto child : root->children) markdown_bullets(state, child, 0, out);
    if (!state.links.empty()) {
        out += "\n## Liens transversaux\n\n";
        for (const auto& l : state.links) {
            out += "- " + single_line(state.map->nodes.at(l.from)->title) + " → " + single_line(state.map->nodes.at(l.to)->title);
            if (!l.label.empty()) out += " (" + l.label + ")";
            out += "\n";
        }
    }
    return out;
}
// A single branch as an indented list, ready for the clipboard or another map.
inline std::string branch_markdown(const State& state, long long id) {
    std::string out;
    markdown_bullets(state, id, 0, out);
    return out;
}

struct OutlineItem { int level = 0; std::string title, note; int marker = 0; };
inline std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}
inline std::string clip_utf8(std::string s, size_t bytes) {
    if (s.size() <= bytes) return s;
    s.resize(bytes);
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty() && static_cast<unsigned char>(s.back()) >= 0xC0) s.pop_back();
    return s;
}
// Reads Markdown headings and lists, "> " notes, or plain text indented with spaces or tabs.
inline std::vector<OutlineItem> parse_outline(const std::string& input) {
    std::string text = input.compare(0, 3, "\xef\xbb\xbf") == 0 ? input.substr(3) : input;
    if (!valid_utf8(text)) throw std::runtime_error("Le texte n'est pas en UTF-8");
    std::vector<std::string> lines;
    { std::istringstream stream(text); std::string line; while (std::getline(stream, line)) lines.push_back(line); }
    auto is_bullet = [](const std::string& c, size_t& skip) {
        if (c.size() >= 2 && (c[0] == '-' || c[0] == '*' || c[0] == '+') && c[1] == ' ') { skip = 2; return true; }
        size_t digits = 0;
        while (digits < c.size() && std::isdigit(static_cast<unsigned char>(c[digits]))) ++digits;
        if (digits && digits + 1 < c.size() && (c[digits] == '.' || c[digits] == ')') && c[digits+1] == ' ') { skip = digits + 2; return true; }
        return false;
    };
    bool structured = false;
    for (const auto& line : lines) {
        auto c = trim(line); size_t skip = 0;
        if (!c.empty() && (c[0] == '#' || is_bullet(c, skip))) { structured = true; break; }
    }
    std::vector<OutlineItem> items;
    std::vector<int> indents;
    int base = 0;
    for (const auto& line : lines) {
        int indent = 0;
        for (char ch : line) { if (ch == ' ') ++indent; else if (ch == '\t') indent += 4; else break; }
        auto content = trim(line);
        if (content.empty()) continue;
        if (content == "## Liens transversaux") break;
        if (content[0] == '>') {
            if (items.empty()) continue;
            auto quoted = content.size() > 1 && content[1] == ' ' ? content.substr(2) : content.substr(1);
            auto& note = items.back().note;
            note += (note.empty() ? "" : "\n") + quoted;
            continue;
        }
        OutlineItem item;
        size_t hashes = 0;
        while (hashes < content.size() && hashes < 7 && content[hashes] == '#') ++hashes;
        size_t skip = 0;
        if (hashes && hashes <= 6 && (hashes == content.size() || content[hashes] == ' ')) {
            item.level = static_cast<int>(hashes) - 1;
            item.title = trim(std::string_view(content).substr(hashes));
            base = item.level + 1;
            indents.clear();
        } else {
            const bool bullet = is_bullet(content, skip);
            if (!bullet && structured && !items.empty()) {
                auto& note = items.back().note;
                note += (note.empty() ? "" : "\n") + content;
                continue;
            }
            while (!indents.empty() && indents.back() > indent) indents.pop_back();
            if (indents.empty() || indents.back() < indent) indents.push_back(indent);
            item.level = base + static_cast<int>(indents.size()) - 1;
            item.title = trim(std::string_view(content).substr(skip));
        }
        for (int marker = 1; marker < marker_count; ++marker) {
            std::string prefix = marker_info(marker).markdown;
            if (item.title.compare(0, prefix.size(), prefix) == 0) { item.marker = marker; item.title.erase(0, prefix.size()); break; }
        }
        if (item.title.compare(0, 4, "[x] ") == 0 || item.title.compare(0, 4, "[X] ") == 0) { item.marker = 2; item.title.erase(0, 4); }
        else if (item.title.compare(0, 4, "[ ] ") == 0) item.title.erase(0, 4);
        if (item.title.size() > 4 && item.title.compare(0, 2, "**") == 0 && item.title.compare(item.title.size() - 2, 2, "**") == 0)
            item.title = item.title.substr(2, item.title.size() - 4);
        item.title = clip_utf8(trim(item.title), 1024);
        if (item.title.empty()) continue;
        items.push_back(std::move(item));
    }
    int previous = -1, lowest = 1 << 20;
    for (const auto& item : items) lowest = std::min(lowest, item.level);
    for (auto& item : items) {
        item.level = std::min(item.level - lowest, previous + 1);
        previous = item.level;
        item.note = clip_utf8(item.note, 16000);
    }
    return items;
}
// Inserts the outline under parent; levels are relative to it. Returns the created ideas.
inline std::vector<long long> apply_outline(State& state, long long parent, const std::vector<OutlineItem>& items) {
    if (items.empty()) throw std::runtime_error("Aucune idée trouvée dans ce texte");
    if (state.map->nodes.size() + items.size() > 2000) throw std::runtime_error("Limite de 2000 idées : le plan est trop long");
    int deepest = 0;
    for (const auto& item : items) deepest = std::max(deepest, item.level);
    if (state.map->depth(parent) + 1 + deepest > 63) throw std::runtime_error("Limite de profondeur atteinte");
    std::vector<long long> created, stack;
    for (const auto& item : items) {
        stack.resize(static_cast<size_t>(item.level));
        long long id = state.map->add(item.level ? stack.back() : parent, item.title);
        state.map->rename(id, item.title, item.note);
        if (item.marker) state.markers[id] = item.marker;
        stack.push_back(id);
        created.push_back(id);
    }
    return created;
}
// A whole document from an outline: a single top-level title becomes the central idea.
inline State from_outline(std::vector<OutlineItem> items, const std::string& fallback_title) {
    if (items.empty()) throw std::runtime_error("Aucune idée trouvée dans ce texte");
    State result;
    auto root = result.map->nodes.at(1);
    size_t top_level = 0;
    for (const auto& item : items) top_level += item.level == 0;
    if (top_level == 1 && items[0].level == 0) {
        root->title = items[0].title; root->note = items[0].note;
        if (items[0].marker) result.markers[1] = items[0].marker;
        items.erase(items.begin());
        for (auto& item : items) --item.level;
    } else root->title = fallback_title.empty() ? "Plan importé" : clip_utf8(fallback_title, 1024);
    if (!items.empty()) apply_outline(result, 1, items);
    layout(result);
    return result;
}

inline std::string hex_color(long long color) {
    std::ostringstream out; out << '#' << std::hex << std::setfill('0') << std::setw(6) << (color & 0xFFFFFF);
    return out.str();
}
// Bounds of the visible ideas and of the cross links that arc around them.
inline Bounds full_bounds(const State& state) {
    auto b = bounds(state);
    for (const auto& l : state.links) {
        const auto from = visible_anchor(state, l.from), to = visible_anchor(state, l.to);
        if (from == to) continue;
        auto c = link_curve(box(state, from), box(state, to));
        for (auto p : {c.at(.5), c.at(.25), c.at(.75)}) {
            b.left = std::min(b.left, p.x - 60); b.right = std::max(b.right, p.x + 60);
            b.top = std::min(b.top, p.y - 20); b.bottom = std::max(b.bottom, p.y + 20);
        }
    }
    return b;
}
inline std::string svg(const State& state) {
    auto b = full_bounds(state);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << b.left-30 << ' ' << b.top-30 << ' '
        << b.right-b.left+60 << ' ' << b.bottom-b.top+60 << "\">\n"
        << "<defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"7\" markerHeight=\"7\" orient=\"auto-start-reverse\">"
        << "<path d=\"M0 0L10 5L0 10z\" fill=\"#7b5bd6\"/></marker></defs>\n"
        << "<rect x=\"" << b.left-30 << "\" y=\"" << b.top-30 << "\" width=\"100%\" height=\"100%\" fill=\"#f5f7fc\"/>\n";
    auto path = [&](const Curve& c) {
        std::ostringstream d; d.imbue(std::locale::classic());
        d << "M " << c.a.x << ' ' << c.a.y << " C " << c.b.x << ' ' << c.b.y << ' ' << c.c.x << ' ' << c.c.y << ' ' << c.d.x << ' ' << c.d.y;
        return d.str();
    };
    auto visible = state.map->visible();
    for (auto id : visible) {
        auto n = state.map->nodes.at(id);
        if (n->parent == -1) continue;
        out << "<path d=\"" << path(connector(box(state, n->parent), box(state, id))) << "\" stroke=\"" << hex_color(n->color)
            << "\" stroke-opacity=\"0.55\" stroke-width=\"2\" fill=\"none\"/>\n";
    }
    for (auto id : visible) {
        auto n = state.map->nodes.at(id);
        auto r = box(state, id); auto m = metrics(state, id);
        const bool root = id == 1;
        out << "<g><title>" << xml(n->title + "\n" + n->note) << "</title><rect x=\"" << r.left() << "\" y=\"" << r.top()
            << "\" width=\"" << r.w << "\" height=\"" << r.h << "\" rx=\"14\" fill=\"" << (root ? hex_color(n->color) : "white")
            << "\" stroke=\"" << hex_color(n->color) << "\" stroke-width=\"2\"/>";
        if (!root) out << "<rect x=\"" << r.left()+10 << "\" y=\"" << r.top()+14 << "\" width=\"3\" height=\"" << r.h-28 << "\" fill=\"" << hex_color(n->color) << "\"/>";
        auto lines = wrap(n->title, m.title_chars, max_title_lines);
        const double line_h = root ? root_line : title_line;
        double y = r.top() + 13 + (r.h - 26 - m.title_h - m.note_h) / 2 + line_h * .75;
        out << "<text text-anchor=\"middle\" font-family=\"Segoe UI, sans-serif\" font-size=\"" << (root ? 16 : 14) << "\""
            << (root ? " font-weight=\"bold\"" : "") << " fill=\"" << (root ? "#ffffff" : "#253047") << "\">";
        for (const auto& line : lines) { out << "<tspan x=\"" << r.x << "\" y=\"" << y << "\">" << xml(line) << "</tspan>"; y += line_h; }
        out << "</text>";
        if (m.note_h > 0) {
            y += 4;
            out << "<text font-family=\"Segoe UI, sans-serif\" font-size=\"12\" fill=\"" << (root ? "#eef0ff" : "#66718a") << "\">";
            for (const auto& line : wrap(n->note, m.note_chars, max_note_lines)) { out << "<tspan x=\"" << r.left()+18 << "\" y=\"" << y << "\">" << xml(line) << "</tspan>"; y += note_line; }
            out << "</text>";
        }
        if (int marker = marker_of(state, id)) {
            const auto& info = marker_info(marker);
            out << "<circle cx=\"" << r.left()+6 << "\" cy=\"" << r.top()+6 << "\" r=\"11\" fill=\"" << hex_color(info.color) << "\" stroke=\"white\" stroke-width=\"2\"/>"
                << "<text x=\"" << r.left()+6 << "\" y=\"" << r.top()+10.5 << "\" text-anchor=\"middle\" font-family=\"Segoe UI Symbol, sans-serif\" font-size=\"12\" font-weight=\"bold\" fill=\"white\">" << info.glyph << "</text>";
        }
        out << "</g>\n";
    }
    for (const auto& l : state.links) {
        const auto from = visible_anchor(state, l.from), to = visible_anchor(state, l.to);
        if (from == to) continue;
        auto c = link_curve(box(state, from), box(state, to));
        out << "<path d=\"" << path(c) << "\" stroke=\"#7b5bd6\" stroke-width=\"2\" stroke-dasharray=\"7 5\" fill=\"none\" marker-end=\"url(#arrow)\"/>\n";
        if (!l.label.empty()) {
            auto mid = c.at(.5);
            const double w = static_cast<double>(glyphs(l.label)) * 6.6 + 16;
            out << "<rect x=\"" << mid.x - w/2 << "\" y=\"" << mid.y - 11 << "\" width=\"" << w << "\" height=\"22\" rx=\"11\" fill=\"white\" stroke=\"#7b5bd6\"/>"
                << "<text x=\"" << mid.x << "\" y=\"" << mid.y + 4 << "\" text-anchor=\"middle\" font-family=\"Segoe UI, sans-serif\" font-size=\"12\" fill=\"#5b3fb8\">" << xml(l.label) << "</text>\n";
        }
    }
    return out.str() + "</svg>\n";
}
} // namespace document
