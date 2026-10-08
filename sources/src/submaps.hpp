#pragma once
// Sub-maps: an idea can point to another .mindmap file, which can itself have ideas with sub-maps
// (histoire → thème 1 → chapitre 1…). Each map stays an ordinary file, so nothing else changes.
#include "workspace.hpp"
#include <cstring>

namespace submap {
namespace fs = workspace::fs;
constexpr int max_depth = 16;
constexpr size_t max_maps = 200;

inline fs::path folder_of(const fs::path& file) { return fs::absolute(file).lexically_normal().parent_path(); }
inline document::State load(const fs::path& file) {
    if (!fs::is_regular_file(file)) throw std::runtime_error("Carte introuvable : " + workspace::path_text(file));
    return document::decode(workspace::read(file));
}
inline void store(const fs::path& file, document::State& state) { document::ensure_uids(state); workspace::write(file, document::encode(state)); }
inline bool same(const fs::path& a, const fs::path& b) {
    std::error_code error;
    if (fs::exists(a, error) && fs::exists(b, error)) return fs::equivalent(a, b, error);
    return fs::absolute(a).lexically_normal() == fs::absolute(b).lexically_normal();
}
inline fs::path resolve(const fs::path& parent_file, const std::string& relative) {
    return (folder_of(parent_file) / workspace::text_path(relative)).lexically_normal();
}
// Relative path stored in the parent map; refuses files outside the parent's folder tree.
inline std::string relative_path(const fs::path& parent_file, const fs::path& child_file) {
    auto relative = fs::absolute(child_file).lexically_normal().lexically_relative(folder_of(parent_file));
    auto text = workspace::path_text(relative);
    if (!document::valid_submap_path(text))
        throw std::runtime_error("La sous-carte doit être un fichier .mindmap placé dans le dossier de la carte parente ou dans un de ses sous-dossiers");
    return text;
}
// File-name safe version of an idea title (Windows forbidden characters and reserved device names).
inline std::string slug(const std::string& title) {
    std::string out;
    for (unsigned char c : title) out += (c < 32 || std::strchr("<>:\"/\\|?*", c)) ? '_' : static_cast<char>(c);
    out = document::clip_utf8(out, 80);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = "Sous-carte";
    std::string upper = out.substr(0, out.find('.'));
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                     "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (auto name : reserved) if (upper == name) { out = "_" + out; break; }
    return out;
}

// True when walking down the sub-maps of `from` reaches `target` (used to refuse loops).
inline bool reaches(const fs::path& from, const fs::path& target, int depth = 0) {
    if (same(from, target)) return true;
    if (depth > max_depth) return true;  // Too deep to be sane: treat as a loop.
    document::State state;
    try { state = load(from); } catch (...) { return false; }
    for (const auto& entry : state.submaps) if (reaches(resolve(from, entry.second), target, depth + 1)) return true;
    return false;
}
// Points an idea at an existing map file.
inline void attach(document::State& parent, const fs::path& parent_file, long long id, const fs::path& child_file) {
    if (parent_file.empty()) throw std::runtime_error("Enregistrez d'abord la carte parente");
    if (id == 1) throw std::runtime_error("Le sujet central ne peut pas avoir de sous-carte : choisissez une de ses idées");
    if (!parent.map->nodes.count(id)) throw std::runtime_error("Idée absente");
    if (parent.submaps.count(id)) throw std::runtime_error("Cette idée a déjà une sous-carte");
    if (!fs::exists(child_file)) throw std::runtime_error("Sous-carte introuvable");
    auto relative = relative_path(parent_file, child_file);
    load(child_file);  // Must be a readable map.
    if (reaches(child_file, parent_file)) throw std::runtime_error("Une carte ne peut pas se contenir elle-même (boucle de sous-cartes)");
    parent.submaps[id] = relative;
}
// Creates the sub-map file of an idea, in <parent folder>/<parent name>/<idea title>.mindmap.
// Its central subject is the idea; an optional Markdown outline fills it. The parent is not written.
inline fs::path create(document::State& parent, const fs::path& parent_file, long long id, const std::string& outline = "") {
    if (parent_file.empty()) throw std::runtime_error("Enregistrez d'abord la carte parente : une sous-carte est un fichier à côté d'elle");
    if (id == 1) throw std::runtime_error("Le sujet central ne peut pas avoir de sous-carte : choisissez une de ses idées");
    if (!parent.map->nodes.count(id)) throw std::runtime_error("Idée absente");
    if (parent.submaps.count(id)) throw std::runtime_error("Cette idée a déjà une sous-carte");
    auto node = parent.map->nodes.at(id);
    document::State child = outline.empty() ? document::State() : document::from_outline(document::parse_outline(outline), node->title);
    child.map->nodes.at(1)->title = node->title;
    if (outline.empty() || child.map->nodes.at(1)->note.empty()) child.map->nodes.at(1)->note = node->note;
    document::layout(child);
    auto folder = folder_of(parent_file) / parent_file.stem();
    fs::create_directories(folder);
    auto file = workspace::unique_file(folder, slug(node->title));
    store(file, child);
    parent.submaps[id] = relative_path(parent_file, file);
    return file;
}

enum class Status { Ok, Missing, Unreadable, Loop, TooDeep };
inline const char* status_name(Status s) {
    switch (s) { case Status::Ok: return "ok"; case Status::Missing: return "introuvable"; case Status::Unreadable: return "illisible";
        case Status::Loop: return "boucle"; default: return "trop profond"; }
}
struct Entry {
    fs::path file;
    std::string title, relative, via_title;
    long long via = 0;  // Idea of the parent map that holds this sub-map (0 for the root map).
    int depth = 0;
    size_t nodes = 0;
    Status status = Status::Ok;
};
// Every map reachable from `root`, parents before children, in the order of the ideas.
inline std::vector<Entry> hierarchy(const fs::path& root) {
    std::vector<Entry> out;
    std::vector<fs::path> chain;
    std::function<void(const fs::path&, Entry)> visit = [&](const fs::path& file, Entry entry) {
        entry.file = file;
        if (out.size() >= max_maps || entry.depth > max_depth) { entry.status = Status::TooDeep; out.push_back(entry); return; }
        for (const auto& ancestor : chain) if (same(ancestor, file)) { entry.status = Status::Loop; out.push_back(entry); return; }
        document::State state;
        if (!fs::exists(file)) { entry.status = Status::Missing; out.push_back(entry); return; }
        try { state = load(file); } catch (...) { entry.status = Status::Unreadable; out.push_back(entry); return; }
        entry.title = state.map->nodes.at(1)->title;
        entry.nodes = state.map->nodes.size();
        out.push_back(entry);
        chain.push_back(file);
        for (long long id : state.map->descendants(1)) {
            auto it = state.submaps.find(id);
            if (it == state.submaps.end()) continue;
            Entry child;
            child.relative = it->second; child.via = id; child.via_title = state.map->nodes.at(id)->title; child.depth = entry.depth + 1;
            child.title = child.via_title;
            visit(resolve(file, it->second), child);
        }
        chain.pop_back();
    };
    Entry first; first.depth = 0;
    visit(fs::absolute(root).lexically_normal(), first);
    return out;
}

// Map files in the folder created for `root`'s sub-maps (<root folder>/<root name>/**) that no map of the hierarchy points to.
inline std::vector<fs::path> orphans(const fs::path& root) {
    std::vector<fs::path> out;
    auto folder = folder_of(root) / root.stem();
    std::error_code error;
    if (!fs::is_directory(folder, error)) return out;
    const auto used = hierarchy(root);
    for (fs::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file() || it->path().extension() != ".mindmap") continue;
        bool known = false;
        for (const auto& entry : used) if (same(entry.file, it->path())) { known = true; break; }
        if (!known) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The whole hierarchy as one Markdown outline: each sub-map is inlined under the idea that holds it.
inline void deep_bullets(const fs::path& file, const document::State& state, long long id, size_t depth, std::string& out, std::vector<fs::path>& chain) {
    auto n = state.map->nodes.at(id);
    out += std::string(depth * 2, ' ') + "- " + document::marker_info(document::marker_of(state, id)).markdown + document::single_line(n->title) + "\n";
    if (!n->note.empty()) {
        std::istringstream lines(n->note); std::string line;
        while (std::getline(lines, line)) out += std::string(depth * 2 + 2, ' ') + "> " + line + "\n";
    }
    for (auto child : n->children) deep_bullets(file, state, child, depth + 1, out, chain);
    auto it = state.submaps.find(id);
    if (it == state.submaps.end()) return;
    auto child_file = resolve(file, it->second);
    const std::string pad((depth + 1) * 2, ' ');
    for (const auto& ancestor : chain) if (same(ancestor, child_file)) { out += pad + "- ⚠ boucle de sous-cartes : " + it->second + "\n"; return; }
    if (chain.size() > static_cast<size_t>(max_depth)) { out += pad + "- ⚠ sous-cartes trop profondes\n"; return; }
    document::State sub;
    try { sub = load(child_file); } catch (...) { out += pad + "- ⚠ sous-carte introuvable ou illisible : " + it->second + "\n"; return; }
    chain.push_back(child_file);
    for (auto child : sub.map->nodes.at(1)->children) deep_bullets(child_file, sub, child, depth + 1, out, chain);
    chain.pop_back();
}
inline std::string deep_markdown(const fs::path& file) {
    auto state = load(file);
    std::string out = "# " + std::string(document::marker_info(document::marker_of(state, 1)).markdown) + document::single_line(state.map->nodes.at(1)->title) + "\n\n";
    if (!state.map->nodes.at(1)->note.empty()) out += state.map->nodes.at(1)->note + "\n\n";
    std::vector<fs::path> chain{file};
    for (auto child : state.map->nodes.at(1)->children) deep_bullets(file, state, child, 0, out, chain);
    return out;
}
// Lowercase, accent-free form used to search: "Révolution" and "revolution" match each other.
inline std::string fold(const std::string& s) {
    static const char* latin1[32] = {"a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i","d","n","o","o","o","o","o","x","o","u","u","u","u","y","th","ss"};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c; size_t n = 1;
        if (c >= 0xF0) { cp = c & 7; n = 4; } else if (c >= 0xE0) { cp = c & 15; n = 3; } else if (c >= 0xC0) { cp = c & 31; n = 2; } else if (c >= 0x80) { ++i; continue; }
        for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 63);
        const std::string original = s.substr(i, n);
        i += n;
        if (cp < 128) out += static_cast<char>(std::tolower(static_cast<int>(cp)));
        else if (cp == 0xFF) out += 'y';
        else if (cp >= 0xC0 && cp <= 0xFF) out += latin1[(cp >= 0xE0 ? cp - 0xE0 : cp - 0xC0)];
        else if (cp == 0x152 || cp == 0x153) out += "oe";
        else if (cp == 0x2018 || cp == 0x2019) out += '\'';
        else out += original;
    }
    return out;
}

struct Hit {
    fs::path file;
    long long id = 0;
    std::string title, excerpt, path;  // path: « Histoire › Thème 1 › Chapitre 1 »
};
// Every idea of the hierarchy below `root` whose title or note contains all the words of `query`.
inline std::vector<Hit> search(const fs::path& root, const std::string& query, size_t limit = 100) {
    std::vector<std::string> words;
    { std::istringstream in(fold(query)); std::string w; while (in >> w) words.push_back(w); }
    std::vector<Hit> out;
    if (words.empty()) return out;
    std::vector<std::string> titles;
    for (const auto& entry : hierarchy(root)) {
        if (entry.status != Status::Ok) continue;
        titles.resize(static_cast<size_t>(entry.depth) + 1);
        titles[static_cast<size_t>(entry.depth)] = entry.title;
        std::string path;
        for (size_t i = 0; i <= static_cast<size_t>(entry.depth); ++i) path += (i ? " › " : "") + titles[i];
        document::State state;
        try { state = load(entry.file); } catch (...) { continue; }
        for (long long id : state.map->descendants(1)) {
            auto n = state.map->nodes.at(id);
            const auto title = fold(n->title), note = fold(n->note);
            bool all = true, in_note = false;
            for (const auto& w : words) {
                const bool t = title.find(w) != std::string::npos, k = note.find(w) != std::string::npos;
                all &= t || k; in_note |= !t && k;
            }
            if (!all) continue;
            Hit hit{entry.file, id, n->title, "", path};
            if (in_note) hit.excerpt = document::clip_utf8(document::single_line(n->note), 90);
            out.push_back(std::move(hit));
            if (out.size() >= limit) return out;
        }
    }
    return out;
}
// The maps to cross to reach `target` from `root` (root first); each entry's `via` is the idea of the previous map holding it.
inline std::vector<Entry> chain(const fs::path& root, const fs::path& target) {
    const auto list = hierarchy(root);
    int found = -1;
    for (size_t i = 0; i < list.size() && found < 0; ++i) if (list[i].status == Status::Ok && same(list[i].file, target)) found = static_cast<int>(i);
    std::vector<Entry> out;
    if (found < 0) return out;
    int depth = list[static_cast<size_t>(found)].depth;
    out.push_back(list[static_cast<size_t>(found)]);
    for (int i = found - 1; i >= 0 && depth > 0; --i) if (list[static_cast<size_t>(i)].depth == depth - 1) { out.push_back(list[static_cast<size_t>(i)]); --depth; }
    std::reverse(out.begin(), out.end());
    return out;
}
} // namespace submap
