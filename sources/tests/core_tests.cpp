#include "../src/documents.hpp"
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
    std::printf("core, documents, malformed inputs and 100 memory cycles: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
