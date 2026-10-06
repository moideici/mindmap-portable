#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objidl.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <filesystem>
#include <deque>
#include <fstream>
#include "documents.hpp"

using namespace Gdiplus;
namespace fs = std::filesystem;

namespace ui {
constexpr UINT autosave_timer = 1, smoke_timer = 2;
enum Command { New=100, Open, Save, SaveAs, Add, Sibling, Delete, Duplicate, Undo, Redo, Layout, Fit,
    Collapse, ExportPng, ExportSvg, ExportMd, Apply, Help, Color0=150 };
constexpr int search_id=401, title_id=402, note_id=403;
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Texte UTF-8 invalide");
    std::wstring result(static_cast<size_t>(size), L' ');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Texte Unicode invalide");
    std::string result(static_cast<size_t>(size), ' ');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring text_of(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    std::wstring result(static_cast<size_t>(length)+1, L'\0');
    GetWindowTextW(edit, result.data(), length+1);
    result.resize(static_cast<size_t>(length));
    return result;
}
std::wstring lowercase(const std::wstring& s) {
    if (s.empty()) return {};
    auto out = s;
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr, 0);
    return out;
}
Color rgb(long long value, BYTE alpha=255) { return Color(alpha, static_cast<BYTE>(value>>16), static_cast<BYTE>(value>>8), static_cast<BYTE>(value)); }
void round_path(GraphicsPath& p, RectF r, REAL radius) {
    REAL d=radius*2;
    p.AddArc(r.X,r.Y,d,d,180,90); p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
    p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90); p.AddArc(r.X,r.GetBottom()-d,d,d,90,90); p.CloseFigure();
}
void box(Graphics& g, RectF rect, Color fill, Color stroke=Color(0,0,0,0), REAL radius=10, REAL thickness=1) {
    GraphicsPath path; round_path(path,rect,radius);
    SolidBrush brush(fill); g.FillPath(&brush,&path);
    if (stroke.GetA()) { Pen pen(stroke,thickness); g.DrawPath(&pen,&path); }
}
void label(Graphics& g,const std::wstring& text,RectF rect,REAL size=14,Color color=Color(255,37,48,71),bool bold=false, bool center=false) {
    Font font(L"Segoe UI",size,bold?FontStyleBold:FontStyleRegular,UnitPixel);
    SolidBrush brush(color); StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetLineAlignment(StringAlignmentCenter);
    if (center) format.SetAlignment(StringAlignmentCenter);
    g.DrawString(text.c_str(),static_cast<INT>(text.size()),&font,rect,&format,&brush);
}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if(value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
std::string read_file(const fs::path& path) {
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Impossible d'ouvrir le fichier");
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.value,&size) || size.QuadPart<0 || size.QuadPart>static_cast<LONGLONG>(document::max_file_bytes)) throw std::runtime_error("Fichier trop volumineux (8 Mio maximum)");
    std::string result(static_cast<size_t>(size.QuadPart),'\0');
    DWORD read=0;
    if(!ReadFile(file.value,result.data(),static_cast<DWORD>(result.size()),&read,nullptr) || read!=result.size()) throw std::runtime_error("Lecture incomplete");
    return result;
}
void atomic_write(const fs::path& path,const std::string& data) {
    auto temporary=path; temporary+=L".tmp";
    {
        Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(file.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Dossier non accessible en ecriture : verifiez la cle USB");
        DWORD written=0;
        if(!WriteFile(file.value,data.data(),static_cast<DWORD>(data.size()),&written,nullptr) || written!=data.size() || !FlushFileBuffers(file.value))
            throw std::runtime_error("Echec de sauvegarde : espace libre ou support indisponible");
    }
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); throw std::runtime_error("Impossible de finaliser la sauvegarde");
    }
}

struct Button { RectF rect; int command; std::wstring title; bool primary=false; };
class App {
public:
    HWND window=nullptr, search=nullptr, title=nullptr, note=nullptr;
    HFONT edit_font=nullptr;
    HBRUSH edit_brush=nullptr;
    document::State state;
    fs::path directory, current_file;
    std::deque<std::string> undo, redo;
    std::vector<Button> buttons;
    std::vector<std::pair<RectF,long long>> outline_hits;
    std::vector<long long> outline_ids;
    RectF canvas;
    REAL scale=1, width=1200, height=780;
    int outline_scroll=0;
    bool updating=false, pending_edit=false, dirty=false, autosave_pending=false, dragging=false, panning=false, moved=false, smoke=false;
    ULONGLONG retry_save_at=0;
    long long drag_id=0;
    PointF last_mouse;
    std::string drag_before;
    ULONGLONG edit_time=0;
    std::wstring status=L"Prêt · tout reste dans le dossier de l’application";
    static constexpr long long palette[6]={0x6274E8,0x26A69A,0xDB9A36,0xCB6BA0,0x539BCB,0xA483D4};

    explicit App(fs::path folder,bool test) : directory(std::move(folder)),smoke(test) {}
    ~App(){ if(edit_font) DeleteObject(edit_font); if(edit_brush) DeleteObject(edit_brush); }
    void repaint(){ InvalidateRect(window,nullptr,FALSE); }
    void show_error(const std::string& text) {
        status=L"Attention · "+wide(text); repaint();
        if(!smoke) MessageBoxW(window,wide(text).c_str(),L"MindMap",MB_OK|MB_ICONWARNING);
    }
    void remember(std::string before) {
        undo.push_back(std::move(before)); redo.clear();
        size_t bytes=0;
        for(const auto& item:undo) bytes+=item.size();
        while(undo.size()>80 || (bytes>32*1024*1024 && undo.size()>1)) { bytes-=undo.front().size(); undo.pop_front(); }
    }
    void changed(const std::wstring& message=L"Modifications enregistrées dans la session portable") {
        dirty=true; autosave_pending=true; status=message; refresh_outline(); repaint(); update_caption();
    }
    void update_caption(){
        std::wstring caption=L"MindMap — "+wide(state.map->nodes.at(1)->title);
        if(dirty) caption+=L" *";
        SetWindowTextW(window,caption.c_str());
    }
    void refresh_outline(){
        outline_ids.clear();
        const auto query=lowercase(text_of(search));
        const auto ids=query.empty()?state.map->visible():state.map->descendants(1);
        for(auto id:ids){
            auto n=state.map->nodes.at(id);
            if(query.empty() || lowercase(wide(n->title+"\n"+n->note)).find(query)!=std::wstring::npos) outline_ids.push_back(id);
        }
        int shown=std::max(1,static_cast<int>((height-237)/36));
        outline_scroll=std::clamp(outline_scroll,0,std::max(0,static_cast<int>(outline_ids.size())-shown));
    }
    void refresh_inspector(){
        updating=true;
        auto n=state.map->nodes.at(state.selected);
        SetWindowTextW(title,wide(n->title).c_str());
        std::wstring notes=wide(n->note);
        // Native edit controls use CRLF, document notes use LF.
        std::wstring native;
        for(size_t i=0;i<notes.size();++i){ if(notes[i]==L'\n' && (i==0 || notes[i-1]!=L'\r')) native+=L'\r'; native+=notes[i]; }
        SetWindowTextW(note,native.c_str());
        pending_edit=false; updating=false;
    }
    bool commit_edit(){
        // Read actual controls even when a programmatic edit does not emit EN_CHANGE.
        auto caption=text_of(title), notes=text_of(note);
        std::wstring normalized;
        for(size_t i=0;i<notes.size();++i){ if(notes[i]==L'\r' && i+1<notes.size() && notes[i+1]==L'\n') continue; normalized+=notes[i]; }
        auto t=utf8(caption), n=utf8(normalized);
        bool blank=true;
        for(wchar_t ch:caption){WORD type=0;GetStringTypeW(CT_CTYPE1,&ch,1,&type);if(!(type&C1_SPACE)){blank=false;break;}}
        if(blank || t.size()>1024 || n.size()>16000){
            status=L"Titre requis (1 024 octets max.) · note limitée à 16 000 octets"; repaint(); return false;
        }
        auto current=state.map->nodes.at(state.selected);
        if(t==current->title && n==current->note){pending_edit=false;return true;}
        std::string before=document::encode(state);
        state.map->rename(state.selected,t,n);
        remember(std::move(before)); pending_edit=false; changed(); return true;
    }
    void select(long long id,bool focus_canvas=true){
        if(!commit_edit()) return;
        state.selected=id;
        auto before=document::encode(state);
        state.map->reveal(id);
        if(before!=document::encode(state)) { remember(std::move(before)); changed(); }
        refresh_inspector(); refresh_outline(); repaint();
        if(focus_canvas) SetFocus(window);
    }
    PointF screen(const std::shared_ptr<MapNode>& n) const {
        return {canvas.X+static_cast<REAL>(state.pan_x+n->x*state.zoom),canvas.Y+static_cast<REAL>(state.pan_y+n->y*state.zoom)};
    }
    long long hit(PointF p) const {
        auto visible=state.map->visible();
        for(auto it=visible.p->rbegin();it!=visible.p->rend();++it){
            auto n=state.map->nodes.at(*it); auto at=screen(n);
            if(RectF(at.X-100*static_cast<REAL>(state.zoom),at.Y-34*static_cast<REAL>(state.zoom),200*static_cast<REAL>(state.zoom),68*static_cast<REAL>(state.zoom)).Contains(p)) return *it;
        }
        return 0;
    }
    void fit(){
        auto b=document::bounds(state);
        state.zoom=std::clamp(std::min((canvas.Width-90)/(b.right-b.left),(canvas.Height-90)/(b.bottom-b.top)),.2,1.4);
        state.pan_x=canvas.Width/2-(b.left+b.right)/2*state.zoom;
        state.pan_y=canvas.Height/2-(b.top+b.bottom)/2*state.zoom;
        autosave_pending=true; repaint();
    }
    void create_demo(){
        state=document::State{};
        state.map=std::make_shared<MindMap>("Mon prochain chapitre");
        auto a=state.map->add(1,"Comprendre");
        auto b=state.map->add(1,"Retenir");
        auto c=state.map->add(1,"S’entraîner");
        state.map->add(a,"Les grandes idées");
        state.map->add(a,"Les liens importants");
        auto dates=state.map->add(b,"Dates et repères");
        state.map->rename(dates,"Dates et repères","Ajoutez ici une explication, une date ou un exemple.\n\nDouble-cliquez sur une idée pour la modifier.");
        state.map->add(b,"Personnages clés");
        state.map->add(c,"Questions possibles");
        state.map->layout(); state.selected=1;
    }
    void initialize(){
        scale=static_cast<REAL>(GetDpiForWindow(window))/96;
        edit_brush=CreateSolidBrush(RGB(255,255,255));
        search=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(search_id),nullptr,nullptr);
        title=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(title_id),nullptr,nullptr);
        note=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,0,0,1,1,window,reinterpret_cast<HMENU>(note_id),nullptr,nullptr);
        SendMessageW(search,EM_SETCUEBANNER,0,reinterpret_cast<LPARAM>(L"Rechercher une idée…"));
        SendMessageW(title,EM_SETLIMITTEXT,256,0); SendMessageW(note,EM_SETLIMITTEXT,8000,0); SendMessageW(search,EM_SETLIMITTEXT,200,0);
        create_demo();
        if(!smoke && fs::exists(directory/L"session.mindmap")){
            try {state=document::decode(read_file(directory/L"session.mindmap"));status=L"Session restaurée · Ctrl+S pour enregistrer une carte";}
            catch(const std::exception& e){
                auto backup=directory/(L"session-invalide-"+std::to_wstring(GetTickCount64())+L".mindmap");
                std::error_code error;fs::copy_file(directory/L"session.mindmap",backup,fs::copy_options::none,error);
                status=(error?L"Session illisible · copie de secours impossible · ":L"Session illisible · copie de secours conservée · ")+wide(e.what());
                if(error)retry_save_at=std::numeric_limits<ULONGLONG>::max();
            }
        }
        resize(); refresh_inspector(); refresh_outline(); update_caption();
        if(!fs::exists(directory/L"session.mindmap") || smoke) fit();
        if(!smoke){
            int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
            if(argv&&argc>1){
                try{auto loaded=document::decode(read_file(fs::path(argv[1])));state=std::move(loaded);current_file=argv[1];refresh_inspector();refresh_outline();update_caption();autosave_pending=true;}
                catch(const std::exception& e){status=L"Ouverture impossible · "+wide(e.what());}
            }
            if(argv)LocalFree(argv);
        }
        SetTimer(window,autosave_timer,400,nullptr);
        if(smoke) SetTimer(window,smoke_timer,600,nullptr);
    }
    void resize(){
        RECT client;GetClientRect(window,&client);width=client.right/scale;height=client.bottom/scale;
        canvas=RectF(238,124,std::max(100.0f,width-540),std::max(100.0f,height-168));
        if(edit_font) DeleteObject(edit_font);
        edit_font=CreateFontW(-static_cast<int>(15*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(HWND control:{search,title,note}) SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(edit_font),TRUE);
        auto place=[&](HWND control,int x,int y,int w,int h){MoveWindow(control,static_cast<int>(x*scale),static_cast<int>(y*scale),static_cast<int>(w*scale),static_cast<int>(h*scale),TRUE);};
        place(search,28,153,180,30);
        place(title,static_cast<int>(width)-270,205,242,38);
        place(note,static_cast<int>(width)-270,440,242,std::max(80,static_cast<int>(height)-545));
        refresh_outline();repaint();
    }
    void draw_map(Graphics& g,const document::State& doc,RectF area,double zoom,double panx,double pany,bool selection){
        auto visible=doc.map->visible();
        auto project=[&](const std::shared_ptr<MapNode>& n){return PointF(area.X+static_cast<REAL>(panx+n->x*zoom),area.Y+static_cast<REAL>(pany+n->y*zoom));};
        for(auto id:visible){
            auto n=doc.map->nodes.at(id);
            if(n->parent==-1)continue;
            auto parent=doc.map->nodes.at(n->parent);auto a=project(parent), b=project(n);
            Pen line(rgb(n->color,125),std::max(1.0f,static_cast<REAL>(2.0*zoom)));
            REAL bend=static_cast<REAL>(std::max(40.0,std::abs(n->x-parent->x)*.45)*zoom);
            g.DrawBezier(&line,a.X+100*static_cast<REAL>(zoom),a.Y,a.X+100*static_cast<REAL>(zoom)+bend,a.Y,b.X-100*static_cast<REAL>(zoom)-bend,b.Y,b.X-100*static_cast<REAL>(zoom),b.Y);
        }
        for(auto id:visible){
            auto n=doc.map->nodes.at(id);auto at=project(n);REAL z=static_cast<REAL>(zoom);
            RectF r(at.X-100*z,at.Y-34*z,200*z,68*z);
            if(r.GetRight()<area.X || r.X>area.GetRight() || r.GetBottom()<area.Y || r.Y>area.GetBottom())continue;
            box(g,RectF(r.X+1*z,r.Y+3*z,r.Width,r.Height),Color(15,30,45,85),Color(0,0,0,0),12*z);
            const bool chosen=selection&&id==doc.selected;
            box(g,r,id==1?rgb(n->color):Color(255,255,255,255),chosen?rgb(0x283FA3):rgb(n->color,180),12*z,chosen?3*z:1.5f*z);
            if(id!=1){SolidBrush accent(rgb(n->color));g.FillRectangle(&accent,r.X+10*z,r.Y+14*z,3*z,40*z);}
            label(g,wide(n->title),RectF(r.X+22*z,r.Y+7*z,r.Width-40*z,r.Height-14*z),14*z,id==1?Color(255,255,255,255):Color(255,37,48,71),id==1,true);
            if(!n->note.empty()){SolidBrush dot(id==1?Color(255,255,255,255):rgb(n->color));g.FillEllipse(&dot,r.GetRight()-13*z,r.GetBottom()-13*z,5*z,5*z);}
            if(!n->children.empty()){
                box(g,RectF(r.GetRight()-7*z,at.Y-10*z,20*z,20*z),Color(255,255,255,255),rgb(n->color),10*z);
                label(g,n->collapsed?L"+":L"−",RectF(r.GetRight()-7*z,at.Y-10*z,20*z,20*z),12*z,rgb(n->color),true,true);
            }
        }
    }
    void paint(HDC dc){
        RECT client;GetClientRect(window,&client);
        Bitmap buffer(std::max(1L,client.right),std::max(1L,client.bottom),PixelFormat32bppPARGB);
        Graphics g(&buffer);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.ScaleTransform(scale,scale);
        g.Clear(Color(255,245,247,252));
        SolidBrush white(Color(255,255,255,255));g.FillRectangle(&white,0.0f,0.0f,width,120.0f);
        box(g,RectF(24,20,36,36),rgb(0x6274E8),Color(0,0,0,0),11);
        label(g,L"M",RectF(24,20,36,36),22,Color(255,255,255,255),true,true);
        label(g,L"MindMap",RectF(72,14,185,34),24,Color(255,30,41,68),true);
        label(g,L"Un espace clair pour relier vos idées",RectF(73,46,350,22),12,Color(255,112,122,145));
        label(g,L"PORTABLE  •  SANS COMPTE",RectF(width-300,22,272,24),11,Color(255,104,116,142),false,true);
        buttons.clear();
        auto button=[&](REAL x,REAL y,REAL w,const wchar_t* name,int command,bool primary=false){
            RectF rect(x,y,w,32);buttons.push_back({rect,command,name,primary});
            box(g,rect,primary?rgb(0x6274E8):Color(255,245,247,252),Color(255,225,230,240),8);
            label(g,name,rect,12,primary?Color(255,255,255,255):Color(255,65,77,105),primary,true);
        };
        button(24,79,80,L"Fichier ▾",0);
        button(114,79,114,L"+ Idée enfant",Add,true);
        button(238,79,82,L"Même niveau",Sibling);
        button(330,79,66,L"Annuler",Undo);button(406,79,66,L"Rétablir",Redo);
        button(482,79,95,L"Réorganiser",Layout);button(587,79,69,L"Centrer",Fit);
        button(666,79,88,L"Enregistrer",Save);
        button(width-96,79,68,L"Aide",Help);
        box(g,RectF(16,132,210,height-177),Color(255,255,255,255),Color(255,230,234,244),12);
        label(g,L"PLAN DE LA CARTE",RectF(28,133,186,18),10,Color(255,108,121,145),true);
        outline_hits.clear();
        int visible_rows=std::max(0,static_cast<int>((height-237)/36));
        const bool searching=!text_of(search).empty();
        for(int row=0;row<visible_rows;++row){
            size_t index=static_cast<size_t>(row+outline_scroll);if(index>=outline_ids.size())break;
            long long id=outline_ids[index];auto n=state.map->nodes.at(id);
            REAL y=195+row*36.0f;RectF r(24,y,194,33);outline_hits.emplace_back(r,id);
            if(id==state.selected)box(g,r,Color(255,237,240,254),Color(0,0,0,0),7);
            REAL indent=searching?0.0f:static_cast<REAL>(std::min(5LL,state.map->depth(id)))*10;
            SolidBrush color(rgb(n->color));g.FillEllipse(&color,32+indent,y+13,6.0f,6.0f);
            label(g,wide(n->title),RectF(45+indent,y,163-indent,33),12);
        }
        if(outline_ids.empty())label(g,L"Aucun résultat",RectF(32,206,176,32),13,Color(255,124,132,151));
        GraphicsState clip=g.Save();g.SetClip(canvas);
        REAL step=std::max(18.0f,static_cast<REAL>(28*state.zoom));
        REAL gx=canvas.X+static_cast<REAL>(std::fmod(state.pan_x,step)),gy=canvas.Y+static_cast<REAL>(std::fmod(state.pan_y,step));
        SolidBrush grid(Color(255,221,226,238));
        for(REAL x=gx;x<canvas.GetRight();x+=step)for(REAL y=gy;y<canvas.GetBottom();y+=step)g.FillEllipse(&grid,x,y,1.5f,1.5f);
        draw_map(g,state,canvas,state.zoom,state.pan_x,state.pan_y,true);g.Restore(clip);
        label(g,L"Tab : enfant   Entrée : même niveau   Double-clic : modifier   Molette : zoom",RectF(canvas.X+12,canvas.GetBottom()-26,canvas.Width-24,24),10,Color(255,110,122,145),false,true);
        box(g,RectF(width-286,132,270,height-177),Color(255,255,255,255),Color(255,230,234,244),12);
        label(g,L"IDÉE SÉLECTIONNÉE",RectF(width-270,145,242,25),11,Color(255,108,121,145),true);
        label(g,L"Titre",RectF(width-270,179,242,24),12,Color(255,64,78,103),true);
        label(g,L"Branche",RectF(width-270,252,242,25),12,Color(255,64,78,103),true);
        button(width-270,284,115,L"Dupliquer",Duplicate);button(width-145,284,117,L"Replier / ouvrir",Collapse);
        button(width-270,324,242,L"Supprimer la branche",Delete);
        label(g,L"Couleur de toute la branche",RectF(width-270,365,242,24),12);
        auto selected=state.map->nodes.at(state.selected);
        for(int i=0;i<6;++i){
            RectF r(width-269+i*40.0f,398,27,27);buttons.push_back({r,Color0+i,L"",false});
            box(g,r,rgb(palette[i]),selected->color==palette[i]?Color(255,31,42,64):Color(0,0,0,0),8,2);
        }
        label(g,L"Notes",RectF(width-270,426,242,14),11,Color(255,64,78,103),true);
        button(width-270,height-88,242,L"Appliquer les modifications",Apply,true);
        std::wstring count=std::to_wstring(state.map->nodes.size())+L" idées  •  "+std::to_wstring(static_cast<int>(state.zoom*100))+L" %";
        label(g,count,RectF(22,height-31,230,25),11,Color(255,102,116,142));
        label(g,status,RectF(265,height-31,width-285,25),11,Color(255,102,116,142));
        Graphics target(dc);target.DrawImage(&buffer,0,0);
    }
    fs::path dialog(bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* suggestion){
        wchar_t filename[32768]{};
        std::wstring initial=directory.wstring();
        if(save){auto proposed=directory/suggestion;wcsncpy(filename,proposed.c_str(),32767);}
        OPENFILENAMEW settings{};settings.lStructSize=sizeof(settings);settings.hwndOwner=window;
        settings.lpstrFilter=filter;settings.lpstrFile=filename;settings.nMaxFile=32768;
        settings.lpstrInitialDir=initial.c_str();settings.lpstrDefExt=extension;
        settings.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(save?GetSaveFileNameW(&settings):GetOpenFileNameW(&settings))return fs::path(filename);
        return {};
    }
    bool save(bool choose){
        if(!commit_edit())return false;
        auto path=current_file;
        if(choose||path.empty())path=dialog(true,L"Carte mentale (*.mindmap)\0*.mindmap\0\0",L"mindmap",L"Ma carte.mindmap");
        if(path.empty())return false;
        atomic_write(path,document::encode(state));current_file=path;dirty=false;retry_save_at=0;
        autosave_pending=true;status=L"Carte enregistrée · "+path.filename().wstring();update_caption();repaint();return true;
    }
    bool may_replace(){
        if(!commit_edit())return false;
        if(!dirty)return true;
        int answer=MessageBoxW(window,L"Enregistrer la carte actuelle avant de continuer ?\nLa session automatique protège aussi vos dernières modifications.",L"Changer de carte",MB_YESNOCANCEL|MB_ICONQUESTION);
        return answer==IDNO||(answer==IDYES&&save(false));
    }
    void checkpoint(){
        if(!commit_edit())return;
        if(autosave_pending){atomic_write(directory/L"session.mindmap",document::encode(state));autosave_pending=false;status=L"Session sauvegardée dans le dossier portable";repaint();}
    }
    void export_png(const fs::path& path){
        auto b=document::bounds(state);
        double w=b.right-b.left+100,h=b.bottom-b.top+100;
        double factor=std::min({1.5,8192.0/w,8192.0/h,std::sqrt(16000000.0/(w*h))});
        INT bw=std::max(1,static_cast<int>(std::ceil(w*factor))),bh=std::max(1,static_cast<int>(std::ceil(h*factor)));
        Bitmap image(bw,bh,PixelFormat32bppARGB);
        if(image.GetLastStatus()!=Ok)throw std::runtime_error("Memoire insuffisante pour l'image");
        Graphics g(&image);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.Clear(Color(255,245,247,252));
        draw_map(g,state,RectF(0,0,static_cast<REAL>(bw),static_cast<REAL>(bh)),factor,(-b.left+50)*factor,(-b.top+50)*factor,false);
        UINT count=0,size=0;GetImageEncodersSize(&count,&size);
        std::vector<BYTE> codecs(size);auto* info=reinterpret_cast<ImageCodecInfo*>(codecs.data());GetImageEncoders(count,size,info);
        CLSID encoder{};bool found=false;
        for(UINT i=0;i<count;++i)if(wcscmp(info[i].MimeType,L"image/png")==0){encoder=info[i].Clsid;found=true;break;}
        if(!found)throw std::runtime_error("Encodeur PNG indisponible");
        auto temporary=path;temporary+=L".tmp";
        if(image.Save(temporary.c_str(),&encoder,nullptr)!=Ok || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(temporary.c_str());throw std::runtime_error("Echec d'export PNG");
        }
    }
    void help(){
        MessageBoxW(window,
            L"Pour commencer\nSélectionnez une idée, puis Tab pour lui ajouter un enfant.\nEntrée ajoute une idée au même niveau. Double-cliquez pour renommer.\n\nNaviguer\nMolette : zoom autour du pointeur. Glisser le fond : déplacer la vue.\nGlisser une idée : déplacer sa branche. Maj + glisser vers une autre idée : changer de parent.\nCliquez sur + / − pour ouvrir ou replier une branche.\nLa recherche retrouve les titres et les notes, même dans les branches repliées.\n\nRaccourcis\nCtrl+N : nouveau  ·  Ctrl+O : ouvrir  ·  Ctrl+S : enregistrer\nCtrl+Maj+S : enregistrer sous  ·  Ctrl+Z / Y : annuler / rétablir\nCtrl+F : recherche  ·  Ctrl+D : dupliquer  ·  F2 : modifier\nSuppr : supprimer  ·  F : centrer  ·  Ctrl+L : réorganiser\n\nPortable\nMindMap.exe et session.mindmap suffisent. Aucun compte, installation ou dossier AppData.\nEnregistrez votre carte avant de retirer la clé USB.\nFichier > Exporter : PNG, SVG ou plan Markdown.\nLa carte d’exemple sert à essayer ; Nouveau crée une carte vierge.",
            L"MindMap · prise en main",MB_OK|MB_ICONINFORMATION);
    }
    void file_menu(POINT at){
        HMENU menu=CreatePopupMenu();
        auto item=[&](UINT id,const wchar_t* text){AppendMenuW(menu,MF_STRING,id,text);};
        item(New,L"Nouvelle carte\tCtrl+N");item(Open,L"Ouvrir…\tCtrl+O");item(Save,L"Enregistrer\tCtrl+S");item(SaveAs,L"Enregistrer sous…\tCtrl+Maj+S");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);item(ExportPng,L"Exporter une image PNG…");item(ExportSvg,L"Exporter en SVG…");item(ExportMd,L"Exporter le plan Markdown…");
        int result=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,at.x,at.y,0,window,nullptr);DestroyMenu(menu);
        if(result)command(result);
    }
    void add_new_node(bool same_level){
        // Native boundary: cap identifiers to the document schema's tested range.
        if(state.map->next_id>=2147481647)throw std::runtime_error("Identifiants epuises : creez une nouvelle carte");
        auto parent=same_level?state.map->nodes.at(state.selected)->parent:state.selected;
        if(parent==-1)parent=1;
        state.selected=state.map->add(parent,"Nouvelle idée");state.map->layout();
    }
    void command(int id){
        try {
            if(id==Help){help();return;}
            if(id==Apply){commit_edit();return;}
            if(!commit_edit())return;
            if(id==Save||id==SaveAs){save(id==SaveAs);return;}
            if(id==New||id==Open){
                if(!may_replace())return;
                if(id==New){state=document::State{};state.map->nodes.at(1)->title="Mon sujet";current_file.clear();fit();}
                else {
                    auto path=dialog(false,L"Carte mentale (*.mindmap)\0*.mindmap\0\0",L"mindmap",L"");if(path.empty())return;
                    auto loaded=document::decode(read_file(path));state=std::move(loaded);current_file=path;
                }
                undo.clear();redo.clear();dirty=id==New;autosave_pending=true;refresh_inspector();refresh_outline();update_caption();repaint();return;
            }
            if(id==ExportPng||id==ExportSvg||id==ExportMd){
                const wchar_t* ext=id==ExportPng?L"png":id==ExportSvg?L"svg":L"md";
                const wchar_t* filter=id==ExportPng?L"Image PNG\0*.png\0\0":id==ExportSvg?L"Image SVG\0*.svg\0\0":L"Plan Markdown\0*.md\0\0";
                std::wstring name=L"Ma carte.";name+=ext;
                auto path=dialog(true,filter,ext,name.c_str());if(path.empty())return;
                if(id==ExportPng)export_png(path);else atomic_write(path,id==ExportSvg?document::svg(state):document::markdown(state));
                status=L"Export terminé · "+path.filename().wstring();repaint();return;
            }
            if(id==Fit){fit();return;}
            if(id==Undo||id==Redo){
                auto& from=id==Undo?undo:redo;auto& to=id==Undo?redo:undo;
                if(from.empty()){status=L"Aucune modification à "+std::wstring(id==Undo?L"annuler":L"rétablir");repaint();return;}
                auto restored=document::decode(from.back());to.push_back(document::encode(state));from.pop_back();state=std::move(restored);
                refresh_inspector();changed();return;
            }
            std::string before=document::encode(state);
            if(id==Add||id==Sibling){
                add_new_node(id==Sibling);
            } else if(id==Delete){
                if(state.selected==1){status=L"Le sujet central se renomme ; il ne se supprime pas.";repaint();return;}
                size_t count=state.map->descendants(state.selected).size();
                if(count>1&&!smoke){std::wstring msg=L"Supprimer cette branche et ses "+std::to_wstring(count-1)+L" sous-idées ?\nCtrl+Z permet de revenir en arrière.";if(MessageBoxW(window,msg.c_str(),L"Supprimer une branche",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;}
                state.selected=state.map->remove(state.selected);
            } else if(id==Duplicate){
                if(state.map->next_id>=2147481647)throw std::runtime_error("Identifiants epuises : creez une nouvelle carte");
                state.selected=state.map->duplicate(state.selected);state.map->layout();
            }
            else if(id==Layout){state.map->layout();}
            else if(id==Collapse){state.map->toggle(state.selected);}
            else if(id>=Color0&&id<Color0+6){state.map->set_color(state.selected,palette[id-Color0],true);}
            else return;
            remember(std::move(before));refresh_inspector();changed();
            if(id==Add||id==Sibling||id==Layout||id==Duplicate)fit();
            if(id==Add||id==Sibling){SetFocus(title);SendMessageW(title,EM_SETSEL,0,-1);}
        } catch(const PyThrow& e){show_error(e.p->py_exc_str());}
          catch(const std::exception& e){show_error(e.what());}
    }
    void mouse_down(PointF point, bool middle){
        if(!middle){
            for(const auto& button:buttons)if(button.rect.Contains(point)){
                if(button.command==0){POINT at{static_cast<LONG>(button.rect.X*scale),static_cast<LONG>(button.rect.GetBottom()*scale)};ClientToScreen(window,&at);file_menu(at);}
                else command(button.command);
                return;
            }
            for(const auto& row:outline_hits)if(row.first.Contains(point)){select(row.second);auto n=state.map->nodes.at(row.second);state.pan_x=canvas.Width/2-n->x*state.zoom;state.pan_y=canvas.Height/2-n->y*state.zoom;autosave_pending=true;repaint();return;}
        }
        if(!canvas.Contains(point)||!commit_edit())return;
        SetFocus(window);last_mouse=point;moved=false;
        long long target=middle?0:hit(point);
        if(target && !(GetKeyState(VK_SPACE)&0x8000)){
            select(target);auto n=state.map->nodes.at(target);auto pos=screen(n);REAL z=static_cast<REAL>(state.zoom);
            if(!n->children.empty()&&point.X>pos.X+93*z&&std::abs(point.Y-pos.Y)<14*z){command(Collapse);return;}
            dragging=true;drag_id=target;drag_before=document::encode(state);
        }else panning=true;
        SetCapture(window);
    }
    void mouse_move(PointF point){
        if(!dragging&&!panning)return;
        REAL dx=point.X-last_mouse.X,dy=point.Y-last_mouse.Y;
        if(std::abs(dx)+std::abs(dy)>0.1f)moved=true;
        if(dragging){
            if(!(GetKeyState(VK_SHIFT)&0x8000)){
                auto ids=state.map->descendants(drag_id);bool safe=true;
                for(auto id:ids){auto n=state.map->nodes.at(id);if(std::abs(n->x+dx/state.zoom)>1000000||std::abs(n->y+dy/state.zoom)>1000000)safe=false;}
                if(safe)state.map->move_branch(drag_id,dx/state.zoom,dy/state.zoom);
            }
        }else{state.pan_x=std::clamp(state.pan_x+dx,-10000000.0,10000000.0);state.pan_y=std::clamp(state.pan_y+dy,-10000000.0,10000000.0);}
        last_mouse=point;repaint();
    }
    void mouse_up(PointF point){
        if(!dragging&&!panning)return;
        if(dragging&&moved){
            try{
                if(GetKeyState(VK_SHIFT)&0x8000){auto target=hit(point);if(target&&target!=drag_id){state.map->reparent(drag_id,target);state.map->layout();}}
                if(drag_before!=document::encode(state)){remember(std::move(drag_before));changed();}
            }catch(const PyThrow& e){state=document::decode(drag_before);refresh_inspector();show_error(e.p->py_exc_str());}
        }
        if(panning&&moved)autosave_pending=true;
        dragging=false;panning=false;ReleaseCapture();repaint();
    }
    void wheel(PointF point,int delta){
        if(point.X<canvas.X){outline_scroll-=delta/120*3;refresh_outline();repaint();return;}
        if(!canvas.Contains(point))return;
        double old=state.zoom;state.zoom=std::clamp(old*std::pow(1.12,delta/120.0),.2,3.0);
        state.pan_x=(point.X-canvas.X)-(point.X-canvas.X-state.pan_x)*state.zoom/old;
        state.pan_y=(point.Y-canvas.Y)-(point.Y-canvas.Y-state.pan_y)*state.zoom/old;
        autosave_pending=true;repaint();
    }
    void smoke_test(){
        KillTimer(window,smoke_timer);
        try{
            auto handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            for(int i=0;i<40;++i){InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);}
            auto after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            if(after>handles+3)throw std::runtime_error("Objets GDI non liberes");
            select(2);command(Add);SetWindowTextW(title,L"Révolution française 🗺");SetWindowTextW(note,L"1789\r\nUne note avec des accents.");pending_edit=false;commit_edit();
            if(state.map->nodes.at(state.selected)->note!="1789\nUne note avec des accents.")throw std::runtime_error("La note du controle n'est pas synchronisee");
            auto id=state.selected;
            command(Color0+1);command(Duplicate);auto size=state.map->nodes.size();command(Undo);command(Redo);
            if(state.map->nodes.size()!=size)throw std::runtime_error("Annuler/retablir incorrect");
            select(id);command(Collapse);command(Collapse);command(Layout);fit();
            auto encoded=document::encode(state);auto decoded=document::decode(encoded);
            if(document::encode(decoded)!=encoded)throw std::runtime_error("Roundtrip document incorrect");
            atomic_write(directory/L"test-carte.mindmap",encoded);
            atomic_write(directory/L"test-plan.md",document::markdown(state));
            atomic_write(directory/L"test-carte.svg",document::svg(state));
            export_png(directory/L"test-carte.png");
            RECT rc;GetClientRect(window,&rc);Bitmap image(rc.right,rc.bottom,PixelFormat32bppARGB);
            {
                Graphics capture(&image);HDC dc=capture.GetHDC();paint(dc);
                for(HWND control:{search,title,note}){
                    RECT r;GetWindowRect(control,&r);POINT p{r.left,r.top};ScreenToClient(window,&p);
                    int saved=SaveDC(dc);SetViewportOrgEx(dc,p.x,p.y,nullptr);
                    SendMessageW(control,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
                    RestoreDC(dc,saved);
                }
                capture.ReleaseHDC(dc);
            }
            UINT count=0,sizebytes=0;GetImageEncodersSize(&count,&sizebytes);std::vector<BYTE> codecs(sizebytes);auto* infos=reinterpret_cast<ImageCodecInfo*>(codecs.data());GetImageEncoders(count,sizebytes,infos);
            for(UINT i=0;i<count;++i)if(wcscmp(infos[i].MimeType,L"image/png")==0)image.Save((directory/L"test-interface.png").c_str(),&infos[i].Clsid,nullptr);
            atomic_write(directory/L"smoke-test-report.json","{\"ok\":true,\"nodes\":"+std::to_string(state.map->nodes.size())+",\"gdi_before\":"+std::to_string(handles)+",\"gdi_after\":"+std::to_string(after)+"}\n");
            dirty=false;autosave_pending=false;pending_edit=false;DestroyWindow(window);
        }catch(const PyThrow& e){atomic_write(directory/L"smoke-test-report.json","{\"ok\":false,\"error\":"+document::quote(e.p->py_exc_str())+"}");DestroyWindow(window);}
         catch(const std::exception& e){atomic_write(directory/L"smoke-test-report.json","{\"ok\":false,\"error\":"+document::quote(e.what())+"}");DestroyWindow(window);}
    }
};

LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=reinterpret_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,wp,lp);
    try{
        switch(message){
            case WM_CREATE:app->initialize();return 0;
            case WM_SIZE:if(app->search)app->resize();return 0;
            case WM_DPICHANGED:{app->scale=LOWORD(wp)/96.0f;auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->resize();return 0;}
            case WM_GETMINMAXINFO:{auto p=reinterpret_cast<MINMAXINFO*>(lp);p->ptMinTrackSize={static_cast<LONG>(940*app->scale),static_cast<LONG>(640*app->scale)};return 0;}
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);app->paint(dc);EndPaint(window,&p);return 0;}
            case WM_CTLCOLOREDIT:{auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,RGB(37,48,71));SetBkColor(dc,RGB(255,255,255));return reinterpret_cast<LRESULT>(app->edit_brush);}
            case WM_COMMAND:
                if(HIWORD(wp)==EN_CHANGE){
                    if(LOWORD(wp)==search_id){app->outline_scroll=0;app->refresh_outline();app->repaint();}
                    else if(!app->updating){app->pending_edit=true;app->edit_time=GetTickCount64();}
                }else if(!lp)app->command(LOWORD(wp));
                return 0;
            case WM_TIMER:
                if(wp==smoke_timer){app->smoke_test();return 0;}
                if(GetTickCount64()-app->edit_time>1200&&(app->pending_edit||GetFocus()==app->title||GetFocus()==app->note))app->commit_edit();
                if(!app->pending_edit&&!app->dragging&&!app->panning&&app->autosave_pending&&GetTickCount64()>=app->retry_save_at){
                    try{app->checkpoint();}catch(const std::exception& e){app->status=L"Sauvegarde impossible · "+wide(e.what())+L" · nouvel essai dans 30 s";app->retry_save_at=GetTickCount64()+30000;app->repaint();}
                }
                return 0;
            case WM_LBUTTONDOWN:case WM_MBUTTONDOWN:app->mouse_down(PointF(GET_X_LPARAM(lp)/app->scale,GET_Y_LPARAM(lp)/app->scale),message==WM_MBUTTONDOWN);return 0;
            case WM_MOUSEMOVE:app->mouse_move(PointF(GET_X_LPARAM(lp)/app->scale,GET_Y_LPARAM(lp)/app->scale));return 0;
            case WM_LBUTTONUP:case WM_MBUTTONUP:app->mouse_up(PointF(GET_X_LPARAM(lp)/app->scale,GET_Y_LPARAM(lp)/app->scale));return 0;
            case WM_LBUTTONDBLCLK:{auto id=app->hit(PointF(GET_X_LPARAM(lp)/app->scale,GET_Y_LPARAM(lp)/app->scale));if(id){app->select(id,false);SetFocus(app->title);SendMessageW(app->title,EM_SETSEL,0,-1);}return 0;}
            case WM_CONTEXTMENU:{
                POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};POINT client=point;ScreenToClient(window,&client);
                auto id=app->hit(PointF(client.x/app->scale,client.y/app->scale));
                if(id)app->select(id);
                HMENU menu=CreatePopupMenu();
                for(auto item:std::vector<std::pair<int,const wchar_t*>>{{Add,L"Ajouter une idée enfant"},{Sibling,L"Ajouter au même niveau"},{Duplicate,L"Dupliquer la branche"},{Collapse,L"Replier / ouvrir"},{Delete,L"Supprimer la branche"},{Fit,L"Centrer la carte"}})AppendMenuW(menu,MF_STRING,item.first,item.second);
                int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);if(action)app->command(action);return 0;
            }
            case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&p);app->wheel(PointF(p.x/app->scale,p.y/app->scale),GET_WHEEL_DELTA_WPARAM(wp));return 0;}
            case WM_CAPTURECHANGED:if(reinterpret_cast<HWND>(lp)!=window&&(app->dragging||app->panning)){app->mouse_up(app->last_mouse);}return 0;
            case WM_DROPFILES:{auto drop=reinterpret_cast<HDROP>(wp);wchar_t path[32768];bool have=DragQueryFileW(drop,0,path,32768)>0;DragFinish(drop);if(have&&app->may_replace()){auto loaded=document::decode(read_file(fs::path(path)));app->state=std::move(loaded);app->current_file=path;app->undo.clear();app->redo.clear();app->dirty=false;app->autosave_pending=true;app->refresh_inspector();app->refresh_outline();app->update_caption();app->repaint();}return 0;}
            case WM_CLOSE:
                if(!app->commit_edit()){if(MessageBoxW(window,L"Certaines modifications du titre ou des notes sont invalides. Fermer sans ces modifications ?",L"MindMap",MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;app->pending_edit=false;}
                try{app->checkpoint();}catch(const std::exception& e){if(MessageBoxW(window,(L"Sauvegarde automatique impossible : "+wide(e.what())+L"\nFermer quand même ?").c_str(),L"MindMap",MB_YESNO|MB_ICONWARNING)!=IDYES)return 0;}
                DestroyWindow(window);return 0;
            case WM_DESTROY:KillTimer(window,autosave_timer);KillTimer(window,smoke_timer);PostQuitMessage(0);return 0;
        }
    }catch(const PyThrow& e){app->show_error(e.p->py_exc_str());}
     catch(const std::exception& e){app->status=L"Attention · "+wide(e.what());app->repaint();if(message==WM_TIMER){KillTimer(window,autosave_timer);}else app->show_error(e.what());}
    return DefWindowProcW(window,message,wp,lp);
}
} // namespace ui

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    ULONG_PTR token=0;GdiplusStartupInput input;
    if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 1;
    int result=0;
    try{
        wchar_t executable[32768];DWORD length=GetModuleFileNameW(nullptr,executable,32768);
        if(!length||length>=32768)throw std::runtime_error("Chemin de l'application inaccessible");
        fs::path folder=fs::path(executable).parent_path();
        bool smoke=wcsstr(GetCommandLineW(),L"--smoke-test")!=nullptr;
        std::wstring mutex_name=L"Local\\MindMapPortable_"+std::to_wstring(std::hash<std::wstring>{}(folder.wstring()));
        ui::Handle mutex(CreateMutexW(nullptr,FALSE,mutex_name.c_str()));
        if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"Une carte MindMap est déjà ouverte depuis ce dossier. Fermez-la avant d'en ouvrir une autre.",L"MindMap",MB_OK);GdiplusShutdown(token);return 0;}
        ui::App app(folder,smoke);
        WNDCLASSW klass{};klass.lpfnWndProc=ui::procedure;klass.hInstance=instance;klass.lpszClassName=L"MindMapPortableWindow";
        klass.style=CS_DBLCLKS;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(2));RegisterClassW(&klass);
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        double dpi=GetDpiForSystem()/96.0;
        int initial_width=std::min(static_cast<int>(1280*dpi),static_cast<int>(work.right-work.left-24));
        int initial_height=std::min(static_cast<int>(820*dpi),static_cast<int>(work.bottom-work.top-24));
        HWND window=CreateWindowExW(WS_EX_ACCEPTFILES,klass.lpszClassName,L"MindMap",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,initial_width,initial_height,nullptr,nullptr,instance,&app);
        if(!window)throw std::runtime_error("Creation de la fenetre impossible");
        DragAcceptFiles(window,TRUE);ShowWindow(window,smoke?SW_SHOWNOACTIVATE:show);UpdateWindow(window);
        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0){
            if(msg.message==WM_KEYDOWN){
                bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
                HWND focus=GetFocus();bool editing=focus==app.title||focus==app.note||focus==app.search;
                int action=0;
                if(control){
                    switch(msg.wParam){case 'N':action=ui::New;break;case 'O':action=ui::Open;break;case 'S':action=shift?ui::SaveAs:ui::Save;break;
                        case 'Z':if(!editing)action=shift?ui::Redo:ui::Undo;break;case 'Y':if(!editing)action=ui::Redo;break;
                        case 'D':if(!editing)action=ui::Duplicate;break;case 'L':action=ui::Layout;break;
                        case 'F':SetFocus(app.search);SendMessageW(app.search,EM_SETSEL,0,-1);continue;
                        case VK_RETURN:action=ui::Apply;break;}
                }else if(!editing){
                    switch(msg.wParam){case VK_TAB:action=ui::Add;break;case VK_RETURN:action=ui::Sibling;break;case VK_DELETE:action=ui::Delete;break;case 'F':action=ui::Fit;break;
                        case VK_F2:SetFocus(app.title);SendMessageW(app.title,EM_SETSEL,0,-1);continue;}
                }
                if(msg.wParam==VK_ESCAPE){if(editing){app.commit_edit();SetFocus(window);continue;}}
                if(action){app.command(action);continue;}
                if(editing&&msg.wParam==VK_TAB){SetFocus(focus==app.title?app.note:focus==app.note?app.search:app.title);continue;}
            }
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
    }catch(const std::exception& e){MessageBoxW(nullptr,ui::wide(e.what()).c_str(),L"MindMap",MB_OK|MB_ICONERROR);result=1;}
    GdiplusShutdown(token);return result;
}
