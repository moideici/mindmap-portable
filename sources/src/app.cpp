#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objidl.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <filesystem>
#include <deque>
#include <fstream>
#include <optional>
#include <set>
#include <unordered_map>
#include "exports.hpp"
#include "history.hpp"
#include "workspace.hpp"
#include "imports.hpp"
#include "courses.hpp"
#include "pdf.hpp"

using namespace Gdiplus;
namespace fs = std::filesystem;

namespace ui {
constexpr UINT autosave_timer = 1, smoke_timer = 2;
enum Command { New=100, Open, Save, SaveAs, Add, Sibling, Delete, Duplicate, Undo, Redo, Layout, Fit,
    Collapse, ExportPng, ExportSvg, ExportMd, Apply, Help, ImportOutline, PasteOutline, CopyBranch,
    LayoutRight, LayoutBalanced, LayoutRadial, ToggleNotes, ToggleMinimap, ToggleDark, LinkMode,
    CustomColor, GlobalSearch, ExportCourse, ExportNemo,
    DeleteLink, ReverseLink, SelectRoot, Home, AddImage, RemoveImage, ExportPdf, SaveTemplate, RestoreVersions, RecoverLegacy, BrowseDrafts, DeleteDraft,
    OpenSubmap, CreateSubmap, DetachSubmap, UpSubmap,
    Color0=150, Marker0=170, LinkLabel0=190, LinkRow0=220, LinkRowDelete0=240,
    Jump0=1000, BrowseTemplates=500, BrowseRecent=501, Template0=600, Recent0=700, Draft0=720, CustomTemplate0=800, Restore0=900 };
constexpr int search_id=401, title_id=402, note_id=403;
constexpr int link_label_custom=8, link_label_none=7;
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
std::wstring to_native_lines(const std::wstring& text) {
    std::wstring native;
    for(size_t i=0;i<text.size();++i){ if(text[i]==L'\n' && (i==0 || text[i-1]!=L'\r')) native+=L'\r'; native+=text[i]; }
    return native;
}
std::wstring kilobytes(size_t bytes) { return std::to_wstring((bytes+1023)/1024)+L" Ko"; }
Color rgb(long long value, BYTE alpha=255) { return Color(alpha, static_cast<BYTE>(value>>16), static_cast<BYTE>(value>>8), static_cast<BYTE>(value)); }
void round_path(GraphicsPath& p, RectF r, REAL radius) {
    REAL d=std::max(0.5f,std::min({radius*2,r.Width,r.Height}));
    p.AddArc(r.X,r.Y,d,d,180,90); p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
    p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90); p.AddArc(r.X,r.GetBottom()-d,d,d,90,90); p.CloseFigure();
}
void panel(Graphics& g, RectF rect, Color fill, Color stroke=Color(0,0,0,0), REAL radius=10, REAL thickness=1, bool dashed=false) {
    GraphicsPath path; round_path(path,rect,radius);
    if (fill.GetA()) { SolidBrush brush(fill); g.FillPath(&brush,&path); }
    if (stroke.GetA()) { Pen pen(stroke,thickness); if(dashed) pen.SetDashStyle(DashStyleDash); g.DrawPath(&pen,&path); }
}

// Two colour schemes; exports always use the light one so they print well.
struct Theme {
    Color bg, panel, border, header, title, text, subtle, node, node_text, note_text, grid, button, button_border, button_text, row, link, selection, highlight;
    COLORREF edit_bg, edit_text;
};
const Theme& theme(bool dark) {
    static const Theme light{Color(255,245,247,252),Color(255,255,255,255),Color(255,230,234,244),Color(255,255,255,255),Color(255,30,41,68),
        Color(255,37,48,71),Color(255,108,121,145),Color(255,255,255,255),Color(255,37,48,71),Color(255,102,113,138),Color(255,221,226,238),
        Color(255,245,247,252),Color(255,225,230,240),Color(255,65,77,105),Color(255,237,240,254),rgb(0x7B5BD6),rgb(0x283FA3),Color(255,255,196,61),
        RGB(255,255,255),RGB(37,48,71)};
    static const Theme night{Color(255,23,26,35),Color(255,32,36,48),Color(255,49,55,72),Color(255,30,34,45),Color(255,232,236,246),
        Color(255,222,227,240),Color(255,140,150,172),Color(255,41,46,62),Color(255,228,232,242),Color(255,160,170,192),Color(255,44,50,66),
        Color(255,42,48,64),Color(255,60,67,86),Color(255,210,216,230),Color(255,52,60,92),rgb(0xA98EFF),rgb(0x9FB0FF),Color(255,255,196,61),
        RGB(41,46,62),RGB(228,232,242)};
    return dark?night:light;
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
std::string clipboard_text(HWND owner) {
    if(!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(owner)) throw std::runtime_error("Le presse-papiers ne contient pas de texte");
    std::wstring text;
    if(HANDLE data=GetClipboardData(CF_UNICODETEXT)) if(auto* chars=static_cast<const wchar_t*>(GlobalLock(data))){text=chars;GlobalUnlock(data);}
    CloseClipboard();
    return utf8(text);
}
void set_clipboard_text(HWND owner,const std::string& value) {
    auto text=to_native_lines(wide(value));
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
    if(!memory) throw std::runtime_error("Memoire insuffisante");
    std::copy(text.c_str(),text.c_str()+text.size()+1,static_cast<wchar_t*>(GlobalLock(memory)));GlobalUnlock(memory);
    if(!OpenClipboard(owner)){GlobalFree(memory);throw std::runtime_error("Presse-papiers indisponible");}
    EmptyClipboard();
    if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);
    CloseClipboard();
}

// Minimal modal text prompt built from an in-memory dialog template (no resource file needed).
struct Prompt { std::wstring prompt, value; };
INT_PTR CALLBACK prompt_procedure(HWND dialog,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_INITDIALOG){
        auto* p=reinterpret_cast<Prompt*>(lp);SetWindowLongPtrW(dialog,DWLP_USER,lp);
        SetDlgItemTextW(dialog,10,p->prompt.c_str());SetDlgItemTextW(dialog,11,p->value.c_str());
        SendDlgItemMessageW(dialog,11,EM_SETLIMITTEXT,200,0);SendDlgItemMessageW(dialog,11,EM_SETSEL,0,-1);
        SetFocus(GetDlgItem(dialog,11));return FALSE;
    }
    if(message==WM_COMMAND&&(LOWORD(wp)==IDOK||LOWORD(wp)==IDCANCEL)){
        if(LOWORD(wp)==IDOK){auto* p=reinterpret_cast<Prompt*>(GetWindowLongPtrW(dialog,DWLP_USER));p->value=text_of(GetDlgItem(dialog,11));}
        EndDialog(dialog,LOWORD(wp));return TRUE;
    }
    return FALSE;
}
std::optional<std::wstring> ask_text(HWND owner,const wchar_t* caption,const wchar_t* prompt,const std::wstring& initial){
    std::vector<WORD> t;
    auto word=[&](WORD w){t.push_back(w);};
    auto dword=[&](DWORD d){word(LOWORD(d));word(HIWORD(d));};
    auto str=[&](const wchar_t* s){while(*s)word(*s++);word(0);};
    auto align=[&]{if(t.size()%2)word(0);};
    dword(DS_MODALFRAME|DS_CENTER|DS_SETFONT|WS_POPUP|WS_CAPTION|WS_SYSMENU);dword(0);word(4);
    word(0);word(0);word(230);word(70);word(0);word(0);str(caption);word(9);str(L"Segoe UI");
    auto item=[&](DWORD style,short x,short y,short w,short h,WORD id,WORD klass,const wchar_t* text){
        align();dword(style|WS_CHILD|WS_VISIBLE);dword(0);word(static_cast<WORD>(x));word(static_cast<WORD>(y));word(static_cast<WORD>(w));word(static_cast<WORD>(h));
        word(id);word(0xFFFF);word(klass);str(text);word(0);
    };
    item(SS_LEFT,8,7,214,10,10,0x0082,L"");
    item(WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,8,20,214,14,11,0x0081,L"");
    item(BS_DEFPUSHBUTTON|WS_TABSTOP,118,46,50,15,IDOK,0x0080,L"OK");
    item(BS_PUSHBUTTON|WS_TABSTOP,172,46,50,15,IDCANCEL,0x0080,L"Annuler");
    Prompt data{prompt,initial};
    if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<LPCDLGTEMPLATEW>(t.data()),owner,prompt_procedure,reinterpret_cast<LPARAM>(&data))!=IDOK)return std::nullopt;
    return data.value;
}

struct Button { RectF rect; int command; };
// One step of the path taken through nested maps: the parent file and the idea that held the sub-map.
struct Crumb { fs::path file; std::wstring title; long long node; };
struct PictureStream {
    IStream* value=nullptr;
    ~PictureStream(){if(value)value->Release();}
};
struct CachedPicture {PictureStream stream;std::unique_ptr<Bitmap> bitmap;std::string encoded;};
enum TextFlags { Bold=1, Center=2, Right=4, Wrap=8, Top=16, Symbol=32 };
class App {
public:
    HWND window=nullptr, search=nullptr, title=nullptr, note=nullptr;
    HFONT edit_font=nullptr;
    HBRUSH edit_brush=nullptr;
    document::State state;
    fs::path directory, current_file;
    workspace::Store store;
    fs::path draft_file;
    std::string disk_snapshot;
    bool home=true;
    std::vector<workspace::Card> draft_cards,custom_templates;
    std::vector<fs::path> restore_paths;
    std::map<long long,std::unique_ptr<CachedPicture>> picture_cache;
    History history;
    std::vector<Button> buttons;
    std::vector<std::pair<RectF,long long>> outline_hits;
    std::vector<long long> outline_ids;
    std::set<long long> matches;
    std::vector<size_t> link_rows;
    std::map<int,std::unique_ptr<Font>> fonts;
    std::unique_ptr<Bitmap> back_buffer;
    RectF canvas, minimap_rect;
    REAL scale=1, width=1200, height=780;
    double minimap_scale=1, minimap_left=0, minimap_top=0;
    int outline_scroll=0, selected_link=-1, hovered_link=-1;
    bool updating=false, pending_edit=false, dirty=false, autosave_pending=false, dragging=false, panning=false, minimap_drag=false, moved=false, smoke=false;
    bool linking=false, tooltip=false, tracking_mouse=false, navigating=false;
    std::vector<Crumb> trail;
    std::vector<std::pair<fs::path,long long>> jumps;  // targets of the open "Cartes" / search-results menu
    fs::path jump_root;
    ULONGLONG retry_save_at=0, hover_since=0;
    long long drag_id=0, link_from=0, hovered=0;
    PointF last_mouse, pointer;
    std::string drag_before;
    ULONGLONG edit_time=0;
    std::wstring status=L"Prêt · tout reste dans le dossier de l’application";
    static constexpr long long palette[6]={0x6274E8,0x26A69A,0xDB9A36,0xCB6BA0,0x539BCB,0xA483D4};
    COLORREF custom_colors[16]{};

    explicit App(fs::path folder,bool test) : directory(std::move(folder)),store(directory,atomic_write),smoke(test) {}
    ~App(){ if(edit_font) DeleteObject(edit_font); if(edit_brush) DeleteObject(edit_brush); }
    const Theme& colors() const { return theme(state.dark); }
    // WS_CLIPCHILDREN keeps the text fields out of the parent's repaint, so they no longer flicker.
    void repaint(){ InvalidateRect(window,nullptr,FALSE); }
    void show_error(const std::string& text) {
        status=L"Attention · "+wide(text); repaint();
        if(!smoke) MessageBoxW(window,wide(text).c_str(),L"MindMap",MB_OK|MB_ICONWARNING);
    }
    // GDI+ fonts are cached by size and style instead of being rebuilt for every label of every frame.
    Font& font(REAL size,bool bold,bool symbol=false){
        const int quarter=std::max(4,static_cast<int>(std::lround(size*4)));
        const int key=quarter*4+(bold?1:0)+(symbol?2:0);
        if(auto it=fonts.find(key);it!=fonts.end())return *it->second;
        if(fonts.size()>96)fonts.clear();
        return *fonts.emplace(key,std::make_unique<Font>(symbol?L"Segoe UI Symbol":L"Segoe UI",quarter/4.0f,bold?FontStyleBold:FontStyleRegular,UnitPixel)).first->second;
    }
    void text(Graphics& g,const std::wstring& s,RectF r,REAL size,Color color,int flags=0){
        if(s.empty()||r.Width<=1||r.Height<=1)return;
        SolidBrush brush(color);StringFormat format;
        format.SetTrimming((flags&Wrap)?StringTrimmingEllipsisWord:StringTrimmingEllipsisCharacter);
        if(!(flags&Wrap))format.SetFormatFlags(StringFormatFlagsNoWrap);
        format.SetLineAlignment((flags&Top)?StringAlignmentNear:StringAlignmentCenter);
        format.SetAlignment((flags&Center)?StringAlignmentCenter:(flags&Right)?StringAlignmentFar:StringAlignmentNear);
        g.DrawString(s.c_str(),static_cast<INT>(s.size()),&font(size,flags&Bold,flags&Symbol),r,&format,&brush);
    }
    // Keeps view preferences (theme, mini-map) when another snapshot or document replaces the map.
    void adopt(document::State loaded){
        picture_cache.clear();
        loaded.dark=state.dark;loaded.minimap=state.minimap;
        state=std::move(loaded);
        if(selected_link>=static_cast<int>(state.links.size()))selected_link=-1;
    }
    void remember(std::string before) { history.remember(before); }
    void changed(const std::wstring& message=L"Modifications en cours de sauvegarde…") {
        document::prune(state);
        if(selected_link>=static_cast<int>(state.links.size()))selected_link=-1;
        dirty=true; autosave_pending=true; status=message; refresh_outline(); repaint(); update_caption();
    }
    void update_caption(){
        std::wstring caption=L"MindMap — "+wide(state.map->nodes.at(1)->title);
        if(home)caption=L"MindMap — Accueil";
        else if(current_file.empty())caption+=L" · Brouillon";
        else caption+=L" · "+current_file.filename().wstring();
        if(dirty) caption+=L" *";
        SetWindowTextW(window,caption.c_str());
    }
    void refresh_outline(){
        outline_ids.clear();matches.clear();
        const auto query=lowercase(text_of(search));
        const auto ids=query.empty()?state.map->visible():state.map->descendants(1);
        for(auto id:ids){
            auto n=state.map->nodes.at(id);
            if(query.empty()) outline_ids.push_back(id);
            else if(lowercase(wide(n->title+"\n"+n->note)).find(query)!=std::wstring::npos){outline_ids.push_back(id);matches.insert(id);}
        }
        int shown=std::max(1,static_cast<int>((height-237)/36));
        outline_scroll=std::clamp(outline_scroll,0,std::max(0,static_cast<int>(outline_ids.size())-shown));
    }
    void refresh_inspector(){
        updating=true;
        auto n=state.map->nodes.at(state.selected);
        // Rewriting an unchanged field makes it flash and moves its caret: only touch what differs.
        auto caption=wide(n->title);
        if(text_of(title)!=caption)SetWindowTextW(title,caption.c_str());
        // Native edit controls use CRLF, document notes use LF.
        auto notes=to_native_lines(wide(n->note));
        if(text_of(note)!=notes)SetWindowTextW(note,notes.c_str());
        pending_edit=false; updating=false;
    }
    // The idea and the central subject of its sub-map share one title: renaming either one renames the other.
    void push_title_to_submap(long long id,const std::string& title){
        auto it=state.submaps.find(id);
        if(it==state.submaps.end()||current_file.empty())return;
        try{
            auto file=submap::resolve(current_file,it->second);
            if(!fs::exists(file))return;
            auto child=submap::load(file);
            if(child.map->nodes.at(1)->title==title)return;
            child.map->nodes.at(1)->title=title;submap::store(file,child);
        }catch(const std::exception&){status=L"Le titre de la sous-carte n’a pas pu être mis à jour (fichier inaccessible).";}
    }
    bool commit_edit(){
        if(home)return true;
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
        const bool retitled=current->title!=t;
        const bool resized=retitled||(state.show_notes&&current->note!=n);
        state.map->rename(state.selected,t,n);
        if(retitled)push_title_to_submap(state.selected,t);
        // Sizes follow the text: re-run the layout only when a box may have grown or shrunk.
        if(resized&&!dragging)document::layout(state);
        remember(std::move(before)); pending_edit=false; changed(); return true;
    }
    void select(long long id,bool focus_canvas=true){
        if(!commit_edit()) return;
        state.selected=id; selected_link=-1;
        // Only serialize a snapshot when a collapsed ancestor really has to be opened.
        bool hidden=false;
        for(auto cursor=state.map->nodes.at(id)->parent;cursor!=-1;cursor=state.map->nodes.at(cursor)->parent)hidden|=state.map->nodes.at(cursor)->collapsed;
        if(hidden){auto before=document::encode(state);state.map->reveal(id);document::layout(state);remember(std::move(before));changed();}
        refresh_inspector(); refresh_outline(); repaint();
        if(focus_canvas) SetFocus(window);
    }
    PointF project(double x,double y) const { return {canvas.X+static_cast<REAL>(state.pan_x+x*state.zoom),canvas.Y+static_cast<REAL>(state.pan_y+y*state.zoom)}; }
    document::Point unproject(PointF p) const { return {(p.X-canvas.X-state.pan_x)/state.zoom,(p.Y-canvas.Y-state.pan_y)/state.zoom}; }
    // Where the +/− bubble sits: on the side where the children grow.
    static document::Point toggle_anchor(const document::State& doc,long long id,const document::Box& b){
        auto n=doc.map->nodes.at(id);
        if(id==1||n->children.empty())return {b.x,b.bottom()};
        auto first=doc.map->nodes.at(n->children[0]);
        if(first->x>b.right())return {b.right(),b.y};
        if(first->x<b.left())return {b.left(),b.y};
        return first->y>=b.y?document::Point{b.x,b.bottom()}:document::Point{b.x,b.top()};
    }
    static document::Point toggle_anchor(const document::State& doc,long long id){
        return toggle_anchor(doc,id,document::box(doc,id));
    }
    long long hit(PointF p) const {
        auto visible=state.map->visible();auto at=unproject(p);
        for(auto it=visible.p->rbegin();it!=visible.p->rend();++it)if(document::box(state,*it).contains(at.x,at.y))return *it;
        return 0;
    }
    int hit_link(PointF p) const {
        auto at=unproject(p);
        for(size_t i=0;i<state.links.size();++i){
            const auto& l=state.links[i];
            auto a=document::visible_anchor(state,l.from),b=document::visible_anchor(state,l.to);
            if(a!=b&&document::link_curve(document::box(state,a),document::box(state,b)).distance(at)*state.zoom<7)return static_cast<int>(i);
        }
        return -1;
    }
    void fit(){
        auto b=document::full_bounds(state);
        state.zoom=std::clamp(std::min((canvas.Width-90)/(b.right-b.left),(canvas.Height-90)/(b.bottom-b.top)),.2,1.4);
        state.pan_x=canvas.Width/2-(b.left+b.right)/2*state.zoom;
        state.pan_y=canvas.Height/2-(b.top+b.bottom)/2*state.zoom;
        autosave_pending=true; repaint();
    }
    void ensure_visible(long long id){
        auto b=document::box(state,id);auto a=project(b.left(),b.top()),z=project(b.right(),b.bottom());
        if(a.X<canvas.X+20||a.Y<canvas.Y+20||z.X>canvas.GetRight()-20||z.Y>canvas.GetBottom()-40){
            state.pan_x=canvas.Width/2-b.x*state.zoom;state.pan_y=canvas.Height/2-b.y*state.zoom;autosave_pending=true;
        }
        repaint();
    }
    void create_demo(){
        state=document::State{};
        state.map=std::make_shared<MindMap>("Mon prochain chapitre");
        state.map->rename(1,"Mon prochain chapitre","Carte d’exemple modifiable. Tab ajoute une idée, L relie deux idées, O ouvre ou crée une sous-carte.");
        auto a=state.map->add(1,"Comprendre");
        auto b=state.map->add(1,"Retenir");
        auto c=state.map->add(1,"S’entraîner");
        auto ideas=state.map->add(a,"Les grandes idées");
        auto causes=state.map->add(a,"Causes et conséquences");
        state.map->rename(causes,"Causes et conséquences","Reliez les idées entre elles : sélectionnez une idée, appuyez sur L puis cliquez sur la seconde.");
        auto dates=state.map->add(b,"Dates et repères");
        state.map->rename(dates,"Dates et repères","Ajoutez ici une explication, une date ou un exemple.\n\nDouble-cliquez sur une idée pour la modifier.");
        auto people=state.map->add(b,"Personnages clés");
        auto questions=state.map->add(c,"Questions possibles");
        state.map->rename(questions,"Questions possibles","Une idée peut contenir une sous-carte : sélectionnez-la puis appuyez sur O.");
        state.map->add(c,"Un exemple par idée");
        state.markers[dates]=5;state.markers[ideas]=1;state.markers[people]=3;
        document::add_link(state,people,causes,"influence");
        document::layout(state); state.selected=1;
    }
    void apply_theme(){
        if(edit_brush)DeleteObject(edit_brush);
        edit_brush=CreateSolidBrush(colors().edit_bg);
        BOOL dark=state.dark;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
        for(HWND control:{search,title,note})InvalidateRect(control,nullptr,TRUE);
        repaint();
    }
    void initialize(){
        scale=static_cast<REAL>(GetDpiForWindow(window))/96;
        search=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(search_id)),nullptr,nullptr);
        title=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(title_id)),nullptr,nullptr);
        note=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(note_id)),nullptr,nullptr);
        SendMessageW(search,EM_SETCUEBANNER,0,reinterpret_cast<LPARAM>(L"Rechercher une idée…"));
        SendMessageW(title,EM_SETLIMITTEXT,256,0); SendMessageW(note,EM_SETLIMITTEXT,8000,0); SendMessageW(search,EM_SETLIMITTEXT,200,0);
        home=!smoke;
        if(smoke)create_demo();
        else try{store.load();refresh_home();status=L"Choisissez une carte ou un modèle pour commencer.";}
             catch(const std::exception& e){status=L"Liste des cartes indisponible · "+wide(e.what());}
        apply_theme();
        resize(); refresh_inspector(); refresh_outline(); update_caption();
        show_editors(!home);
        if(smoke)fit();
        if(!smoke){
            int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
            if(argv&&argc>1){
                try{import_path(fs::path(argv[1]));}
                catch(const std::exception& e){status=L"Ouverture impossible · "+wide(e.what());}
            }
            if(argv)LocalFree(argv);
        }
        SetTimer(window,autosave_timer,200,nullptr);
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
        place(title,static_cast<int>(width)-270,194,242,34);
        place(note,static_cast<int>(width)-270,524,242,std::max(60,static_cast<int>(height)-96-524));
        back_buffer.reset();
        refresh_outline();repaint();
    }
    void draw_map(Graphics& g,const document::State& doc,RectF area,double zoom,double panx,double pany,bool interactive){
        const Theme& t=interactive?colors():theme(false);
        const REAL z=static_cast<REAL>(zoom);
        // Grid-fitted text grows wider at small zooms; plain anti-aliasing scales linearly with the boxes.
        const auto hint=g.GetTextRenderingHint();g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        auto visible=doc.map->visible();
        struct NodeGeometry { document::Box box; document::Metrics metrics; };
        std::unordered_map<long long,NodeGeometry> boxes;
        boxes.reserve(visible.size());
        for(auto id:visible){
            auto n=doc.map->nodes.at(id);auto m=document::metrics(doc,id);
            boxes.emplace(id,NodeGeometry{{n->x,n->y,m.w,m.h},m});
        }
        auto at=[&](document::Point p){return PointF(area.X+static_cast<REAL>(panx+p.x*zoom),area.Y+static_cast<REAL>(pany+p.y*zoom));};
        auto outside=[&](const document::Curve& c){
            auto a=at(c.a),b=at(c.b),k=at(c.c),d=at(c.d);
            return std::max({a.X,b.X,k.X,d.X})<area.X||std::min({a.X,b.X,k.X,d.X})>area.GetRight()||std::max({a.Y,b.Y,k.Y,d.Y})<area.Y||std::min({a.Y,b.Y,k.Y,d.Y})>area.GetBottom();
        };
        for(auto id:visible){
            auto n=doc.map->nodes.at(id);
            if(n->parent==-1)continue;
            auto c=document::connector(boxes.at(n->parent).box,boxes.at(id).box);
            if(outside(c))continue;
            Pen line(rgb(n->color,doc.dark&&interactive?170:140),std::max(1.0f,2.2f*z));
            g.DrawBezier(&line,at(c.a),at(c.b),at(c.c),at(c.d));
        }
        // Cross links: dashed arcs with an arrow, drawn under the ideas; labels are drawn on top later.
        std::vector<std::pair<PointF,size_t>> labels;
        for(size_t i=0;i<doc.links.size();++i){
            const auto& l=doc.links[i];
            auto a=document::visible_anchor(doc,l.from),b=document::visible_anchor(doc,l.to);
            if(a==b||!boxes.count(a)||!boxes.count(b))continue;
            auto c=document::link_curve(boxes.at(a).box,boxes.at(b).box);
            const bool chosen=interactive&&static_cast<int>(i)==selected_link, hover=interactive&&static_cast<int>(i)==hovered_link;
            if(!outside(c)){
                Pen pen(chosen?t.selection:Color(hover?255:210,t.link.GetR(),t.link.GetG(),t.link.GetB()),std::max(1.2f,(chosen?3.4f:hover?2.8f:2.0f)*z));
                pen.SetDashStyle(chosen?DashStyleSolid:DashStyleDash);
                AdjustableArrowCap arrow(4.2f,4.2f,TRUE);pen.SetCustomEndCap(&arrow);
                g.DrawBezier(&pen,at(c.a),at(c.b),at(c.c),at(c.d));
            }
            if(!l.label.empty())labels.emplace_back(at(c.at(.5)),i);
        }
        for(auto id:visible){
            auto n=doc.map->nodes.at(id);const auto& b=boxes.at(id).box;
            auto corner=at({b.left(),b.top()});
            RectF r(corner.X,corner.Y,static_cast<REAL>(b.w*zoom),static_cast<REAL>(b.h*zoom));
            if(r.GetRight()+30*z<area.X || r.X-30*z>area.GetRight() || r.GetBottom()+30*z<area.Y || r.Y-30*z>area.GetBottom())continue;
            const bool root=id==1;
            const bool chosen=interactive&&id==doc.selected&&selected_link<0;
            if(interactive&&matches.count(id))panel(g,RectF(r.X-6*z,r.Y-6*z,r.Width+12*z,r.Height+12*z),Color(110,t.highlight.GetR(),t.highlight.GetG(),t.highlight.GetB()),Color(0,0,0,0),16*z);
            panel(g,RectF(r.X+1*z,r.Y+3*z,r.Width,r.Height),Color(doc.dark&&interactive?60:15,10,20,45),Color(0,0,0,0),12*z);
            Color fill=root?rgb(n->color):t.node;
            panel(g,r,fill,chosen?t.selection:rgb(n->color,190),12*z,chosen?3*z:1.5f*z);
            {
                if(!root){SolidBrush accent(rgb(n->color));g.FillRectangle(&accent,r.X+10*z,r.Y+14*z,3*z,r.Height-28*z);}
                const auto& m=boxes.at(id).metrics;
                const REAL top=r.Y+static_cast<REAL>((b.h-m.title_h-m.note_h-m.picture_h)/2*zoom);
                text(g,wide(n->title),RectF(r.X+16*z,top,r.Width-32*z,static_cast<REAL>(m.title_h*zoom)+2*z),(root?16:14)*z,root?Color(255,255,255,255):t.node_text,(root?Bold:0)|Center|Wrap);
                if(m.note_h>0)text(g,wide(n->note),RectF(r.X+18*z,top+static_cast<REAL>(m.title_h*zoom)+3*z,r.Width-36*z,static_cast<REAL>((m.note_h-6)*zoom)),12*z,root?Color(235,255,255,255):t.note_text,Wrap|Top);
                else if(!n->note.empty()){SolidBrush dot(root?Color(255,255,255,255):rgb(n->color));g.FillEllipse(&dot,r.GetRight()-13*z,r.GetBottom()-13*z,5*z,5*z);}
                if(auto p=doc.pictures.find(id);p!=doc.pictures.end()){
                    const REAL ih=static_cast<REAL>((m.picture_h-10)*zoom),iw=static_cast<REAL>(std::min(b.w-36,(m.picture_h-10)*p->second.width/p->second.height)*zoom);
                    try{auto& image=picture(id,p->second);g.DrawImage(&image,RectF(r.X+(r.Width-iw)/2,r.GetBottom()-13*z-ih,iw,ih));}
                    catch(const std::exception&){text(g,L"Image illisible",RectF(r.X,r.GetBottom()-13*z-ih,r.Width,ih),12*z,t.subtle,Center);}
                }
            }
            if(int marker=document::marker_of(doc,id)){
                const auto& info=document::marker_info(marker);
                RectF badge(r.X-5*z,r.Y-5*z,22*z,22*z);
                SolidBrush fillb(rgb(info.color));Pen ring(t.node,2*z);g.FillEllipse(&fillb,badge);g.DrawEllipse(&ring,badge);
                text(g,wide(info.glyph),RectF(badge.X,badge.Y+0.5f*z,badge.Width,badge.Height),12*z,Color(255,255,255,255),Bold|Center|Symbol);
            }
            if(doc.submaps.count(id)){
                RectF tag(r.GetRight()-17*z,r.Y-5*z,22*z,22*z);
                SolidBrush fillb(rgb(n->color));Pen ring(t.node,2*z);g.FillEllipse(&fillb,tag);g.DrawEllipse(&ring,tag);
                text(g,L"▶",RectF(tag.X+1*z,tag.Y+0.5f*z,tag.Width,tag.Height),9*z,Color(255,255,255,255),Bold|Center|Symbol);
            }
            if(!n->children.empty()){
                auto anchor=at(toggle_anchor(doc,id,b));
                std::wstring sign=n->collapsed?L"+"+std::to_wstring(doc.map->descendants(id).size()-1):L"−";
                REAL w=(n->collapsed&&sign.size()>2?30:20)*z;
                panel(g,RectF(anchor.X-w/2,anchor.Y-10*z,w,20*z),t.node,rgb(n->color),10*z);
                text(g,sign,RectF(anchor.X-w/2,anchor.Y-10*z,w,20*z),(n->collapsed?10:12)*z,rgb(n->color),Bold|Center);
            }
        }
        for(const auto& [mid,i]:labels){
            auto label=wide(doc.links[i].label);
            RectF measure;g.MeasureString(label.c_str(),static_cast<INT>(label.size()),&font(12*z,false),PointF(0,0),&measure);
            RectF pill(mid.X-measure.Width/2-8*z,mid.Y-11*z,measure.Width+16*z,22*z);
            if(!pill.IntersectsWith(area))continue;
            const bool chosen=interactive&&static_cast<int>(i)==selected_link;
            panel(g,pill,chosen?t.selection:t.node,t.link,11*z,1.2f*z);
            text(g,label,pill,12*z,chosen?Color(255,255,255,255):t.link,Center);
        }
        g.SetTextRenderingHint(hint);
    }
    void draw_minimap(Graphics& g){
        minimap_rect=RectF(0,0,0,0);
        if(!state.minimap||canvas.Width<420||canvas.Height<300||state.map->nodes.size()<2)return;
        const Theme& t=colors();
        minimap_rect=RectF(canvas.GetRight()-196,canvas.GetBottom()-166,182,126);
        panel(g,minimap_rect,Color(235,t.panel.GetR(),t.panel.GetG(),t.panel.GetB()),t.border,10);
        auto b=document::full_bounds(state);
        minimap_scale=std::min((minimap_rect.Width-16)/(b.right-b.left),(minimap_rect.Height-16)/(b.bottom-b.top));
        minimap_left=b.left-((minimap_rect.Width-16)/minimap_scale-(b.right-b.left))/2;
        minimap_top=b.top-((minimap_rect.Height-16)/minimap_scale-(b.bottom-b.top))/2;
        auto at=[&](double x,double y){return PointF(minimap_rect.X+8+static_cast<REAL>((x-minimap_left)*minimap_scale),minimap_rect.Y+8+static_cast<REAL>((y-minimap_top)*minimap_scale));};
        GraphicsState saved=g.Save();g.SetClip(minimap_rect);
        for(auto id:state.map->visible()){
            auto box=document::box(state,id);auto a=at(box.left(),box.top());
            SolidBrush brush(rgb(state.map->nodes.at(id)->color,id==state.selected?255:170));
            g.FillRectangle(&brush,a.X,a.Y,std::max(2.0f,static_cast<REAL>(box.w*minimap_scale)),std::max(2.0f,static_cast<REAL>(box.h*minimap_scale)));
        }
        auto from=unproject(PointF(canvas.X,canvas.Y)),to=unproject(PointF(canvas.GetRight(),canvas.GetBottom()));
        auto a=at(from.x,from.y),z=at(to.x,to.y);
        SolidBrush shade(Color(30,t.selection.GetR(),t.selection.GetG(),t.selection.GetB()));Pen frame(t.selection,1.5f);
        g.FillRectangle(&shade,a.X,a.Y,z.X-a.X,z.Y-a.Y);g.DrawRectangle(&frame,a.X,a.Y,z.X-a.X,z.Y-a.Y);
        g.Restore(saved);
    }
    void minimap_jump(PointF p){
        const double x=minimap_left+(p.X-minimap_rect.X-8)/minimap_scale,y=minimap_top+(p.Y-minimap_rect.Y-8)/minimap_scale;
        state.pan_x=canvas.Width/2-x*state.zoom;state.pan_y=canvas.Height/2-y*state.zoom;autosave_pending=true;repaint();
    }
    void draw_tooltip(Graphics& g){
        if(!tooltip||!hovered||dragging||panning||linking||!state.map->nodes.count(hovered))return;
        auto n=state.map->nodes.at(hovered);
        if(n->note.empty())return;
        const Theme& t=colors();
        auto body=wide(n->note),head=wide(n->title);
        RectF measure;
        g.MeasureString(body.c_str(),static_cast<INT>(body.size()),&font(13,false),RectF(0,0,296,600),&measure);
        REAL h=std::min(360.0f,measure.Height+52);
        REAL x=std::min(pointer.X+18,canvas.GetRight()-330),y=std::min(pointer.Y+18,canvas.GetBottom()-h-8);
        RectF card(std::max(canvas.X+6,x),std::max(canvas.Y+6,y),320,h);
        panel(g,RectF(card.X+2,card.Y+4,card.Width,card.Height),Color(40,0,0,0),Color(0,0,0,0),12);
        panel(g,card,t.panel,rgb(n->color),12,1.5f);
        text(g,head,RectF(card.X+12,card.Y+8,296,24),14,t.title,Bold);
        text(g,body,RectF(card.X+12,card.Y+36,296,h-44),13,t.text,Wrap|Top);
    }
    void button(Graphics& g,REAL x,REAL y,REAL w,const std::wstring& name,int command,bool primary=false,bool active=false,bool enabled=true,REAL h=32){
        const Theme& t=colors();
        RectF rect(x,y,w,h);buttons.push_back({rect,command});
        Color fill=primary||active?rgb(active&&!primary?0x4E5FC9:0x6274E8):t.button;
        panel(g,rect,fill,t.button_border,8);
        Color ink=primary||active?Color(255,255,255,255):enabled?t.button_text:Color(255,t.subtle.GetR(),t.subtle.GetG(),t.subtle.GetB());
        text(g,name,rect,12,ink,(primary?Bold:0)|Center);
    }
    void paint_inspector(Graphics& g){
        const Theme& t=colors();const REAL x=width-270;
        auto selected=state.map->nodes.at(state.selected);
        text(g,L"IDÉE SÉLECTIONNÉE",RectF(x,140,242,25),11,t.subtle,Bold);
        text(g,L"Titre",RectF(x,170,242,22),12,t.text,Bold);
        button(g,x,238,78,L"Dupliquer",Duplicate,false,false,state.selected!=1,30);
        button(g,x+82,238,78,selected->collapsed?L"Ouvrir":L"Replier",Collapse,false,false,!selected->children.empty(),30);
        button(g,x+164,238,78,L"Supprimer",Delete,false,false,state.selected!=1,30);
        text(g,L"Couleur de la branche",RectF(x,274,242,22),12,t.text,Bold);
        for(int i=0;i<6;++i){
            RectF r(x+i*34.0f,298,26,26);buttons.push_back({r,Color0+i});
            panel(g,r,rgb(palette[i]),selected->color==palette[i]?t.title:Color(0,0,0,0),8,2);
        }
        RectF custom(x+204,298,26,26);buttons.push_back({custom,CustomColor});
        bool own=std::find(std::begin(palette),std::end(palette),selected->color)==std::end(palette);
        panel(g,custom,own?rgb(selected->color):t.button,own?t.title:t.button_border,8,own?2.0f:1.0f);
        text(g,L"…",custom,13,own?Color(255,255,255,255):t.button_text,Bold|Center);
        const int marker=document::marker_of(state,state.selected);
        text(g,L"Repère",RectF(x,330,80,22),12,t.text,Bold);
        text(g,wide(document::marker_info(marker).name),RectF(x+70,330,172,22),11,t.subtle,Right);
        for(int i=0;i<document::marker_count;++i){
            RectF r(x+i*41.0f,354,37,28);buttons.push_back({r,Marker0+i});
            const auto& info=document::marker_info(i);
            panel(g,r,marker==i?rgb(info.color):t.button,marker==i?rgb(info.color):t.button_border,8);
            text(g,i?wide(info.glyph):L"∅",r,14,marker==i?Color(255,255,255,255):rgb(info.color),Bold|Center|Symbol);
        }
        text(g,L"Liens vers d’autres idées",RectF(x,390,160,24),12,t.text,Bold);
        button(g,x+168,390,74,linking?L"Annuler":L"+ Relier",LinkMode,false,linking,true,24);
        link_rows.clear();
        for(size_t i=0;i<state.links.size();++i)if(state.links[i].from==state.selected||state.links[i].to==state.selected)link_rows.push_back(i);
        for(size_t row=0;row<std::min<size_t>(2,link_rows.size());++row){
            const auto& l=state.links[link_rows[row]];
            RectF r(x,418+row*24.0f,242,22);
            if(row==1&&link_rows.size()>2){text(g,L"+ "+std::to_wstring(link_rows.size()-1)+L" autres liens (voir la carte)",r,11,t.subtle);break;}
            const bool chosen=static_cast<int>(link_rows[row])==selected_link;
            panel(g,r,chosen?t.row:t.button,Color(0,0,0,0),6);
            buttons.push_back({RectF(x,r.Y,214,22),static_cast<int>(LinkRow0+row)});
            buttons.push_back({RectF(x+216,r.Y,26,22),static_cast<int>(LinkRowDelete0+row)});
            const bool outgoing=l.from==state.selected;
            std::wstring caption=(outgoing?L"→ ":L"← ")+wide(state.map->nodes.at(outgoing?l.to:l.from)->title);
            if(!l.label.empty())caption+=L"  ·  "+wide(l.label);
            text(g,caption,RectF(x+8,r.Y,206,22),11,t.link);
            text(g,L"×",RectF(x+216,r.Y,26,22),14,t.subtle,Center);
        }
        if(link_rows.empty())text(g,L"Aucun lien · L puis clic sur une autre idée",RectF(x,420,242,25),11,t.subtle);
        if(state.submaps.count(state.selected)){
            button(g,x,467,166,L"▶ Ouvrir la sous-carte",OpenSubmap,true,false,true,22);
            button(g,x+170,467,72,L"Détacher",DetachSubmap,false,false,true,22);
        }else button(g,x,467,242,L"+ Créer une sous-carte",CreateSubmap,false,false,state.selected!=1,22);
        text(g,L"Notes",RectF(x,500,80,20),12,t.text,Bold);
        button(g,x+76,493,116,state.pictures.count(state.selected)?L"Changer l’image":L"+ Image / schéma",AddImage,false,false,true,26);
        if(state.pictures.count(state.selected))button(g,x+198,493,44,L"×",RemoveImage,false,false,true,26);
        button(g,x,height-84,242,L"Appliquer les modifications",Apply,true);
    }
    void paint_home(Graphics& g){
        const auto& t=colors();buttons.clear();outline_hits.clear();minimap_rect=RectF(0,0,0,0);
        text(g,L"MindMap",RectF(44,28,300,45),30,t.title,Bold);
        text(g,L"Choisissez une carte. Vos brouillons et versions sont conservés automatiquement.",RectF(44,80,width-88,28),14,t.subtle);
        button(g,44,126,170,L"+ Nouvelle carte",New,true);
        button(g,226,126,170,L"Ouvrir une carte…",Open);
        button(g,408,126,170,L"Importer un fichier…",ImportOutline);
        button(g,width-202,38,74,state.dark?L"Clair":L"Sombre",ToggleDark);
        button(g,width-116,38,74,L"Aide",Help);
        text(g,L"PARTIR D’UN MODÈLE",RectF(44,184,width-88,22),11,t.subtle,Bold);
        const REAL model_w=(width-112)/5;
        for(size_t i=0;i<workspace::models().size();++i){
            const auto& model=workspace::models()[i];REAL x=44+i*(model_w+6);
            RectF card(x,216,model_w,108);panel(g,card,t.panel,t.border,12);buttons.push_back({card,static_cast<int>(Template0+i)});
            text(g,wide(model.name),RectF(x+14,230,model_w-28,30),14,t.title,Bold);
            text(g,wide(model.description),RectF(x+14,266,model_w-28,44),12,t.subtle,Wrap|Top);
        }
        const REAL column=(width-108)/2;
        text(g,L"CARTES RÉCENTES",RectF(44,350,column,24),11,t.subtle,Bold);
        for(size_t i=0;i<std::min<size_t>(4,store.recent.size());++i){
            const auto& path=store.recent[i];RectF row(44,384+i*52,column,46);
            panel(g,row,t.panel,t.border,8);buttons.push_back({row,static_cast<int>(Recent0+i)});
            text(g,path.filename().wstring(),RectF(58,row.Y+3,column-28,23),13,t.title,Bold);
            text(g,path.parent_path().wstring(),RectF(58,row.Y+25,column-28,16),10,t.subtle);
        }
        if(store.recent.empty())text(g,L"Les cartes que vous ouvrez apparaîtront ici.",RectF(44,386,column,40),13,t.subtle,Wrap);
        if(store.recent.size()>4)button(g,44,596,210,L"Toutes les cartes récentes…",BrowseRecent);
        const REAL right=64+column;
        text(g,L"BROUILLONS À REPRENDRE",RectF(right,350,column,24),11,t.subtle,Bold);
        for(size_t i=0;i<std::min<size_t>(3,draft_cards.size());++i){
            RectF row(right,384+i*52,column,46);panel(g,row,t.panel,t.border,8);buttons.push_back({row,static_cast<int>(Draft0+i)});
            text(g,wide(draft_cards[i].title),RectF(right+14,row.Y+3,column-28,23),13,t.title,Bold);
            text(g,L"Brouillon · nom et emplacement à choisir",RectF(right+14,row.Y+25,column-28,16),10,t.subtle);
        }
        if(draft_cards.empty())text(g,L"Une nouvelle carte est protégée ici jusqu’à son premier enregistrement.",RectF(right,386,column,54),13,t.subtle,Wrap|Top);
        if(draft_cards.size()>3)button(g,right,545,210,L"Voir tous les brouillons…",BrowseDrafts);
        if(!store.legacy_migrated&&fs::exists(directory/L"session.mindmap"))button(g,right,588,column,L"Récupérer l’ancienne session",RecoverLegacy);
        if(!custom_templates.empty()){
            text(g,L"MES MODÈLES",RectF(44,608,column,20),11,t.subtle,Bold);
            for(size_t i=0;i<std::min<size_t>(3,custom_templates.size());++i)
                button(g,44+i*(column/3+2),636,column/3-4,wide(custom_templates[i].title),static_cast<int>(CustomTemplate0+i));
        }
        if(custom_templates.size()>3)button(g,width-260,636,216,L"Tous mes modèles…",BrowseTemplates);
        text(g,status,RectF(44,height-32,width-88,24),11,t.subtle);
    }
    void paint(HDC dc){
        RECT client;GetClientRect(window,&client);
        // The back buffer is reused between frames instead of allocating a full-window bitmap each time.
        if(!back_buffer||back_buffer->GetWidth()!=static_cast<UINT>(std::max(1L,client.right))||back_buffer->GetHeight()!=static_cast<UINT>(std::max(1L,client.bottom)))
            back_buffer=std::make_unique<Bitmap>(std::max(1L,client.right),std::max(1L,client.bottom),PixelFormat32bppPARGB);
        const Theme& t=colors();
        Graphics g(back_buffer.get());g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.ScaleTransform(scale,scale);
        g.Clear(t.bg);
        if(home){paint_home(g);Graphics target(dc);target.DrawImage(back_buffer.get(),0,0);return;}
        SolidBrush header(t.header);g.FillRectangle(&header,0.0f,0.0f,width,120.0f);
        panel(g,RectF(24,20,36,36),rgb(0x6274E8),Color(0,0,0,0),11);
        text(g,L"M",RectF(24,20,36,36),22,Color(255,255,255,255),Bold|Center);
        text(g,L"MindMap",RectF(72,14,185,34),24,t.title,Bold);
        text(g,current_file.empty()?L"Brouillon · Enregistrer pour choisir son emplacement":current_file.wstring(),RectF(73,46,width-300,22),12,t.subtle);
        buttons.clear();
        text(g,L"PORTABLE  •  SANS COMPTE",RectF(width-480,24,290,26),11,t.subtle,Right);
        button(g,width-178,22,74,state.dark?L"Clair":L"Sombre",ToggleDark,false,false,true,30);
        button(g,width-96,22,68,L"Aide",Help,false,false,true,30);
        button(g,24,79,72,L"Fichier ▾",0);
        button(g,104,79,106,L"+ Idée enfant",Add,true);
        button(g,218,79,84,L"Même niveau",Sibling);
        button(g,310,79,64,L"Annuler",Undo,false,false,history.can_undo());
        button(g,380,79,64,L"Rétablir",Redo,false,false,history.can_redo());
        button(g,452,79,90,L"Réorganiser",Layout);
        button(g,550,79,92,L"Affichage ▾",-1);
        button(g,650,79,64,L"Centrer",Fit);
        button(g,722,79,64,L"Relier",LinkMode,false,linking);
        button(g,794,79,86,L"Enregistrer",Save);
        button(g,886,79,70,L"Accueil",Home);
        button(g,964,79,82,L"Cartes ▾",-2);
        panel(g,RectF(16,132,210,height-177),t.panel,t.border,12);
        text(g,L"PLAN DE LA CARTE",RectF(28,133,186,18),10,t.subtle,Bold);
        outline_hits.clear();
        int visible_rows=std::max(0,static_cast<int>((height-237)/36));
        const bool searching=!text_of(search).empty();
        for(int row=0;row<visible_rows;++row){
            size_t index=static_cast<size_t>(row+outline_scroll);if(index>=outline_ids.size())break;
            long long id=outline_ids[index];auto n=state.map->nodes.at(id);
            REAL y=195+row*36.0f;RectF r(24,y,194,33);outline_hits.emplace_back(r,id);
            if(id==state.selected)panel(g,r,t.row,Color(0,0,0,0),7);
            REAL indent=searching?0.0f:static_cast<REAL>(std::min(5LL,state.map->depth(id)))*10;
            if(int marker=document::marker_of(state,id)){
                const auto& info=document::marker_info(marker);SolidBrush b(rgb(info.color));g.FillEllipse(&b,28+indent,y+9,14.0f,14.0f);
                text(g,wide(info.glyph),RectF(28+indent,y+9,14,14),9,Color(255,255,255,255),Bold|Center|Symbol);
            }else{SolidBrush color(rgb(n->color));g.FillEllipse(&color,32+indent,y+13,6.0f,6.0f);}
            text(g,wide(n->title),RectF(47+indent,y,161-indent,33),12,t.text);
        }
        if(outline_ids.empty())text(g,L"Aucun résultat",RectF(32,206,176,32),13,t.subtle);
        GraphicsState clip=g.Save();g.SetClip(canvas);
        REAL step=std::max(18.0f,static_cast<REAL>(28*state.zoom));
        REAL gx=canvas.X+static_cast<REAL>(std::fmod(state.pan_x,step)),gy=canvas.Y+static_cast<REAL>(std::fmod(state.pan_y,step));
        SolidBrush grid(t.grid);
        for(REAL x=gx;x<canvas.GetRight();x+=step)for(REAL y=gy;y<canvas.GetBottom();y+=step)g.FillEllipse(&grid,x,y,1.5f,1.5f);
        draw_map(g,state,canvas,state.zoom,state.pan_x,state.pan_y,true);
        if(linking&&state.map->nodes.count(link_from)){
            auto b=document::box(state,link_from);Pen band(t.link,2);band.SetDashStyle(DashStyleDash);
            AdjustableArrowCap arrow(4,4,TRUE);band.SetCustomEndCap(&arrow);
            g.DrawLine(&band,project(b.x,b.y),pointer);
        }
        draw_minimap(g);
        draw_tooltip(g);
        g.Restore(clip);
        const wchar_t* hint=linking?L"Cliquez sur l’idée à relier   ·   Échap : annuler"
            :L"Tab : enfant   Entrée : même niveau   Flèches : naviguer   1-5 : repères   L : relier   O : sous-carte";
        text(g,hint,RectF(canvas.X+12,canvas.GetBottom()-26,canvas.Width-24,24),10,t.subtle,Center);
        if(!trail.empty()){
            std::wstring path;for(const auto& c:trail)path+=c.title+L"  ›  ";path+=wide(state.map->nodes.at(1)->title);
            button(g,canvas.X+10,canvas.Y+8,104,L"↑ Remonter",UpSubmap,false,false,true,28);
            text(g,path,RectF(canvas.X+122,canvas.Y+8,std::max(60.0f,canvas.Width-132),28),12,t.subtle);
        }
        panel(g,RectF(width-286,132,270,height-177),t.panel,t.border,12);
        paint_inspector(g);
        std::wstring count=std::to_wstring(state.map->nodes.size())+L" idées  •  "+std::to_wstring(state.links.size())+L" liens  •  "+std::to_wstring(static_cast<int>(state.zoom*100))+L" %";
        text(g,count,RectF(22,height-31,230,25),11,t.subtle);
        text(g,status,RectF(265,height-31,width-285,25),11,t.subtle);
        Graphics target(dc);target.DrawImage(back_buffer.get(),0,0);
    }
    fs::path dialog(bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* suggestion,const fs::path& start={}){
        wchar_t filename[32768]{};
        auto folder=!start.empty()?start:current_file.empty()?directory:current_file.parent_path();
        std::wstring initial=folder.wstring();
        if(save){auto proposed=folder/suggestion;wcsncpy(filename,proposed.c_str(),32767);}
        OPENFILENAMEW settings{};settings.lStructSize=sizeof(settings);settings.hwndOwner=window;
        settings.lpstrFilter=filter;settings.lpstrFile=filename;settings.nMaxFile=32768;
        settings.lpstrInitialDir=initial.c_str();settings.lpstrDefExt=extension;
        settings.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(save?GetSaveFileNameW(&settings):GetOpenFileNameW(&settings))return fs::path(filename);
        return {};
    }
    bool save(bool choose){
        if(home)return false;
        if(!commit_edit())return false;
        auto path=current_file;
        if(choose||path.empty()){
            auto name=wide(state.map->nodes.at(1)->title);for(auto& ch:name)if(wcschr(L"<>:\"/\\|?*",ch)||ch<32)ch=L'_';
            while(!name.empty()&&(name.back()==L'.'||name.back()==L' '))name.pop_back();
            if(name.empty())name=L"Ma carte";
            name=name.substr(0,120)+L".mindmap";
            path=dialog(true,L"Carte mentale (*.mindmap)\0*.mindmap\0\0",L"mindmap",name.c_str());
        }
        if(path.empty())return false;
        auto data=document::encode(state);auto expected=path==current_file&&!disk_snapshot.empty()?&disk_snapshot:nullptr;
        store.save_named(path,data,expected,true);current_file=path;disk_snapshot=data;dirty=false;retry_save_at=0;autosave_pending=false;
        store.forget_draft(draft_file);draft_file.clear();
        status=L"Carte enregistrée · "+path.wstring();
        try{store.opened(path);}catch(const std::exception& e){status+=L" · liste des cartes non enregistrée : "+wide(e.what());}
        update_caption();repaint();return true;
    }
    bool may_replace(){
        if(!commit_edit())return false;
        checkpoint();return true;
    }
    void loaded_document(bool is_dirty){
        linking=false;selected_link=-1;if(!navigating)trail.clear();
        updating=true;SetWindowTextW(search,L"");updating=false;outline_scroll=0;retry_save_at=0;
        home=false;show_editors(true);picture_cache.clear();
        history.clear();dirty=is_dirty;autosave_pending=true;refresh_inspector();refresh_outline();update_caption();repaint();
    }
    void open_path(const fs::path& path){
        auto data=read_file(path);auto loaded=document::decode(data);
        adopt(std::move(loaded));current_file=fs::absolute(path).lexically_normal();disk_snapshot=data;draft_file.clear();loaded_document(false);
        status=L"Carte ouverte · "+current_file.wstring();
        try{store.opened(current_file);}catch(const std::exception& e){status+=L" · liste des cartes indisponible : "+wide(e.what());}
        repaint();
    }
    void import_path(const fs::path& path){
        auto ext=lowercase(path.extension().wstring());if(ext==L".mindmap"){open_path(path);fit();return;}
        auto imported=document::import_map(read_file(path),utf8(ext),utf8(path.stem().wstring()));
        document::encode(imported);
        adopt(std::move(imported));current_file.clear();disk_snapshot.clear();draft_file.clear();loaded_document(true);fit();
        status=L"Carte importée · brouillon protégé automatiquement · Enregistrer pour choisir son nom";repaint();
    }
    void checkpoint(){
        if(home||!commit_edit()||!autosave_pending)return;
        auto data=document::encode(state);
        if(current_file.empty()){
            if(!dirty){autosave_pending=false;return;}
            if(draft_file.empty())draft_file=store.new_draft();
            store.draft(draft_file,data);
            status=L"Brouillon protégé · Enregistrer pour choisir un nom et un emplacement";
        }else{
            try{store.save_named(current_file,data,disk_snapshot.empty()?nullptr:&disk_snapshot,false);}
            catch(const std::exception&){
                if(draft_file.empty())draft_file=store.new_draft();
                store.draft(draft_file,data);throw;
            }
            disk_snapshot=data;dirty=false;status=L"Enregistré automatiquement · "+current_file.wstring();
            store.forget_draft(draft_file);draft_file.clear();
        }
        autosave_pending=false;update_caption();repaint();
    }
    // Sub-maps: each is an ordinary .mindmap file next to the parent; trail remembers the way back up.
    void open_submap(){
        auto it=state.submaps.find(state.selected);
        if(it==state.submaps.end()){create_submap();return;}
        if(!may_replace())return;
        if(current_file.empty()){status=L"Enregistrez d’abord cette carte pour ouvrir ses sous-cartes.";repaint();return;}
        auto child=submap::resolve(current_file,it->second);
        if(!fs::exists(child)){repair_submap(child);return;}
        Crumb crumb{current_file,wide(state.map->nodes.at(1)->title),state.selected};
        navigating=true;
        try{open_path(child);}catch(...){navigating=false;throw;}
        navigating=false;trail.push_back(std::move(crumb));fit();update_caption();repaint();
    }
    // Topmost map of the hierarchy being explored: searches and the map list start from there.
    fs::path hierarchy_root() const { return trail.empty()?current_file:trail.front().file; }
    // Opens any map of the hierarchy below `root` directly, rebuilding the breadcrumb to it, and selects an idea.
    void jump_to(const fs::path& root,const fs::path& file,long long node){
        if(!may_replace())return;
        auto way=submap::chain(root,file);
        if(way.empty())throw std::runtime_error("Cette carte ne fait plus partie de la hiérarchie : "+workspace::path_text(file));
        std::vector<Crumb> next;
        for(size_t i=0;i+1<way.size();++i)next.push_back({way[i].file,wide(way[i].title),way[i+1].via});
        if(!submap::same(file,current_file)){
            navigating=true;
            try{open_path(file);}catch(...){navigating=false;throw;}
            navigating=false;fit();
        }
        trail=std::move(next);
        if(state.map->nodes.count(node)){select(node);ensure_visible(node);}
        update_caption();repaint();
    }
    std::wstring clipped(const std::wstring& text,size_t limit) const { return text.size()>limit?text.substr(0,limit-1)+L"…":text; }
    // "Cartes" menu: search everywhere, then every map of the hierarchy (indented) to jump to.
    void cards_menu(POINT at){
        if(home||!commit_edit())return;
        if(current_file.empty()){status=L"Enregistrez d’abord cette carte pour explorer ses sous-cartes.";repaint();return;}
        checkpoint();
        jump_root=hierarchy_root();jumps.clear();
        HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING,GlobalSearch,L"Rechercher dans toutes les cartes…\tCtrl+Maj+F");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        for(const auto& entry:submap::hierarchy(jump_root)){
            if(jumps.size()>=200)break;
            std::wstring label=std::wstring(static_cast<size_t>(entry.depth)*3,L' ')+(entry.depth?L"▸ ":L"")+clipped(wide(entry.title),60);
            if(entry.status!=submap::Status::Ok)label+=L"  ⚠ "+wide(submap::status_name(entry.status));
            UINT flags=MF_STRING|(entry.status==submap::Status::Ok?0:MF_GRAYED)|(submap::same(entry.file,current_file)?MF_CHECKED:0);
            jumps.emplace_back(entry.file,1);AppendMenuW(menu,flags,Jump0+jumps.size()-1,label.c_str());
        }
        if(int result=popup(menu,at))command(result);
    }
    void show_hits(const std::vector<submap::Hit>& hits,POINT at){
        jump_root=hierarchy_root();jumps.clear();
        HMENU menu=CreatePopupMenu();
        for(const auto& hit:hits){
            if(jumps.size()>=30)break;
            jumps.emplace_back(hit.file,hit.id);
            AppendMenuW(menu,MF_STRING,Jump0+jumps.size()-1,clipped(wide(hit.path+"  ›  "+hit.title),100).c_str());
        }
        if(int result=popup(menu,at))command(result);
    }
    void global_search(){
        if(current_file.empty()){status=L"Enregistrez d’abord cette carte pour chercher dans ses sous-cartes.";repaint();return;}
        if(!may_replace())return;
        auto answer=ask_text(window,L"Rechercher dans toutes les cartes",L"Mots à chercher dans les idées et les notes de cette carte et de toutes ses sous-cartes :",L"");
        if(!answer)return;
        auto query=document::trim(utf8(*answer));if(query.empty())return;
        auto hits=submap::search(hierarchy_root(),query,30);
        if(hits.empty()){status=L"Aucun résultat pour « "+wide(query)+L" » dans toutes les cartes.";repaint();return;}
        status=std::to_wstring(hits.size())+L" résultat(s) pour « "+wide(query)+L" »";repaint();
        POINT at{};GetCursorPos(&at);show_hits(hits,at);
    }
    // Whole hierarchy of the current map as one Markdown course (sections per sub-map level).
    void export_course_to(const fs::path& path){
        atomic_write(path,course::markdown(current_file));
        status=L"Cours complet exporté · "+path.filename().wstring();repaint();
    }
    // Nemo reads the course files of one folder: the first export asks where it is, then the choice is remembered.
    void export_nemo(){
        if(current_file.empty()){status=L"Enregistrez d’abord cette carte pour l’exporter vers Nemo.";repaint();return;}
        checkpoint();
        auto folder=nemo::courses_folder(directory);
        auto suggested=nemo::course_file(folder.empty()?fs::path():folder,state).filename().wstring();
        auto path=dialog(true,L"Cours Markdown (*.md)\0*.md\0\0",L"md",suggested.c_str(),folder);
        if(path.empty())return;
        nemo::set_courses_folder(directory,path.parent_path());
        export_course_to(path);
        status=L"Cours exporté pour Nemo · "+path.wstring()+L" · il sera lu à la prochaine synchronisation des cours";repaint();
    }
    // The file of a sub-map was moved or deleted: let the user point to its new place, or detach it.
    void repair_submap(const fs::path& missing){
        status=L"Sous-carte introuvable : "+missing.wstring();repaint();
        if(smoke)return;
        std::wstring message=L"Le fichier de cette sous-carte est introuvable :\n"+missing.wstring()+L"\n\nOui : le retrouver (choisir son nouvel emplacement)\nNon : détacher la sous-carte de cette idée\nAnnuler : ne rien faire";
        const int answer=MessageBoxW(window,message.c_str(),L"Sous-carte introuvable",MB_YESNOCANCEL|MB_ICONWARNING);
        if(answer==IDNO){command(DetachSubmap);return;}
        if(answer!=IDYES)return;
        auto chosen=dialog(false,L"Carte MindMap (*.mindmap)\0*.mindmap\0\0",L"mindmap",L"",current_file.parent_path());
        if(chosen.empty())return;
        auto previous=state.submaps.at(state.selected);
        auto before=document::encode(state);
        state.submaps.erase(state.selected);
        try{submap::attach(state,current_file,state.selected,chosen);}
        catch(...){state.submaps[state.selected]=previous;throw;}
        remember(std::move(before));changed(L"Sous-carte retrouvée · "+chosen.filename().wstring());
    }
    void up_submap(){
        if(trail.empty()){status=L"Cette carte n’est pas une sous-carte ouverte depuis une autre.";repaint();return;}
        if(!may_replace())return;
        auto crumb=trail.back();trail.pop_back();
        const auto child_title=state.map->nodes.at(1)->title;
        navigating=true;
        try{open_path(crumb.file);}catch(...){navigating=false;trail.push_back(crumb);throw;}
        navigating=false;
        if(state.map->nodes.count(crumb.node)){
            select(crumb.node);ensure_visible(crumb.node);
            auto holder=state.map->nodes.at(crumb.node);
            if(holder->title!=child_title){
                auto before=document::encode(state);
                state.map->rename(crumb.node,child_title,holder->note);document::layout(state);
                remember(std::move(before));refresh_inspector();changed(L"Titre de l’idée mis à jour d’après la sous-carte");
            }
        }
        update_caption();repaint();
    }
    void create_submap(){
        if(state.selected==1){status=L"Choisissez une idée (pas le sujet central) pour y créer une sous-carte.";repaint();return;}
        if(current_file.empty()&&!save(true))return;
        checkpoint();
        auto before=document::encode(state);
        auto file=submap::create(state,current_file,state.selected);
        remember(std::move(before));changed(L"Sous-carte créée · "+file.filename().wstring());
        checkpoint();open_submap();
    }
    void show_editors(bool show){for(HWND control:{search,title,note})ShowWindow(control,show?SW_SHOW:SW_HIDE);}
    void refresh_home(){draft_cards=store.cards("drafts");custom_templates=store.cards("templates");}
    void go_home(){if(!may_replace())return;trail.clear();home=true;autosave_pending=false;show_editors(false);refresh_home();update_caption();repaint();}
    static std::string bitmap_bytes(Bitmap& image,const wchar_t* mime){
        UINT count=0,size=0;GetImageEncodersSize(&count,&size);std::vector<BYTE> codecs(size);
        auto* info=reinterpret_cast<ImageCodecInfo*>(codecs.data());GetImageEncoders(count,size,info);CLSID encoder{};bool found=false;
        for(UINT i=0;i<count;++i)if(wcscmp(info[i].MimeType,mime)==0){encoder=info[i].Clsid;found=true;break;}
        if(!found)throw std::runtime_error("Encodeur d'image indisponible");
        PictureStream stream;
        if(CreateStreamOnHGlobal(nullptr,TRUE,&stream.value)!=S_OK||image.Save(stream.value,&encoder,nullptr)!=Ok)throw std::runtime_error("Encodage d'image impossible");
        STATSTG stat{};if(stream.value->Stat(&stat,STATFLAG_NONAME)!=S_OK||stat.cbSize.QuadPart>64*1024*1024)throw std::runtime_error("Image exportée trop grande");
        std::string bytes(static_cast<size_t>(stat.cbSize.QuadPart),'\0');LARGE_INTEGER start{};ULONG read=0;
        if(stream.value->Seek(start,STREAM_SEEK_SET,nullptr)!=S_OK||stream.value->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read)!=S_OK||read!=bytes.size())throw std::runtime_error("Lecture d'image impossible");
        return bytes;
    }
    Bitmap& picture(long long id,const document::Picture& source){
        auto it=picture_cache.find(id);if(it!=picture_cache.end()&&it->second->encoded==source.data)return *it->second->bitmap;
        if(picture_cache.size()>=16)picture_cache.clear();
        auto cached=std::make_unique<CachedPicture>();
        auto bytes=document::unbase64(source.data);
        if(CreateStreamOnHGlobal(nullptr,TRUE,&cached->stream.value)!=S_OK)throw std::runtime_error("Image inaccessible");
        ULONG written=0;
        if(cached->stream.value->Write(bytes.data(),static_cast<ULONG>(bytes.size()),&written)!=S_OK||written!=bytes.size())throw std::runtime_error("Image inaccessible");
        LARGE_INTEGER start{};cached->stream.value->Seek(start,STREAM_SEEK_SET,nullptr);
        cached->bitmap.reset(Bitmap::FromStream(cached->stream.value));
        if(!cached->bitmap||cached->bitmap->GetLastStatus()!=Ok)throw std::runtime_error("Image PNG illisible");
        cached->encoded=source.data;auto* result=cached->bitmap.get();picture_cache[id]=std::move(cached);return *result;
    }
    void attach_picture(const fs::path& path){
        Bitmap source(path.c_str());if(source.GetLastStatus()!=Ok)throw std::runtime_error("Image illisible : choisissez un PNG, JPEG ou BMP");
        const auto w=source.GetWidth(),h=source.GetHeight();if(!w||!h||static_cast<uint64_t>(w)*h>64000000)throw std::runtime_error("Image trop grande (64 millions de pixels maximum)");
        double factor=std::min({1.0,640.0/w,640.0/h});UINT rw=std::max(1u,static_cast<UINT>(w*factor)),rh=std::max(1u,static_cast<UINT>(h*factor));
        Bitmap resized(rw,rh,PixelFormat32bppARGB);Graphics g(&resized);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.Clear(Color(0,255,255,255));g.DrawImage(&source,0,0,rw,rh);
        auto media=document::picture_from_png(bitmap_bytes(resized,L"image/png"));auto before=document::encode(state);
        state.pictures[state.selected]=std::move(media);
        try{document::encode(state);}catch(...){adopt(document::decode(before));throw;}
        picture_cache.erase(state.selected);document::layout(state);remember(std::move(before));changed(L"Image ajoutée · intégrée au fichier de la carte");
    }
    std::unique_ptr<Bitmap> map_bitmap(){
        auto b=document::full_bounds(state);
        double w=b.right-b.left+100,h=b.bottom-b.top+100;
        double factor=std::min({1.5,8192.0/w,8192.0/h,std::sqrt(16000000.0/(w*h))});
        INT bw=std::max(1,static_cast<int>(std::ceil(w*factor))),bh=std::max(1,static_cast<int>(std::ceil(h*factor)));
        auto image=std::make_unique<Bitmap>(bw,bh,PixelFormat32bppARGB);
        if(image->GetLastStatus()!=Ok)throw std::runtime_error("Memoire insuffisante pour l'image");
        Graphics g(image.get());g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.Clear(theme(false).bg);
        draw_map(g,state,RectF(0,0,static_cast<REAL>(bw),static_cast<REAL>(bh)),factor,(-b.left+50)*factor,(-b.top+50)*factor,false);
        return image;
    }
    void export_png(const fs::path& path){auto image=map_bitmap();save_png(*image,path);}
    void export_pdf(const fs::path& path){auto image=map_bitmap();atomic_write(path,document::pdf_image(bitmap_bytes(*image,L"image/jpeg"),image->GetWidth(),image->GetHeight()));}
    static void save_png(Bitmap& image,const fs::path& path){
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
        std::wstring memory=L"\n\nMémoire\nHistorique : "+std::to_wstring(history.steps())+L" étapes, "+kilobytes(history.bytes())+L" en mémoire ("+kilobytes(history.raw_bytes())+L" sans compression).";
        MessageBoxW(window,(
            L"Pour commencer\nSélectionnez une idée, puis Tab pour lui ajouter un enfant. Entrée ajoute une idée au même niveau.\nDouble-cliquez pour renommer. Les flèches déplacent la sélection sur la carte.\n\n"
            L"Relier et annoter\nL (ou « Relier ») puis clic sur une autre idée : lien transversal. Clic droit sur un lien : libellé (cause, conséquence…), inverser, supprimer.\n"
            L"Touches 1 à 5 : repères ★ important, ✓ acquis, ? à revoir, ! attention, ⚑ repère clé. 0 retire le repère.\n\n"
            L"Afficher\nAffichage ▾ : disposition équilibrée, radiale ou vers la droite ; aperçu des notes (N) ; mini-carte (M) ; thème sombre.\n"
            L"Survolez une idée pour lire toute sa note. La recherche surligne les idées trouvées.\n\n"
            L"Sous-cartes\nO ouvre (ou crée) la sous-carte de l’idée, Retour arrière remonte. Cartes ▾ liste toutes les cartes de la hiérarchie ; Ctrl+Maj+F cherche dans toutes les cartes.\nFichier > Exporter pour Nemo écrit le cours complet en Markdown dans le dossier des cours.\n\n"
            L"Plans\nCtrl+Maj+C copie une branche en texte ; Ctrl+Maj+V colle un plan (Markdown, liste ou texte indenté) sous l’idée choisie.\nFichier > Importer un plan crée une carte depuis un fichier .md ou .txt.\n\n"
            L"Raccourcis\nCtrl+N / O / S : nouveau, ouvrir, enregistrer · Ctrl+Z / Y : annuler, rétablir · Ctrl+F : recherche\nCtrl+D : dupliquer · F2 : modifier · Suppr : supprimer · F : centrer · Ctrl+L : réorganiser · Origine : sujet central\n\n"
            L"Accueil et sauvegardes\nChoisissez Nouvelle carte, Ouvrir ou un modèle. Les cartes récentes gardent leur emplacement. Les brouillons sont protégés jusqu’au premier enregistrement.\n"
            L"Une carte enregistrée est sauvegardée automatiquement dans son fichier ; Fichier > Restaurer une version précédente retrouve les dix dernières versions conservées.\n"
            L"L’ancienne session.mindmap se récupère depuis l’accueil : elle n’est plus rechargée comme une carte sans nom.\n\n"
            L"Images et modèles\n+ Image / schéma ajoute une illustration intégrée au fichier ; vous pouvez aussi la glisser sur une idée sélectionnée. Fichier > Enregistrer cette carte comme modèle ajoute un modèle personnel à l’accueil.\n"
            L"Imports / exports\nFreeMind (.mm), XMind (.xmind), Markdown et texte ; export PDF A4 paysage, PNG, SVG ou plan Markdown.\n\n"
            L"Portable\nCopiez MindMap.exe, vos cartes et le dossier .mindmap-data ensemble pour conserver brouillons, modèles et versions. Aucun compte ni dossier AppData."+memory).c_str(),
            L"MindMap · prise en main",MB_OK|MB_ICONINFORMATION);
    }
    int popup(HMENU menu,POINT at){
        int result=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RIGHTBUTTON,at.x,at.y,0,window,nullptr);DestroyMenu(menu);
        return result;
    }
    void file_menu(POINT at){
        HMENU menu=CreatePopupMenu();
        auto item=[&](UINT id,const wchar_t* text){AppendMenuW(menu,MF_STRING,id,text);};
        item(New,L"Nouvelle carte\tCtrl+N");item(Open,L"Ouvrir…\tCtrl+O");item(Save,L"Enregistrer\tCtrl+S");item(SaveAs,L"Enregistrer sous…\tCtrl+Maj+S");
        item(Home,L"Retour à l’accueil");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        item(ImportOutline,L"Importer (FreeMind, XMind, Markdown, texte)…\tCtrl+I");item(PasteOutline,L"Coller un plan sous l’idée choisie\tCtrl+Maj+V");item(CopyBranch,L"Copier la branche en texte\tCtrl+Maj+C");
        item(SaveTemplate,L"Enregistrer cette carte comme modèle…");item(RestoreVersions,L"Restaurer une version précédente…");
        if(current_file.empty()&&!draft_file.empty())item(DeleteDraft,L"Supprimer ce brouillon…");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        item(ExportPdf,L"Exporter en PDF (A4 paysage)…");item(ExportPng,L"Exporter une image PNG…");item(ExportSvg,L"Exporter en SVG…");item(ExportMd,L"Exporter le plan Markdown…");
        item(ExportCourse,L"Exporter le cours complet (sous-cartes comprises)…");item(ExportNemo,L"Exporter pour Nemo…");
        if(int result=popup(menu,at))command(result);
    }
    void view_menu(POINT at){
        HMENU menu=CreatePopupMenu();
        auto item=[&](UINT id,const wchar_t* text,bool checked){AppendMenuW(menu,MF_STRING|(checked?MF_CHECKED:0),id,text);};
        item(LayoutBalanced,L"Branches des deux côtés",state.layout==document::LayoutBalanced);
        item(LayoutRadial,L"Branches tout autour (radiale)",state.layout==document::LayoutRadial);
        item(LayoutRight,L"Branches vers la droite",state.layout==document::LayoutRight);
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        item(ToggleNotes,L"Aperçu des notes sur la carte\tN",state.show_notes);
        item(ToggleMinimap,L"Mini-carte\tM",state.minimap);
        item(ToggleDark,L"Thème sombre",state.dark);
        if(int result=popup(menu,at))command(result);
    }
    void link_menu(POINT at){
        if(selected_link<0)return;
        HMENU menu=CreatePopupMenu();const auto& current=state.links[static_cast<size_t>(selected_link)].label;
        for(int i=0;i<7;++i)AppendMenuW(menu,MF_STRING|(current==document::link_presets[i]?MF_CHECKED:0),LinkLabel0+i,wide(document::link_presets[i]).c_str());
        AppendMenuW(menu,MF_STRING|(current.empty()?MF_CHECKED:0),LinkLabel0+link_label_none,L"Sans libellé");
        AppendMenuW(menu,MF_STRING,LinkLabel0+link_label_custom,L"Libellé personnalisé…");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,ReverseLink,L"Inverser le sens");
        AppendMenuW(menu,MF_STRING,DeleteLink,L"Supprimer le lien\tSuppr");
        if(int result=popup(menu,at))command(result);
    }
    void node_menu(POINT at){
        HMENU menu=CreatePopupMenu(),markers=CreatePopupMenu();
        for(int i=0;i<document::marker_count;++i){
            std::wstring name=(i?wide(document::marker_info(i).glyph)+L"  ":L"")+wide(document::marker_info(i).name)+L"\t"+std::to_wstring(i);
            AppendMenuW(markers,MF_STRING|(document::marker_of(state,state.selected)==i?MF_CHECKED:0),Marker0+i,name.c_str());
        }
        for(auto item:std::vector<std::pair<int,const wchar_t*>>{{Add,L"Ajouter une idée enfant\tTab"},{Sibling,L"Ajouter au même niveau\tEntrée"},{LinkMode,L"Relier à une autre idée…\tL"}})AppendMenuW(menu,MF_STRING,item.first,item.second);
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(markers),L"Repère");
        if(state.submaps.count(state.selected)){AppendMenuW(menu,MF_STRING,OpenSubmap,L"Ouvrir la sous-carte\tO");AppendMenuW(menu,MF_STRING,DetachSubmap,L"Détacher la sous-carte");}
        else if(state.selected!=1)AppendMenuW(menu,MF_STRING,CreateSubmap,L"Créer une sous-carte…\tO");
        if(!trail.empty())AppendMenuW(menu,MF_STRING,UpSubmap,L"Remonter à la carte parente\tRetour arrière");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        for(auto item:std::vector<std::pair<int,const wchar_t*>>{{Duplicate,L"Dupliquer la branche\tCtrl+D"},{CopyBranch,L"Copier la branche en texte\tCtrl+Maj+C"},{PasteOutline,L"Coller un plan ici\tCtrl+Maj+V"},{Collapse,L"Replier / ouvrir"},{Delete,L"Supprimer la branche\tSuppr"}})AppendMenuW(menu,MF_STRING,item.first,item.second);
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,Fit,L"Centrer la carte\tF");
        if(int result=popup(menu,at))command(result);
    }
    void add_new_node(bool same_level){
        // Native boundary: cap identifiers to the document schema's tested range.
        if(state.map->next_id>=2147481647)throw std::runtime_error("Identifiants epuises : creez une nouvelle carte");
        auto parent=same_level?state.map->nodes.at(state.selected)->parent:state.selected;
        if(parent==-1)parent=1;
        state.selected=state.map->add(parent,"Nouvelle idée");document::layout(state);
    }
    void navigate(int dx,int dy){
        if(home)return;
        auto from=document::box(state,state.selected);long long best=0;double score=1e300;
        for(auto id:state.map->visible()){
            if(id==state.selected)continue;
            auto b=document::box(state,id);const double vx=b.x-from.x,vy=b.y-from.y;
            const double along=vx*dx+vy*dy,across=std::abs(vx*dy-vy*dx);
            if(along<=8)continue;
            if(along+across*2.5<score){score=along+across*2.5;best=id;}
        }
        if(best){select(best);ensure_visible(best);}
    }
    // Home screen, recent cards, drafts, templates and legacy recovery. Returns true when the command was handled.
    bool open_flow(int id){
        if(id==Help){help();return true;}
        if(id==BrowseTemplates||id==BrowseRecent){
            HMENU menu=CreatePopupMenu();size_t size=id==BrowseTemplates?std::min<size_t>(100,custom_templates.size()):store.recent.size();
            for(size_t i=0;i<size;++i)AppendMenuW(menu,MF_STRING,(id==BrowseTemplates?CustomTemplate0:Recent0)+i,
                (id==BrowseTemplates?wide(custom_templates[i].title):store.recent[i].filename().wstring()).c_str());
            POINT at{};GetCursorPos(&at);if(auto action=popup(menu,at))command(action);return true;
        }
        if(id==Home){go_home();return true;}
        if(id>=Recent0&&id<Recent0+12){if(static_cast<size_t>(id-Recent0)<store.recent.size()&&may_replace()){open_path(store.recent[id-Recent0]);fit();}return true;}
        if(id==BrowseDrafts){
            if(!may_replace())return true;
            auto path=dialog(false,L"Brouillon (*.mindmap)\0*.mindmap\0\0",L"mindmap",L"",store.data()/"drafts");if(path.empty())return true;
            auto content=document::decode(read_file(path));adopt(std::move(content));current_file.clear();disk_snapshot.clear();
            draft_file=path.parent_path()==store.data()/"drafts"?path:fs::path{};loaded_document(true);fit();return true;
        }
        if(id>=Draft0&&id<Draft0+20){
            if(static_cast<size_t>(id-Draft0)>=draft_cards.size()||!may_replace())return true;
            auto path=draft_cards[id-Draft0].path;auto content=document::decode(read_file(path));adopt(std::move(content));current_file.clear();disk_snapshot.clear();draft_file=path;loaded_document(true);fit();return true;
        }
        if((id>=Template0&&id<Template0+static_cast<int>(workspace::models().size()))||(id>=CustomTemplate0&&id<CustomTemplate0+100)){
            if(!may_replace())return true;
            document::State model;
            if(id>=CustomTemplate0){if(static_cast<size_t>(id-CustomTemplate0)>=custom_templates.size())return true;model=document::decode(read_file(custom_templates[id-CustomTemplate0].path));}
            else model=document::from_outline(document::parse_outline(workspace::models()[id-Template0].outline),"Mon sujet");
            auto name=ask_text(window,L"Nouvelle carte",L"Quel est le sujet de votre carte ?",wide(model.map->nodes.at(1)->title));if(!name)return true;
            auto label=document::trim(utf8(*name));if(label.empty())return true;model.map->nodes.at(1)->title=document::clip_utf8(label,1024);
            adopt(std::move(model));current_file.clear();draft_file.clear();disk_snapshot.clear();loaded_document(true);fit();return true;
        }
        if(id==RecoverLegacy){
            if(store.legacy_migrated||!may_replace())return true;
            auto old=directory/L"session.mindmap";auto data=read_file(old);auto recovered=document::decode(data);
            auto draft=store.data()/"drafts"/"legacy-session.mindmap";
            if(fs::exists(draft))recovered=document::decode(read_file(draft));else store.draft(draft,document::encode(recovered));
            store.legacy_migrated=true;try{store.save_index();}catch(...){store.legacy_migrated=false;refresh_home();throw;}
            adopt(std::move(recovered));draft_file=draft;current_file.clear();disk_snapshot.clear();loaded_document(true);fit();
            std::error_code error;fs::create_directories(store.data()/"legacy",error);if(!error)fs::rename(old,store.data()/"legacy"/"session-v2.mindmap",error);
            status=error?L"Ancienne session récupérée · le fichier d’origine est conservé":L"Ancienne session récupérée · choisissez un nom avec Enregistrer";repaint();return true;
        }
        return false;
    }
    // New, open, import, templates, versions, images, exports and clipboard. Returns true when handled.
    bool file_flow(int id){
        if(id==New||id==Open||id==ImportOutline){
            if(!may_replace())return true;
            if(id==New){command(Template0);return true;}
            else if(id==Open){
                auto path=dialog(false,L"Cartes et plans\0*.mindmap;*.mm;*.xmind;*.md;*.txt\0Carte MindMap\0*.mindmap\0\0",L"mindmap",L"");if(path.empty())return true;
                import_path(path);return true;
            } else {
                auto path=dialog(false,L"FreeMind, XMind, Markdown ou texte\0*.mm;*.xmind;*.md;*.markdown;*.txt\0\0",L"mm",L"");if(path.empty())return true;
                import_path(path);return true;
            }
            loaded_document(id==New);return true;
        }
        if(id==SaveTemplate){auto label=ask_text(window,L"Enregistrer un modèle",L"Nom du modèle :",wide(state.map->nodes.at(1)->title));if(label){store.custom_model(utf8(*label),state);refresh_home();status=L"Modèle ajouté à l’accueil";repaint();}return true;}
        if(id==DeleteDraft){
            if(!current_file.empty())return true;
            if(MessageBoxW(window,L"Supprimer définitivement ce brouillon ?\nLes cartes déjà enregistrées ne seront pas supprimées.",L"Supprimer le brouillon",MB_YESNO|MB_ICONWARNING)!=IDYES)return true;
            store.forget_draft(draft_file);draft_file.clear();dirty=false;autosave_pending=false;home=true;show_editors(false);refresh_home();update_caption();repaint();return true;
        }
        if(id==RestoreVersions){
            if(current_file.empty()){status=L"Enregistrez d’abord cette carte pour retrouver ses versions.";repaint();return true;}
            restore_paths=store.versions(current_file);if(restore_paths.empty()){status=L"Aucune version précédente pour cette carte.";repaint();return true;}
            HMENU menu=CreatePopupMenu();for(size_t i=0;i<restore_paths.size();++i)AppendMenuW(menu,MF_STRING,Restore0+i,restore_paths[i].stem().wstring().c_str());POINT at{};GetCursorPos(&at);if(auto action=popup(menu,at))command(action);return true;
        }
        if(id>=Restore0&&id<Restore0+10){
            if(static_cast<size_t>(id-Restore0)>=restore_paths.size()||current_file.empty())return true;
            auto restored=document::decode(read_file(restore_paths[id-Restore0]));auto data=document::encode(restored);
            store.save_named(current_file,data,disk_snapshot.empty()?nullptr:&disk_snapshot,true);disk_snapshot=data;
            adopt(std::move(restored));loaded_document(false);status=L"Version restaurée · la version remplacée est conservée dans les sauvegardes";repaint();return true;
        }
        if(id==AddImage){auto path=dialog(false,L"Images PNG, JPEG, BMP\0*.png;*.jpg;*.jpeg;*.bmp\0\0",L"png",L"");if(!path.empty())attach_picture(path);return true;}
        if(id==RemoveImage){if(!state.pictures.count(state.selected))return true;auto before=document::encode(state);state.pictures.erase(state.selected);picture_cache.erase(state.selected);document::layout(state);remember(std::move(before));changed(L"Image retirée");return true;}
        if(id==ExportPng||id==ExportSvg||id==ExportMd||id==ExportPdf){
            const wchar_t* ext=id==ExportPng?L"png":id==ExportSvg?L"svg":id==ExportPdf?L"pdf":L"md";
            const wchar_t* filter=id==ExportPng?L"Image PNG\0*.png\0\0":id==ExportSvg?L"Image SVG\0*.svg\0\0":id==ExportPdf?L"Document PDF\0*.pdf\0\0":L"Plan Markdown\0*.md\0\0";
            std::wstring name=L"Ma carte.";name+=ext;
            auto path=dialog(true,filter,ext,name.c_str());if(path.empty())return true;
            if(id==ExportPng)export_png(path);else if(id==ExportPdf)export_pdf(path);else atomic_write(path,id==ExportSvg?document::svg(state):document::markdown(state));
            status=L"Export terminé · "+path.filename().wstring();repaint();return true;
        }
        if(id==CopyBranch){set_clipboard_text(window,document::branch_markdown(state,state.selected));status=L"Branche copiée en texte : collez-la dans vos notes ou dans une autre carte (Ctrl+Maj+V).";repaint();return true;}
        return false;
    }
    // Navigation, sub-maps, link mode, undo/redo. These never change the map by themselves. Returns true when handled.
    bool view_flow(int id){
        if(id==GlobalSearch){global_search();return true;}
        if(id>=Jump0&&id<Jump0+300){
            if(static_cast<size_t>(id-Jump0)<jumps.size())jump_to(jump_root,jumps[id-Jump0].first,jumps[id-Jump0].second);
            return true;
        }
        if(id==ExportNemo){export_nemo();return true;}
        if(id==ExportCourse){
            if(current_file.empty()){status=L"Enregistrez d’abord cette carte pour exporter son cours complet.";repaint();return true;}
            checkpoint();
            auto path=dialog(true,L"Cours Markdown (*.md)\0*.md\0\0",L"md",L"Mon cours.md");if(path.empty())return true;
            export_course_to(path);return true;
        }
        if(id==Fit){fit();return true;}
        if(id==SelectRoot){select(1);ensure_visible(1);return true;}
        if(id==OpenSubmap){open_submap();return true;}
        if(id==UpSubmap){up_submap();return true;}
        if(id==CreateSubmap){create_submap();return true;}
        if(id==DetachSubmap){
            if(!state.submaps.count(state.selected))return true;
            auto before=document::encode(state);state.submaps.erase(state.selected);remember(std::move(before));
            changed(L"Sous-carte détachée · le fichier de la sous-carte est conservé");return true;
        }
        if(id==LinkMode){
            linking=!linking;link_from=state.selected;selected_link=-1;
            status=linking?L"Cliquez sur l’idée à relier à « "+wide(state.map->nodes.at(link_from)->title)+L" »":L"Liaison annulée";repaint();return true;
        }
        if(id==Undo||id==Redo){
            if(!(id==Undo?history.can_undo():history.can_redo())){status=L"Aucune modification à "+std::wstring(id==Undo?L"annuler":L"rétablir");repaint();return true;}
            auto current=document::encode(state);
            adopt(document::decode(id==Undo?history.undo(current):history.redo(current)));
            refresh_inspector();changed();return true;
        }
        if(id>=LinkRow0&&id<LinkRow0+3){
            auto row=static_cast<size_t>(id-LinkRow0);if(row>=link_rows.size())return true;
            selected_link=static_cast<int>(link_rows[row]);repaint();
            POINT at;GetCursorPos(&at);link_menu(at);return true;
        }
        return false;
    }
    // Commands that modify the map: one snapshot before, one history entry after.
    void edit_flow(int id){
        std::string before=document::encode(state);
        if(id==Add||id==Sibling){
            add_new_node(id==Sibling);
        } else if(id==Delete||id==DeleteLink||(id>=LinkRowDelete0&&id<LinkRowDelete0+3)){
            if(id>=LinkRowDelete0){auto row=static_cast<size_t>(id-LinkRowDelete0);if(row>=link_rows.size())return;selected_link=static_cast<int>(link_rows[row]);}
            if(selected_link>=0){state.links.erase(state.links.begin()+selected_link);selected_link=-1;}
            else if(id==DeleteLink)return;
            else {
                if(state.selected==1){status=L"Le sujet central se renomme ; il ne se supprime pas.";repaint();return;}
                size_t count=state.map->descendants(state.selected).size();
                if(count>1&&!smoke){std::wstring msg=L"Supprimer cette branche et ses "+std::to_wstring(count-1)+L" sous-idées ?\nCtrl+Z permet de revenir en arrière.";if(MessageBoxW(window,msg.c_str(),L"Supprimer une branche",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;}
                state.selected=state.map->remove(state.selected);document::layout(state);
            }
        } else if(id==Duplicate){
            if(state.map->next_id>=2147481647)throw std::runtime_error("Identifiants epuises : creez une nouvelle carte");
            state.selected=document::duplicate_branch(state,state.selected);document::layout(state);
        } else if(id==PasteOutline){
            if(state.map->next_id>=2147479647)throw std::runtime_error("Identifiants epuises : creez une nouvelle carte");
            auto created=document::apply_outline(state,state.selected,document::parse_outline(clipboard_text(window)));
            document::layout(state);state.selected=created.front();
            remember(std::move(before));refresh_inspector();changed(std::to_wstring(created.size())+L" idées ajoutées depuis le presse-papiers");ensure_visible(state.selected);return;
        }
        else if(id==Layout){document::layout(state);}
        else if(id==LayoutRight||id==LayoutBalanced||id==LayoutRadial){state.layout=id==LayoutRight?document::LayoutRight:id==LayoutBalanced?document::LayoutBalanced:document::LayoutRadial;document::layout(state);}
        else if(id==ToggleNotes){state.show_notes=!state.show_notes;document::layout(state);}
        else if(id==Collapse){if(state.map->nodes.at(state.selected)->children.empty())return;state.map->toggle(state.selected);document::layout(state);}
        else if(id>=Color0&&id<Color0+6){state.map->set_color(state.selected,palette[id-Color0],true);}
        else if(id==CustomColor){
            auto current=state.map->nodes.at(state.selected)->color;
            CHOOSECOLORW pick{};pick.lStructSize=sizeof(pick);pick.hwndOwner=window;pick.lpCustColors=custom_colors;
            pick.rgbResult=RGB(current>>16&255,current>>8&255,current&255);pick.Flags=CC_FULLOPEN|CC_RGBINIT;
            if(!ChooseColorW(&pick))return;
            state.map->set_color(state.selected,(GetRValue(pick.rgbResult)<<16)|(GetGValue(pick.rgbResult)<<8)|GetBValue(pick.rgbResult),true);
        }
        else if(id>=Marker0&&id<Marker0+document::marker_count){
            const int marker=id-Marker0;
            if(document::marker_of(state,state.selected)==marker||marker==0)state.markers.erase(state.selected);else state.markers[state.selected]=marker;
            status=L"Repère : "+wide(document::marker_info(document::marker_of(state,state.selected)).name);
        }
        else if(id>=LinkLabel0&&id<=LinkLabel0+link_label_custom&&selected_link>=0){
            auto& link=state.links[static_cast<size_t>(selected_link)];
            if(id==LinkLabel0+link_label_custom){
                auto answer=ask_text(window,L"Libellé du lien",L"Texte affiché sur le lien (200 caractères au plus) :",wide(link.label));
                if(!answer)return;
                link.label=document::clip_utf8(document::trim(utf8(*answer)),document::max_link_label);
            } else link.label=id==LinkLabel0+link_label_none?"":document::link_presets[id-LinkLabel0];
        }
        else if(id==ReverseLink&&selected_link>=0){auto& link=state.links[static_cast<size_t>(selected_link)];std::swap(link.from,link.to);}
        else return;
        std::string after;
        try{after=document::encode(state);}
        catch(...){adopt(document::decode(before));refresh_inspector();refresh_outline();repaint();throw;}
        if(after==before){repaint();return;}
        remember(std::move(before));refresh_inspector();
        const bool keep_status=id>=Marker0&&id<Marker0+document::marker_count;
        if(keep_status)changed(status);else changed();
        if(id==Layout||id==LayoutRight||id==LayoutBalanced||id==LayoutRadial)fit();
        if(id==Add||id==Sibling||id==Duplicate){ensure_visible(state.selected);}
        if(id==Add||id==Sibling){SetFocus(title);SendMessageW(title,EM_SETSEL,0,-1);}
    }
    void command(int id){
        try {
            if(open_flow(id))return;
            if(home&&id!=New&&id!=Open&&id!=ImportOutline&&id!=ToggleDark)return;
            if(id==Apply){commit_edit();return;}
            if(!commit_edit())return;
            if(id==Save||id==SaveAs){save(id==SaveAs);return;}
            if(id==ToggleDark){state.dark=!state.dark;apply_theme();autosave_pending=true;return;}
            if(id==ToggleMinimap){state.minimap=!state.minimap;autosave_pending=true;repaint();return;}
            if(file_flow(id))return;
            if(view_flow(id))return;
            edit_flow(id);
        } catch(const PyThrow& e){show_error(e.p->py_exc_str());}
          catch(const std::exception& e){show_error(e.what());}
    }
    void finish_link(long long target){
        linking=false;
        if(!target){status=L"Liaison annulée";repaint();return;}
        if(target==link_from){status=L"Choisissez une autre idée que celle de départ.";linking=true;repaint();return;}
        auto before=document::encode(state);
        try{
            if(!document::add_link(state,link_from,target)){status=L"Ces deux idées sont déjà reliées.";repaint();return;}
            selected_link=static_cast<int>(state.links.size())-1;
            remember(std::move(before));changed(L"Lien créé · clic droit dessus pour lui donner un libellé (cause, conséquence…)");
        }catch(const std::exception& e){show_error(e.what());}
    }
    // Keyboard shortcuts. Returns true when the key was consumed; "editing" means a text field has the focus.
    bool key_down(WPARAM key,bool control,bool shift,bool editing,HWND focus){
            int action=0;
            if(control){
                switch(key){case 'N':action=ui::New;break;case 'O':action=ui::Open;break;case 'S':action=shift?ui::SaveAs:ui::Save;break;
                    case 'I':action=ui::ImportOutline;break;
                    case 'Z':if(!editing)action=shift?ui::Redo:ui::Undo;break;case 'Y':if(!editing)action=ui::Redo;break;
                    case 'D':if(!editing)action=ui::Duplicate;break;case 'L':action=ui::Layout;break;
                    case 'C':if(shift&&!editing)action=ui::CopyBranch;break;case 'V':if(shift&&!editing)action=ui::PasteOutline;break;
                    case 'F':if(shift){action=ui::GlobalSearch;break;}if(!home){SetFocus(search);SendMessageW(search,EM_SETSEL,0,-1);}return true;
                    case VK_RETURN:action=ui::Apply;break;}
            }else if(!editing){
                
                if(key>='0'&&key<='5')action=ui::Marker0+static_cast<int>(key-'0');
                else if(key>=VK_NUMPAD0&&key<=VK_NUMPAD5)action=ui::Marker0+static_cast<int>(key-VK_NUMPAD0);
                else switch(key){case VK_TAB:action=ui::Add;break;case VK_RETURN:action=ui::Sibling;break;case VK_DELETE:action=ui::Delete;break;case 'F':action=ui::Fit;break;
                    case 'L':action=ui::LinkMode;break;case 'N':action=ui::ToggleNotes;break;case 'M':action=ui::ToggleMinimap;break;case 'O':action=ui::OpenSubmap;break;case VK_BACK:action=ui::UpSubmap;break;
                    case VK_HOME:action=ui::SelectRoot;break;
                    case VK_LEFT:navigate(-1,0);return true;case VK_RIGHT:navigate(1,0);return true;
                    case VK_UP:navigate(0,-1);return true;case VK_DOWN:navigate(0,1);return true;
                    case VK_F2:if(!home){SetFocus(title);SendMessageW(title,EM_SETSEL,0,-1);}return true;}
            }
            if(key==VK_ESCAPE){
                if(editing){commit_edit();SetFocus(window);return true;}
                if(linking){linking=false;status=L"Liaison annulée";repaint();return true;}
                if(selected_link>=0){selected_link=-1;repaint();return true;}
            }
            if(action){command(action);return true;}
            if(editing&&key==VK_TAB){SetFocus(focus==title?note:focus==note?search:title);return true;}
        return false;
    }
    void mouse_down(PointF point, bool middle){
        if(!middle){
            for(const auto& button:buttons)if(button.rect.Contains(point)){
                POINT at{static_cast<LONG>(button.rect.X*scale),static_cast<LONG>(button.rect.GetBottom()*scale)};ClientToScreen(window,&at);
                if(button.command==0)file_menu(at);
                else if(button.command==-1)view_menu(at);
                else if(button.command==-2)cards_menu(at);
                else command(button.command);
                return;
            }
            for(const auto& row:outline_hits)if(row.first.Contains(point)){
                select(row.second);
                auto n=state.map->nodes.at(row.second);state.pan_x=canvas.Width/2-n->x*state.zoom;state.pan_y=canvas.Height/2-n->y*state.zoom;autosave_pending=true;repaint();return;
            }
        }
        if(home||!canvas.Contains(point)||!commit_edit())return;
        SetFocus(window);last_mouse=point;moved=false;tooltip=false;
        if(!middle&&minimap_rect.Contains(point)){minimap_drag=true;minimap_jump(point);SetCapture(window);return;}
        long long target=middle?0:hit(point);
        if(linking&&!middle){finish_link(target);return;}
        if(target && !(GetKeyState(VK_SPACE)&0x8000)){
            select(target);
            auto n=state.map->nodes.at(target);
            if(state.submaps.count(target)){
                // The ▶ badge on the top-right corner opens the sub-map.
                auto b=document::box(state,target);auto pos=project(b.right(),b.top());
                if(std::abs(point.X-(pos.X-6*state.zoom))<11*state.zoom&&std::abs(point.Y-(pos.Y+6*state.zoom))<11*state.zoom){command(OpenSubmap);return;}
            }
            if(!n->children.empty()){
                auto anchor=toggle_anchor(state,target);auto pos=project(anchor.x,anchor.y);
                if(std::abs(point.X-pos.X)<15*state.zoom&&std::abs(point.Y-pos.Y)<11*state.zoom){command(Collapse);return;}
            }
            dragging=true;drag_id=target;drag_before.clear();
        }else{
            int link=middle?-1:hit_link(point);
            if(link>=0){selected_link=link;repaint();return;}
            if(selected_link>=0){selected_link=-1;repaint();}
            panning=true;
        }
        SetCapture(window);
    }
    void mouse_move(PointF point){
        if(home)return;
        pointer=point;
        if(!tracking_mouse){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};tracking_mouse=TrackMouseEvent(&track);}
        if(minimap_drag){minimap_jump(point);return;}
        if(!dragging&&!panning){
            long long over=canvas.Contains(point)&&!minimap_rect.Contains(point)?hit(point):0;
            int link=over||!canvas.Contains(point)?-1:hit_link(point);
            if(over!=hovered||link!=hovered_link){hovered=over;hovered_link=link;hover_since=GetTickCount64();tooltip=false;repaint();}
            else if(linking)repaint();
            return;
        }
        REAL dx=point.X-last_mouse.X,dy=point.Y-last_mouse.Y;
        if(std::abs(dx)+std::abs(dy)>0.1f)moved=true;
        if(dragging){
            if(!(GetKeyState(VK_SHIFT)&0x8000)){
                // The snapshot is taken lazily: a simple click on an idea serializes nothing.
                if(drag_before.empty())drag_before=document::encode(state);
                auto ids=state.map->descendants(drag_id);bool safe=true;
                for(auto id:ids){auto n=state.map->nodes.at(id);if(std::abs(n->x+dx/state.zoom)>1000000||std::abs(n->y+dy/state.zoom)>1000000)safe=false;}
                if(safe)state.map->move_branch(drag_id,dx/state.zoom,dy/state.zoom);
            }
        }else{state.pan_x=std::clamp(state.pan_x+dx,-10000000.0,10000000.0);state.pan_y=std::clamp(state.pan_y+dy,-10000000.0,10000000.0);}
        last_mouse=point;repaint();
    }
    void mouse_up(PointF point){
        if(minimap_drag){minimap_drag=false;ReleaseCapture();return;}
        if(!dragging&&!panning)return;
        if(dragging&&moved){
            if(drag_before.empty())drag_before=document::encode(state);
            try{
                if((GetKeyState(VK_SHIFT)&0x8000)){auto target=hit(point);if(target&&target!=drag_id){state.map->reparent(drag_id,target);document::layout(state);}}
                if(drag_before!=document::encode(state)){remember(std::move(drag_before));changed();}
            }catch(const PyThrow& e){adopt(document::decode(drag_before));refresh_inspector();show_error(e.p->py_exc_str());}
        }
        if(panning&&moved)autosave_pending=true;
        dragging=false;panning=false;drag_before.clear();ReleaseCapture();repaint();
    }
    void wheel(PointF point,int delta){
        if(home)return;
        if(point.X<canvas.X){outline_scroll-=delta/120*3;refresh_outline();repaint();return;}
        if(!canvas.Contains(point))return;
        double old=state.zoom;state.zoom=std::clamp(old*std::pow(1.12,delta/120.0),.2,3.0);
        state.pan_x=(point.X-canvas.X)-(point.X-canvas.X-state.pan_x)*state.zoom/old;
        state.pan_y=(point.Y-canvas.Y)-(point.Y-canvas.Y-state.pan_y)*state.zoom/old;
        autosave_pending=true;tooltip=false;repaint();
    }
    void timer(){
        if(hovered&&!tooltip&&GetTickCount64()-hover_since>450){tooltip=true;repaint();}
        if(GetTickCount64()-edit_time>1200&&(pending_edit||GetFocus()==title||GetFocus()==note))commit_edit();
        if(!pending_edit&&!dragging&&!panning&&autosave_pending&&GetTickCount64()>=retry_save_at){
            try{checkpoint();}catch(const std::exception& e){status=L"Sauvegarde impossible · "+wide(e.what())+L" · nouvel essai dans 30 s";retry_save_at=GetTickCount64()+30000;repaint();}
        }
    }
    void capture(const fs::path& path){
        RECT rc;GetClientRect(window,&rc);Bitmap image(rc.right,rc.bottom,PixelFormat32bppARGB);
        {
            Graphics shot(&image);HDC dc=shot.GetHDC();paint(dc);
            for(HWND control:{search,title,note}){
                if(!IsWindowVisible(control))continue;
                RECT r;GetWindowRect(control,&r);POINT p{r.left,r.top};ScreenToClient(window,&p);
                int saved=SaveDC(dc);SetViewportOrgEx(dc,p.x,p.y,nullptr);
                SendMessageW(control,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
                RestoreDC(dc,saved);
            }
            shot.ReleaseHDC(dc);
        }
        save_png(image,path);
    }
    void smoke_test(){
        KillTimer(window,smoke_timer);
        auto expect=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
        try{
            auto handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            for(int i=0;i<40;++i){InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);}
            auto after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            expect(after<=handles+3,"Objets GDI non liberes");
            select(2);command(Add);SetWindowTextW(title,L"Révolution française 🗺");SetWindowTextW(note,L"1789\r\nUne note avec des accents.");pending_edit=false;commit_edit();
            expect(state.map->nodes.at(state.selected)->note=="1789\nUne note avec des accents.","La note du controle n'est pas synchronisee");
            auto id=state.selected;
            command(Color0+1);command(Duplicate);auto size=state.map->nodes.size();command(Undo);command(Redo);
            expect(state.map->nodes.size()==size,"Annuler/retablir incorrect");
            select(id);command(Collapse);command(Collapse);command(Layout);fit();
            // Links, markers, layouts and outlines.
            command(Marker0+1);expect(document::marker_of(state,id)==1,"Repere non applique");
            command(LinkMode);finish_link(3);expect(selected_link>=0&&state.links.back().to==3,"Lien non cree");
            command(LinkLabel0);expect(state.links.back().label==document::link_presets[0],"Libelle de lien");
            command(ReverseLink);command(Undo);expect(state.links.back().from==id,"Annuler l'inversion");
            auto items=document::parse_outline("- Causes\n  - Crise financière\n  > Note importée\n- Conséquences\n");
            auto before=document::encode(state);auto created=document::apply_outline(state,id,items);document::layout(state);remember(before);changed();
            expect(created.size()==3&&state.map->nodes.at(created[1])->note=="Note importée","Import de plan");
            command(ToggleNotes);expect(state.show_notes,"Apercu des notes");
            for(int mode:{LayoutRadial,LayoutRight,LayoutBalanced}){command(mode);auto ids=state.map->visible();
                for(size_t i=0;i<ids.size();++i)for(size_t j=i+1;j<ids.size();++j)expect(!document::box(state,ids[i]).overlaps(document::box(state,ids[j])),"Chevauchement");}
            select(id);navigate(1,0);expect(state.selected!=id,"Navigation clavier");
            select(1);fit();
            auto encoded=document::encode(state);auto decoded=document::decode(encoded);
            expect(document::encode(decoded)==encoded,"Roundtrip document incorrect");
            atomic_write(directory/L"test-carte.mindmap",encoded);
            atomic_write(directory/L"test-plan.md",document::markdown(state));
            atomic_write(directory/L"test-carte.svg",document::svg(state));
            expect(document::svg(state).find("marker-end")!=std::string::npos,"Liens absents du SVG");
            export_png(directory/L"test-carte.png");
            hovered=id;tooltip=true;pointer=project(state.map->nodes.at(id)->x,state.map->nodes.at(id)->y);
            capture(directory/L"test-interface.png");hovered=0;tooltip=false;
            command(LayoutRadial);command(ToggleNotes);command(ToggleDark);select(1);fit();
            capture(directory/L"test-interface-sombre.png");
            command(ToggleDark);command(LayoutBalanced);
            select(1);
            {
                Bitmap diagram(180,80,PixelFormat32bppARGB);
                {Graphics drawn(&diagram);drawn.Clear(Color(255,255,255,255));Pen line(rgb(0x6274E8),3);drawn.DrawLine(&line,20,40,160,40);drawn.DrawRectangle(&line,5,15,55,50);drawn.DrawRectangle(&line,120,15,55,50);}
                save_png(diagram,directory/L"test-schema.png");
            }
            attach_picture(directory/L"test-schema.png");
            expect(state.pictures.count(1)&&document::decode(document::encode(state)).pictures==state.pictures,"Image non conservée");
            expect(document::svg(state).find("data:image/png;base64,")!=std::string::npos,"Image absente du SVG");
            export_pdf(directory/L"test-carte.pdf");expect(read_file(directory/L"test-carte.pdf").starts_with("%PDF-1.4"),"PDF invalide");
            current_file=directory/L"test-carte.mindmap";disk_snapshot=read_file(current_file);expect(save(false),"Sauvegarde nommée impossible");
            SetWindowTextW(title,L"Carte modifiée automatiquement");commit_edit();checkpoint();
            expect(document::decode(read_file(current_file)).map->nodes.at(1)->title=="Carte modifiée automatiquement","Sauvegarde automatique dans le fichier d'origine");
            expect(!store.versions(current_file).empty(),"Versions de secours absentes");
            {
                // Sub-maps: create from the selected idea, travel down, then back up to the same idea.
                const auto parent_file=current_file;const auto parent_title=state.map->nodes.at(1)->title;const auto holder=id;
                select(holder);command(CreateSubmap);
                expect(trail.size()==1&&current_file!=parent_file&&current_file.parent_path().filename()==parent_file.stem(),"Sous-carte non ouverte");
                expect(state.map->nodes.at(1)->title==document::decode(read_file(parent_file)).map->nodes.at(holder)->title,"Titre de la sous-carte");
                command(Add);expect(state.map->nodes.size()==2,"Idée de sous-carte");SetWindowTextW(title,L"Chapitre de test");commit_edit();
                capture(directory/L"test-sous-carte.png");
                command(UpSubmap);
                expect(trail.empty()&&current_file==parent_file&&state.selected==holder&&state.submaps.count(holder),"Retour à la carte parente");
                {auto n=state.map->nodes.at(holder);state.zoom=1.2;state.pan_x=canvas.Width/2-n->x*state.zoom;state.pan_y=canvas.Height/2-n->y*state.zoom;}
                capture(directory/L"test-carte-parente.png");
                // Titles stay shared: renamed inside the sub-map, the idea follows on the way up; renamed in the parent, the sub-map follows.
                select(holder);command(OpenSubmap);select(1);SetWindowTextW(title,L"Titre modifié dans la sous-carte");commit_edit();command(UpSubmap);
                expect(state.map->nodes.at(holder)->title=="Titre modifié dans la sous-carte","Titre de l'idée après remontée");
                select(holder);SetWindowTextW(title,L"Retitré dans la carte parente");commit_edit();checkpoint();
                const auto child_path=submap::resolve(current_file,state.submaps.at(holder));
                expect(document::decode(read_file(child_path)).map->nodes.at(1)->title=="Retitré dans la carte parente","Titre de la sous-carte après renommage de l'idée");
                // A missing sub-map file is reported, never opened blindly.
                {
                    const auto kept=read_file(child_path);fs::remove(child_path);
                    command(OpenSubmap);expect(trail.empty()&&status.find(L"introuvable")!=std::wstring::npos,"Sous-carte introuvable");
                    atomic_write(child_path,kept);
                }
                // Same trip with the keyboard: O opens, Backspace goes back up; typing in a field never triggers shortcuts.
                select(holder);
                expect(!key_down('O',false,false,true,nullptr)&&trail.empty(),"O ne doit rien faire pendant la saisie");
                expect(key_down('O',false,false,false,nullptr)&&trail.size()==1&&state.map->nodes.size()==2,"Touche O : ouvrir la sous-carte");
                expect(key_down(VK_BACK,false,false,false,nullptr)&&trail.empty()&&state.selected==holder,"Retour arrière : remonter");
                expect(!key_down(VK_BACK,false,false,false,nullptr)||trail.empty(),"Retour arrière sans parent");
                expect(key_down(VK_HOME,false,false,false,nullptr)&&state.selected==1,"Origine : sujet central");
                expect(key_down('N',false,false,false,nullptr)&&state.show_notes&&key_down('N',false,false,false,nullptr)&&!state.show_notes,"Touche N");
                expect(key_down('1',false,false,false,nullptr)&&document::marker_of(state,1)==1,"Touche 1 : repère");
                key_down('0',false,false,false,nullptr);
                select(1);
                // Three levels: jump around the hierarchy, search every level, export it as one course.
                const auto top=current_file;
                select(holder);command(OpenSubmap);select(2);command(CreateSubmap);
                expect(trail.size()==2,"Sous-sous-carte");
                const auto deepest=current_file;
                select(1);SetWindowTextW(title,L"Mot rarissime xylophone");commit_edit();checkpoint();
                auto hits=submap::search(top,"XYLOPHONE");
                expect(!hits.empty()&&submap::same(hits[0].file,deepest)&&hits[0].path.find(" › ")!=std::string::npos,"Recherche globale");
                jump_to(top,top,1);expect(trail.empty()&&submap::same(current_file,top),"Saut vers la carte racine");
                jump_to(top,deepest,1);expect(trail.size()==2&&submap::same(current_file,deepest)&&submap::same(trail[0].file,top),"Saut vers la sous-sous-carte");
                command(UpSubmap);command(UpSubmap);expect(trail.empty()&&submap::same(current_file,top),"Remontée sur deux niveaux");
                const auto course_path=directory/L"test-cours.md";export_course_to(course_path);
                const auto course_text=read_file(course_path);
                expect(course_text.starts_with("<!--")&&course_text.find("\n## ")!=std::string::npos&&course_text.find("\n### ")!=std::string::npos,"Cours complet : sections par niveau");
                fs::create_directories(directory/L"test-nemo");nemo::set_courses_folder(directory,directory/L"test-nemo");
                expect(submap::same(nemo::courses_folder(directory),directory/L"test-nemo"),"Dossier des cours Nemo");
                expect(fs::exists(nemo::export_course(top,nemo::courses_folder(directory))),"Export Nemo");
            }
            store.custom_model("Mon modèle de test",state);go_home();expect(home&&!IsWindowVisible(title),"Accueil incorrect");capture(directory/L"test-accueil.png");
            atomic_write(directory/L"smoke-test-report.json","{\"ok\":true,\"nodes\":"+std::to_string(state.map->nodes.size())+",\"links\":"+std::to_string(state.links.size())
                +",\"gdi_before\":"+std::to_string(handles)+",\"gdi_after\":"+std::to_string(after)
                +",\"history_steps\":"+std::to_string(history.steps())+",\"history_bytes\":"+std::to_string(history.bytes())+",\"history_raw_bytes\":"+std::to_string(history.raw_bytes())+"}\n");
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
            case WM_GETMINMAXINFO:{auto p=reinterpret_cast<MINMAXINFO*>(lp);p->ptMinTrackSize={static_cast<LONG>(1000*app->scale),static_cast<LONG>(700*app->scale)};return 0;}
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);app->paint(dc);EndPaint(window,&p);return 0;}
            case WM_CTLCOLOREDIT:{auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,app->colors().edit_text);SetBkColor(dc,app->colors().edit_bg);return reinterpret_cast<LRESULT>(app->edit_brush);}
            case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT&&app->linking){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;}break;
            case WM_COMMAND:
                if(HIWORD(wp)==EN_CHANGE){
                    if(LOWORD(wp)==search_id){app->outline_scroll=0;app->refresh_outline();app->repaint();}
                    else if(!app->updating){app->pending_edit=true;app->edit_time=GetTickCount64();}
                }else if(!lp)app->command(LOWORD(wp));
                return 0;
            case WM_TIMER:
                if(wp==smoke_timer){app->smoke_test();return 0;}
                app->timer();
                return 0;
            case WM_LBUTTONDOWN:case WM_MBUTTONDOWN:app->mouse_down(PointF(GET_X_LPARAM(lp)/app->scale,GET_Y_LPARAM(lp)/app->scale),message==WM_MBUTTONDOWN);return 0;
            case WM_MOUSEMOVE:app->mouse_move(PointF(GET_X_LPARAM(lp)/app->scale,GET_Y_LPARAM(lp)/app->scale));return 0;
            case WM_MOUSELEAVE:app->tracking_mouse=false;if(app->hovered||app->hovered_link>=0){app->hovered=0;app->hovered_link=-1;app->tooltip=false;app->repaint();}return 0;
            case WM_LBUTTONUP:case WM_MBUTTONUP:app->mouse_up(PointF(GET_X_LPARAM(lp)/app->scale,GET_Y_LPARAM(lp)/app->scale));return 0;
            case WM_LBUTTONDBLCLK:{
                if(app->home)return 0;
                PointF p(GET_X_LPARAM(lp)/app->scale,GET_Y_LPARAM(lp)/app->scale);
                if(!app->canvas.Contains(p)||app->linking)return 0;
                if(auto id=app->hit(p)){app->select(id,false);SetFocus(app->title);SendMessageW(app->title,EM_SETSEL,0,-1);}
                else if(int link=app->hit_link(p);link>=0){app->selected_link=link;app->command(LinkLabel0+link_label_custom);}
                return 0;
            }
            case WM_CONTEXTMENU:{
                if(app->home)return 0;
                POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                if(point.x==-1&&point.y==-1){auto b=document::box(app->state,app->state.selected);auto at=app->project(b.x,b.y);point={static_cast<LONG>(at.X*app->scale),static_cast<LONG>(at.Y*app->scale)};ClientToScreen(window,&point);app->node_menu(point);return 0;}
                POINT client=point;ScreenToClient(window,&client);PointF at(client.x/app->scale,client.y/app->scale);
                if(!app->canvas.Contains(at))return 0;
                app->linking=false;
                if(auto id=app->hit(at)){app->select(id);app->node_menu(point);}
                else if(int link=app->hit_link(at);link>=0){app->selected_link=link;app->repaint();app->link_menu(point);}
                else{
                    HMENU menu=CreatePopupMenu();
                    for(auto item:std::vector<std::pair<int,const wchar_t*>>{{Fit,L"Centrer la carte\tF"},{Layout,L"Réorganiser\tCtrl+L"},{PasteOutline,L"Coller un plan sous l’idée choisie\tCtrl+Maj+V"}})AppendMenuW(menu,MF_STRING,item.first,item.second);
                    if(int action=app->popup(menu,point))app->command(action);
                }
                return 0;
            }
            case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&p);app->wheel(PointF(p.x/app->scale,p.y/app->scale),GET_WHEEL_DELTA_WPARAM(wp));return 0;}
            case WM_CAPTURECHANGED:if(reinterpret_cast<HWND>(lp)!=window&&(app->dragging||app->panning||app->minimap_drag)){app->mouse_up(app->last_mouse);}return 0;
            case WM_DROPFILES:{
                auto drop=reinterpret_cast<HDROP>(wp);wchar_t path[32768];bool have=DragQueryFileW(drop,0,path,32768)>0;DragFinish(drop);
                if(have){
                    fs::path file(path);auto ext=lowercase(file.extension().wstring());
                    if(ext==L".png"||ext==L".jpg"||ext==L".jpeg"||ext==L".bmp"){
                        if(app->home){app->status=L"Ouvrez ou créez une carte pour y ajouter cette image.";app->repaint();}
                        else if(app->commit_edit())app->attach_picture(file);
                    }else if(app->may_replace())app->import_path(file);
                }
                return 0;
            }
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
        {
            ui::App app(folder,smoke);
            WNDCLASSW klass{};klass.lpfnWndProc=ui::procedure;klass.hInstance=instance;klass.lpszClassName=L"MindMapPortableWindow";
            klass.style=CS_DBLCLKS;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(2));RegisterClassW(&klass);
            RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
            double dpi=GetDpiForSystem()/96.0;
            int initial_width=std::min(static_cast<int>(1320*dpi),static_cast<int>(work.right-work.left-24));
            int initial_height=std::min(static_cast<int>(840*dpi),static_cast<int>(work.bottom-work.top-24));
            HWND window=CreateWindowExW(WS_EX_ACCEPTFILES,klass.lpszClassName,L"MindMap",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,initial_width,initial_height,nullptr,nullptr,instance,&app);
            if(!window)throw std::runtime_error("Creation de la fenetre impossible");
            DragAcceptFiles(window,TRUE);ShowWindow(window,smoke?SW_SHOWNOACTIVATE:show);UpdateWindow(window);
            MSG msg{};
            while(GetMessageW(&msg,nullptr,0,0)>0){
                if(msg.message==WM_KEYDOWN){
                    bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
                    HWND focus=GetFocus();bool editing=focus==app.title||focus==app.note||focus==app.search;
                    if(GetAncestor(focus,GA_ROOT)!=window){TranslateMessage(&msg);DispatchMessageW(&msg);continue;}
                    if(app.key_down(msg.wParam,control,shift,editing,focus))continue;
                }
                TranslateMessage(&msg);DispatchMessageW(&msg);
            }
        }
    }catch(const std::exception& e){MessageBoxW(nullptr,ui::wide(e.what()).c_str(),L"MindMap",MB_OK|MB_ICONERROR);result=1;}
    GdiplusShutdown(token);return result;
}
