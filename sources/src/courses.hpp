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
    out += std::string(std::min<size_t>(level, 6), '#') + " " + document::marker_info(document::marker_of(state, id)).markdown + document::single_line(n->title) + document::uid_comment(state, id) + "\n\n";
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
    for (auto child : state.map->nodes.at(1)->children) document::markdown_bullets(state, child, 0, out, true);
    if (any) out += "\n";
    std::vector<long long> sections;
    holders(state, 1, sections);
    for (auto id : sections) emit_section(file, state, id, level, out, chain);
}
inline std::string markdown(const fs::path& file) {
    auto state = submap::load(file);
    auto root = state.map->nodes.at(1);
    std::string out = "<!-- Généré depuis « " + workspace::path_text(file.filename()) + " » par MindMap. Modifiez la carte, pas ce fichier : il est régénéré à chaque export. -->\n\n";
    out += "# " + std::string(document::marker_info(document::marker_of(state, 1)).markdown) + document::single_line(root->title) + document::uid_comment(state, 1) + "\n\n";
    if (!root->note.empty()) out += root->note + "\n\n";
    std::vector<fs::path> chain{file};
    emit_map(file, state, 2, out, chain);
    while (out.size() > 1 && out.back() == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    return out;
}

// ----- Markdown course -> map hierarchy -----
// Ideas at the first `split` levels that have sub-ideas become sub-maps (split 0 = a single map).
inline void build_map(const fs::path& file, const std::string& title, const std::string& note, int marker,
                      const std::vector<document::OutlineItem>& items, int split, int depth, size_t& maps, const std::string& uid = "") {
    if (++maps > submap::max_maps) throw std::runtime_error("Trop de sous-cartes (200 au plus) : réduisez --split");
    document::State state;
    state.map->nodes.at(1)->title = title;
    state.map->nodes.at(1)->note = note;
    if (marker) state.markers[1] = marker;
    if (!uid.empty()) state.uids[1] = uid;
    size_t i = 0;
    while (i < items.size()) {
        size_t j = i + 1;
        while (j < items.size() && items[j].level > 0) ++j;
        const auto& top = items[i];
        if (depth < split && j > i + 1) {
            auto id = state.map->add(1, top.title);
            state.map->rename(id, top.title, top.note);
            if (top.marker) state.markers[id] = top.marker;
            if (!top.uid.empty() && !document::uid_used(state, top.uid)) state.uids[id] = top.uid;
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
    std::string uid;
    size_t top_level = 0;
    for (const auto& item : items) top_level += item.level == 0;
    if (top_level == 1 && items[0].level == 0) {
        title = items[0].title; note = items[0].note; marker = items[0].marker; uid = items[0].uid;
        items.erase(items.begin());
        for (auto& item : items) --item.level;
    }
    size_t maps = 0;
    build_map(file, title, note, marker, items, std::max(0, split), 0, maps, uid);
    return maps;
}
} // namespace course

namespace nemo {
namespace fs = submap::fs;
// Nemo reads the course files of a folder (synchronised to its storage): maps are exported there as Markdown.
inline fs::path config_file(const fs::path& workspace_root) { return fs::absolute(workspace_root) / ".mindmap-data" / "nemo.json"; }
struct Config { fs::path courses; bool auto_export = false; bool mastery_badges = false; };
inline Config load_config(const fs::path& workspace_root) {
    Config c;
    auto file = config_file(workspace_root);
    if (!fs::exists(file)) return c;
    auto j = document::Parser(workspace::read(file)).parse();
    if (j.at("format").text() != "mindmap-nemo") throw std::runtime_error("Configuration Nemo illisible");
    c.courses = workspace::text_path(j.at("courses").text());
    if (j.object.count("autoExport")) c.auto_export = j.at("autoExport").flag();
    if (j.object.count("masteryBadges")) c.mastery_badges = j.at("masteryBadges").flag();
    return c;
}
inline void save_config(const fs::path& workspace_root, const Config& c) {
    auto file = config_file(workspace_root);
    fs::create_directories(file.parent_path());
    workspace::write(file, "{\"format\":\"mindmap-nemo\",\"version\":1,\"courses\":" + document::quote(workspace::path_text(c.courses)) + ",\"autoExport\":" + (c.auto_export ? "true" : "false") + ",\"masteryBadges\":" + (c.mastery_badges ? "true" : "false") + "}\n");
}
inline fs::path courses_folder(const fs::path& workspace_root) { return load_config(workspace_root).courses; }
inline bool auto_export(const fs::path& workspace_root) { try { return load_config(workspace_root).auto_export; } catch (...) { return false; } }
inline void set_courses_folder(const fs::path& workspace_root, const fs::path& folder) {
    if (!fs::is_directory(folder)) throw std::runtime_error("Dossier introuvable : " + workspace::path_text(folder));
    auto c = load_config(workspace_root);
    c.courses = fs::absolute(folder).lexically_normal();
    save_config(workspace_root, c);
}
inline bool mastery_badges(const fs::path& workspace_root) { try { return load_config(workspace_root).mastery_badges; } catch (...) { return false; } }
inline void set_mastery_badges(const fs::path& workspace_root, bool on) {
    auto c = load_config(workspace_root);
    c.mastery_badges = on;
    save_config(workspace_root, c);
}
inline void set_auto_export(const fs::path& workspace_root, bool on) {
    auto c = load_config(workspace_root);
    if (on && c.courses.empty()) throw std::runtime_error("Configurez d'abord le dossier des cours : nemo-config --courses <dossier>");
    c.auto_export = on;
    save_config(workspace_root, c);
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

// ----- Exports suivis : renommer ou supprimer une carte ne laisse pas un ancien cours « fantome » dans le dossier de Nemo -----
inline const std::string course_suffix = " (carte mentale).md";
inline fs::path manifest_file(const fs::path& workspace_root) { return fs::absolute(workspace_root) / ".mindmap-data" / "nemo-exports.json"; }
struct Tracked { std::string map, file; };
inline std::vector<Tracked> load_manifest(const fs::path& workspace_root) {
    std::vector<Tracked> out;
    auto file = manifest_file(workspace_root);
    if (!fs::exists(file)) return out;
    try {
        auto j = document::Parser(workspace::read(file)).parse();
        for (const auto& e : j.at("exports").array) out.push_back({e.at("map").text(), e.at("file").text()});
    } catch (...) { out.clear(); }
    return out;
}
inline void save_manifest(const fs::path& workspace_root, const std::vector<Tracked>& list) {
    std::string out = "{\"format\":\"mindmap-nemo-exports\",\"version\":1,\"exports\":[";
    for (size_t i = 0; i < list.size(); ++i) out += (i ? "," : "") + std::string("{\"map\":") + document::quote(list[i].map) + ",\"file\":" + document::quote(list[i].file) + "}";
    auto file = manifest_file(workspace_root);
    fs::create_directories(file.parent_path());
    workspace::write(file, out + "]}\n");
}
// Never deletes anything but a file this tool generated (its name ends with the course suffix) inside the courses folder.
inline void remove_generated(const fs::path& folder, const std::string& file_text) {
    auto file = workspace::text_path(file_text);
    std::error_code e;
    if (!submap::same(file.parent_path(), folder)) return;
    if (file_text.size() < course_suffix.size() || file_text.compare(file_text.size() - course_suffix.size(), course_suffix.size(), course_suffix) != 0) return;
    fs::remove(file, e);
}
inline void record_titles(const fs::path& workspace_root, const fs::path& map_file);
// Exports map_file and keeps the manifest in step: a renamed map replaces its old course file, a vanished map's file is removed.
inline fs::path export_tracked(const fs::path& workspace_root, const fs::path& map_file, const fs::path& folder) {
    auto target = export_course(map_file, folder);
    auto list = load_manifest(workspace_root);
    const auto map_text = workspace::path_text(fs::absolute(map_file).lexically_normal());
    const auto target_text = workspace::path_text(fs::absolute(target).lexically_normal());
    std::vector<Tracked> next;
    for (auto& t : list) {
        const bool same_map = submap::same(workspace::text_path(t.map), map_file);
        const bool gone = !fs::exists(workspace::text_path(t.map));
        if ((same_map && t.file != target_text) || gone) remove_generated(folder, t.file);
        if (!same_map && !gone) next.push_back(t);
    }
    next.push_back({map_text, target_text});
    save_manifest(workspace_root, next);
    try { record_titles(workspace_root, map_file); } catch (...) {}
    return target;
}
// Root maps of a folder: every .mindmap that no other map of the folder holds as a sub-map.
inline std::vector<fs::path> root_maps(const fs::path& folder) {
    std::vector<fs::path> all;
    std::error_code e;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, e), end; !e && it != end; it.increment(e)) {
        if (it->is_directory(e) && it->path().filename() == ".mindmap-data") { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file(e) && it->path().extension() == ".mindmap") all.push_back(it->path());
    }
    std::vector<fs::path> held;
    for (const auto& f : all) {
        try { auto h = submap::hierarchy(f); for (size_t i = 1; i < h.size(); ++i) held.push_back(h[i].file); } catch (...) {}
    }
    std::vector<fs::path> roots;
    for (const auto& f : all) {
        bool is_held = false;
        for (const auto& h : held) if (submap::same(h, f)) { is_held = true; break; }
        if (!is_held) roots.push_back(f);
    }
    return roots;
}

// ----- Ideas and their former titles: a renamed idea keeps its review history on Nemo -----
// Nemo finds a notion by its identifier (uid written in the map, "mm-<uid>" on Nemo) or else by subject + title. MindMap remembers, per idea,
// the titles it has had when it published or asked for mastery; nemo-mastery then also asks for the former titles and reports the history it finds.
// An idea is known by its uid (it survives renames and moves between maps) or, in a map saved before identifiers existed, by map file + number.
struct Idea { fs::path file; long long id = 0; std::string uid, title; std::vector<std::string> former; };
inline fs::path ideas_file(const fs::path& workspace_root) { return fs::absolute(workspace_root) / ".mindmap-data" / "nemo-ideas.json"; }
inline std::string idea_key(const fs::path& file, long long id) { return workspace::path_text(fs::absolute(file).lexically_normal()) + "#" + std::to_string(id); }
inline std::string uid_key(const std::string& uid) { return "uid:" + uid; }
inline std::string idea_key(const Idea& idea) { return idea.uid.empty() ? idea_key(idea.file, idea.id) : uid_key(idea.uid); }
inline std::map<std::string, std::vector<std::string>> load_titles(const fs::path& workspace_root) {
    std::map<std::string, std::vector<std::string>> out;
    auto file = ideas_file(workspace_root);
    if (!fs::exists(file)) return out;
    try {
        auto j = document::Parser(workspace::read(file)).parse();
        for (const auto& e : j.at("ideas").array) {
            std::vector<std::string> titles;
            for (const auto& t : e.at("titles").array) titles.push_back(t.text());
            out[e.at("key").text()] = titles;
        }
    } catch (...) { out.clear(); }
    return out;
}
// Every idea of every map of the hierarchy (the central subject is the map title, not an idea), with its former titles.
inline std::vector<Idea> ideas_of(const fs::path& root_map, const std::map<std::string, std::vector<std::string>>& known = {}) {
    std::vector<Idea> out;
    for (const auto& entry : submap::hierarchy(root_map)) {
        if (entry.status != submap::Status::Ok) continue;
        auto state = submap::load(entry.file);
        for (long long id : state.map->descendants(1)) {
            if (id == 1) continue;
            auto title = document::single_line(state.map->nodes.at(id)->title);
            if (title.empty()) continue;
            Idea idea{entry.file, id, document::uid_of(state, id), title, {}};
            for (const auto& key : {idea_key(idea), idea_key(entry.file, id)})  // the file + number key of an older record still counts
                if (auto it = known.find(key); it != known.end())
                    for (const auto& t : it->second)
                        if (t != title && idea.former.size() < 5 && std::find(idea.former.begin(), idea.former.end(), t) == idea.former.end()) idea.former.push_back(t);
            out.push_back(std::move(idea));
        }
    }
    return out;
}
// Remembers the current title of every idea of a map hierarchy (newest first, 6 titles at most per idea).
inline void record_titles(const fs::path& workspace_root, const fs::path& map_file) {
    auto known = load_titles(workspace_root);
    for (const auto& idea : ideas_of(map_file)) {
        auto& titles = known[idea_key(idea)];
        if (!idea.uid.empty()) {  // moves an older file + number record under the identifier
            if (auto old = known.find(idea_key(idea.file, idea.id)); old != known.end()) {
                for (const auto& t : old->second) if (std::find(titles.begin(), titles.end(), t) == titles.end()) titles.push_back(t);
                known.erase(old);
            }
        }
        titles.erase(std::remove(titles.begin(), titles.end(), idea.title), titles.end());
        titles.insert(titles.begin(), idea.title);
        if (titles.size() > 6) titles.resize(6);
    }
    std::string out = "{\"format\":\"mindmap-nemo-ideas\",\"version\":1,\"ideas\":[";
    bool first = true;
    for (const auto& [key, titles] : known) {
        if (!first) out += ",";
        first = false;
        out += "{\"key\":" + document::quote(key) + ",\"titles\":[";
        for (size_t i = 0; i < titles.size(); ++i) out += (i ? "," : "") + document::quote(titles[i]);
        out += "]}";
    }
    auto file = ideas_file(workspace_root);
    fs::create_directories(file.parent_path());
    workspace::write(file, out + "]}\n");
}

// ----- Mastery badges: nemo-mastery --cache writes what Nemo knows; the window only READS this file (it never uses the network) -----
inline fs::path mastery_cache_file(const fs::path& workspace_root) { return fs::absolute(workspace_root) / ".mindmap-data" / "nemo-mastery.json"; }
inline std::map<std::string, std::string> load_mastery_cache(const fs::path& workspace_root) {
    std::map<std::string, std::string> out;
    auto file = mastery_cache_file(workspace_root);
    if (!fs::exists(file)) return out;
    try {
        auto j = document::Parser(workspace::read(file)).parse();
        for (const auto& e : j.at("ideas").array) out[e.at("key").text()] = e.at("status").text();
    } catch (...) { out.clear(); }
    return out;
}
// Replaces the entries of every map of "files" (a hierarchy) and of the identifiers "uids" by "fresh" and keeps the others.
inline void save_mastery_cache(const fs::path& workspace_root, const std::vector<fs::path>& files, const std::map<std::string, std::string>& fresh,
                               const std::vector<std::string>& uids = {}) {
    auto all = load_mastery_cache(workspace_root);
    for (const auto& f : files) {
        const auto prefix = workspace::path_text(fs::absolute(f).lexically_normal()) + "#";
        for (auto it = all.begin(); it != all.end();) it = it->first.rfind(prefix, 0) == 0 ? all.erase(it) : std::next(it);
    }
    for (const auto& uid : uids) all.erase(uid_key(uid));
    for (const auto& [key, status] : fresh) all[key] = status;
    std::string out = "{\"format\":\"mindmap-nemo-mastery\",\"version\":1,\"ideas\":[";
    bool first = true;
    for (const auto& [key, status] : all) {
        out += std::string(first ? "" : ",") + "{\"key\":" + document::quote(key) + ",\"status\":" + document::quote(status) + "}";
        first = false;
    }
    auto file = mastery_cache_file(workspace_root);
    fs::create_directories(file.parent_path());
    workspace::write(file, out + "]}\n");
}
} // namespace nemo
