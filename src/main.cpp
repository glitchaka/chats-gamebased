#include "ChatNet.h"
#include "SoundFx.h"
#include "resource.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <objidl.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"GameBasedChatWindow";
constexpr wchar_t kUnreadClass[] = L"GameBasedChatUnread";
constexpr UINT_PTR kNetworkTimer = 1;
constexpr UINT kSendMessageCommand = WM_APP + 1;
constexpr UINT kTrayMessage = WM_APP + 2;
constexpr UINT kTrayOpen = 4001;
constexpr UINT kTrayExit = 4002;
constexpr int kWindowWidth = 920;
constexpr int kWindowHeight = 620;
constexpr int kUnreadWidth = 250;
constexpr int kUnreadHeight = 42;
constexpr std::size_t kMaxStickerBytes = 512u * 1024u;

struct Theme {
    COLORREF bg;
    COLORREF frame;
    COLORREF accent;
    COLORREF accentBright;
    COLORREF panel;
    COLORREF panel2;
    COLORREF text;
    COLORREF muted;
};

const Theme kThemes[] = {
    {RGB(3,5,6), RGB(219,92,0), RGB(0,181,240), RGB(121,225,247), RGB(4,42,50), RGB(6,54,64), RGB(222,244,250), RGB(103,154,165)},
    {RGB(3,5,6), RGB(219,92,0), RGB(52,147,130), RGB(168,225,210), RGB(5,43,39), RGB(8,65,56), RGB(224,246,239), RGB(110,158,148)},
    {RGB(3,5,6), RGB(219,92,0), RGB(172,58,58), RGB(235,166,166), RGB(48,21,21), RGB(69,28,28), RGB(248,230,230), RGB(164,111,111)},
    {RGB(3,5,6), RGB(219,92,0), RGB(148,128,43), RGB(228,218,164), RGB(44,40,12), RGB(65,58,17), RGB(248,244,222), RGB(157,148,103)},
    {RGB(3,5,6), RGB(219,92,0), RGB(102,102,102), RGB(204,204,204), RGB(36,36,36), RGB(54,54,54), RGB(242,242,242), RGB(150,150,150)},
    {RGB(3,5,6), RGB(219,92,0), RGB(83,75,150), RGB(187,181,231), RGB(27,24,48), RGB(42,37,72), RGB(238,235,250), RGB(137,130,174)},
    {RGB(3,5,6), RGB(219,92,0), RGB(40,128,176), RGB(168,216,241), RGB(8,35,49), RGB(12,52,71), RGB(230,245,252), RGB(108,157,181)}
};

struct ChatLine {
    enum class Kind { Text, Sticker } kind{Kind::Text};
    std::wstring name;
    std::wstring text;
    std::wstring channel;
    std::filesystem::path stickerPath;
    bool local{};
};

struct FriendItem { std::string endpoint; std::string label; };
struct GroupItem { std::string name; std::vector<std::string> members; };
struct StickerItem { std::filesystem::path path; std::unique_ptr<Gdiplus::Bitmap> image; };
enum class TargetMode { All, Friend, Group };

struct App {
    HINSTANCE instance{};
    HWND hwnd{};
    HWND unreadHwnd{};
    HWND edit{};
    HWND friendEdit{};
    HWND groupEdit{};
    WNDPROC oldEditProc{};
    HBRUSH editBrush{};
    HFONT bodyFont{};
    HFONT smallFont{};

    chatnet::ChatNet net;
    sfx::SoundFx sounds;
    std::vector<ChatLine> lines;
    std::vector<FriendItem> friends;
    std::vector<GroupItem> groups;
    std::vector<StickerItem> stickers;

    std::unique_ptr<Gdiplus::Bitmap> fontAtlas;
    std::unique_ptr<Gdiplus::Bitmap> unreadBar;

    TargetMode targetMode{TargetMode::All};
    int selectedFriend{-1};
    int selectedGroup{-1};
    int themeIndex{0};
    int unreadCount{0};
    bool stickerMenuOpen{false};
    bool trayAdded{false};
    bool exiting{false};

    RECT closeRect{};
    RECT copyRect{};
    RECT themeRect{};
    RECT addFriendRect{};
    RECT addGroupRect{};
    RECT allRect{};
    RECT sendRect{};
    RECT stickerButtonRect{};
    RECT stickerImportRect{};
    RECT friendRows[7]{};
    RECT groupRows[4]{};
    std::vector<RECT> stickerMenuRects;

    std::wstring status;
};

const Theme& theme(const App& app) { return kThemes[app.themeIndex % static_cast<int>(std::size(kThemes))]; }

std::wstring utf8ToWide(const std::string& input)
{
    if (input.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), result.data(), needed);
    return result;
}

std::string wideToUtf8(const std::wstring& input)
{
    if (input.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), result.data(), needed, nullptr, nullptr);
    return result;
}

std::wstring readEdit(HWND edit)
{
    const int len = GetWindowTextLengthW(edit);
    if (len <= 0) return {};
    std::wstring value(static_cast<std::size_t>(len + 1), L'\0');
    const int copied = GetWindowTextW(edit, value.data(), len + 1);
    if (copied <= 0) return {};
    value.resize(static_cast<std::size_t>(copied));
    return value;
}

std::string defaultPlayerName()
{
    wchar_t computer[64]{};
    DWORD len = 64;
    if (!GetComputerNameW(computer, &len) || len == 0) return "PLAYER";
    std::string utf8 = wideToUtf8(std::wstring(computer, computer + len));
    if (utf8.size() > 24) utf8.resize(24);
    return utf8.empty() ? "PLAYER" : utf8;
}

std::filesystem::path executableDirectory()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD count = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (count == 0 || count >= MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buffer, buffer + count)).parent_path();
}

std::filesystem::path appDataDirectory()
{
    wchar_t path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return std::filesystem::path(path) / L"chats-gamebased";
    }
    return executableDirectory() / L"data";
}

std::filesystem::path stickerDirectory() { return appDataDirectory() / L"stickers"; }
std::filesystem::path receivedDirectory() { return appDataDirectory() / L"received"; }

bool isImageExtension(std::filesystem::path path)
{
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp";
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto size = file.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > kMaxStickerBytes) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file ? bytes : std::vector<std::uint8_t>{};
}

void loadBitmap(std::unique_ptr<Gdiplus::Bitmap>& target, const std::filesystem::path& path)
{
    target.reset();
    if (!std::filesystem::exists(path)) return;
    auto image = std::make_unique<Gdiplus::Bitmap>(path.c_str(), FALSE);
    if (image->GetLastStatus() == Gdiplus::Ok) target = std::move(image);
}

void loadUiAssets(App& app)
{
    const auto ui = executableDirectory() / L"assets" / L"ui";
    loadBitmap(app.fontAtlas, ui / L"font_atlas.png");
    loadBitmap(app.unreadBar, ui / L"unread_bar.png");
}

void loadUserStickers(App& app)
{
    app.stickers.clear();
    std::error_code ec;
    std::filesystem::create_directories(stickerDirectory(), ec);
    for (const auto& entry : std::filesystem::directory_iterator(stickerDirectory(), ec)) {
        if (ec || !entry.is_regular_file() || !isImageExtension(entry.path())) continue;
        auto image = std::make_unique<Gdiplus::Bitmap>(entry.path().c_str(), FALSE);
        if (image->GetLastStatus() != Gdiplus::Ok) continue;
        app.stickers.push_back(StickerItem{entry.path(), std::move(image)});
    }
    std::sort(app.stickers.begin(), app.stickers.end(), [](const StickerItem& a, const StickerItem& b) {
        std::error_code ea, eb;
        return std::filesystem::last_write_time(a.path, ea) > std::filesystem::last_write_time(b.path, eb);
    });
}

HFONT makeFont(int height, int weight)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

void fillSolid(HDC dc, const RECT& r, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void drawChamferedBox(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int cut = 8)
{
    POINT points[8] = {{rect.left+cut,rect.top},{rect.right-cut,rect.top},{rect.right,rect.top+cut},{rect.right,rect.bottom-cut},
                       {rect.right-cut,rect.bottom},{rect.left+cut,rect.bottom},{rect.left,rect.bottom-cut},{rect.left,rect.top+cut}};
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 2, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, 8);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
    DeleteObject(pen); DeleteObject(brush);
}

void drawText(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT flags)
{
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags);
    SelectObject(dc, oldFont);
}

int glyphIndex(wchar_t ch)
{
    if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
    if (ch >= L'A' && ch <= L'Z') return ch - L'A';
    if (ch >= L'0' && ch <= L'9') return 26 + (ch - L'0');
    return -1;
}

void drawPixelText(App& app, HDC dc, const std::wstring& text, int x, int y, int height)
{
    if (!app.fontAtlas) {
        RECT r{x,y,x+500,y+height+4};
        drawText(dc, text, r, app.smallFont, theme(app).text, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        return;
    }
    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    const int w = std::max(6, height * 6 / 7);
    int cursor = x;
    for (wchar_t ch : text) {
        if (ch == L' ') { cursor += w/2; continue; }
        const int index = glyphIndex(ch);
        if (index < 0) { cursor += w/2; continue; }
        const int col = index % 10;
        const int row = index / 10;
        g.DrawImage(app.fontAtlas.get(), Gdiplus::Rect(cursor,y,w,height), col*48,row*56,48,56,Gdiplus::UnitPixel);
        cursor += w;
    }
}

void drawImageFit(HDC dc, Gdiplus::Bitmap* image, const RECT& rect)
{
    if (!image || image->GetLastStatus() != Gdiplus::Ok) return;
    const UINT iw = image->GetWidth(), ih = image->GetHeight();
    if (!iw || !ih) return;
    const int bw = rect.right-rect.left, bh = rect.bottom-rect.top;
    const double scale = std::min(static_cast<double>(bw)/iw, static_cast<double>(bh)/ih);
    const int dw = std::max(1, static_cast<int>(iw*scale));
    const int dh = std::max(1, static_cast<int>(ih*scale));
    const int x = rect.left+(bw-dw)/2, y=rect.top+(bh-dh)/2;
    Gdiplus::Graphics g(dc); g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.DrawImage(image, Gdiplus::Rect(x,y,dw,dh));
}

void copyToClipboard(HWND hwnd, const std::wstring& value)
{
    if (value.empty() || !OpenClipboard(hwnd)) return;
    EmptyClipboard();
    const std::size_t bytes=(value.size()+1)*sizeof(wchar_t);
    HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if (mem) {
        void* ptr=GlobalLock(mem);
        if (ptr) { std::memcpy(ptr,value.c_str(),bytes); GlobalUnlock(mem); if(!SetClipboardData(CF_UNICODETEXT,mem)) GlobalFree(mem); }
        else GlobalFree(mem);
    }
    CloseClipboard();
}

void trimHistory(App& app)
{
    constexpr std::size_t maxHistory=160;
    if(app.lines.size()>maxHistory) app.lines.erase(app.lines.begin(),app.lines.begin()+static_cast<std::ptrdiff_t>(app.lines.size()-maxHistory));
}

int findFriend(const App& app,const std::string& endpoint)
{
    for(std::size_t i=0;i<app.friends.size();++i) if(app.friends[i].endpoint==endpoint) return static_cast<int>(i);
    return -1;
}

void ensureFriend(App& app,const std::string& endpoint,const std::string& label)
{
    if(endpoint.empty()) return;
    int found=findFriend(app,endpoint);
    if(found>=0){ if(!label.empty()) app.friends[static_cast<std::size_t>(found)].label=label; return; }
    app.friends.push_back({endpoint,label.empty()?endpoint:label});
}

int findGroup(const App& app,const std::string& name)
{
    for(std::size_t i=0;i<app.groups.size();++i) if(app.groups[i].name==name) return static_cast<int>(i);
    return -1;
}

std::vector<std::string> allFriendEndpoints(const App& app)
{
    std::vector<std::string> out; out.reserve(app.friends.size());
    for(const auto& f:app.friends) out.push_back(f.endpoint);
    return out;
}

std::wstring targetLabel(const App& app)
{
    if(app.targetMode==TargetMode::Friend&&app.selectedFriend>=0&&app.selectedFriend<static_cast<int>(app.friends.size())) return L"FRIEND "+utf8ToWide(app.friends[app.selectedFriend].label);
    if(app.targetMode==TargetMode::Group&&app.selectedGroup>=0&&app.selectedGroup<static_cast<int>(app.groups.size())) return L"GROUP "+utf8ToWide(app.groups[app.selectedGroup].name);
    return L"ALL FRIENDS";
}

bool sendTextToTarget(App& app,const std::string& text)
{
    if(app.targetMode==TargetMode::Friend&&app.selectedFriend>=0&&app.selectedFriend<static_cast<int>(app.friends.size())) return app.net.sendChatTo(app.friends[app.selectedFriend].endpoint,text);
    if(app.targetMode==TargetMode::Group&&app.selectedGroup>=0&&app.selectedGroup<static_cast<int>(app.groups.size())) { const auto& g=app.groups[app.selectedGroup]; return app.net.sendGroup(g.name,g.members,text); }
    return app.net.sendChat(text);
}

bool sendStickerToTarget(App& app,const StickerItem& sticker)
{
    const auto bytes=readBytes(sticker.path);
    if(bytes.empty()){app.status=L"STICKER TOO LARGE OR UNREADABLE";return false;}
    const std::string filename=wideToUtf8(sticker.path.filename().wstring());
    if(app.targetMode==TargetMode::Friend&&app.selectedFriend>=0&&app.selectedFriend<static_cast<int>(app.friends.size())) return app.net.sendStickerFileTo(app.friends[app.selectedFriend].endpoint,filename,bytes);
    if(app.targetMode==TargetMode::Group&&app.selectedGroup>=0&&app.selectedGroup<static_cast<int>(app.groups.size())) { const auto& g=app.groups[app.selectedGroup]; return app.net.sendGroupStickerFile(g.name,g.members,filename,bytes); }
    return app.net.sendStickerFile(filename,bytes);
}

void sendCurrentMessage(App& app)
{
    std::wstring text=readEdit(app.edit);
    while(!text.empty()&&(text.back()==L'\r'||text.back()==L'\n'||text.back()==L' ')) text.pop_back();
    if(text.empty()) return;
    if(!sendTextToTarget(app,wideToUtf8(text))) app.status=L"NETWORK SEND FAILED: "+utf8ToWide(app.net.lastError());
    ChatLine line; line.name=utf8ToWide(app.net.localName()); line.text=text; line.local=true;
    if(app.targetMode==TargetMode::Group&&app.selectedGroup>=0) line.channel=utf8ToWide(app.groups[app.selectedGroup].name);
    app.lines.push_back(std::move(line)); trimHistory(app); SetWindowTextW(app.edit,L""); app.sounds.play(sfx::Cue::Click); InvalidateRect(app.hwnd,nullptr,FALSE);
}

void sendUserSticker(App& app,std::size_t index)
{
    if(index>=app.stickers.size()) return;
    if(!sendStickerToTarget(app,app.stickers[index])) { app.status=L"STICKER SEND FAILED: "+utf8ToWide(app.net.lastError()); return; }
    ChatLine line; line.kind=ChatLine::Kind::Sticker; line.name=utf8ToWide(app.net.localName()); line.stickerPath=app.stickers[index].path; line.local=true;
    if(app.targetMode==TargetMode::Group&&app.selectedGroup>=0) line.channel=utf8ToWide(app.groups[app.selectedGroup].name);
    app.lines.push_back(std::move(line)); trimHistory(app); app.sounds.play(sfx::Cue::Sticker); app.stickerMenuOpen=false; InvalidateRect(app.hwnd,nullptr,FALSE);
}

bool importSticker(App& app)
{
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=app.hwnd; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=L"Sticker images\0*.png;*.jpg;*.jpeg;*.bmp\0PNG\0*.png\0JPEG\0*.jpg;*.jpeg\0Bitmap\0*.bmp\0\0";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameW(&ofn)) return false;
    std::filesystem::path source=file;
    if(!isImageExtension(source)){app.status=L"UNSUPPORTED IMAGE";return false;}
    std::error_code ec;
    const auto size=std::filesystem::file_size(source,ec);
    if(ec||size==0||size>kMaxStickerBytes){app.status=L"STICKER MUST BE 512 KB OR LESS";return false;}
    std::filesystem::create_directories(stickerDirectory(),ec);
    const auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::filesystem::path dest=stickerDirectory()/(std::to_wstring(stamp)+source.extension().wstring());
    std::filesystem::copy_file(source,dest,std::filesystem::copy_options::overwrite_existing,ec);
    if(ec){app.status=L"COULD NOT IMPORT STICKER";return false;}
    loadUserStickers(app); app.status=L"STICKER ADDED"; app.stickerMenuOpen=true; InvalidateRect(app.hwnd,nullptr,FALSE); return true;
}

void addFriendFromEdit(App& app)
{
    const std::string endpoint=wideToUtf8(readEdit(app.friendEdit)); if(endpoint.empty()) return;
    if(!app.net.addFriendAddress(endpoint)){app.status=L"BAD ADDRESS: "+utf8ToWide(app.net.lastError());return;}
    ensureFriend(app,endpoint,endpoint); app.selectedFriend=findFriend(app,endpoint); app.selectedGroup=-1; app.targetMode=TargetMode::Friend; SetWindowTextW(app.friendEdit,L""); InvalidateRect(app.hwnd,nullptr,FALSE);
}

void addGroupFromEdit(App& app)
{
    std::string name=wideToUtf8(readEdit(app.groupEdit)); if(name.empty()) return;
    if(app.friends.empty()){app.status=L"ADD FRIENDS BEFORE CREATING A GROUP";return;}
    if(name.size()>31) name.resize(31); int index=findGroup(app,name);
    if(index<0){app.groups.push_back({name,allFriendEndpoints(app)});index=static_cast<int>(app.groups.size())-1;} else app.groups[index].members=allFriendEndpoints(app);
    app.selectedGroup=index; app.selectedFriend=-1; app.targetMode=TargetMode::Group; SetWindowTextW(app.groupEdit,L""); InvalidateRect(app.hwnd,nullptr,FALSE);
}

std::filesystem::path saveIncomingSticker(const chatnet::Event& event)
{
    std::error_code ec; std::filesystem::create_directories(receivedDirectory(),ec);
    std::wstring filename=utf8ToWide(event.stickerFilename); if(filename.empty()) filename=L"sticker.png";
    const auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto path=receivedDirectory()/(std::to_wstring(stamp)+L"_"+filename);
    std::ofstream file(path,std::ios::binary); if(!file) return {};
    file.write(reinterpret_cast<const char*>(event.stickerData.data()),static_cast<std::streamsize>(event.stickerData.size()));
    return file?path:std::filesystem::path{};
}

void updateUnreadHud(App& app)
{
    if(!app.unreadHwnd) return;
    if(app.unreadCount<=0||IsWindowVisible(app.hwnd)){ShowWindow(app.unreadHwnd,SW_HIDE);return;}
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    SetWindowPos(app.unreadHwnd,HWND_TOPMOST,work.right-kUnreadWidth-18,work.top+18,kUnreadWidth,kUnreadHeight,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(app.unreadHwnd,nullptr,FALSE);
}

void restoreMain(App& app)
{
    app.unreadCount=0; updateUnreadHud(app); ShowWindow(app.hwnd,SW_SHOW); SetForegroundWindow(app.hwnd); SetFocus(app.edit); InvalidateRect(app.hwnd,nullptr,FALSE);
}

void noteIncoming(App& app)
{
    if(!IsWindowVisible(app.hwnd)){++app.unreadCount;updateUnreadHud(app);}
}

void updateNetwork(App& app)
{
    bool changed=false;
    for(const auto& event:app.net.update()){
        if(!event.endpoint.empty()) ensureFriend(app,event.endpoint,event.name);
        if(event.type==chatnet::EventType::Message){ChatLine line;line.name=utf8ToWide(event.name);line.text=utf8ToWide(event.text);line.channel=utf8ToWide(event.channel);app.lines.push_back(std::move(line));trimHistory(app);app.sounds.play(sfx::Cue::Incoming);noteIncoming(app);changed=true;}
        else if(event.type==chatnet::EventType::StickerImage){auto path=saveIncomingSticker(event);if(!path.empty()){ChatLine line;line.kind=ChatLine::Kind::Sticker;line.name=utf8ToWide(event.name);line.channel=utf8ToWide(event.channel);line.stickerPath=path;app.lines.push_back(std::move(line));trimHistory(app);app.sounds.play(sfx::Cue::Sticker);noteIncoming(app);}changed=true;}
        else changed=true;
    }
    const auto peers=app.net.peers(); app.status=app.net.isRunning()?(L"UDP  •  "+std::to_wstring(peers.size())+L" ONLINE  •  "+targetLabel(app)):L"NETWORK OFFLINE";
    if(!app.net.publicEndpoint().empty()) app.status+=L"  •  PUBLIC READY";
    if(changed) InvalidateRect(app.hwnd,nullptr,FALSE);
}

LRESULT CALLBACK editSubclass(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    App* app=reinterpret_cast<App*>(GetPropW(hwnd,L"GameChatApp"));
    if(app&&msg==WM_KEYDOWN&&wParam==VK_RETURN){PostMessageW(app->hwnd,kSendMessageCommand,0,0);return 0;}
    if(app&&msg==WM_CHAR&&wParam==VK_RETURN) return 0;
    return app&&app->oldEditProc?CallWindowProcW(app->oldEditProc,hwnd,msg,wParam,lParam):DefWindowProcW(hwnd,msg,wParam,lParam);
}

HWND createEdit(HWND parent,HFONT font,int limit)
{
    HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,10,10,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(edit,EM_SETLIMITTEXT,limit,0);SendMessageW(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(4,4));return edit;
}

void layout(App& app)
{
    app.closeRect={866,16,900,50}; app.copyRect={776,88,892,120}; app.themeRect={650,88,766,120}; app.allRect={636,150,884,178};
    app.addFriendRect={810,190,884,220}; app.addGroupRect={810,386,884,416}; app.stickerButtonRect={640,522,774,570}; app.sendRect={786,522,892,570};
    if(app.edit) SetWindowPos(app.edit,nullptr,40,531,584,29,SWP_NOZORDER|SWP_NOACTIVATE);
    if(app.friendEdit) SetWindowPos(app.friendEdit,nullptr,638,194,160,24,SWP_NOZORDER|SWP_NOACTIVATE);
    if(app.groupEdit) SetWindowPos(app.groupEdit,nullptr,638,390,160,24,SWP_NOZORDER|SWP_NOACTIVATE);
    for(int i=0;i<7;++i) app.friendRows[i]={638,226+i*24,884,248+i*24};
    for(int i=0;i<4;++i) app.groupRows[i]={638,422+i*22,884,442+i*22};
}

void paintStickerPopup(App& app,HDC dc)
{
    app.stickerMenuRects.clear(); if(!app.stickerMenuOpen) return;
    const Theme& t=theme(app); RECT popup{594,326,900,512}; drawChamferedBox(dc,popup,t.panel2,t.accentBright,8);
    drawPixelText(app,dc,L"MY STICKERS",610,338,15);
    app.stickerImportRect={782,334,884,364}; drawChamferedBox(dc,app.stickerImportRect,t.accent,t.accentBright,6); drawPixelText(app,dc,L"ADD",814,342,13);
    const std::size_t visible=std::min<std::size_t>(app.stickers.size(),8);
    for(std::size_t i=0;i<visible;++i){int col=static_cast<int>(i%4),row=static_cast<int>(i/4);RECT r{610+col*68,374+row*62,666+col*68,426+row*62};drawChamferedBox(dc,r,t.panel,t.accent,6);drawImageFit(dc,app.stickers[i].image.get(),RECT{r.left+4,r.top+4,r.right-4,r.bottom-4});app.stickerMenuRects.push_back(r);}
    if(app.stickers.empty()){RECT empty{610,390,870,430};drawText(dc,L"NO STICKERS - ADD AN IMAGE",empty,app.smallFont,t.muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
}

void paint(App& app,HDC target)
{
    RECT client{};GetClientRect(app.hwnd,&client);HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,client.right,client.bottom);HGDIOBJ oldBitmap=SelectObject(dc,bitmap);
    const Theme& t=theme(app);fillSolid(dc,client,t.bg);
    HPEN frame=CreatePen(PS_SOLID,3,t.frame);HGDIOBJ op=SelectObject(dc,frame),ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));RoundRect(dc,6,6,client.right-6,client.bottom-6,22,22);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(frame);
    drawPixelText(app,dc,L"GAME CHAT",24,22,27);drawText(dc,L"DIRECT INTERNET  //  FRIENDS  //  GROUPS",RECT{24,60,600,82},app.smallFont,t.frame,DT_LEFT|DT_SINGLELINE);
    drawChamferedBox(dc,app.closeRect,RGB(48,18,18),RGB(232,94,94),6);drawPixelText(app,dc,L"X",877,25,15);
    RECT share{24,84,900,126};drawChamferedBox(dc,share,t.panel,t.accent,8);drawPixelText(app,dc,L"MY ADDRESS",38,96,13);
    std::string endpoint=app.net.publicEndpoint().empty()?app.net.localEndpoint():app.net.publicEndpoint();drawText(dc,utf8ToWide(endpoint),RECT{156,91,640,117},app.bodyFont,t.accentBright,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    drawChamferedBox(dc,app.themeRect,t.panel2,t.accentBright,6);drawText(dc,L"THEME "+std::to_wstring(app.themeIndex+1),app.themeRect,app.smallFont,t.text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    drawChamferedBox(dc,app.copyRect,t.panel2,t.accentBright,6);drawPixelText(app,dc,L"COPY",810,98,13);
    RECT messages{24,140,610,506},side{624,140,900,506};drawChamferedBox(dc,messages,t.panel,t.accent,10);drawChamferedBox(dc,side,t.panel,t.accent,10);
    RECT mt{40,130,188,158},st{640,130,782,158};drawChamferedBox(dc,mt,t.accent,t.accentBright,7);drawChamferedBox(dc,st,t.accent,t.accentBright,7);drawPixelText(app,dc,L"MESSAGES",61,138,13);drawPixelText(app,dc,L"CONTACTS",657,138,13);
    drawChamferedBox(dc,app.allRect,app.targetMode==TargetMode::All?t.accent:t.panel2,t.accentBright,6);drawText(dc,L"ALL FRIENDS",app.allRect,app.smallFont,t.text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    drawText(dc,L"ADD IP/HOST:PORT",RECT{638,176,798,192},app.smallFont,t.frame,DT_LEFT|DT_SINGLELINE);drawChamferedBox(dc,app.addFriendRect,t.panel2,t.accentBright,6);drawPixelText(app,dc,L"ADD",827,198,12);
    for(int i=0;i<7&&i<static_cast<int>(app.friends.size());++i){bool sel=app.targetMode==TargetMode::Friend&&app.selectedFriend==i;drawChamferedBox(dc,app.friendRows[i],sel?t.accent:t.panel2,sel?t.accentBright:t.muted,5);RECT r=app.friendRows[i];r.left+=8;drawText(dc,utf8ToWide(app.friends[i].label),r,app.smallFont,sel?t.text:t.accentBright,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}
    drawText(dc,L"NEW GROUP (ALL FRIENDS)",RECT{638,370,802,388},app.smallFont,t.frame,DT_LEFT|DT_SINGLELINE);drawChamferedBox(dc,app.addGroupRect,t.panel2,t.accentBright,6);drawPixelText(app,dc,L"NEW",827,394,12);
    for(int i=0;i<4&&i<static_cast<int>(app.groups.size());++i){bool sel=app.targetMode==TargetMode::Group&&app.selectedGroup==i;drawChamferedBox(dc,app.groupRows[i],sel?t.accent:t.panel2,sel?t.accentBright:t.muted,5);RECT r=app.groupRows[i];r.left+=8;drawText(dc,L"# "+utf8ToWide(app.groups[i].name),r,app.smallFont,t.text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}
    HRGN clip=CreateRectRgn(messages.left+12,messages.top+16,messages.right-12,messages.bottom-12);SelectClipRgn(dc,clip);int y=messages.top+22;int start=std::max(0,static_cast<int>(app.lines.size())-10);
    for(int i=start;i<static_cast<int>(app.lines.size());++i){const auto& line=app.lines[i];RECT nr{messages.left+18,y,messages.left+160,y+20};drawText(dc,line.name+L":",nr,app.smallFont,line.local?t.accentBright:RGB(71,197,157),DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);if(line.kind==ChatLine::Kind::Text){drawText(dc,line.text,RECT{messages.left+164,y,messages.right-16,y+22},app.smallFont,t.text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);y+=27;}else{auto img=std::make_unique<Gdiplus::Bitmap>(line.stickerPath.c_str(),FALSE);RECT sr{messages.left+164,y-3,messages.left+240,y+69};drawChamferedBox(dc,sr,t.panel2,t.accent,6);if(img->GetLastStatus()==Gdiplus::Ok)drawImageFit(dc,img.get(),RECT{sr.left+4,sr.top+4,sr.right-4,sr.bottom-4});y+=78;}}
    SelectClipRgn(dc,nullptr);DeleteObject(clip);
    RECT input{24,518,628,578};drawChamferedBox(dc,input,t.panel2,t.accent,9);RECT tab{40,508,214,536};drawChamferedBox(dc,tab,t.accent,t.accentBright,6);drawText(dc,targetLabel(app),tab,app.smallFont,t.bg,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    drawChamferedBox(dc,app.stickerButtonRect,t.panel2,t.accentBright,8);drawPixelText(app,dc,L"STICKERS",657,537,12);drawChamferedBox(dc,app.sendRect,t.panel2,t.accentBright,8);drawPixelText(app,dc,L"SEND",810,537,12);
    drawText(dc,app.status,RECT{24,584,892,610},app.smallFont,t.muted,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    paintStickerPopup(app,dc);
    BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);
}

void paintUnread(App& app,HDC target)
{
    RECT r{};GetClientRect(app.unreadHwnd,&r);const Theme& t=theme(app);fillSolid(target,r,t.bg);drawChamferedBox(target,RECT{1,1,r.right-1,r.bottom-1},t.panel2,t.frame,7);
    const int segments=10,filled=std::min(app.unreadCount,segments);for(int i=0;i<segments;++i){RECT s{12+i*18,13,26+i*18,29};fillSolid(target,s,i<filled?t.accentBright:t.panel);}
    drawText(target,std::to_wstring(app.unreadCount),RECT{202,8,240,34},app.smallFont,t.text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

void addTrayIcon(App& app)
{
    NOTIFYICONDATAW nid{};nid.cbSize=sizeof(nid);nid.hWnd=app.hwnd;nid.uID=1;nid.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;nid.uCallbackMessage=kTrayMessage;
    nid.hIcon=LoadIconW(app.instance,MAKEINTRESOURCEW(IDI_APP_ICON));if(!nid.hIcon)nid.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(nid.szTip,L"Game Chat");
    app.trayAdded=Shell_NotifyIconW(NIM_ADD,&nid)!=FALSE;
}

void removeTrayIcon(App& app)
{
    if(!app.trayAdded)return;NOTIFYICONDATAW nid{};nid.cbSize=sizeof(nid);nid.hWnd=app.hwnd;nid.uID=1;Shell_NotifyIconW(NIM_DELETE,&nid);app.trayAdded=false;
}

LRESULT CALLBACK unreadProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(msg==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(lParam);app=static_cast<App*>(cs->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(hwnd,msg,wParam,lParam);
    if(msg==WM_LBUTTONUP){restoreMain(*app);return 0;}if(msg==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);paintUnread(*app,dc);EndPaint(hwnd,&ps);return 0;}if(msg==WM_ERASEBKGND)return 1;return DefWindowProcW(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(msg==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(lParam);app=static_cast<App*>(cs->lpCreateParams);app->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(hwnd,msg,wParam,lParam);
    switch(msg){
    case WM_CREATE: app->bodyFont=makeFont(18,FW_BOLD);app->smallFont=makeFont(14,FW_BOLD);app->editBrush=CreateSolidBrush(theme(*app).panel2);app->edit=createEdit(hwnd,app->bodyFont,900);SetPropW(app->edit,L"GameChatApp",app);app->oldEditProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(app->edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editSubclass)));app->friendEdit=createEdit(hwnd,app->smallFont,120);app->groupEdit=createEdit(hwnd,app->smallFont,31);layout(*app);SetTimer(hwnd,kNetworkTimer,60,nullptr);addTrayIcon(*app);return 0;
    case WM_TIMER: if(wParam==kNetworkTimer)updateNetwork(*app);return 0;
    case kSendMessageCommand:sendCurrentMessage(*app);return 0;
    case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(wParam),theme(*app).text);SetBkColor(reinterpret_cast<HDC>(wParam),theme(*app).panel2);return reinterpret_cast<INT_PTR>(app->editBrush);
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);updateUnreadHud(*app);return 0;
    case kTrayMessage:if(lParam==WM_LBUTTONUP||lParam==WM_LBUTTONDBLCLK){restoreMain(*app);return 0;}if(lParam==WM_RBUTTONUP){POINT p{};GetCursorPos(&p);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,kTrayOpen,L"Open chat");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,kTrayExit,L"Exit");SetForegroundWindow(hwnd);TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);DestroyMenu(menu);return 0;}break;
    case WM_COMMAND:if(LOWORD(wParam)==kTrayOpen){restoreMain(*app);return 0;}if(LOWORD(wParam)==kTrayExit){app->exiting=true;DestroyWindow(hwnd);return 0;}break;
    case WM_LBUTTONUP:{POINT pt{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        if(PtInRect(&app->closeRect,pt)){ShowWindow(hwnd,SW_HIDE);updateUnreadHud(*app);return 0;}
        if(PtInRect(&app->copyRect,pt)){std::string value=app->net.publicEndpoint().empty()?app->net.localEndpoint():app->net.publicEndpoint();copyToClipboard(hwnd,utf8ToWide(value));app->status=L"ADDRESS COPIED";InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(PtInRect(&app->themeRect,pt)){app->themeIndex=(app->themeIndex+1)%static_cast<int>(std::size(kThemes));if(app->editBrush)DeleteObject(app->editBrush);app->editBrush=CreateSolidBrush(theme(*app).panel2);InvalidateRect(hwnd,nullptr,TRUE);InvalidateRect(app->unreadHwnd,nullptr,TRUE);return 0;}
        if(PtInRect(&app->addFriendRect,pt)){addFriendFromEdit(*app);return 0;}if(PtInRect(&app->addGroupRect,pt)){addGroupFromEdit(*app);return 0;}
        if(PtInRect(&app->allRect,pt)){app->targetMode=TargetMode::All;app->selectedFriend=app->selectedGroup=-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(PtInRect(&app->sendRect,pt)){sendCurrentMessage(*app);SetFocus(app->edit);return 0;}
        if(PtInRect(&app->stickerButtonRect,pt)){app->stickerMenuOpen=!app->stickerMenuOpen;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(app->stickerMenuOpen&&PtInRect(&app->stickerImportRect,pt)){importSticker(*app);return 0;}
        if(app->stickerMenuOpen){for(std::size_t i=0;i<app->stickerMenuRects.size();++i)if(PtInRect(&app->stickerMenuRects[i],pt)){sendUserSticker(*app,i);return 0;}}
        for(int i=0;i<7&&i<static_cast<int>(app->friends.size());++i)if(PtInRect(&app->friendRows[i],pt)){app->targetMode=TargetMode::Friend;app->selectedFriend=i;app->selectedGroup=-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        for(int i=0;i<4&&i<static_cast<int>(app->groups.size());++i)if(PtInRect(&app->groupRows[i],pt)){app->targetMode=TargetMode::Group;app->selectedGroup=i;app->selectedFriend=-1;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        break;}
    case WM_NCHITTEST:{LRESULT base=DefWindowProcW(hwnd,msg,wParam,lParam);if(base!=HTCLIENT)return base;POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(hwnd,&p);if(p.y<76&&p.x<840)return HTCAPTION;return HTCLIENT;}
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);paint(*app,dc);EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_DESTROY:KillTimer(hwnd,kNetworkTimer);removeTrayIcon(*app);app->net.stop();if(app->edit)RemovePropW(app->edit,L"GameChatApp");if(app->editBrush)DeleteObject(app->editBrush);if(app->bodyFont)DeleteObject(app->bodyFont);if(app->smallFont)DeleteObject(app->smallFont);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return 1;
    App app;app.instance=instance;loadUiAssets(app);loadUserStickers(app);app.net.start(defaultPlayerName(),34197);
    HICON icon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON));
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=wndProc;wc.lpszClassName=kWindowClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=icon;wc.hIconSm=icon;RegisterClassExW(&wc);
    WNDCLASSEXW uwc{};uwc.cbSize=sizeof(uwc);uwc.hInstance=instance;uwc.lpfnWndProc=unreadProc;uwc.lpszClassName=kUnreadClass;uwc.hCursor=LoadCursorW(nullptr,IDC_HAND);RegisterClassExW(&uwc);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int x=std::max(work.left,work.right-kWindowWidth-24),y=work.top+24;
    HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,kWindowClass,L"Game Chat",WS_POPUP,x,y,kWindowWidth,kWindowHeight,nullptr,nullptr,instance,&app);if(!hwnd){Gdiplus::GdiplusShutdown(token);return 1;}
    app.unreadHwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,kUnreadClass,L"",WS_POPUP,0,0,kUnreadWidth,kUnreadHeight,nullptr,nullptr,instance,&app);
    HRGN region=CreateRoundRectRgn(0,0,kWindowWidth+1,kWindowHeight+1,24,24);SetWindowRgn(hwnd,region,TRUE);const DWORD corner=2;DwmSetWindowAttribute(hwnd,33,&corner,sizeof(corner));
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);SetFocus(app.edit);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}app.fontAtlas.reset();app.unreadBar.reset();Gdiplus::GdiplusShutdown(token);return static_cast<int>(msg.wParam);
}
