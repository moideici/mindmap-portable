#pragma once
#include "exports.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace workspace {
namespace fs=std::filesystem;
inline std::string path_text(const fs::path& path){auto s=path.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
inline fs::path text_path(const std::string& text){return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));}
inline std::string read(const fs::path& path){
    if(fs::file_size(path)>document::max_file_bytes)throw std::runtime_error("Fichier trop volumineux (8 Mio maximum)");
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Fichier inaccessible");
    std::string out((std::istreambuf_iterator<char>(f)),{});if(f.bad())throw std::runtime_error("Lecture impossible");return out;
}
inline void write(const fs::path& path,const std::string& data){
    auto temp=path;temp+=".tmp";
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.write(data.data(),static_cast<std::streamsize>(data.size()));f.flush();if(!f)throw std::runtime_error("Sauvegarde impossible");}
#ifdef _WIN32
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Sauvegarde impossible");
#else
    fs::rename(temp,path);
#endif
}
inline std::string key(std::string_view text){
    uint64_t hash=14695981039346656037ULL;for(unsigned char c:text){hash^=c;hash*=1099511628211ULL;}
    std::ostringstream out;out<<std::hex<<hash;return out.str();
}
inline std::string stamp(){
    const auto now=std::chrono::system_clock::now();auto time=std::chrono::system_clock::to_time_t(now);std::tm date{};
#ifdef _WIN32
    localtime_s(&date,&time);
#else
    localtime_r(&time,&date);
#endif
    std::ostringstream out;out<<std::put_time(&date,"%Y-%m-%d_%H-%M-%S")<<'-'<<std::setw(3)<<std::setfill('0')<<std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000;
    return out.str();
}
inline fs::path unique_file(const fs::path& folder,const std::string& name){
    auto file=folder/text_path(name+".mindmap");size_t n=1;
    while(fs::exists(file)){
        std::ostringstream suffix;suffix<<'_'<<std::setw(4)<<std::setfill('0')<<n++;
        file=folder/text_path(name+suffix.str()+".mindmap");
    }
    return file;
}
struct Card {fs::path path;std::string title;};
struct Model {const char* name;const char* description;const char* outline;};
inline const std::vector<Model>& models(){
    static const std::vector<Model> entries={
        {"Carte vide","Commencer avec un sujet central","# Mon sujet\n"},
        {"Un chapitre","Notions, exemples et questions","# Mon chapitre\n- Notions clés\n  - Définition\n  - Exemple\n- Dates et repères\n- Causes et conséquences\n- Questions pour s'entraîner\n"},
        {"Dissertation","Problématique et arguments","# Sujet de dissertation\n- Problématique\n- Introduction\n- Partie I\n  - Argument\n  - Exemple\n- Partie II\n  - Argument\n  - Exemple\n- Conclusion\n"},
        {"Chronologie","Événements et conséquences","# Ma chronologie\n- Première période\n  - Date et événement\n  - Conséquences\n- Deuxième période\n  - Date et événement\n  - Conséquences\n"},
        {"Comparer deux idées","Points communs et différences","# Comparaison\n- Idée A\n  - Définition\n  - Points forts\n  - Limites\n- Idée B\n  - Définition\n  - Points forts\n  - Limites\n- Points communs\n- Différences\n"}
    };return entries;
}
class Store {
    fs::path root;
    std::function<void(const fs::path&,const std::string&)> writer;
    std::string stored_path(const fs::path& path)const{
        auto relative=fs::absolute(path).lexically_normal().lexically_relative(root);
        if(!relative.empty()&&*relative.begin()!="..")return path_text(relative);
        return path_text(fs::absolute(path).lexically_normal());
    }
    fs::path restore_path(const std::string& path)const{auto p=text_path(path);return p.is_absolute()?p:root/p;}
public:
    std::vector<fs::path> recent;
    bool legacy_migrated=false;
    explicit Store(fs::path folder,std::function<void(const fs::path&,const std::string&)> write_fn=write)
        :root(fs::absolute(folder).lexically_normal()),writer(std::move(write_fn)){}
    fs::path data()const{return root/".mindmap-data";}
    void load(){
        const auto file=data()/"workspace.json";if(!fs::exists(file))return;
        auto j=document::Parser(read(file)).parse();
        if(j.at("format").text()!="mindmap-workspace"||j.at("version").integer(1,1)!=1)throw std::runtime_error("Liste des cartes illisible");
        const auto& list=j.at("recent");if(list.kind!=document::Json::Array||list.array.size()>12)throw std::runtime_error("Liste des cartes invalide");
        std::vector<fs::path> loaded;
        for(const auto& value:list.array){auto p=restore_path(value.text());if(p.extension()==".mindmap"&&std::find(loaded.begin(),loaded.end(),p)==loaded.end())loaded.push_back(p);}
        recent=std::move(loaded);legacy_migrated=j.at("legacy_migrated").flag();
    }
    void save_index(){
        fs::create_directories(data());std::string text="{\"format\":\"mindmap-workspace\",\"version\":1,\"legacy_migrated\":";
        text+=legacy_migrated?"true":"false";text+=",\"recent\":[";
        for(size_t i=0;i<recent.size();++i){if(i)text+=',';text+=document::quote(stored_path(recent[i]));}writer(data()/"workspace.json",text+"]}\n");
    }
    void opened(const fs::path& path){
        auto p=fs::absolute(path).lexically_normal();std::error_code error;
        std::erase_if(recent,[&](const fs::path& item){return item==p||fs::equivalent(item,p,error);});
        recent.insert(recent.begin(),p);if(recent.size()>12)recent.resize(12);save_index();
    }
    std::vector<Card> cards(const std::string& section)const{
        std::vector<Card> out;auto folder=data()/section;if(!fs::exists(folder))return out;
        for(const auto& entry:fs::directory_iterator(folder))if(entry.is_regular_file()&&entry.path().extension()==".mindmap"){
            try{out.push_back({entry.path(),document::decode(read(entry.path())).map->nodes.at(1)->title});}catch(const std::exception&){/* Keep unreadable files intact for manual recovery. */}
        }
        std::sort(out.begin(),out.end(),[](const Card& a,const Card& b){return a.path.filename()>b.path.filename();});return out;
    }
    fs::path new_draft(){auto folder=data()/"drafts";fs::create_directories(folder);return unique_file(folder,stamp());}
    void draft(const fs::path& file,const std::string& text){
        if(file.parent_path()!=data()/"drafts")throw std::runtime_error("Emplacement de brouillon invalide");
        fs::create_directories(file.parent_path());writer(file,text);
    }
    void forget_draft(const fs::path& file){if(!file.empty()&&file.parent_path()==data()/"drafts")fs::remove(file);}
    fs::path backup_folder(const fs::path& file)const{return data()/"backups"/key(stored_path(file));}
    std::vector<fs::path> versions(const fs::path& file)const{
        std::vector<fs::path> out;auto folder=backup_folder(file);if(!fs::exists(folder))return out;
        for(const auto& e:fs::directory_iterator(folder))if(e.is_regular_file()&&e.path().extension()==".mindmap")out.push_back(e.path());
        std::sort(out.begin(),out.end(),std::greater<fs::path>());return out;
    }
    void save_named(const fs::path& file,const std::string& text,const std::string* expected,bool manual){
        if(fs::exists(file)){
            auto before=read(file);
            if(expected&&before!=*expected)throw std::runtime_error("Cette carte a été modifiée par un autre programme. Vos changements restent dans un brouillon de secours : utilisez Enregistrer sous ou rouvrez la carte.");
            if(before==text)return;
            auto list=versions(file);
            const bool due=list.empty()||fs::file_time_type::clock::now()-fs::last_write_time(list.front())>=std::chrono::minutes(5);
            if((manual||due)&&(list.empty()||read(list.front())!=before)){
                document::decode(before);auto folder=backup_folder(file);fs::create_directories(folder);writer(unique_file(folder,stamp()),before);
            }
        }else if(expected)throw std::runtime_error("Le fichier de la carte a disparu. Utilisez Enregistrer sous pour choisir un emplacement.");
        writer(file,text);
        auto list=versions(file);for(size_t i=10;i<list.size();++i){std::error_code error;fs::remove(list[i],error);}
    }
    void custom_model(const std::string& name,const document::State& state){
        auto label=document::trim(name);if(label.empty())throw std::runtime_error("Donnez un nom au modèle");
        if(cards("templates").size()>=100)throw std::runtime_error("La bibliothèque contient déjà 100 modèles");
        auto copy=document::decode(document::encode(state));copy.selected=1;copy.zoom=1;copy.pan_x=260;copy.pan_y=340;
        copy.map->nodes.at(1)->title=document::clip_utf8(label,1024);
        auto folder=data()/"templates";fs::create_directories(folder);writer(unique_file(folder,stamp()),document::encode(copy));
        // The root title names the template in the gallery; the original document is untouched.
    }
};
} // namespace workspace
