#include "ChatNet.h"
#include "SoundFx.h"
#include "resource.h"
#include "PixelFontData.h"

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
constexpr int kUnreadWidth = 190;
constexpr int kUnreadHeight = 50;
constexpr int kUnreadMax = 10;
constexpr std::size_t kMaxStickerBytes = 512u * 1024u;

struct Theme {
    const wchar_t* name;
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
    {L"CYAN",   RGB(3,5,6), RGB(219,92,0), RGB(0,181,240), RGB(121,225,247), RGB(4,42,50), RGB(6,54,64), RGB(222,244,250), RGB(103,154,165)},
    {L"GREEN",  RGB(3,5,6), RGB(219,92,0), RGB(52,147,130), RGB(168,225,210), RGB(5,43,39), RGB(8,65,56), RGB(224,246,239), RGB(110,158,148)},
    {L"RED",    RGB(3,5,6), RGB(219,92,0), RGB(172,58,58), RGB(235,166,166), RGB(48,21,21), RGB(69,28,28), RGB(248,230,230), RGB(164,111,111)},
    {L"GOLD",   RGB(3,5,6), RGB(219,92,0), RGB(148,128,43), RGB(228,218,164), RGB(44,40,12), RGB(65,58,17), RGB(248,244,222), RGB(157,148,103)},
    {L"SILVER", RGB(3,5,6), RGB(219,92,0), RGB(102,102,102), RGB(204,204,204), RGB(36,36,36), RGB(54,54,54), RGB(242,242,242), RGB(150,150,150)},
    {L"VIOLET", RGB(3,5,6), RGB(219,92,0), RGB(83,75,150), RGB(187,181,231), RGB(27,24,48), RGB(42,37,72), RGB(238,235,250), RGB(137,130,174)},
    {L"BLUE",   RGB(3,5,6), RGB(219,92,0), RGB(40,128,176), RGB(168,216,241), RGB(8,35,49), RGB(12,52,71), RGB(230,245,252), RGB(108,157,181)}
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
    HFONT editFont{};
    HFONT smallEditFont{};

    chatnet::ChatNet net;
    sfx::SoundFx sounds;
    std::vector<ChatLine> lines;
    std::vector<FriendItem> friends;
    std::vector<GroupItem> groups;
    std::vector<StickerItem> stickers;

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

const Theme& theme(const App& app) {
    return kThemes[app.themeIndex % static_cast<int>(std::size(kThemes))];
}

std::wstring utf8ToWide(const std::string& input) {
    if (input.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), result.data(), needed);
    return result;
}

std::string wideToUtf8(const std::wstring& input) {
    if (input.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), result.data(), needed, nullptr, nullptr);
    return result;
}

std::wstring readEdit(HWND edit) {
    const int len = GetWindowTextLengthW(edit);
    if (len <= 0) return {};
    std::wstring value(static_cast<std::size_t>(len + 1), L'\0');
    const int copied = GetWindowTextW(edit, value.data(), len + 1);
    if (copied <= 0) return {};
    value.resize(static_cast<std::size_t>(copied));
    return value;
}

std::string defaultPlayerName() {
    wchar_t computer[64]{};
    DWORD len = 64;
    if (!GetComputerNameW(computer, &len) || len == 0) return "PLAYER";
    std::string utf8 = wideToUtf8(std::wstring(computer, computer + len));
    if (utf8.size() > 24) utf8.resize(24);
    return utf8.empty() ? "PLAYER" : utf8;
}

std::filesystem::path executableDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD count = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (count == 0 || count >= MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buffer, buffer + count)).parent_path();
}

std::filesystem::path appDataDirectory() {
    wchar_t path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return std::filesystem::path(path) / L"chats-gamebased";
    }
    return executableDirectory() / L"data";
}

std::filesystem::path stickerDirectory() { return appDataDirectory() / L"stickers"; }
std::filesystem::path receivedDirectory() { return appDataDirectory() / L"received"; }

bool isImageExtension(std::filesystem::path path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp";
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto size = file.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > kMaxStickerBytes) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file ? bytes : std::vector<std::uint8_t>{};
}

void loadBitmap(std::unique_ptr<Gdiplus::Bitmap>& target, const std::filesystem::path& path) {
