#pragma once
// Bridges between maps and courses: a whole map hierarchy as a Markdown course (what Nemo reads),
// a Markdown course split into nested sub-maps, and the folder where Nemo's course files live.
#include "submaps.hpp"

namespace course {
namespace fs = submap::fs;

// ----- Map hierarchy -> Markdown course -----
// Each map is written as a bullet outline; every idea that holds a sub-map then gets a section of its own
// (# Histoire, ## Thème 1, ### Chapitre 1…), at any depth of the map.
inline void holders(const document::State& state, long long id, std::vector<long long>& out) {
    for (auto child : state.map->nodes.at(id)->children) {
        if (state.submaps.count(child)) out.push_back(child);
        holders(state, child, out);
    }
}
inline void emit_map(const fs::path& file, const document::State& state, size_t level, std::string& out, std::vector<fs::path>& chain);
inline void emit_section(const fs::path& file, const document::State& state, long long id, size_t level, std::string& out, std::vector<fs::path>& chain) {
    auto n = state.map->nodes.at(id);
    out += std::string(std::min<size_t>(level, 6), '#') + " " + document::marker_info(document::marker_of(state, id)).markdown + document::single_line(n->title) + "\n\n";
    if (!n->note.empty()) out += n->note + "\n\n";
    auto child_file = submap::resolve(file, state.submaps.at(id));
    for (const auto& ancestor : chain) if (submap::same(ancestor, child_file)) { out += "⚠ boucle de sous-cartes : " + state.submaps.at(id) + "\n\n"; return; }
    if (chain.size() > static_cast<size_t>(submap::max_depth)) { out += "⚠ sous-cartes trop profondes\n\n"; return; }
    document::State sub;
    try { sub = submap::load(child_file); } catch (...) { out += "⚠ sous-carte introuvable ou illisible : " + state.submaps.at(id) + "\n\n"; return; }
    chain.push_back(child_file);
    const auto& root_note = sub.map->nodes.at(1)->note;
    if (!root_note.empty() && root_note != n->note) out += root_note + "\n\n";
    emit_map(child_file, sub, level + 1, out, chain);
    chain.pop_back();
}
inline void emit_map(const fs::path& file, const document::State& state, size_t level, std::string& out, std::vector<fs::path>& chain) {
    const bool any = !state.map->nodes.at(1)->children.empty();
    for (auto child : state.map->nodes.at(1)->children) document::markdown_bullets(state, child, 0, out);
    if (any) out += "\n";
    std::vector<long long> sections;
    holders(state, 1, sections);
    for (auto id : sections) emit_section(file, state, id, level, out, chain);
}
inline std::string markdown(const fs::path& file) {
    auto state = submap::load(file);
    auto root = state.map->nodes.at(1);
    std::string out = "<!-- Généré depuis « " + workspace::path_text(file.filename()) + " » par MindMap. Modifiez la carte, pas ce fichier : il est régénéré à chaque export. -->\n\n";
    out += "# " + std::string(document::marker_info(document::marker_of(state, 1)).markdown) + document::single_line(root->title) + "\n\n";
    if (!root->note.empty()) out += root->note + "\n\n";
    std::vector<fs::path> chain{file};
    emit_map(file, state, 2, out, chain);
    while (out.size() > 1 && out.back() == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    return out;
}

// ----- Markdown course -> map hierarchy -----
// Ideas at the first `split` levels that have sub-ideas become sub-maps (split 0 = a single map).
inline void build_map(const fs::path& file, const std::string& title, const std::string& note, int marker,
                      const std::vector<document::OutlineItem>& items, int split, int depth, size_t& maps) {
    if (++maps > submap::max_maps) throw std::runtime_error("Trop de sous-cartes (200 au plus) : réduisez --split");
    document::State state;
    state.map->nodes.at(1)->title = title;
    state.map->nodes.at(1)->note = note;
    if (marker) state.markers[1] = marker;
    size_t i = 0;
    while (i < items.size()) {
        size_t j = i + 1;
        while (j < items.size() && items[j].level > 0) ++j;
        const auto& top = items[i];
        if (depth < split && j > i + 1) {
            auto id = state.map->add(1, top.title);
            state.map->rename(id, top.title, top.note);
            if (top.marker) state.markers[id] = top.marker;
            std::vector<document::OutlineItem> below(items.begin() + static_cast<long>(i) + 1, items.begin() + static_cast<long>(j));
            for (auto& item : below) --item.level;
            auto child = workspace::unique_file(submap::folder_of(file) / file.stem(), submap::slug(top.title));
            build_map(child, top.title, top.note, top.marker, below, split, depth + 1, maps);
            state.submaps[id] = submap::relative_path(file, child);
        } else {
            document::apply_outline(state, 1, std::vector<document::OutlineItem>(items.begin() + static_cast<long>(i), items.begin() + static_cast<long>(j)));
        }
        i = j;
    }
    document::layout(state);
    fs::create_directories(submap::folder_of(file));
    submap::store(file, state);
}
// Returns the number of map files written. Refuses to overwrite `file`.
inline size_t import_course(const fs::path& file, const std::string& text, const std::string& fallback_title, int split) {
    if (fs::exists(file)) throw std::runtime_error("Le fichier existe déjà : " + workspace::path_text(file));
    auto items = document::parse_outline(text);
    if (items.empty()) throw std::runtime_error("Aucune idée trouvée dans ce texte");
    std::string title = fallback_title.empty() ? "Cours importé" : document::clip_utf8(fallback_title, 1024), note;
    int marker = 0;
    size_t top_level = 0;
    for (const auto& item : items) top_level += item.level == 0;
    if (top_level == 1 && items[0].level == 0) {
        title = items[0].title; note = items[0].note; marker = items[0].marker;
        items.erase(items.begin());
        for (auto& item : items) --item.level;
    }
    size_t maps = 0;
    build_map(file, title, note, marker, items, std::max(0, split), 0, maps);
    return maps;
}
} // namespace course

namespace nemo {
namespace fs = submap::fs;
// Nemo reads the course files of a folder (synchronised to its storage): maps are exported there as Markdown.
inline fs::path config_file(const fs::path& workspace_root) { return fs::absolute(workspace_root) / ".mindmap-data" / "nemo.json"; }
inline fs::path courses_folder(const fs::path& workspace_root) {
    auto file = config_file(workspace_root);
    if (!fs::exists(file)) return {};
    auto j = document::Parser(workspace::read(file)).parse();
    if (j.at("format").text() != "mindmap-nemo") throw std::runtime_error("Configuration Nemo illisible");
    return workspace::text_path(j.at("courses").text());
}
inline void set_courses_folder(const fs::path& workspace_root, const fs::path& folder) {
    if (!fs::is_directory(folder)) throw std::runtime_error("Dossier introuvable : " + workspace::path_text(folder));
    auto file = config_file(workspace_root);
    fs::create_directories(file.parent_path());
    workspace::write(file, "{\"format\":\"mindmap-nemo\",\"version\":1,\"courses\":" + document::quote(workspace::path_text(fs::absolute(folder).lexically_normal())) + "}\n");
}
inline fs::path course_file(const fs::path& folder, const document::State& state) {
    return folder / workspace::text_path(submap::slug(state.map->nodes.at(1)->title) + " (carte mentale).md");
}
// Writes the whole hierarchy of `map_file` as one Markdown course in `folder`; returns the file written.
inline fs::path export_course(const fs::path& map_file, const fs::path& folder) {
    if (!fs::is_directory(folder)) throw std::runtime_error("Dossier des cours Nemo introuvable : " + workspace::path_text(folder));
    auto text = course::markdown(map_file);
    auto target = course_file(folder, submap::load(map_file));
    workspace::write(target, text);
    return target;
}
} // namespace nemo
