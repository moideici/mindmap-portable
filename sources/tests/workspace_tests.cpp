#include "../src/workspace.hpp"
#include "../src/imports.hpp"
#include "../src/pdf.hpp"
#include "../src/submaps.hpp"
#include <cstdio>
#include <source_location>
#ifdef _WIN32
#include <shellapi.h>
#endif

static int failures=0;
#define CHECK(c) do {if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
template<class F>void rejects(F fn,std::source_location site=std::source_location::current()){
    bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}catch(const PyThrow&){rejected=true;}
    if(!rejected){std::printf("FAIL %u: input should have been rejected\n",site.line());++failures;}
}
static void le(std::string& bytes,uint32_t n,int count){for(int i=0;i<count;++i)bytes+=static_cast<char>(n>>(i*8));}
static std::string zip(const std::string& name,const std::string& text){
    // A real stored ZIP fixture, also exercises CRC and central-directory boundaries.
    std::string out;le(out,0x04034b50,4);le(out,20,2);le(out,0,2);le(out,0,2);le(out,0,4);le(out,document::zip_crc(text),4);le(out,text.size(),4);le(out,text.size(),4);le(out,name.size(),2);le(out,0,2);out+=name+text;
    const auto index=out.size();le(out,0x02014b50,4);le(out,20,2);le(out,20,2);le(out,0,2);le(out,0,2);le(out,0,4);le(out,document::zip_crc(text),4);le(out,text.size(),4);le(out,text.size(),4);le(out,name.size(),2);le(out,0,2);le(out,0,2);le(out,0,2);le(out,0,2);le(out,0,4);le(out,0,4);out+=name;
    const auto length=out.size()-index;le(out,0x06054b50,4);le(out,0,4);le(out,1,2);le(out,1,2);le(out,length,4);le(out,index,4);le(out,0,2);return out;
}
// Narrow argv and __FILE__ are not in a reliable encoding on Windows: accented folders (e.g. "hist géo") would fail.
static workspace::fs::path argument(int index){
#ifdef _WIN32
    int count=0;LPWSTR* wide=CommandLineToArgvW(GetCommandLineW(),&count);
    workspace::fs::path result=wide&&index<count?workspace::fs::path(wide[index]):workspace::fs::path();
    if(wide)LocalFree(wide);
    return result;
#else
    (void)index;return {};
#endif
}
int main(int argc,char** argv){
#ifndef _WIN32
    auto argument=[&](int index){return workspace::fs::path(argv[index]);};
#else
    (void)argv;
#endif
    const auto folder=workspace::fs::temp_directory_path()/workspace::text_path("mindmap-tests-"+workspace::stamp());workspace::fs::create_directories(folder);
    try{
        workspace::Store store(folder);store.load();CHECK(store.recent.empty());
        auto first=document::State{};first.map->nodes.at(1)->title="Cours de français";
        auto data=document::encode(first);auto file=folder/"francais.mindmap";
        store.save_named(file,data,nullptr,true);store.opened(file);store.opened(file);CHECK(store.recent.size()==1);CHECK(store.versions(file).empty());
        auto second=document::decode(data);second.map->add(1,"Exemple");auto next=document::encode(second);
        store.save_named(file,next,&data,false);CHECK(workspace::read(file)==next);CHECK(store.versions(file).size()==1);CHECK(workspace::read(store.versions(file)[0])==data);
        store.save_named(file,next,&next,false);CHECK(store.versions(file).size()==1);
        auto third=document::decode(next);third.map->add(1,"Détail");auto newer=document::encode(third);
        store.save_named(file,newer,&next,false);CHECK(store.versions(file).size()==1);
        store.save_named(file,data,&newer,true);CHECK(store.versions(file).size()==2);CHECK(workspace::read(store.versions(file)[0])==newer);
        // External edits are never overwritten by autosave, including deleted files.
        workspace::write(file,next);rejects([&]{store.save_named(file,newer,&data,false);});CHECK(workspace::read(file)==next);
        workspace::fs::remove(file);rejects([&]{store.save_named(file,data,&next,false);});store.save_named(file,data,nullptr,true);
        auto draft=store.new_draft();store.draft(draft,data);store.draft(draft,next);CHECK(store.cards("drafts").size()==1);CHECK(workspace::read(draft)==next);
        rejects([&]{store.draft(folder/"other.mindmap",data);});
        store.forget_draft(draft);CHECK(store.cards("drafts").empty());
        // Manual saves retain a bounded history; the current file is independent of that history.
        std::string expected=data;
        for(int i=0;i<14;++i){auto s=document::decode(expected);s.map->nodes.at(1)->title="Version "+std::to_string(i);auto text=document::encode(s);store.save_named(file,text,&expected,true);expected=text;}
        CHECK(store.versions(file).size()==10);CHECK(workspace::read(file)==expected);
        store.legacy_migrated=true;store.save_index();workspace::Store again(folder);again.load();CHECK(again.legacy_migrated&&again.recent.size()==1&&again.recent[0]==file);
        CHECK(!workspace::fs::exists(folder/"session.mindmap"));
        for(const auto& model:workspace::models()){auto s=document::from_outline(document::parse_outline(model.outline),"");CHECK(s.map->nodes.size()>0);document::decode(document::encode(s));}
        store.custom_model("Mon modèle",second);CHECK(store.cards("templates").size()==1);CHECK(store.cards("templates")[0].title=="Mon modèle");CHECK(second.map->nodes.at(1)->title=="Cours de français");
        // Moving the entire portable folder keeps relative recent-card paths valid.
        auto moved=folder/"copy";workspace::fs::create_directories(moved/".mindmap-data");workspace::fs::copy_file(file,moved/file.filename());workspace::fs::copy_file(store.data()/"workspace.json",moved/".mindmap-data"/"workspace.json");
        workspace::Store portable(moved);portable.load();CHECK(portable.recent[0]==moved/file.filename());

        // Sub-maps: history -> theme -> chapter, each an ordinary file referenced by a relative path.
        {
            for(const char* good:{"a.mindmap","dossier/a.mindmap","Thème 1/Chapitre é.mindmap"})CHECK(document::valid_submap_path(good));
            for(const char* bad:{"","a.txt","../a.mindmap","a/../b.mindmap","/a.mindmap","C:/a.mindmap","a\\b.mindmap","a//b.mindmap","./a.mindmap",".mindmap","a/.mindmap/"})CHECK(!document::valid_submap_path(bad));
            const auto root=folder/"histoire";workspace::fs::create_directories(root);
            const auto main_file=root/"Histoire.mindmap";
            document::State history_map;history_map.map->nodes.at(1)->title="Histoire";
            auto theme_node=history_map.map->add(1,"Thème 1 : l'Europe");history_map.map->add(1,"Thème 2");
            CHECK(document::encode(history_map).find("\"version\":3")!=std::string::npos);
            rejects([&]{submap::create(history_map,{},theme_node);});
            rejects([&]{submap::create(history_map,main_file,1);});
            rejects([&]{submap::create(history_map,main_file,999);});
            auto theme_file=submap::create(history_map,main_file,theme_node,"# Autre titre\n- Chapitre 1\n  - 1789\n- Chapitre 2\n");
            CHECK(theme_file.parent_path()==root/"Histoire"&&workspace::fs::exists(theme_file));
            CHECK(history_map.submaps.at(theme_node)==submap::relative_path(main_file,theme_file));
            rejects([&]{submap::create(history_map,main_file,theme_node);});
            submap::store(main_file,history_map);CHECK(document::encode(history_map).find("\"version\":4")!=std::string::npos);
            auto reread=submap::load(main_file);CHECK(reread.submaps==history_map.submaps);
            auto theme=submap::load(theme_file);CHECK(theme.map->nodes.at(1)->title=="Thème 1 : l'Europe"&&theme.map->nodes.size()==4);
            auto chapter_node=theme.map->nodes.at(1)->children[0];
            auto chapter_file=submap::create(theme,theme_file,chapter_node);submap::store(theme_file,theme);
            CHECK(chapter_file.parent_path()==root/"Histoire"/"Thème 1 _ l'Europe");
            auto list=submap::hierarchy(main_file);CHECK(list.size()==3&&list[0].depth==0&&list[1].depth==1&&list[2].depth==2&&list[2].title=="Chapitre 1");
            for(const auto& entry:list)CHECK(entry.status==submap::Status::Ok);
            auto deep=submap::deep_markdown(main_file);CHECK(deep.find("- Thème 1 : l'Europe\n  - Chapitre 1\n    - 1789\n  - Chapitre 2\n")!=std::string::npos);
            // Loops, escapes and missing files are refused or reported, never followed forever.
            rejects([&]{submap::attach(theme,theme_file,theme.map->nodes.at(1)->children[1],main_file);});
            auto sibling=root/"Histoire"/"Loop.mindmap";document::State loop_map;loop_map.map->nodes.at(1)->title="Boucle";auto holder=loop_map.map->add(1,"Retour");
            submap::store(sibling,loop_map);
            auto theme2=submap::load(theme_file);submap::attach(theme2,theme_file,theme2.map->nodes.at(1)->children[1],sibling);submap::store(theme_file,theme2);
            auto looping=submap::load(sibling);rejects([&]{submap::attach(looping,sibling,holder,theme_file);});
            rejects([&]{submap::attach(history_map,main_file,history_map.map->nodes.at(1)->children[1],folder/"francais.mindmap");});
            workspace::fs::remove(chapter_file);list=submap::hierarchy(main_file);bool missing=false;for(const auto& entry:list)missing|=entry.status==submap::Status::Missing;CHECK(missing);
            CHECK(submap::deep_markdown(main_file).find("introuvable")!=std::string::npos);
            // A hand-edited file cannot smuggle a path outside the folder tree.
            auto hostile=document::encode(history_map);hostile.replace(hostile.find("Histoire/"),9,"../x/Histo");rejects([&]{document::decode(hostile);});
            CHECK(submap::slug("CON")=="_CON"&&submap::slug("a/b:c?")=="a_b_c_"&&submap::slug("  ")=="Sous-carte"&&submap::slug("fin...")=="fin");
            document::State pruned=history_map;pruned.map->remove(theme_node);document::prune(pruned);CHECK(pruned.submaps.empty());
        }
        for(const std::string& raw:std::vector<std::string>{"", "a", "ab", "abc", "abcd",std::string("\0\xff\x80",3)})CHECK(document::unbase64(document::base64(raw))==raw);
        for(const std::string bad:{"a","!!!!","====","a===","YQ=A","YQ==YQ==","YR==","YWJ="})rejects([&]{document::unbase64(bad);});
        const auto png=workspace::read(workspace::text_path(__FILE__).parent_path()/"fixtures"/"pixel.png");
        auto picture=document::picture_from_png(png);CHECK(picture.width==1&&picture.height==1);
        second.pictures[2]=picture;auto restored=document::decode(document::encode(second));CHECK(restored.pictures==second.pictures);CHECK(document::metrics(second,2).picture_h>0);CHECK(document::svg(second).find("data:image/png;base64,")!=std::string::npos);
        auto copy=document::duplicate_branch(second,2);CHECK(second.pictures.at(copy)==picture);second.map->remove(copy);document::prune(second);CHECK(!second.pictures.count(copy));
        for(int mode:{document::LayoutRight,document::LayoutBalanced,document::LayoutRadial}){
            auto illustrated=document::decode(document::encode(second));illustrated.layout=mode;illustrated.map->add(1,"Une autre branche");document::layout(illustrated);
            auto ids=illustrated.map->visible();for(size_t i=0;i<ids.size();++i)for(size_t j=i+1;j<ids.size();++j)CHECK(!document::box(illustrated,ids[i]).overlaps(document::box(illustrated,ids[j])));
        }
        {
            document::State order;auto a=order.map->add(1,"A");auto b=order.map->add(1,"B");order.map->reparent(a,1);
            auto back=document::decode(document::encode(order));CHECK(back.map->nodes.at(1)->children[0]==b&&back.map->nodes.at(1)->children[1]==a);
        }
        std::string nested=R"({"id":"leaf","title":"Dernière idée"})";
        for(int i=0;i<63;++i)nested=R"({"title":"Idée","children":{"attached":[)"+nested+"]}}";
        auto deep=document::import_xmind_json("[{\"rootTopic\":"+nested+"}]");CHECK(deep.map->nodes.size()==64);
        auto corrupt=png;corrupt[corrupt.size()/2]^=1;rejects([&]{document::picture_from_png(corrupt);});rejects([&]{document::picture_from_png(png.substr(0,33));});
        auto mm=document::import_freemind("<?xml version=\"1.0\"?><map><node TEXT=\"Sujet &amp; idées\" ID=\"root\"><node TEXT=\"Événement &#x1f5fa;\" ID=\"a\" FOLDED=\"true\"><richcontent TYPE=\"NOTE\"><html><p>Une note</p><p>Deux lignes</p></html></richcontent><icon BUILTIN=\"help\"/><arrowlink DESTINATION=\"b\"/></node><node TEXT=\"Autre\" ID=\"b\"/></node></map>");
        CHECK(mm.map->nodes.size()==3&&mm.map->nodes.at(1)->title=="Sujet & idées"&&mm.map->nodes.at(2)->title=="Événement 🗺");CHECK(mm.map->nodes.at(2)->note.find("Deux lignes")!=std::string::npos);CHECK(mm.markers.at(2)==3&&mm.links.size()==1&&mm.map->nodes.at(2)->collapsed);
        rejects([&]{document::import_freemind("<!DOCTYPE map [<!ENTITY test SYSTEM 'file:///etc/passwd'>]><map><node TEXT='&test;'/></map>");});rejects([&]{document::import_freemind("<map><node TEXT='A'></map>");});
        const std::string json=R"([{"rootTopic":{"id":"root","title":"Chapitre","children":{"attached":[{"id":"a","title":"Cause","notes":{"plain":{"content":"Une note"}}},{"id":"b","title":"Conséquence"}]}},"relationships":[{"end1Id":"a","end2Id":"b","title":"cause de"}]}])";
        auto xmind=document::import_map(zip("content.json",json),".xmind","");CHECK(xmind.map->nodes.size()==3&&xmind.links.size()==1&&xmind.map->nodes.at(2)->note=="Une note");
        auto xml=document::import_map(zip("content.xml","<xmap-content><sheet><topic><title>Sujet</title><children><topics type='attached'><topic><title>Idée</title></topic></topics></children></topic></sheet></xmap-content>"),".xmind","");CHECK(xml.map->nodes.size()==2);
        auto damaged=zip("content.json",json);damaged[35]^=1;rejects([&]{document::import_map(damaged,".xmind","");});rejects([&]{document::zip_member("PK","content.json");});
        CHECK(document::zip_member(zip("other.txt","abc"),"content.json").empty());
        auto jpeg=workspace::read(workspace::text_path(__FILE__).parent_path()/"fixtures"/"pixel.jpg");
        auto pdf=document::pdf_image(jpeg,1,1);CHECK(pdf.starts_with("%PDF-1.4")&&pdf.find("/MediaBox [0 0 842 595]")!=std::string::npos&&pdf.ends_with("%%EOF\n"));
        if(argc>2)workspace::write(argument(2),pdf);
        auto offset=std::stoull(pdf.substr(pdf.find("startxref\n")+10));CHECK(pdf.compare(offset,4,"xref")==0);
        rejects([&]{document::pdf_image("not jpeg",100,100);});
        if(argc>1){auto compressed=document::import_map(workspace::read(argument(1)),".xmind","");CHECK(compressed.map->nodes.size()==3&&compressed.links.size()==1);}
    }catch(const std::exception& e){std::printf("Exception: %s\n",e.what());++failures;}catch(const PyThrow& e){std::printf("Exception: %s\n",e.p->py_exc_str().c_str());++failures;}
    std::error_code error;workspace::fs::remove_all(folder,error);
    std::printf("workspace, autosave, conflicts, version retention, templates, media, imports and PDF: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
