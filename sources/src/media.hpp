#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>

namespace document {
constexpr size_t max_picture_bytes = 1024 * 1024;
inline std::string base64(std::string_view bytes) {
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((bytes.size()+2)/3*4);
    for(size_t i=0;i<bytes.size();i+=3){
        uint32_t v=static_cast<unsigned char>(bytes[i])<<16;
        if(i+1<bytes.size())v|=static_cast<unsigned char>(bytes[i+1])<<8;
        if(i+2<bytes.size())v|=static_cast<unsigned char>(bytes[i+2]);
        out+=alphabet[(v>>18)&63];out+=alphabet[(v>>12)&63];
        out+=i+1<bytes.size()?alphabet[(v>>6)&63]:'=';out+=i+2<bytes.size()?alphabet[v&63]:'=';
    }
    return out;
}
inline std::string unbase64(std::string_view text) {
    if(text.size()%4 || text.size()>(max_picture_bytes+2)/3*4)throw std::runtime_error("Image trop volumineuse ou encodage invalide");
    auto digit=[](char c)->int {
        if(c>='A'&&c<='Z')return c-'A';
        if(c>='a'&&c<='z')return c-'a'+26;
        if(c>='0'&&c<='9')return c-'0'+52;
        if(c=='+')return 62;
        if(c=='/')return 63;
        return -1;
    };
    std::string out;out.reserve(text.size()/4*3);
    for(size_t i=0;i<text.size();i+=4){
        int a=digit(text[i]),b=digit(text[i+1]),c=text[i+2]=='='?0:digit(text[i+2]),d=text[i+3]=='='?0:digit(text[i+3]);
        const bool pad2=text[i+2]=='=',pad1=text[i+3]=='=';
        if(a<0||b<0||c<0||d<0||(pad2&&!pad1)||((pad1||pad2)&&i+4!=text.size())||(pad2&&(b&15))||(pad1&&!pad2&&(c&3)))
            throw std::runtime_error("Encodage d'image invalide");
        uint32_t v=(a<<18)|(b<<12)|(c<<6)|d;
        out+=static_cast<char>(v>>16);if(!pad2)out+=static_cast<char>(v>>8);if(!pad1)out+=static_cast<char>(v);
    }
    if(out.size()>max_picture_bytes)throw std::runtime_error("Image trop volumineuse (1 Mio maximum)");
    return out;
}
struct Picture {
    std::string data;
    uint32_t width=0,height=0;
    bool operator==(const Picture&) const = default;
};
inline uint32_t media_crc(std::string_view text){uint32_t crc=~0u;for(unsigned char c:text){crc^=c;for(int k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&static_cast<uint32_t>(-static_cast<int>(crc&1)));}return ~crc;}
inline Picture picture_from_png(const std::string& png) {
    if(png.size()<33||png.size()>max_picture_bytes||png.compare(0,8,"\x89PNG\r\n\x1a\n",8)||png.compare(12,4,"IHDR"))
        throw std::runtime_error("Image PNG invalide");
    auto number=[&](size_t at){uint32_t v=0;for(size_t i=0;i<4;++i)v=(v<<8)|static_cast<unsigned char>(png[at+i]);return v;};
    auto w=number(16),h=number(20);
    if(!w||!h||w>1600||h>1600||static_cast<uint64_t>(w)*h>1600000)
        throw std::runtime_error("Dimensions d'image hors limites");
    bool pixels=false,ended=false;
    for(size_t at=8;at<png.size();){
        if(png.size()-at<12)throw std::runtime_error("Image PNG tronquée");
        auto size=number(at);
        if(size>png.size()-at-12||media_crc(std::string_view(png).substr(at+4,size+4))!=number(at+8+size))throw std::runtime_error("Image PNG corrompue");
        auto kind=png.substr(at+4,4);
        if(at==8&&(kind!="IHDR"||size!=13))throw std::runtime_error("Entête PNG invalide");
        if(at!=8&&kind=="IHDR")throw std::runtime_error("Entête PNG dupliquée");
        if(kind=="IDAT")pixels=true;
        if(kind=="IEND"){if(size||at+12!=png.size())throw std::runtime_error("Fin de PNG invalide");ended=true;}
        at+=12+size;
    }
    if(!pixels||!ended)throw std::runtime_error("Image PNG incomplète");
    return {base64(png),w,h};
}
} // namespace document
