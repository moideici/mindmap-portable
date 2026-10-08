#pragma once
#include "exports.hpp"
#include "../vendor/miniz/miniz_tinfl.h"
#include <functional>

namespace document {
struct XmlNode {
    std::string name,text;
    std::map<std::string,std::string> attributes;
    std::vector<XmlNode> children;
    std::string attr(const std::string& key)const{auto it=attributes.find(key);return it==attributes.end()?"":it->second;}
    const XmlNode* child(const std::string& key)const{for(const auto& c:children)if(c.name==key)return &c;return nullptr;}
};
class XmlReader {
    const std::string& input;size_t pos=0,count=0;
    [[noreturn]] void fail()const{throw std::runtime_error("Document XML invalide ou non pris en charge");}
    bool starts(std::string_view s)const{return input.compare(pos,s.size(),s)==0;}
    void space(){while(pos<input.size()&&std::isspace(static_cast<unsigned char>(input[pos])))++pos;}
    std::string name(){size_t from=pos;while(pos<input.size()&&(std::isalnum(static_cast<unsigned char>(input[pos]))||input[pos]==':'||input[pos]=='_'||input[pos]=='-'||input[pos]=='.'))++pos;if(pos==from)fail();return input.substr(from,pos-from);}
    std::string entities(std::string_view text){
        std::string out;
        for(size_t i=0;i<text.size();++i){
            if(text[i]!='&'){out+=text[i];continue;}
            auto end=text.find(';',i+1);if(end==std::string::npos||end-i>16)fail();auto code=text.substr(i+1,end-i-1);
            if(code=="amp")out+='&';else if(code=="lt")out+='<';else if(code=="gt")out+='>';else if(code=="quot")out+='"';else if(code=="apos")out+='\'';else if(code=="nbsp")out+=' ';
            else if(!code.empty()&&code[0]=='#'){
                bool hex=code.size()>1&&(code[1]=='x'||code[1]=='X');auto digits=code.substr(hex?2:1);unsigned value=0;
                auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),value,hex?16:10);
                if(parsed.ec!=std::errc{}||parsed.ptr!=digits.data()+digits.size()||!value||value>0x10FFFF||(value>=0xD800&&value<=0xDFFF))fail();
                utf8_append(out,value);
            }else fail();i=end;
        }return out;
    }
    void skip(const char* ending){auto at=input.find(ending,pos);if(at==std::string::npos)fail();pos=at+std::char_traits<char>::length(ending);}
    XmlNode node(size_t depth){
        if(depth>256||++count>30000||pos>=input.size()||input[pos++]!='<')fail();
        XmlNode out;out.name=name();
        for(;;){
            space();if(starts("/>")){pos+=2;return out;}if(starts(">")){++pos;break;}
            auto key=name();space();if(pos>=input.size()||input[pos++]!='=')fail();space();
            if(pos>=input.size()||(input[pos]!='\''&&input[pos]!='"'))fail();
            char quote=input[pos++];auto end=input.find(quote,pos);if(end==std::string::npos)fail();
            auto value=entities(std::string_view(input).substr(pos,end-pos));if(!out.attributes.emplace(key,value).second)fail();pos=end+1;
        }
        for(;;){
            if(pos>=input.size())fail();
            if(starts("</")){pos+=2;auto closing=name();space();if(closing!=out.name||pos>=input.size()||input[pos++]!='>')fail();return out;}
            if(starts("<!--")){skip("-->");continue;}
            if(starts("<![CDATA[")){pos+=9;auto end=input.find("]]>",pos);if(end==std::string::npos)fail();out.text+=input.substr(pos,end-pos);pos=end+3;continue;}
            if(starts("<")){if(starts("<!")||starts("<?"))fail();auto c=node(depth+1);out.text+=c.text;if(c.name=="p"||c.name=="div"||c.name=="br")out.text+='\n';out.children.push_back(std::move(c));}
            else{auto end=input.find('<',pos);if(end==std::string::npos)fail();out.text+=entities(std::string_view(input).substr(pos,end-pos));pos=end;}
        }
    }
public:
    explicit XmlReader(const std::string& text):input(text){}
    XmlNode parse(){
        if(input.size()>max_file_bytes||!valid_utf8(input))fail();
        if(input.compare(0,3,"\xef\xbb\xbf")==0)pos=3;
        space();if(starts("<?xml"))skip("?>");space();while(starts("<!--")){skip("-->");space();}
        // DTDs and external entities are not needed for these formats.
        if(starts("<!"))fail();
        auto result=node(0);space();if(pos!=input.size())fail();return result;
    }
};
inline State import_freemind(const std::string& text){
    auto xml=XmlReader(text).parse();if(xml.name!="map")throw std::runtime_error("Ce fichier n'est pas une carte FreeMind");
    const auto* root=xml.child("node");if(!root)throw std::runtime_error("Sujet central absent");
    State result;std::map<std::string,long long> ids;std::vector<std::pair<long long,std::string>> arrows;
    std::function<void(const XmlNode&,long long,size_t)> add=[&](const XmlNode& n,long long parent,size_t depth){
        if(depth>63)throw std::runtime_error("Plus de 63 niveaux dans la carte importée");
        auto title=n.attr("TEXT");std::string note;
        for(const auto& c:n.children)if(c.name=="richcontent"){if(c.attr("TYPE")=="NODE"&&title.empty())title=trim(c.text);else if(c.attr("TYPE")=="NOTE")note=trim(c.text);}
        title=clip_utf8(trim(title),1024);if(title.empty())title="Idée";
        auto id=parent?result.map->add(parent,title):1;result.map->rename(id,title,clip_utf8(note,16000));
        if(auto color=n.attr("COLOR");color.size()==7&&color[0]=='#'){unsigned v=0;auto p=std::from_chars(color.data()+1,color.data()+7,v,16);if(p.ec==std::errc{}&&p.ptr==color.data()+7)result.map->nodes.at(id)->color=v;}
        result.map->nodes.at(id)->collapsed=n.attr("FOLDED")=="true";
        if(auto key=n.attr("ID");!key.empty()){if(!ids.emplace(key,id).second)throw std::runtime_error("Identifiants FreeMind dupliqués");}
        for(const auto& c:n.children){
            if(c.name=="icon"){auto icon=c.attr("BUILTIN");if(icon=="button_ok")result.markers[id]=2;else if(icon=="help")result.markers[id]=3;else if(icon=="yes")result.markers[id]=1;else if(icon=="messagebox_warning")result.markers[id]=4;}
            else if(c.name=="arrowlink")arrows.emplace_back(id,c.attr("DESTINATION"));
            else if(c.name=="node")add(c,id,depth+1);
        }
    };add(*root,0,0);
    for(const auto& [from,to]:arrows)if(ids.count(to)&&from!=ids.at(to))add_link(result,from,ids.at(to));
    layout(result);return result;
}
inline State import_xmind_json(const std::string& text){
    auto content=Parser(text,300).parse();if(content.kind!=Json::Array||content.array.empty())throw std::runtime_error("Feuille XMind absente");
    State result;std::map<std::string,long long> ids;
    auto optional=[](const Json& j,const char* k)->const Json*{auto p=j.object.find(k);return j.kind==Json::Object&&p!=j.object.end()?&p->second:nullptr;};
    std::function<void(const Json&,long long,size_t)> add=[&](const Json& topic,long long parent,size_t depth){
        if(depth>63)throw std::runtime_error("Plus de 63 niveaux dans la carte importée");
        auto title=clip_utf8(trim(topic.at("title").text()),1024);if(title.empty())title="Idée";auto id=parent?result.map->add(parent,title):1;
        result.map->rename(id,title,"");if(auto key=optional(topic,"id"))ids[key->text()]=id;
        if(auto notes=optional(topic,"notes"))if(auto plain=optional(*notes,"plain"))if(auto body=optional(*plain,"content"))result.map->nodes.at(id)->note=clip_utf8(body->text(),16000);
        if(auto children=optional(topic,"children"))for(const char* group:{"attached","detached"})if(auto list=optional(*children,group)){
            if(list->kind!=Json::Array)throw std::runtime_error("Branches XMind invalides");
            for(const auto& child:list->array)add(child,id,depth+1);
        }
    };
    // Multiple sheets get distinct branches instead of silently importing only the first one.
    const bool multiple=content.array.size()>1;result.map->nodes.at(1)->title="Classeur XMind";
    for(const auto& sheet:content.array){add(sheet.at("rootTopic"),multiple?1:0,multiple?1:0);
        if(auto relations=optional(sheet,"relationships")){if(relations->kind!=Json::Array)throw std::runtime_error("Liens XMind invalides");
            for(const auto& r:relations->array){auto from=r.at("end1Id").text(),to=r.at("end2Id").text();auto label=optional(r,"title");
                if(ids.count(from)&&ids.count(to)&&ids.at(from)!=ids.at(to))add_link(result,ids.at(from),ids.at(to),label?clip_utf8(label->text(),max_link_label):"");}
        }
    }layout(result);return result;
}
inline State import_xmind_xml(const std::string& text){
    auto xml=XmlReader(text).parse();if(xml.name!="xmap-content")throw std::runtime_error("Contenu XMind XML invalide");
    std::vector<OutlineItem> items;
    std::function<void(const XmlNode&,int)> walk=[&](const XmlNode& topic,int depth){
        const auto* title=topic.child("title");auto label=title?clip_utf8(trim(title->text),1024):"Idée";if(label.empty())label="Idée";
        if(items.size()>=2000||depth>63)throw std::runtime_error("Carte XMind trop grande");
        std::string note;
        if(auto notes=topic.child("notes"))note=clip_utf8(trim(notes->text),16000);
        items.push_back({depth,label,note,0,{}});
        if(auto children=topic.child("children"))for(const auto& group:children->children)if(group.name=="topics")for(const auto& child:group.children)if(child.name=="topic")walk(child,depth+1);
    };
    for(const auto& sheet:xml.children)if(sheet.name=="sheet")if(auto root=sheet.child("topic"))walk(*root,0);
    return from_outline(std::move(items),"Classeur XMind");
}
inline uint32_t zip_crc(std::string_view text){uint32_t crc=~0u;for(unsigned char c:text){crc^=c;for(int k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&static_cast<uint32_t>(-static_cast<int>(crc&1)));}return ~crc;}
// Read one bounded member in memory. No archive paths are ever written to the filesystem.
inline std::string zip_member(const std::string& bytes,const std::string& wanted){
    auto number=[&](size_t at,size_t n)->uint32_t{if(at>bytes.size()||n>bytes.size()-at)throw std::runtime_error("Archive XMind tronquée");uint32_t v=0;for(size_t i=0;i<n;++i)v|=static_cast<uint32_t>(static_cast<unsigned char>(bytes[at+i]))<<(i*8);return v;};
    size_t end=std::string::npos;if(bytes.size()>=22)for(size_t i=bytes.size()-22;;--i){if(number(i,4)==0x06054b50&&i+22+number(i+20,2)==bytes.size()){end=i;break;}if(i==0||bytes.size()-i>65557)break;}
    if(end==std::string::npos||number(end+4,2)||number(end+6,2)||number(end+8,2)!=number(end+10,2))throw std::runtime_error("Archive XMind invalide");
    size_t at=number(end+16,4);auto count=number(end+10,2);if(count>10000||at>end)throw std::runtime_error("Archive XMind trop grande");
    for(size_t i=0;i<count;++i){
        if(number(at,4)!=0x02014b50)throw std::runtime_error("Index XMind invalide");
        auto flags=number(at+8,2),method=number(at+10,2),crc=number(at+16,4),packed=number(at+20,4),raw=number(at+24,4);
        auto name_len=number(at+28,2),extra=number(at+30,2),comment=number(at+32,2),offset=number(at+42,4);size_t next=at+46+name_len+extra+comment;
        if(next>end)throw std::runtime_error("Index XMind tronqué");
        if(bytes.compare(at+46,name_len,wanted)==0){
            if(flags&0x41||raw>max_file_bytes||!raw||number(offset,4)!=0x04034b50||number(offset+8,2)!=method||number(offset+6,2)&0x41)
                throw std::runtime_error("Contenu XMind invalide, chiffré ou trop grand");
            if(bytes.compare(offset+30,number(offset+26,2),wanted)!=0)throw std::runtime_error("Nom de membre XMind incohérent");
            size_t data=static_cast<size_t>(offset)+30+number(offset+26,2)+number(offset+28,2);if(data>at||packed>at-data)throw std::runtime_error("Contenu XMind tronqué");
            std::string out(raw,'\0');
            if(method==0){if(packed!=raw)throw std::runtime_error("Taille XMind invalide");out=bytes.substr(data,raw);}
            else if(method==8){auto written=tinfl_decompress_mem_to_mem(out.data(),out.size(),bytes.data()+data,packed,0);if(written!=raw)throw std::runtime_error("Décompression XMind impossible");}
            else throw std::runtime_error("Compression XMind non prise en charge");
            if(zip_crc(out)!=crc)throw std::runtime_error("Contenu XMind corrompu");
            return out;
        }at=next;
    }return {};
}
inline State import_map(const std::string& bytes,std::string extension,const std::string& name){
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".mindmap")return decode(bytes);
    if(extension==".mm")return import_freemind(bytes);
    if(extension==".xmind"){
        auto json=zip_member(bytes,"content.json");if(!json.empty())return import_xmind_json(json);
        auto xml=zip_member(bytes,"content.xml");if(!xml.empty())return import_xmind_xml(xml);
        throw std::runtime_error("Cette archive ne contient aucune carte XMind");
    }
    if(extension==".md"||extension==".markdown"||extension==".txt")return from_outline(parse_outline(bytes),name);
    throw std::runtime_error("Format d'import non pris en charge");
}
} // namespace document
