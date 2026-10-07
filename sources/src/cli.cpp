// mindmap-cli : édition et création de cartes en ligne de commande (pensé pour les IA et les scripts).
// Chaque commande lit un fichier .mindmap, applique UNE modification et l'écrit de façon atomique.
#include "submaps.hpp"
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

std::string read_stdin() {
    std::string out; char buffer[8192]; size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, stdin)) > 0) { out.append(buffer, n); if (out.size() > document::max_file_bytes) throw std::runtime_error("Entrée trop volumineuse"); }
    return out;
}
// "-" lit l'entrée standard ; sinon c'est un fichier texte (Markdown ou plan indenté).
std::string read_text_source(const std::string& source) { return source == "-" ? read_stdin() : workspace::read(workspace::text_path(source)); }
// Une option --note-file / --outline peut donner du texte multi-lignes ; --note donne du texte direct (\n accepté).
std::string unescape_newlines(std::string s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out += '\n'; ++i; }
        else out += s[i];
    }
    return out;
}
std::string note_value(const Args& a) {
    if (a.has("note-file")) return read_text_source(a.get("note-file"));
    return unescape_newlines(a.get("note"));
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

// ----- Sorties -----
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
void print(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); std::fflush(stdout); }
// Réponse des commandes d'écriture : JSON compact avec --json, sinon une phrase.
void report(const Args& a, const std::string& sentence, const std::string& json_fields) {
    if (a.has("json")) print("{\"ok\":true" + (json_fields.empty() ? "" : "," + json_fields) + "}\n");
    else print(sentence + "\n");
}

// ----- Commandes -----
void cmd_new(const Args& a) {
    auto file = map_path(a);
    if (fs::exists(file)) throw std::runtime_error("Le fichier existe déjà : " + workspace::path_text(file));
    auto title = a.has("title") ? checked_title(a.get("title")) : workspace::path_text(file.stem());
    document::State s = a.has("outline") ? document::from_outline(document::parse_outline(read_text_source(a.get("outline"))), title) : document::State();
    if (a.has("title") || !a.has("outline")) s.map->nodes.at(1)->title = title;
    finish(s, a);
    if (file.has_parent_path()) fs::create_directories(file.parent_path());
    submap::store(file, s);
    report(a, "Carte créée : " + workspace::path_text(file) + " (" + std::to_string(s.map->nodes.size()) + " idées)",
        "\"file\":" + quote(workspace::path_text(file)) + ",\"nodes\":" + std::to_string(s.map->nodes.size()));
}
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
void cmd_add(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    auto parent = a.has("parent") ? a.id("parent") : 1;
    need_node(s, parent);
    auto id = s.map->add(parent, checked_title(a.need("title")));
    auto note = note_value(a);
    if (!note.empty()) { if (note.size() > 16000) throw Usage("Note trop longue (16000 octets maximum)"); s.map->nodes.at(id)->note = note; }
    if (a.has("marker")) { if (int m = parse_marker(a.get("marker"))) s.markers[id] = m; }
    if (a.has("color")) s.map->nodes.at(id)->color = parse_color(a.get("color"));
    if (s.map->nodes.size() > 2000) throw std::runtime_error("Limite de 2000 idées atteinte");
    finish(s, a); submap::store(file, s);
    report(a, "Idée ajoutée : [" + std::to_string(id) + "]", "\"id\":" + std::to_string(id));
}
void cmd_add_outline(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    auto parent = a.has("parent") ? a.id("parent") : 1;
    need_node(s, parent);
    auto created = document::apply_outline(s, parent, document::parse_outline(read_text_source(a.get("outline", "-"))));
    finish(s, a); submap::store(file, s);
    std::string ids;
    for (size_t i = 0; i < created.size(); ++i) ids += (i ? "," : "") + std::to_string(created[i]);
    report(a, std::to_string(created.size()) + " idées ajoutées sous [" + std::to_string(parent) + "] : " + ids, "\"count\":" + std::to_string(created.size()) + ",\"ids\":[" + ids + "]");
}
void cmd_set(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
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
    finish(s, a); submap::store(file, s);
    report(a, "Idée [" + std::to_string(id) + "] modifiée", "\"id\":" + std::to_string(id));
}
void cmd_move(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    auto id = a.id("id"), parent = a.id("parent");
    need_node(s, id); need_node(s, parent);
    s.map->reparent(id, parent);
    finish(s, a); submap::store(file, s);
    report(a, "Idée [" + std::to_string(id) + "] déplacée sous [" + std::to_string(parent) + "]", "\"id\":" + std::to_string(id));
}
void cmd_remove(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    auto id = a.id("id"); need_node(s, id);
    const auto before = s.map->nodes.size();
    s.map->remove(id);
    finish(s, a); submap::store(file, s);
    auto removed = before - s.map->nodes.size();
    report(a, std::to_string(removed) + " idée(s) supprimée(s) (les fichiers de sous-cartes ne sont pas effacés)", "\"removed\":" + std::to_string(removed));
}
void cmd_link(const Args& a, bool remove) {
    auto file = map_path(a); auto s = submap::load(file);
    auto from = a.id("from"), to = a.id("to"); need_node(s, from); need_node(s, to);
    if (remove) {
        auto count = std::erase_if(s.links, [&](const document::Link& l) { return (l.from == from && l.to == to) || (l.from == to && l.to == from); });
        if (!count) throw std::runtime_error("Aucun lien entre ces deux idées");
    } else {
        auto label = a.get("label");
        if (label.size() > document::max_link_label) throw Usage("Libellé trop long (200 octets maximum)");
        if (!document::add_link(s, from, to, label)) throw std::runtime_error("Ces deux idées sont déjà reliées");
    }
    finish(s, a); submap::store(file, s);
    report(a, remove ? "Lien supprimé" : "Lien créé", "");
}
void cmd_submap_new(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    auto id = a.id("id");
    need_node(s, id);
    if (a.has("title")) s.map->nodes.at(id)->title = checked_title(a.get("title"));
    auto outline = a.has("outline") ? read_text_source(a.get("outline")) : std::string();
    auto child = submap::create(s, fs::absolute(file), id, outline);
    finish(s, a); submap::store(file, s);
    report(a, "Sous-carte créée : " + workspace::path_text(child) + " (idée [" + std::to_string(id) + "])",
        "\"file\":" + quote(workspace::path_text(child)) + ",\"relative\":" + quote(s.submaps.at(id)));
}
void cmd_submap_attach(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    auto id = a.id("id");
    auto target = workspace::text_path(a.need("file"));
    // Le chemin donné est relatif au dossier de la carte parente (comme dans le fichier) ou absolu.
    if (!target.is_absolute()) target = submap::folder_of(file) / target;
    submap::attach(s, fs::absolute(file), id, target);
    finish(s, a); submap::store(file, s);
    report(a, "Idée [" + std::to_string(id) + "] reliée à la sous-carte " + s.submaps.at(id), "\"relative\":" + quote(s.submaps.at(id)));
}
void cmd_submap_detach(const Args& a) {
    auto file = map_path(a); auto s = submap::load(file);
    auto id = a.id("id");
    if (!s.submaps.erase(id)) throw std::runtime_error("Cette idée n'a pas de sous-carte");
    finish(s, a); submap::store(file, s);
    report(a, "Sous-carte détachée (le fichier est conservé)", "");
}
void cmd_validate(const Args& a) {
    auto file = map_path(a);
    std::vector<std::string> problems;
    auto list = submap::hierarchy(file);
    for (const auto& e : list) if (e.status != submap::Status::Ok)
        problems.push_back(std::string(submap::status_name(e.status)) + " : " + (e.via ? e.relative : workspace::path_text(e.file)));
    std::string out;
    if (a.has("json")) {
        out = "{\"ok\":" + std::string(problems.empty() ? "true" : "false") + ",\"maps\":" + std::to_string(list.size()) + ",\"problems\":[";
        for (size_t i = 0; i < problems.size(); ++i) out += (i ? "," : "") + quote(problems[i]);
        print(out + "]}\n");
    } else if (problems.empty()) print("OK : " + std::to_string(list.size()) + " carte(s) valide(s)\n");
    else { for (const auto& p : problems) out += "⚠ " + p + "\n"; print(out); }
    if (!problems.empty()) throw std::runtime_error(std::to_string(problems.size()) + " problème(s) détecté(s)");
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
    report(a, "Exporté : " + workspace::path_text(out), "\"file\":" + quote(workspace::path_text(out)));
}

const char* help_text = R"(mindmap-cli — éditer et créer des cartes MindMap en ligne de commande

Usage : mindmap-cli <commande> <fichier.mindmap> [options]        (ajoutez --json pour une sortie lisible par un programme)

Lire
  show      <carte>                      arbre des idées avec leurs numéros [id]
  outline   <carte> [--deep]             plan Markdown ; --deep insère les sous-cartes sous leur idée
  tree      <carte>                      carte, sous-cartes, sous-sous-cartes… avec leur état
  validate  <carte>                      vérifie la carte et toute sa hiérarchie (code 1 si problème)
  export    <carte> --to f.md|f.svg [--deep]

Créer / modifier une carte
  new         <carte> [--title T] [--outline plan.md|-]       nouvelle carte, éventuellement remplie par un plan Markdown
  add         <carte> --title T [--parent ID] [--note N|--note-file f] [--marker M] [--color #RRGGBB]
  add-outline <carte> [--parent ID] [--outline plan.md|-]     insère un plan Markdown sous une idée (défaut : stdin)
  set         <carte> --id ID [--title T] [--note N|--note-file f] [--marker M] [--color #RRGGBB [--only-node]] [--collapsed|--expanded]
  move        <carte> --id ID --parent ID
  remove      <carte> --id ID                                  supprime l'idée et ses descendants
  link / unlink <carte> --from ID --to ID [--label L]          lien transversal

Sous-cartes (une idée peut contenir une autre carte, qui peut elle-même en contenir…)
  submap-new    <carte> --id ID [--title T] [--outline plan.md|-]   crée le fichier <carte>/<idée>.mindmap et le relie à l'idée
  submap-attach <carte> --id ID --file chemin/relatif.mindmap       relie une carte existante (dans le dossier de la carte)
  submap-detach <carte> --id ID

Repères (--marker) : 0 aucun, 1 important, 2 acquis, 3 revoir, 4 attention, 5 repere
Dans --note, \n produit un retour à la ligne. Les numéros d'idée viennent de « show ». --no-layout garde les positions telles quelles.
)";

int run(const std::vector<std::string>& argv) {
    if (argv.empty() || argv[0] == "help" || argv[0] == "--help" || argv[0] == "-h") { print(help_text); return argv.empty() ? 2 : 0; }
    auto a = parse_args(argv);
    if (a.has("help")) { print(help_text); return 0; }
    const auto& c = a.positional.empty() ? std::string() : a.positional[0];
    if (c == "new") cmd_new(a);
    else if (c == "show") cmd_show(a);
    else if (c == "outline") cmd_outline(a);
    else if (c == "tree") cmd_tree(a);
    else if (c == "add") cmd_add(a);
    else if (c == "add-outline") cmd_add_outline(a);
    else if (c == "set") cmd_set(a);
    else if (c == "move") cmd_move(a);
    else if (c == "remove") cmd_remove(a);
    else if (c == "link") cmd_link(a, false);
    else if (c == "unlink") cmd_link(a, true);
    else if (c == "submap-new") cmd_submap_new(a);
    else if (c == "submap-attach") cmd_submap_attach(a);
    else if (c == "submap-detach") cmd_submap_detach(a);
    else if (c == "validate") cmd_validate(a);
    else if (c == "export") cmd_export(a);
    else throw Usage("Commande inconnue : " + c + " (essayez --help)");
    return 0;
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
int main(int argc, char** argv) { return guarded(std::vector<std::string>(argv + 1, argv + argc)); }
#endif
