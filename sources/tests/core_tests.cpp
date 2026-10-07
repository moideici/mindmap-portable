#include "../src/exports.hpp"
#ifdef _WIN32
#include "../src/history.hpp"
#endif
#include <cstdio>

static int failures=0;
#define CHECK(c) do { if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;} } while(0)
template<class F> void rejects(F&& fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}catch(const PyThrow&){rejected=true;}CHECK(rejected);}

int main(){
    document::State s;
    auto a=s.map->add(1,"Dates"),b=s.map->add(1,"Personnages"),c=s.map->add(a,"1789");
    s.map->rename(c,"Révolution 🗺", "Une note\nAvec \"guillemets\" et \\barre.");
    s.map->layout();
    CHECK(s.map->depth(c)==2);
    s.map->toggle(a);CHECK(s.map->visible().size()==3);
    s.map->reveal(c);CHECK(s.map->visible().size()==4);
    auto duplicate=s.map->duplicate(a);
    CHECK(s.map->descendants(duplicate).size()==2);
    s.map->reparent(c,b);CHECK(s.map->nodes.at(c)->parent==b);
    rejects([&]{s.map->reparent(b,c);});
    CHECK(s.map->nodes.at(b)->parent==1);
    s.map->set_color(b,0x123456,true);CHECK(s.map->nodes.at(c)->color==0x123456);
    rejects([&]{s.map->remove(1);});
    rejects([&]{s.map->add(1," ");});
    s.map->remove(duplicate);
    s.selected=c;s.zoom=.8;s.pan_x=154.5;s.pan_y=-333;
    auto encoded=document::encode(s);
    auto decoded=document::decode(encoded);
    CHECK(document::encode(decoded)==encoded);
    CHECK(decoded.map->nodes.at(c)->title=="Révolution 🗺");
    CHECK(document::svg(s).find("Révolution")!=std::string::npos);
    CHECK(document::markdown(s).find("- Révolution")!=std::string::npos);
    CHECK(document::xml("<>&\"'")=="&lt;&gt;&amp;&quot;&apos;");

    for(const std::string bad:{"", "{}", "{\"a\":1,\"a\":2}", "[01]", "[1,]", "[1e9999]", "[NaN]", "\"\\ud800\"", "\"\\udc00\"", "[true]extra", "{\"a\":1,}"})
        rejects([&]{document::Parser(bad).parse();if(bad=="{}")document::decode(bad);});
    CHECK(document::Parser("\"\\ud83d\\uddfa\"").parse().text()=="🗺");
    CHECK(!document::valid_utf8(std::string("\xc0\xaf",2)));
    auto corrupt=encoded;
    auto start=corrupt.find("\"parent\":-1");corrupt.replace(start,11,"\"parent\":9");rejects([&]{document::decode(corrupt);});
    corrupt=encoded;start=corrupt.find("\"color\":");auto end=corrupt.find(',',start);corrupt.replace(start,end-start,"\"color\":-1");rejects([&]{document::decode(corrupt);});
    rejects([&]{document::decode(std::string(document::max_file_bytes+1,'x'));});
    {
        document::State chain;
        auto parent=1LL;
        for(int i=0;i<63;++i)parent=chain.map->add(parent,"deep");
        rejects([&]{chain.map->add(parent,"too deep");});
        auto roundtrip=document::decode(document::encode(chain));
        CHECK(roundtrip.map->depth(parent)==63);
    }
    {
        std::weak_ptr<MapNode> weak;
        {
            auto map=std::make_shared<MindMap>("memory");auto id=map->add(1,"temporary");
            auto retained=map->nodes.at(id);weak=retained;
            map->remove(id);CHECK(!weak.expired());retained.reset();CHECK(weak.expired());
        }
        CHECK(weak.expired());
    }
    for(int cycle=0;cycle<100;++cycle){
        std::weak_ptr<MindMap> map_weak;std::weak_ptr<MapNode> node_weak;
        {
            document::State current;map_weak=current.map;
            for(int i=0;i<100;++i)current.map->add(1,"item");
            node_weak=current.map->nodes.at(50);
            auto other=document::decode(document::encode(current));
            CHECK(other.map->nodes.size()==101);
        }
        CHECK(map_weak.expired());CHECK(node_weak.expired());
    }
    // --- Format v2 : liens, repères, préférences d'affichage ---
    {
        document::State v1=document::decode("{\"format\":\"mindmap-portable\",\"version\":1,\"next_id\":3,\"selected\":2,\"view\":{\"zoom\":1,\"x\":0,\"y\":0},"
            "\"nodes\":[{\"id\":1,\"parent\":-1,\"title\":\"Sujet\",\"note\":\"\",\"x\":0,\"y\":0,\"color\":6452456,\"collapsed\":false},"
            "{\"id\":2,\"parent\":1,\"title\":\"A\",\"note\":\"\",\"x\":260,\"y\":0,\"color\":6452456,\"collapsed\":false}]}");
        CHECK(v1.layout==document::LayoutRight && v1.links.empty() && v1.markers.empty());
        CHECK(document::encode(v1).find("\"version\":3")!=std::string::npos);
    }
    {
        document::State m;m.map=std::make_shared<MindMap>("Guerre froide");
        auto a=m.map->add(1,"Causes"),b=m.map->add(1,"Conséquences"),c=m.map->add(a,"Rivalité idéologique"),d=m.map->add(b,"Course aux armements");
        CHECK(document::add_link(m,c,d,"cause de"));
        CHECK(!document::add_link(m,d,c));
        rejects([&]{document::add_link(m,c,c);});
        m.markers[c]=1;m.markers[d]=3;m.show_notes=true;m.dark=true;m.layout=document::LayoutRadial;
        auto text=document::encode(m);auto back=document::decode(text);
        CHECK(back.links==m.links && back.markers==m.markers && back.layout==document::LayoutRadial && back.dark && back.show_notes);
        CHECK(document::encode(back)==text);
        auto bad=text;auto target="\"to\":"+std::to_string(d);bad.replace(bad.find(target),target.size(),"\"to\":999");rejects([&]{document::decode(bad);});
        auto copy=document::duplicate_branch(m,a);
        CHECK(document::marker_of(m,m.map->nodes.at(copy)->children[0])==1);
        m.map->remove(b);document::prune(m);
        CHECK(m.links.empty() && !m.markers.count(d));
        auto e=m.map->add(copy,"Plan Marshall");document::add_link(m,c,e,"");
        auto copy2=document::duplicate_branch(m,copy);
        CHECK(m.links.size()==1);  // the link leaves the branch: it is not copied
        document::add_link(m,m.map->nodes.at(copy2)->children[0],m.map->nodes.at(copy2)->children[1]);
        document::duplicate_branch(m,copy2);CHECK(m.links.size()==3);
    }
    // --- Dispositions : aucune idée ne se chevauche, branches des deux côtés ---
    for(int mode:{document::LayoutRight,document::LayoutBalanced,document::LayoutRadial})for(bool notes:{false,true}){
        document::State m;m.layout=mode;m.show_notes=notes;
        for(int i=0;i<7;++i){auto k=m.map->add(1,"Branche principale numéro "+std::to_string(i));
            for(int j=0;j<i%4+1;++j){auto g=m.map->add(k,"Sous-idée "+std::to_string(j));m.map->rename(g,"Sous-idée "+std::to_string(j),"Une note assez longue pour occuper plusieurs lignes dans l'aperçu.");
                if(j==1)m.map->add(g,"Détail");}}
        document::layout(m);
        auto ids=m.map->visible();bool overlap=false;int left=0,right=0;
        for(size_t i=0;i<ids.size();++i){auto bi=document::box(m,ids[i]);left+=bi.x<-1;right+=bi.x>1;
            for(size_t j=i+1;j<ids.size();++j)if(bi.overlaps(document::box(m,ids[j]))){overlap=true;std::printf("overlap mode %d: %lld %lld\n",mode,ids[i],ids[j]);}}
        CHECK(!overlap);
        CHECK(mode==document::LayoutRight?left==0:(left>3&&right>3));
    }
    {
        document::Box a{0,0,100,50},b{300,200,100,50};
        auto c=document::link_curve(a,b);
        CHECK(c.distance(c.at(.5))<1);CHECK(c.distance({1000,1000})>500);
        CHECK(!a.contains(c.a.x,c.a.y)&&!b.contains(c.d.x,c.d.y));
        auto right=document::connector(a,b);CHECK(right.a.x==a.right()&&right.d.x==b.left());
        auto left=document::connector(b,a);CHECK(left.a.x==b.left()&&left.d.x==a.right());
        auto lines=document::wrap("Une idée assez longue pour être coupée proprement",16,2);
        CHECK(lines.size()==2 && lines[1].find("…")!=std::string::npos);
    }
    // --- Plans : Markdown exporté puis réimporté, texte indenté ---
    {
        document::State m;m.map=std::make_shared<MindMap>("Révolution française");m.map->rename(1,"Révolution française","Note du sujet");
        auto a=m.map->add(1,"1789");m.map->rename(a,"1789","Prise de la Bastille\nÉtats généraux");m.markers[a]=5;
        auto b=m.map->add(a,"Abolition des privilèges");m.markers[b]=3;m.map->add(1,"Terreur");document::add_link(m,b,a,"conséquence");
        auto md=document::markdown(m);
        CHECK(md.find("## Liens transversaux")!=std::string::npos);
        auto back=document::from_outline(document::parse_outline(md),"x");
        CHECK(back.map->nodes.size()==4 && back.map->nodes.at(1)->title=="Révolution française" && back.map->nodes.at(1)->note=="Note du sujet");
        bool found=false;for(const auto& kv:back.map->nodes){auto n=kv.second;if(n->title=="1789")found=n->note=="Prise de la Bastille\nÉtats généraux"&&document::marker_of(back,n->id)==5;}
        CHECK(found);
        auto plain=document::parse_outline("Chapitre\n\tPartie A\n\t\tExemple\n\tPartie B\nAutre\n");
        CHECK(plain.size()==5 && plain[2].level==2 && plain[3].level==1 && plain[4].level==0);
        auto list=document::parse_outline("1. Un\n   - [x] deux\n      * trois\n");
        CHECK(list.size()==3 && list[1].marker==2 && list[2].level==2 && list[1].title=="deux");
        auto created=document::apply_outline(m,a,plain);
        CHECK(created.size()==5 && m.map->nodes.at(created[4])->parent==a);
        rejects([&]{document::parse_outline(std::string("\xff",1));});
        rejects([&]{document::apply_outline(m,1,{});});
        CHECK(document::branch_markdown(m,b).rfind("- [?] Abolition",0)==0);
        CHECK(document::svg(m).find("marker-end")!=std::string::npos);
    }
    // --- Historique compressé ---
#ifdef _WIN32
    {
        History history;document::State big;
        for(int i=0;i<300;++i)big.map->add(1+(i/10),"Idée de révision numéro "+std::to_string(i));
        std::string first=document::encode(big);
        for(int i=0;i<60;++i){history.remember(document::encode(big));big.map->rename(2,"Titre "+std::to_string(i),"");}
        CHECK(history.steps()==60);
        if(history.compressed())CHECK(history.bytes()*4<history.raw_bytes());
        std::printf("history: %zu steps, %zu bytes stored for %zu raw bytes\n",history.steps(),history.bytes(),history.raw_bytes());
        std::string current=document::encode(big);
        for(int i=0;i<60;++i)current=history.undo(current);
        CHECK(current==first && !history.can_undo() && history.can_redo());
        current=history.redo(current);CHECK(history.steps()==1);
        history.remember(first);CHECK(history.steps()==1);  // identical snapshots are not stored twice
        history.max_steps=10;for(int i=0;i<30;++i)history.remember(std::to_string(i));CHECK(history.steps()==10&&!history.can_redo());
    }
#else
    std::printf("Windows compression history: skipped (non-Windows host)\n");
#endif
    std::printf("core, documents, links, layouts, outlines, malformed inputs and 100 memory cycles: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
