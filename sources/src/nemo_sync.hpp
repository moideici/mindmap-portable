#pragma once
// Optional network bridge to Nemo (off by default; used by mindmap-cli only, never by the window):
//   - nemo-login / nemo-logout : the token lives in the Windows Credential Manager, never in a file or the repository;
//   - nemo-push                : uploads the Markdown course straight to Nemo (no synchronisation daemon), with an offline queue;
//   - nemo-mastery             : read-only, what Nemo knows about each idea (acquis / à revoir / en cours / inconnu).
#include "courses.hpp"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <wincred.h>
#endif

namespace nemo {
namespace fs = submap::fs;

// ----- Remote settings (.mindmap-data/nemo-remote.json) -----
struct Remote {
    std::string url;            // https://nemo.example (no trailing slash)
    std::string subject;        // course subject folder in Nemo, e.g. "hist géo"
    std::string folder = "cartes"; // sub-folder of the subject that holds generated courses ("" = the subject folder itself)
    bool class_scope = false;   // true = shared class courses, false = personal courses
};
inline fs::path remote_file(const fs::path& root) { return fs::absolute(root) / ".mindmap-data" / "nemo-remote.json"; }
inline Remote load_remote(const fs::path& root) {
    Remote r;
    auto file = remote_file(root);
    if (!fs::exists(file)) return r;
    auto j = document::Parser(workspace::read(file)).parse();
    if (j.at("format").text() != "mindmap-nemo-remote") throw std::runtime_error("Configuration réseau Nemo illisible");
    auto opt = [&](const char* k, std::string& out) { if (j.object.count(k)) out = j.at(k).text(); };
    opt("url", r.url); opt("subject", r.subject); opt("folder", r.folder);
    if (j.object.count("scope")) r.class_scope = j.at("scope").text() == "class";
    while (!r.url.empty() && r.url.back() == '/') r.url.pop_back();
    return r;
}
inline void save_remote(const fs::path& root, const Remote& r) {
    auto file = remote_file(root);
    fs::create_directories(file.parent_path());
    workspace::write(file, "{\"format\":\"mindmap-nemo-remote\",\"version\":1,\"url\":" + document::quote(r.url) + ",\"subject\":" + document::quote(r.subject)
        + ",\"folder\":" + document::quote(r.folder) + ",\"scope\":" + document::quote(r.class_scope ? "class" : "personal") + "}\n");
}
inline void need_remote(const Remote& r) {
    if (r.url.empty() || r.subject.empty()) throw std::runtime_error("Réseau Nemo non configuré : nemo-remote --url https://… --subject \"hist géo\" [--scope class] puis nemo-login");
    if (r.url.rfind("https://", 0) != 0 && r.url.rfind("http://127.0.0.1", 0) != 0 && r.url.rfind("http://localhost", 0) != 0)
        throw std::runtime_error("L'adresse de Nemo doit être en https:// (http n'est accepté que pour 127.0.0.1 / localhost)");
}

// ----- Percent-encoding of a path segment -----
inline std::string pct(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
// Key of a generated course on Nemo: <subject>/<folder>/<file>; the path is sent with "/" kept and each segment encoded.
inline std::string course_key(const Remote& r, const std::string& file_name) { return r.subject + "/" + (r.folder.empty() ? "" : r.folder + "/") + file_name; }
inline std::string encode_key(const std::string& key) {
    std::string out;
    size_t start = 0;
    while (true) {
        auto slash = key.find('/', start);
        out += pct(key.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        out += '/';
        start = slash + 1;
    }
    return out;
}
inline std::string course_url(const Remote& r, const std::string& key) {
    return r.url + (r.class_scope ? "/api/admin/classe/courses/" : "/api/admin/courses/") + encode_key(key);
}

#ifdef _WIN32
// ----- Token in the Windows Credential Manager -----
inline const wchar_t* credential_target = L"MindMap/Nemo";
inline std::wstring wide_text(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
inline void store_token(const std::string& token) {
    std::wstring user = L"nemo";
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = const_cast<LPWSTR>(credential_target);
    c.CredentialBlobSize = static_cast<DWORD>(token.size());
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(token.data()));
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.UserName = user.data();
    if (!CredWriteW(&c, 0)) throw std::runtime_error("Impossible d'enregistrer le jeton dans le Gestionnaire d'identification Windows");
}
inline void clear_token() { CredDeleteW(credential_target, CRED_TYPE_GENERIC, 0); }
// NEMO_TOKEN (environment) wins, handy for scripts and tests; otherwise the Credential Manager.
inline std::string load_token() {
    if (const char* env = std::getenv("NEMO_TOKEN")) if (*env) return env;
    PCREDENTIALW c = nullptr;
    if (!CredReadW(credential_target, CRED_TYPE_GENERIC, 0, &c)) return {};
    std::string token(reinterpret_cast<const char*>(c->CredentialBlob), c->CredentialBlobSize);
    CredFree(c);
    return token;
}

// ----- HTTP (WinHTTP, short timeouts: a missing network must never hang the tool) -----
struct Reply { long status = 0; std::string body; };
struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET v = nullptr) : h(v) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
// Throws std::runtime_error("réseau : …") when the server cannot be reached (callers queue the work in that case).
inline Reply http(const std::string& method, const std::string& url, const std::string& token, const std::string& content_type, const std::string& body) {
    auto wurl = wide_text(url);
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[4096];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) throw std::runtime_error("Adresse invalide : " + url);
    Handle session(WinHttpOpen(L"MindMap/3 (Nemo)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) throw std::runtime_error("réseau : WinHTTP indisponible");
    WinHttpSetTimeouts(session.h, 5000, 5000, 20000, 20000);
    Handle connection(WinHttpConnect(session.h, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0));
    if (!connection.h) throw std::runtime_error("réseau : connexion impossible");
    Handle request(WinHttpOpenRequest(connection.h, wide_text(method).c_str(), std::wstring(path, uc.dwUrlPathLength).c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    if (!request.h) throw std::runtime_error("réseau : requête impossible");
    std::wstring headers = L"Authorization: Bearer " + wide_text(token) + L"\r\n" + (content_type.empty() ? L"" : L"Content-Type: " + wide_text(content_type) + L"\r\n");
    if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(headers.size()), body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)
        || !WinHttpReceiveResponse(request.h, nullptr))
        throw std::runtime_error("réseau : Nemo injoignable (" + std::to_string(GetLastError()) + ")");
    Reply reply;
    DWORD size = sizeof(DWORD), code = 0;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
    reply.status = static_cast<long>(code);
    for (DWORD available = 0; WinHttpQueryDataAvailable(request.h, &available) && available > 0;) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read) || read == 0) break;
        reply.body.append(chunk, 0, read);
        if (reply.body.size() > (4u << 20)) break;
    }
    return reply;
}
#else
inline void store_token(const std::string&) { throw std::runtime_error("Réseau Nemo : Windows uniquement"); }
inline void clear_token() {}
inline std::string load_token() { if (const char* env = std::getenv("NEMO_TOKEN")) return env; return {}; }
struct Reply { long status = 0; std::string body; };
inline Reply http(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&) { throw std::runtime_error("réseau : Windows uniquement"); }
#endif

inline std::string reply_problem(const Reply& r, const std::string& need = "le dépôt de cours demande un profil administrateur") {
    if (r.status == 401 || r.status == 403) return "Nemo refuse le jeton (HTTP " + std::to_string(r.status) + ") : " + need + " ; refaites nemo-login.";
    if (r.status == 413) return "Plafond de stockage Nemo atteint (HTTP 413) : supprimez des cours avant de publier.";
    return "Nemo a répondu HTTP " + std::to_string(r.status) + (r.body.empty() ? "" : " : " + r.body.substr(0, 200));
}

// ----- Publishing (PUT <file> and its .txt companion, which is the text Nemo reads) with an offline queue -----
inline fs::path queue_folder(const fs::path& root) { return fs::absolute(root) / ".mindmap-data" / "nemo-queue"; }
inline std::string checked_token() {
    auto token = load_token();
    if (token.empty()) throw std::runtime_error("Aucun jeton Nemo : echo VOTRE_JETON | mindmap-cli nemo-login");
    return token;
}
// true = published, false = network unreachable (the caller queues it); throws on a refusal that retrying cannot fix.
inline bool put_course(const Remote& r, const std::string& token, const std::string& file_name, const std::string& text) {
    const auto key = course_key(r, file_name);
    try {
        for (const auto& suffix : {std::string(), std::string(".txt")}) {
            auto reply = http("PUT", course_url(r, key + suffix), token, "application/octet-stream", text);
            if (reply.status < 200 || reply.status >= 300) throw std::runtime_error(reply_problem(reply));
        }
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()).rfind("réseau :", 0) == 0) return false;
        throw;
    }
    return true;
}
inline void delete_course(const Remote& r, const std::string& token, const std::string& key) {
    for (const auto& suffix : {std::string(), std::string(".txt")}) {
        auto reply = http("DELETE", course_url(r, key + suffix), token, "", "");
        if (reply.status < 200 || reply.status >= 300) throw std::runtime_error(reply_problem(reply));
    }
}
// Sends what the offline queue holds; returns the number of files sent.
inline size_t flush_queue(const fs::path& root, const Remote& r, const std::string& token) {
    size_t sent = 0;
    std::error_code e;
    if (!fs::is_directory(queue_folder(root), e)) return 0;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(queue_folder(root), e)) if (entry.is_regular_file()) files.push_back(entry.path());
    for (const auto& file : files) {
        if (!put_course(r, token, workspace::path_text(file.filename()), workspace::read(file))) break;
        fs::remove(file, e);
        ++sent;
    }
    return sent;
}
// Remote keys of the previous publication of a map (manifest "pushed"): a renamed map deletes its old course on Nemo.
inline fs::path pushed_file(const fs::path& root) { return fs::absolute(root) / ".mindmap-data" / "nemo-pushed.json"; }
inline std::vector<Tracked> load_pushed(const fs::path& root) {
    std::vector<Tracked> out;
    auto file = pushed_file(root);
    if (!fs::exists(file)) return out;
    try {
        auto j = document::Parser(workspace::read(file)).parse();
        for (const auto& e : j.at("pushed").array) out.push_back({e.at("map").text(), e.at("file").text()});
    } catch (...) { out.clear(); }
    return out;
}
inline void save_pushed(const fs::path& root, const std::vector<Tracked>& list) {
    std::string out = "{\"format\":\"mindmap-nemo-pushed\",\"version\":1,\"pushed\":[";
    for (size_t i = 0; i < list.size(); ++i) out += (i ? "," : "") + std::string("{\"map\":") + document::quote(list[i].map) + ",\"file\":" + document::quote(list[i].file) + "}";
    auto file = pushed_file(root);
    fs::create_directories(file.parent_path());
    workspace::write(file, out + "]}\n");
}
// Publishes one map's whole hierarchy. Returns "sent" or "queued".
inline std::string push_map(const fs::path& root, const Remote& r, const std::string& token, const fs::path& map_file) {
    auto state = submap::load(map_file);
    const auto name = workspace::path_text(course_file(fs::path(), state).filename());
    const auto text = course::markdown(map_file);
    const auto key = course_key(r, name);
    if (!put_course(r, token, name, text)) {
        fs::create_directories(queue_folder(root));
        workspace::write(queue_folder(root) / workspace::text_path(name), text);
        return "queued";
    }
    try { record_titles(root, map_file); } catch (...) {}
    auto list = load_pushed(root);
    std::vector<Tracked> next;
    const auto map_text = workspace::path_text(fs::absolute(map_file).lexically_normal());
    for (auto& t : list) {
        if (submap::same(workspace::text_path(t.map), map_file)) { if (t.file != key) { try { delete_course(r, token, t.file); } catch (...) {} } }
        else next.push_back(t);
    }
    next.push_back({map_text, key});
    save_pushed(root, next);
    return "sent";
}

// ----- Mastery -----
struct Mastery { std::string status = "inconnu"; int box = 0; double mastery = 0; std::string next; };
struct MasteryReply { std::map<std::string, Mastery> by_title, by_uid; };
inline Mastery mastery_of(const document::Json& item) {
    Mastery m;
    m.status = item.at("status").text();
    if (item.object.count("box")) m.box = static_cast<int>(item.at("box").num());
    if (item.object.count("mastery")) m.mastery = item.at("mastery").num();
    if (item.object.count("nextReviewAt")) m.next = item.at("nextReviewAt").text();
    return m;
}
// Asks Nemo about the titles and the identified ideas ({uid, title}), 200 of each at a time. Titles answer in "items", keyed by the title
// as sent; ideas in "ideas", keyed by uid (a Nemo without identifiers ignores them: the titles still answer).
inline MasteryReply lookup_mastery(const Remote& r, const std::string& token, const std::vector<std::string>& titles,
                                   const std::vector<std::pair<std::string, std::string>>& ideas = {}) {
    MasteryReply out;
    for (size_t from = 0; from < titles.size() || from < ideas.size(); from += 200) {
        std::string body = "{\"subject\":" + document::quote(r.subject) + ",\"titles\":[";
        for (size_t i = from; i < std::min(titles.size(), from + 200); ++i) body += (i > from ? "," : "") + document::quote(titles[i]);
        body += "]";
        if (from < ideas.size()) {
            body += ",\"ideas\":[";
            for (size_t i = from; i < std::min(ideas.size(), from + 200); ++i)
                body += (i > from ? "," : "") + std::string("{\"id\":") + document::quote(ideas[i].first) + ",\"title\":" + document::quote(ideas[i].second) + "}";
            body += "]";
        }
        body += "}";
        auto reply = http("POST", r.url + "/api/mastery/lookup", token, "application/json", body);
        if (reply.status != 200) throw std::runtime_error(reply_problem(reply, "la lecture de la maîtrise demande le jeton du profil élève (ou le jeton principal)"));
        auto j = document::Parser(reply.body).parse();
        for (const auto& item : j.at("items").array) out.by_title[item.at("title").text()] = mastery_of(item);
        if (auto it = j.object.find("ideas"); it != j.object.end() && it->second.kind == document::Json::Array)
            for (const auto& item : it->second.array) out.by_uid[item.at("id").text()] = mastery_of(item);
    }
    return out;
}
} // namespace nemo
