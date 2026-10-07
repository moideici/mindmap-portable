// mindmap-cli : édition et création de cartes en ligne de commande (pensé pour les IA et les scripts).
// Chaque commande lit un fichier .mindmap, applique une modification (ou, avec « batch », plusieurs d'un coup)
// et l'écrit de façon atomique. Les commandes sont déclarées dans une table : l'aide en est générée.
#include "courses.hpp"
#include "imports.hpp"
#include <cstdio>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <shellapi.h>
#endif

namespace {
using document::quote;
namespace fs = workspace::fs;

struct Usage : std::runtime_error { using std::runtime_error::runtime_error; };

// ----- Arguments -----
struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::set<std::string> flags;
    bool has(const std::string& k) const { return options.count(k) || flags.count(k); }
    std::string get(const std::string& k, const std::string& fallback = "") const { auto it = options.find(k); return it == options.end() ? fallback : it->second; }
    std::string need(const std::string& k) const { auto it = options.find(k); if (it == options.end()) throw Usage("Option obligatoire manquante : --" + k); return it->second; }
    long long id(const std::string& k) const {
        auto text = need(k); long long v = 0;
        auto r = std::from_chars(text.data(), text.data() + text.size(), v);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size() || v < 1) throw Usage("--" + k + " attend un numéro d'idée (entier positif)");
        return v;
    }
};
fs::path executable_folder;  // dossier de l'exécutable : c'est la racine de l'espace de travail portable (.mindmap-data)
const std::set<std::string> boolean_flags = {"json", "deep", "only-node", "collapsed", "expanded", "no-layout", "help"};
Args parse_args(const std::vector<std::string>& v) {
    Args a;
    for (size_t i = 0; i < v.size(); ++i) {
        const auto& s = v[i];
        if (s.size() > 2 && s.compare(0, 2, "--") == 0) {
            auto name = s.substr(2);
            if (auto eq = name.find('='); eq != std::string::npos) { a.options[name.substr(0, eq)] = name.substr(eq + 1); continue; }
            if (boolean_flags.count(name)) { a.flags.insert(name); continue; }
            if (i + 1 >= v.size()) throw Usage("--" + name + " attend une valeur");
            a.options[name] = v[++i];
        } else a.positional.push_back(s);
    }
    return a;
}

// ----- Entrées / validations -----
std::string read_stdin() {
    std::string out; char buffer[8192]; size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, stdin)) > 0) { out.append(buffer, n); if (out.size() > document::max_file_bytes) throw std::runtime_error("Entrée trop volumineuse"); }
    return out;
}
// "-" lit l'entrée standard ; sinon c'est un fichier texte (Markdown ou plan indenté).
std::string read_text_source(const std::string& source) { return source == "-" ? read_stdin() : workspace::read(workspace::text_path(source)); }
std::string unescape_newlines(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out += '\n'; ++i; }
        else out += s[i];
    }
    return out;
}
// --note accepte \n ; les notes venant d'un fichier ou d'un batch JSON sont prises telles quelles.
std::string note_value(const Args& a) {
    if (a.has("note-file")) return read_text_source(a.get("note-file"));
    return a.flags.count("raw-text") ? a.get("note") : unescape_newlines(a.get("note"));
}
// --outline : fichier ou "-" en ligne de commande ; texte direct dans un batch JSON.
std::string outline_value(const Args& a, const std::string& fallback = "") {
    if (a.flags.count("raw-text")) return a.get("outline", fallback);
    return a.has("outline") || !fallback.empty() ? read_text_source(a.get("outline", fallback)) : std::string();
}
std::string checked_title(const std::string& raw) {
    auto t = document::trim(raw);
    if (t.empty()) throw Usage("Le titre ne peut pas être vide");
    if (t.size() > 1024) throw Usage("Titre trop long (1024 octets maximum)");
    if (!document::valid_utf8(t) || t.find('\0') != std::string::npos) throw Usage("Titre invalide");
    return t;
}
int parse_marker(const std::string& text) {
    static const std::map<std::string, int> names = {{"aucun", 0}, {"none", 0}, {"important", 1}, {"etoile", 1}, {"acquis", 2}, {"ok", 2},
        {"revoir", 3}, {"a-revoir", 3}, {"attention", 4}, {"repere", 5}, {"cle", 5}};
    if (auto it = names.find(text); it != names.end()) return it->second;
    if (text.size() == 1 && text[0] >= '0' && text[0] < '0' + document::marker_count) return text[0] - '0';
    throw Usage("Repère inconnu : " + text + " (0 aucun, 1 important, 2 acquis, 3 revoir, 4 attention, 5 repere)");
}
long long parse_color(const std::string& text) {
    if (text.size() == 7 && text[0] == '#') {
        unsigned v = 0; auto r = std::from_chars(text.data() + 1, text.data() + 7, v, 16);
        if (r.ec == std::errc{} && r.ptr == text.data() + 7) return v;
    }
    throw Usage("Couleur attendue au format #RRGGBB");
}
fs::path map_path(const Args& a, size_t index = 1) {
    if (a.positional.size() <= index) throw Usage("Indiquez le fichier .mindmap");
    return workspace::text_path(a.positional[index]);
}
void need_node(const document::State& s, long long id) { if (!s.map->nodes.count(id)) throw std::runtime_error("Idée " + std::to_string(id) + " introuvable"); }
void finish(document::State& s, const Args& a) {
    document::prune(s);
    if (!a.has("no-layout")) document::layout(s);
}
void print(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); std::fflush(stdout); }

// ----- Sorties de lecture -----
std::string json_node(const document::State& s, long long id) {
    auto n = s.map->nodes.at(id);
    std::string out = "{\"id\":" + std::to_string(id) + ",\"title\":" + quote(n->title);
    if (!n->note.empty()) out += ",\"note\":" + quote(n->note);
    if (int m = document::marker_of(s, id)) out += ",\"marker\":" + quote(document::marker_info(m).name);
    out += ",\"color\":" + quote(document::hex_color(n->color));
    if (n->collapsed) out += ",\"collapsed\":true";
    if (s.pictures.count(id)) out += ",\"picture\":true";
    if (auto it = s.submaps.find(id); it != s.submaps.end()) out += ",\"submap\":" + quote(it->second);
    if (!n->children.empty()) {
        out += ",\"children\":[";
        bool first = true;
        for (auto c : n->children) { if (!first) out += ','; first = false; out += json_node(s, c); }
        out += ']';
    }
    return out + '}';
}
std::string json_map(const fs::path& file, const document::State& s) {
    std::string out = "{\"file\":" + quote(workspace::path_text(file)) + ",\"root\":" + json_node(s, 1) + ",\"links\":[";
    bool first = true;
    for (const auto& l : s.links) {
        if (!first) out += ',';
        first = false;
        out += "{\"from\":" + std::to_string(l.from) + ",\"to\":" + std::to_string(l.to) + ",\"label\":" + quote(l.label) + '}';
    }
    return out + "]}";
}
void text_node(const document::State& s, long long id, int depth, std::string& out) {
    auto n = s.map->nodes.at(id);
    out += std::string(static_cast<size_t>(depth) * 2, ' ') + "[" + std::to_string(id) + "] " + document::single_line(n->title);
    if (int m = document::marker_of(s, id)) out += std::string("  ") + document::marker_info(m).glyph;
    if (auto it = s.submaps.find(id); it != s.submaps.end()) out += "  ▸ sous-carte : " + it->second;
    if (n->collapsed) out += "  (replié)";
    out += "\n";
    if (!n->note.empty()) {
        std::istringstream lines(n->note); std::string line;
        while (std::getline(lines, line)) out += std::string(static_cast<size_t>(depth) * 2 + 4, ' ') + "> " + line + "\n";
    }
    for (auto c : n->children) text_node(s, c, depth + 1, out);
}

// ----- Commandes de lecture -----
void cmd_show(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    if (a.has("json")) { print(json_map(file, s) + "\n"); return; }
    std::string out; text_node(s, 1, 0, out);
    if (!s.links.empty()) {
        out += "\nLiens :\n";
        for (const auto& l : s.links) out += "  [" + std::to_string(l.from) + "] → [" + std::to_string(l.to) + "]" + (l.label.empty() ? "" : " (" + l.label + ")") + "\n";
    }
    print(out);
}
void cmd_outline(const Args& a) {
    auto file = map_path(a);
    print(a.has("deep") ? submap::deep_markdown(file) : document::markdown(submap::load(file)));
}
void cmd_tree(const Args& a) {
    auto file = map_path(a); auto list = submap::hierarchy(file);
    if (a.has("json")) {
        std::string out = "{\"maps\":[";
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& e = list[i];
            if (i) out += ',';
            out += "{\"file\":" + quote(workspace::path_text(e.file)) + ",\"title\":" + quote(e.title) + ",\"depth\":" + std::to_string(e.depth)
                + ",\"status\":" + quote(submap::status_name(e.status)) + ",\"nodes\":" + std::to_string(e.nodes);
            if (e.via) out += ",\"held_by_idea\":" + std::to_string(e.via) + ",\"relative\":" + quote(e.relative);
            out += '}';
        }
        print(out + "]}\n"); return;
    }
    std::string out;
    for (const auto& e : list) {
        out += std::string(static_cast<size_t>(e.depth) * 2, ' ') + e.title;
        if (e.via) out += "  (idée " + std::to_string(e.via) + " → " + e.relative + ")";
        if (e.status != submap::Status::Ok) out += std::string("  ⚠ ") + submap::status_name(e.status);
        else out += "  · " + std::to_string(e.nodes) + " idées";
        out += "\n";
    }
    print(out);
}
void cmd_validate(const Args& a) {
    auto file = map_path(a);
    std::vector<std::string> problems, warnings;
    auto list = submap::hierarchy(file);
    for (const auto& e : list) if (e.status != submap::Status::Ok)
        problems.push_back(std::string(submap::status_name(e.status)) + " : " + (e.via ? e.relative : workspace::path_text(e.file)));
    for (const auto& orphan : submap::orphans(file)) warnings.push_back("fichier non utilisé : " + workspace::path_text(orphan));
    auto list_json = [](const std::vector<std::string>& v) { std::string out = "["; for (size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + quote(v[i]); return out + "]"; };
    if (a.has("json"))
        print("{\"ok\":" + std::string(problems.empty() ? "true" : "false") + ",\"maps\":" + std::to_string(list.size()) + ",\"problems\":" + list_json(problems) + ",\"warnings\":" + list_json(warnings) + "}\n");
    else {
        std::string out;
        if (problems.empty()) out = "OK : " + std::to_string(list.size()) + " carte(s) valide(s)\n";
        for (const auto& p : problems) out += "⚠ " + p + "\n";
        for (const auto& w : warnings) out += "ℹ " + w + "\n";
        print(out);
    }
    if (!problems.empty()) throw std::runtime_error(std::to_string(problems.size()) + " problème(s) détecté(s)");
}
void cmd_orphans(const Args& a) {
    auto list = submap::orphans(map_path(a));
    if (a.has("json")) {
        std::string out = "{\"orphans\":[";
        for (size_t i = 0; i < list.size(); ++i) out += (i ? "," : "") + quote(workspace::path_text(list[i]));
        print(out + "]}\n");
    } else if (list.empty()) print("Aucun fichier orphelin\n");
    else { std::string out; for (const auto& f : list) out += workspace::path_text(f) + "\n"; print(out); }
}
void cmd_export(const Args& a) {
    auto file = map_path(a);
    auto out = workspace::text_path(a.need("to"));
    auto s = submap::load(file);
    auto ext = out.extension().string();
    std::string data;
    if (ext == ".md") data = a.has("deep") ? submap::deep_markdown(file) : document::markdown(s);
    else if (ext == ".svg") data = document::svg(s);
    else throw Usage("Export pris en charge : .md ou .svg (PNG et PDF restent dans l'application)");
    workspace::write(out, data);
    print(a.has("json") ? "{\"ok\":true,\"file\":" + quote(workspace::path_text(out)) + "}\n" : "Exporté : " + workspace::path_text(out) + "\n");
}
void cmd_new(const Args& a) {
    auto file = map_path(a);
    if (fs::exists(file)) throw std::runtime_error("Le fichier existe déjà : " + workspace::path_text(file));
    auto title = a.has("title") ? checked_title(a.get("title")) : workspace::path_text(file.stem());
    document::State s = a.has("outline") ? document::from_outline(document::parse_outline(read_text_source(a.get("outline"))), title) : document::State();
    if (a.has("title") || !a.has("outline")) s.map->nodes.at(1)->title = title;
    finish(s, a);
    if (file.has_parent_path()) fs::create_directories(file.parent_path());
    submap::store(file, s);
    const auto count = std::to_string(s.map->nodes.size());
    print(a.has("json") ? "{\"ok\":true,\"file\":" + quote(workspace::path_text(file)) + ",\"nodes\":" + count + "}\n"
                        : "Carte créée : " + workspace::path_text(file) + " (" + count + " idées)\n");
}

void cmd_search(const Args& a) {
    auto file = map_path(a);
    std::string query;
    for (size_t i = 2; i < a.positional.size(); ++i) query += (i > 2 ? " " : "") + a.positional[i];
    if (query.empty()) throw Usage("Indiquez le texte à chercher : search <carte> <mots>");
    auto hits = submap::search(file, query, a.has("limit") ? static_cast<size_t>(a.id("limit")) : 100);
    if (a.has("json")) {
        std::string out = "{\"count\":" + std::to_string(hits.size()) + ",\"hits\":[";
        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& h = hits[i];
            out += (i ? "," : "") + std::string("{\"file\":") + quote(workspace::path_text(h.file)) + ",\"id\":" + std::to_string(h.id) + ",\"title\":" + quote(h.title)
                + ",\"path\":" + quote(h.path) + (h.excerpt.empty() ? "" : ",\"excerpt\":" + quote(h.excerpt)) + "}";
        }
        print(out + "]}\n");
        return;
    }
    std::string out;
    for (const auto& h : hits) out += h.path + "  ›  [" + std::to_string(h.id) + "] " + h.title + (h.excerpt.empty() ? "" : "   (note : " + h.excerpt + ")") + "\n  " + workspace::path_text(h.file) + "\n";
    print(hits.empty() ? "Aucun résultat\n" : out);
}
fs::path workspace_folder(const Args& a) { return a.has("workspace") ? workspace::text_path(a.get("workspace")) : executable_folder; }
void cmd_nemo_config(const Args& a) {
    auto root = workspace_folder(a);
    if (a.has("courses")) nemo::set_courses_folder(root, workspace::text_path(a.get("courses")));
    auto folder = nemo::courses_folder(root);
    const auto text = folder.empty() ? std::string() : workspace::path_text(folder);
    if (a.has("json")) print("{\"ok\":true,\"courses\":" + (text.empty() ? "null" : quote(text)) + "}\n");
    else print(text.empty() ? "Aucun dossier de cours Nemo configuré : nemo-config --courses <dossier>\n" : "Dossier des cours Nemo : " + text + "\n");
}
void cmd_nemo_export(const Args& a) {
    auto file = map_path(a);
    auto folder = a.has("to") ? workspace::text_path(a.get("to")) : nemo::courses_folder(workspace_folder(a));
    if (folder.empty()) throw Usage("Indiquez --to <dossier> ou configurez-le une fois : nemo-config --courses <dossier des cours synchronisés par Nemo>");
    auto written = nemo::export_course(file, folder);
    print(a.has("json") ? "{\"ok\":true,\"file\":" + quote(workspace::path_text(written)) + "}\n"
                        : "Cours exporté pour Nemo : " + workspace::path_text(written) + "\n(Nemo le lira à sa prochaine synchronisation des cours.)\n");
}
void cmd_import_course(const Args& a) {
    auto file = map_path(a);
    const int split = a.has("split") ? static_cast<int>(std::min<long long>(a.id("split"), submap::max_depth)) : 0;
    auto count = course::import_course(file, read_text_source(a.get("from", "-")), a.get("title"), split);
    print(a.has("json") ? "{\"ok\":true,\"file\":" + quote(workspace::path_text(file)) + ",\"maps\":" + std::to_string(count) + "}\n"
                        : "Cours importé : " + workspace::path_text(file) + " (" + std::to_string(count) + " carte(s))\n");
}

// ----- Modifications -----
// Chaque modification travaille sur une carte déjà chargée : « batch » en enchaîne plusieurs avant d'écrire une seule fois.
struct Outcome {
    std::string text, json;  // phrase lisible ; champs JSON sans accolades
    long long id = 0;        // idée créée ou ciblée (alimente les références « as » / « $nom » de batch)
    std::vector<fs::path> created;  // fichiers créés (supprimés si un batch échoue)
};
using Mutator = Outcome (*)(document::State&, const fs::path&, const Args&);

Outcome do_add(document::State& s, const fs::path&, const Args& a) {
    auto parent = a.has("parent") ? a.id("parent") : 1;
    need_node(s, parent);
    auto id = s.map->add(parent, checked_title(a.need("title")));
    auto note = note_value(a);
    if (!note.empty()) { if (note.size() > 16000) throw Usage("Note trop longue (16000 octets maximum)"); s.map->nodes.at(id)->note = note; }
    if (a.has("marker")) { if (int m = parse_marker(a.get("marker"))) s.markers[id] = m; }
    if (a.has("color")) s.map->nodes.at(id)->color = parse_color(a.get("color"));
    if (s.map->nodes.size() > 2000) throw std::runtime_error("Limite de 2000 idées atteinte");
    return {"Idée ajoutée : [" + std::to_string(id) + "]", "\"id\":" + std::to_string(id), id, {}};
}
Outcome do_add_outline(document::State& s, const fs::path&, const Args& a) {
    auto parent = a.has("parent") ? a.id("parent") : 1;
    need_node(s, parent);
    auto created = document::apply_outline(s, parent, document::parse_outline(outline_value(a, "-")));
    std::string ids;
    for (size_t i = 0; i < created.size(); ++i) ids += (i ? "," : "") + std::to_string(created[i]);
    return {std::to_string(created.size()) + " idées ajoutées sous [" + std::to_string(parent) + "] : " + ids,
            "\"count\":" + std::to_string(created.size()) + ",\"ids\":[" + ids + "]", created.front(), {}};
}
Outcome do_set(document::State& s, const fs::path&, const Args& a) {
    auto id = a.id("id"); need_node(s, id);
    auto n = s.map->nodes.at(id);
    bool changed = false;
    if (a.has("title")) { n->title = checked_title(a.get("title")); changed = true; }
    if (a.has("note") || a.has("note-file")) {
        auto note = note_value(a);
        if (note.size() > 16000) throw Usage("Note trop longue (16000 octets maximum)");
        n->note = note; changed = true;
    }
    if (a.has("marker")) { int m = parse_marker(a.get("marker")); if (m) s.markers[id] = m; else s.markers.erase(id); changed = true; }
    if (a.has("color")) { s.map->set_color(id, parse_color(a.get("color")), !a.has("only-node")); changed = true; }
    if (a.has("collapsed")) { n->collapsed = true; changed = true; }
    if (a.has("expanded")) { n->collapsed = false; changed = true; }
    if (!changed) throw Usage("Rien à modifier : indiquez --title, --note, --note-file, --marker, --color, --collapsed ou --expanded");
    return {"Idée [" + std::to_string(id) + "] modifiée", "\"id\":" + std::to_string(id), id, {}};
}
Outcome do_move(document::State& s, const fs::path&, const Args& a) {
    auto id = a.id("id"), parent = a.id("parent");
    need_node(s, id); need_node(s, parent);
    s.map->reparent(id, parent);
    return {"Idée [" + std::to_string(id) + "] déplacée sous [" + std::to_string(parent) + "]", "\"id\":" + std::to_string(id), id, {}};
}
Outcome do_remove(document::State& s, const fs::path&, const Args& a) {
    auto id = a.id("id"); need_node(s, id);
    const auto before = s.map->nodes.size();
    s.map->remove(id);
    document::prune(s);
    auto removed = before - s.map->nodes.size();
    return {std::to_string(removed) + " idée(s) supprimée(s) (les fichiers de sous-cartes ne sont pas effacés)", "\"removed\":" + std::to_string(removed), 0, {}};
}
Outcome change_link(document::State& s, const Args& a, bool remove) {
    auto from = a.id("from"), to = a.id("to"); need_node(s, from); need_node(s, to);
    if (remove) {
        auto count = std::erase_if(s.links, [&](const document::Link& l) { return (l.from == from && l.to == to) || (l.from == to && l.to == from); });
        if (!count) throw std::runtime_error("Aucun lien entre ces deux idées");
    } else {
        auto label = a.get("label");
        if (label.size() > document::max_link_label) throw Usage("Libellé trop long (200 octets maximum)");
        if (!document::add_link(s, from, to, label)) throw std::runtime_error("Ces deux idées sont déjà reliées");
    }
    return {remove ? "Lien supprimé" : "Lien créé", "", 0, {}};
}
Outcome do_link(document::State& s, const fs::path&, const Args& a) { return change_link(s, a, false); }
Outcome do_unlink(document::State& s, const fs::path&, const Args& a) { return change_link(s, a, true); }
Outcome do_submap_new(document::State& s, const fs::path& file, const Args& a) {
    auto id = a.id("id");
    need_node(s, id);
    const auto old_title = s.map->nodes.at(id)->title;
    if (a.has("title")) s.map->nodes.at(id)->title = checked_title(a.get("title"));
    auto outline = outline_value(a);
    fs::path child;
    try { child = submap::create(s, fs::absolute(file), id, outline); }
    catch (...) { s.map->nodes.at(id)->title = old_title; throw; }
    return {"Sous-carte créée : " + workspace::path_text(child) + " (idée [" + std::to_string(id) + "])",
            "\"id\":" + std::to_string(id) + ",\"file\":" + quote(workspace::path_text(child)) + ",\"relative\":" + quote(s.submaps.at(id)), id, {child}};
}
Outcome do_submap_attach(document::State& s, const fs::path& file, const Args& a) {
    auto id = a.id("id");
    auto target = workspace::text_path(a.need("file"));
    // Le chemin donné est relatif au dossier de la carte parente (comme dans le fichier) ou absolu.
    if (!target.is_absolute()) target = submap::folder_of(file) / target;
    submap::attach(s, fs::absolute(file), id, target);
    return {"Idée [" + std::to_string(id) + "] reliée à la sous-carte " + s.submaps.at(id), "\"id\":" + std::to_string(id) + ",\"relative\":" + quote(s.submaps.at(id)), id, {}};
}
Outcome do_submap_detach(document::State& s, const fs::path&, const Args& a) {
    auto id = a.id("id");
    if (!s.submaps.erase(id)) throw std::runtime_error("Cette idée n'a pas de sous-carte");
    return {"Sous-carte détachée (le fichier est conservé)", "\"id\":" + std::to_string(id), id, {}};
}

void report(const Args& a, const Outcome& o) {
    if (a.has("json")) print("{\"ok\":true" + (o.json.empty() ? "" : "," + o.json) + "}\n");
    else print(o.text + "\n");
}
template <Mutator F> void edit(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    auto outcome = F(s, file, a);
    try { finish(s, a); submap::store(file, s); }
    catch (...) { for (const auto& created : outcome.created) { std::error_code e; fs::remove(created, e); } throw; }
    report(a, outcome);
}

// ----- batch : plusieurs modifications, une seule écriture, tout ou rien -----
const std::map<std::string, Mutator>& mutators() {
    static const std::map<std::string, Mutator> table = {
        {"add", do_add}, {"add-outline", do_add_outline}, {"set", do_set}, {"move", do_move}, {"remove", do_remove},
        {"link", do_link}, {"unlink", do_unlink}, {"submap-new", do_submap_new}, {"submap-attach", do_submap_attach}, {"submap-detach", do_submap_detach}};
    return table;
}
// {"ops":[{"op":"add","parent":1,"title":"Thème","as":"t"},{"op":"submap-new","id":"$t","outline":"- Chapitre 1"}]}
// Les valeurs "$nom" désignent une idée créée plus haut avec "as". "outline" et "note" sont donnés directement en texte.
void cmd_batch(const Args& a) {
    auto file = map_path(a);
    auto json = document::Parser(a.has("from") ? read_text_source(a.get("from")) : read_stdin(), 20).parse();
    const auto& ops = json.at("ops");
    if (ops.kind != document::Json::Array || ops.array.empty() || ops.array.size() > 500) throw Usage("« ops » doit être une liste de 1 à 500 opérations");
    auto s = submap::load(file);
    std::map<std::string, long long> refs;
    std::vector<fs::path> created;
    std::string results = "[", sentences;
    int index = 0;
    try {
        for (const auto& op : ops.array) {
            ++index;
            if (op.kind != document::Json::Object) throw Usage("une opération doit être un objet JSON");
            Args step;
            step.flags.insert("raw-text");
            std::string name, alias;
            for (const auto& [key, value] : op.object) {
                if (key == "op") { name = value.text(); continue; }
                if (key == "as") { alias = value.text(); continue; }
                std::string text;
                if (value.kind == document::Json::String) text = value.string;
                else if (value.kind == document::Json::Number) text = std::to_string(static_cast<long long>(value.number));
                else if (value.kind == document::Json::Boolean) { if (value.boolean) step.flags.insert(key); continue; }
                else throw Usage("valeur non prise en charge pour « " + key + " »");
                const bool is_id = key == "id" || key == "parent" || key == "from" || key == "to";
                if (is_id && text.size() > 1 && text[0] == '$') {
                    auto it = refs.find(text.substr(1));
                    if (it == refs.end()) throw Usage("référence inconnue : " + text);
                    text = std::to_string(it->second);
                }
                step.options[key] = text;
            }
            auto it = mutators().find(name);
            if (it == mutators().end()) throw Usage("opération inconnue : « " + name + " »");
            auto outcome = it->second(s, file, step);
            created.insert(created.end(), outcome.created.begin(), outcome.created.end());
            if (!alias.empty()) { if (!outcome.id) throw Usage("« as » demande une opération qui renvoie une idée"); refs[alias] = outcome.id; }
            results += (index > 1 ? "," : "") + std::string("{\"op\":") + quote(name) + (outcome.json.empty() ? "" : "," + outcome.json) + "}";
            sentences += std::to_string(index) + ". " + outcome.text + "\n";
        }
        finish(s, a);
        submap::store(file, s);
    } catch (...) {
        for (const auto& f : created) { std::error_code e; fs::remove(f, e); }
        std::fprintf(stderr, "Batch annulé à l'opération %d : aucune modification écrite.\n", index);
        throw;
    }
    print(a.has("json") ? "{\"ok\":true,\"count\":" + std::to_string(ops.array.size()) + ",\"results\":" + results + "]}\n" : sentences);
}

// ----- Table des commandes (sert aussi à générer l'aide) -----
struct Command { const char* group; const char* name; const char* usage; const char* summary; void (*run)(const Args&); };
const std::vector<Command>& commands() {
    static const std::vector<Command> table = {
        {"Lire", "show", "show <carte>", "arbre des idées avec leurs numéros [id]", cmd_show},
        {"Lire", "outline", "outline <carte> [--deep]", "plan Markdown ; --deep insère les sous-cartes sous leur idée", cmd_outline},
        {"Lire", "tree", "tree <carte>", "carte, sous-cartes, sous-sous-cartes… avec leur état", cmd_tree},
        {"Lire", "validate", "validate <carte>", "vérifie toute la hiérarchie (code 1 si problème) et signale les fichiers non utilisés", cmd_validate},
        {"Lire", "orphans", "orphans <carte>", "fichiers de sous-cartes que plus aucune idée ne référence", cmd_orphans},
        {"Lire", "export", "export <carte> --to f.md|f.svg [--deep]", "exporte le plan ou l'image", cmd_export},
        {"Lire", "search", "search <carte> <mots> [--limit N]", "cherche dans les idées et notes de la carte et de toutes ses sous-cartes (accents et casse ignorés)", cmd_search},
        {"Créer / modifier une carte", "new", "new <carte> [--title T] [--outline plan.md|-]", "nouvelle carte, éventuellement remplie par un plan Markdown", cmd_new},
        {"Créer / modifier une carte", "add", "add <carte> --title T [--parent ID] [--note N|--note-file f] [--marker M] [--color #RRGGBB]", "ajoute une idée", edit<do_add>},
        {"Créer / modifier une carte", "add-outline", "add-outline <carte> [--parent ID] [--outline plan.md|-]", "insère un plan Markdown sous une idée (défaut : stdin)", edit<do_add_outline>},
        {"Créer / modifier une carte", "set", "set <carte> --id ID [--title T] [--note N|--note-file f] [--marker M] [--color #RRGGBB [--only-node]] [--collapsed|--expanded]", "modifie une idée", edit<do_set>},
        {"Créer / modifier une carte", "move", "move <carte> --id ID --parent ID", "change le parent d'une branche", edit<do_move>},
        {"Créer / modifier une carte", "remove", "remove <carte> --id ID", "supprime l'idée et ses descendants", edit<do_remove>},
        {"Créer / modifier une carte", "link", "link <carte> --from ID --to ID [--label L]", "lien transversal", edit<do_link>},
        {"Créer / modifier une carte", "unlink", "unlink <carte> --from ID --to ID", "supprime un lien", edit<do_unlink>},
        {"Créer / modifier une carte", "batch", "batch <carte> [--from ops.json|-]", "plusieurs modifications, une seule écriture, tout ou rien (voir docs/CLI.md)", cmd_batch},
        {"Cours et Nemo", "import-course", "import-course <carte> [--from cours.md|-] [--split N] [--title T]", "crée une carte (et N niveaux de sous-cartes) depuis un cours en Markdown ou texte", cmd_import_course},
        {"Cours et Nemo", "nemo-config", "nemo-config [--courses <dossier>]", "dossier des cours que Nemo synchronise (affiché si aucune option)", cmd_nemo_config},
        {"Cours et Nemo", "nemo-export", "nemo-export <carte> [--to <dossier>]", "écrit toute la hiérarchie en un cours Markdown « <titre> (carte mentale).md » que Nemo peut lire", cmd_nemo_export},
        {"Sous-cartes (une idée peut contenir une autre carte, qui peut elle-même en contenir…)", "submap-new", "submap-new <carte> --id ID [--title T] [--outline plan.md|-]", "crée <carte>/<idée>.mindmap et le relie à l'idée", edit<do_submap_new>},
        {"Sous-cartes (une idée peut contenir une autre carte, qui peut elle-même en contenir…)", "submap-attach", "submap-attach <carte> --id ID --file chemin/relatif.mindmap", "relie une carte existante (dans le dossier de la carte)", edit<do_submap_attach>},
        {"Sous-cartes (une idée peut contenir une autre carte, qui peut elle-même en contenir…)", "submap-detach", "submap-detach <carte> --id ID", "détache la sous-carte (le fichier est conservé)", edit<do_submap_detach>},
    };
    return table;
}
std::string help_text() {
    std::string out = "mindmap-cli — éditer et créer des cartes MindMap en ligne de commande\n\n"
                      "Usage : mindmap-cli <commande> <fichier.mindmap> [options]     (--json : sortie lisible par un programme)\n";
    const char* group = "";
    for (const auto& c : commands()) {
        if (std::strcmp(group, c.group) != 0) { group = c.group; out += std::string("\n") + group + "\n"; }
        out += std::string("  ") + c.usage + "\n      " + c.summary + "\n";
    }
    return out + "\nRepères (--marker) : 0 aucun, 1 important, 2 acquis, 3 revoir, 4 attention, 5 repere\n"
                 "Dans --note, \\n produit un retour à la ligne. Les numéros d'idée viennent de « show ». --no-layout garde les positions telles quelles.\n";
}

int run(const std::vector<std::string>& argv) {
    if (argv.empty() || argv[0] == "help" || argv[0] == "--help" || argv[0] == "-h") { print(help_text()); return argv.empty() ? 2 : 0; }
    auto a = parse_args(argv);
    if (a.has("help")) { print(help_text()); return 0; }
    const auto name = a.positional.empty() ? std::string() : a.positional[0];
    for (const auto& c : commands()) if (name == c.name) { c.run(a); return 0; }
    throw Usage("Commande inconnue : " + name + " (essayez --help)");
}
int guarded(const std::vector<std::string>& args) {
    try { return run(args); }
    catch (const Usage& e) { std::fprintf(stderr, "Erreur d'utilisation : %s\n", e.what()); return 2; }
    catch (const std::exception& e) { std::fprintf(stderr, "Erreur : %s\n", e.what()); return 1; }
    catch (const PyThrow& e) { std::fprintf(stderr, "Erreur : %s\n", e.p ? e.p->py_exc_str().c_str() : "opération refusée"); return 1; }
}
} // namespace

#ifdef _WIN32
// Arguments en UTF-16 : les dossiers accentués (« hist géo ») et les titres ne passent pas par la page de codes.
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc > 0) executable_folder = fs::absolute(fs::path(argv[0])).parent_path();
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string text(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
        if (size > 1) WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, text.data(), size, nullptr, nullptr);
        args.push_back(std::move(text));
    }
    return guarded(args);
}
#else
int main(int argc, char** argv) { executable_folder = fs::absolute(argv[0]).parent_path(); return guarded(std::vector<std::string>(argv + 1, argv + argc)); }
#endif
