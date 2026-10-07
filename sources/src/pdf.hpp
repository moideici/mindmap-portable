#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>

namespace document {
// One A4 landscape page. The JPEG contains the same rendering as PNG, including embedded images.
inline std::string pdf_image(const std::string& jpeg,unsigned width,unsigned height){
    if(jpeg.size()<4||static_cast<unsigned char>(jpeg[0])!=255||static_cast<unsigned char>(jpeg[1])!=216||!width||!height)
        throw std::runtime_error("Image JPEG invalide pour le PDF");
    const double scale=std::min(794.0/width,547.0/height),w=width*scale,h=height*scale;
    std::ostringstream commands;commands.imbue(std::locale::classic());commands<<"q\n"<<w<<" 0 0 "<<h<<' '<<(842-w)/2<<' '<<(595-h)/2<<" cm\n/Map Do\nQ\n";
    auto stream=commands.str();
    std::vector<std::string> objects={
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] /Resources << /XObject << /Map 4 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /XObject /Subtype /Image /Width "+std::to_string(width)+" /Height "+std::to_string(height)+" /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length "+std::to_string(jpeg.size())+" >>\nstream\n"+jpeg+"\nendstream",
        "<< /Length "+std::to_string(stream.size())+" >>\nstream\n"+stream+"endstream"
    };
    std::string out="%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";std::vector<size_t> offsets{0};
    for(size_t i=0;i<objects.size();++i){offsets.push_back(out.size());out+=std::to_string(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}
    const size_t xref=out.size();out+="xref\n0 6\n0000000000 65535 f \n";
    for(size_t i=1;i<offsets.size();++i){std::ostringstream line;line<<std::setw(10)<<std::setfill('0')<<offsets[i]<<" 00000 n \n";out+=line.str();}
    return out+"trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
}
} // namespace document
