#include "ChatNet.h"
#include "SoundFx.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"GameBasedChatWindow";
constexpr UINT_PTR kNetworkTimer = 1;
constexpr UINT kSendMessageCommand = WM_APP + 1;
constexpr int kWindowWidth = 920;
constexpr int kWindowHeight = 620;

constexpr COLORREF kBg = RGB(3, 5, 6);
constexpr COLORREF kOrange = RGB(219, 92, 0);
constexpr COLORREF kCyan = RGB(0, 181, 240);
constexpr COLORREF kCyanBright = RGB(121, 225, 247);
constexpr COLORREF kPanel = RGB(4, 42, 50);
constexpr COLORREF kPanel2 = RGB(6, 54, 64);
constexpr COLORREF kText = RGB(222, 244, 250);
constexpr COLORREF kMuted = RGB(103, 154, 165);
constexpr COLORREF kGreen = RGB(71, 197, 157);
constexpr COLORREF kRed = RGB(232, 94, 94);
constexpr COLORREF kGold = RGB(220, 203, 79);

struct ChatLine {
    enum class Kind { Text, Sticker } kind{Kind::Text};
    std::wstring name;
    std::wstring text;
    std::wstring channel;
    std::uint8_t stickerId{};
    bool local{};
};

struct FriendItem {
    std::string endpoint;
    std::string label;
};

struct GroupItem {
    std::string name;
    std::vector<std::string> members;
};

enum class TargetMode {
    All,
    Friend,
    Group
};

struct App {
    HWND hwnd{};
    HWND edit{};
    HWND friendEdit{};
    HWND groupEdit{};
    WNDPROC oldEditProc{};
    HBRUSH editBrush{};
    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};

    chatnet::ChatNet net;
    sfx::SoundFx sounds;
    std::vector<ChatLine> lines;
    std::vector<FriendItem> friends;
    std::vector<GroupItem> groups;

    TargetMode targetMode{TargetMode::All};
    int selectedFriend{-1};
    int selectedGroup{-1};

    RECT sendRect{};
    RECT closeRect{};
    RECT copyRect{};
    RECT addFriendRect{};
    RECT addGroupRect{};
    RECT allRect{};
    RECT stickerRects[4]{};
    RECT friendRows[7]{};
    RECT groupRows[4]{};

    std::array<std::unique_ptr<Gdiplus::Bitmap>, 4> stickerImages;
    std::wstring status;
};

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
    std::wstring name(computer, computer + len);
    std::string utf8 = wideToUtf8(name);
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

void loadStickerAssets(App& app)
{
    const auto root = executableDirectory() / L"assets" / L"stickers";
    const wchar_t* extensions[] = {L".png", L".jpg", L".bmp"};

    for (int i = 0; i < 4; ++i) {
        for (const wchar_t* ext : extensions) {
            const auto path = root / (std::to_wstring(i + 1) + ext);
            if (!std::filesystem::exists(path)) continue;
            auto image = std::make_unique<Gdiplus::Bitmap>(path.c_str(), FALSE);
            if (image->GetLastStatus() == Gdiplus::Ok) {
                app.stickerImages[static_cast<std::size_t>(i)] = std::move(image);
                break;
            }
        }
    }
}

HFONT makeFont(int height, int weight)
{
    return CreateFontW(
        -height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

void drawChamferedBox(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int cut = 8)
{
    POINT points[8] = {
        {rect.left + cut, rect.top},
        {rect.right - cut, rect.top},
        {rect.right, rect.top + cut},
        {rect.right, rect.bottom - cut},
        {rect.right - cut, rect.bottom},
        {rect.left + cut, rect.bottom},
        {rect.left, rect.bottom - cut},
        {rect.left, rect.top + cut}
    };

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 2, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, 8);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawText(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT flags)
{
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags);
    SelectObject(dc, oldFont);
}

void fillSolid(HDC dc, const RECT& r, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void drawStickerFallback(HDC dc, const RECT& bounds, std::uint8_t id)
{
    const int w = bounds.right - bounds.left;
    const int h = bounds.bottom - bounds.top;
    const int s = std::max(2, std::min(w, h) / 10);

    auto px = [&](int x, int y, int ww, int hh, COLORREF color) {
        RECT r{bounds.left + x * s, bounds.top + y * s,
               bounds.left + (x + ww) * s, bounds.top + (y + hh) * s};
        fillSolid(dc, r, color);
    };

    switch (id) {
    case 1:
        px(2, 2, 2, 2, kRed); px(6, 2, 2, 2, kRed);
        px(1, 3, 8, 3, kRed); px(2, 6, 6, 1, kRed);
        px(3, 7, 4, 1, kRed); px(4, 8, 2, 1, kRed);
        break;
    case 2:
        px(5, 1, 3, 2, kGold); px(4, 3, 3, 2, kGold);
        px(3, 5, 4, 2, kGold); px(2, 7, 3, 1, kGold);
        px(4, 7, 2, 2, kGold); px(3, 9, 1, 1, kGold);
        break;
    case 3:
        px(2, 2, 6, 1, kGreen); px(1, 3, 1, 5, kGreen); px(8, 3, 1, 5, kGreen);
        px(2, 8, 6, 1, kGreen); px(3, 4, 1, 1, kCyanBright); px(6, 4, 1, 1, kCyanBright);
        px(3, 6, 1, 1, kCyanBright); px(4, 7, 2, 1, kCyanBright); px(6, 6, 1, 1, kCyanBright);
        break;
    case 4:
        px(3, 2, 4, 1, kText); px(2, 3, 6, 4, kText); px(3, 7, 4, 2, kText);
        px(3, 4, 1, 2, kBg); px(6, 4, 1, 2, kBg); px(4, 6, 2, 1, kBg);
        px(4, 8, 1, 1, kBg); px(6, 8, 1, 1, kBg);
        break;
    }
}

void drawSticker(App& app, HDC dc, const RECT& bounds, std::uint8_t id)
{
    drawChamferedBox(dc, bounds, RGB(7, 36, 43), kCyan, 6);
    if (id >= 1 && id <= 4) {
        auto& image = app.stickerImages[static_cast<std::size_t>(id - 1)];
        if (image && image->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics graphics(dc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            const int pad = 3;
            graphics.DrawImage(image.get(),
                               Gdiplus::Rect(bounds.left + pad, bounds.top + pad,
                                             (bounds.right - bounds.left) - pad * 2,
                                             (bounds.bottom - bounds.top) - pad * 2));
            return;
        }
    }
    drawStickerFallback(dc, bounds, id);
}

void copyToClipboard(HWND hwnd, const std::wstring& value)
{
    if (value.empty() || !OpenClipboard(hwnd)) return;
    EmptyClipboard();
    const std::size_t bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* ptr = GlobalLock(memory);
        if (ptr) {
            std::memcpy(ptr, value.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void trimHistory(App& app)
{
    constexpr std::size_t kMaxHistory = 160;
    if (app.lines.size() > kMaxHistory) {
        app.lines.erase(app.lines.begin(), app.lines.begin() + static_cast<std::ptrdiff_t>(app.lines.size() - kMaxHistory));
    }
}

int findFriend(const App& app, const std::string& endpoint)
{
    for (std::size_t i = 0; i < app.friends.size(); ++i) {
        if (app.friends[i].endpoint == endpoint) return static_cast<int>(i);
    }
    return -1;
}

void ensureFriend(App& app, const std::string& endpoint, const std::string& label)
{
    if (endpoint.empty()) return;
    const int found = findFriend(app, endpoint);
    if (found >= 0) {
        if (!label.empty()) app.friends[static_cast<std::size_t>(found)].label = label;
        return;
    }
    app.friends.push_back(FriendItem{endpoint, label.empty() ? endpoint : label});
}

int findGroup(const App& app, const std::string& name)
{
    for (std::size_t i = 0; i < app.groups.size(); ++i) {
        if (app.groups[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void ensureIncomingGroup(App& app, const std::string& channel, const std::string& senderEndpoint)
{
    if (channel.empty()) return;
    int index = findGroup(app, channel);
    if (index < 0) {
        GroupItem group;
        group.name = channel;
        for (const auto& friendItem : app.friends) group.members.push_back(friendItem.endpoint);
        if (std::find(group.members.begin(), group.members.end(), senderEndpoint) == group.members.end()) {
            group.members.push_back(senderEndpoint);
        }
        app.groups.push_back(std::move(group));
    }
}

std::vector<std::string> allFriendEndpoints(const App& app)
{
    std::vector<std::string> result;
    result.reserve(app.friends.size());
    for (const auto& item : app.friends) result.push_back(item.endpoint);
    return result;
}

std::wstring targetLabel(const App& app)
{
    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 &&
        app.selectedFriend < static_cast<int>(app.friends.size())) {
        return L"FRIEND: " + utf8ToWide(app.friends[static_cast<std::size_t>(app.selectedFriend)].label);
    }
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 &&
        app.selectedGroup < static_cast<int>(app.groups.size())) {
        return L"GROUP: " + utf8ToWide(app.groups[static_cast<std::size_t>(app.selectedGroup)].name);
    }
    return L"ALL FRIENDS";
}

bool sendTextToTarget(App& app, const std::string& text)
{
    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 &&
        app.selectedFriend < static_cast<int>(app.friends.size())) {
        return app.net.sendChatTo(app.friends[static_cast<std::size_t>(app.selectedFriend)].endpoint, text);
    }
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 &&
        app.selectedGroup < static_cast<int>(app.groups.size())) {
        const auto& group = app.groups[static_cast<std::size_t>(app.selectedGroup)];
        return app.net.sendGroup(group.name, group.members, text);
    }
    return app.net.sendChat(text);
}

bool sendStickerToTarget(App& app, std::uint8_t stickerId)
{
    if (app.targetMode == TargetMode::Friend && app.selectedFriend >= 0 &&
        app.selectedFriend < static_cast<int>(app.friends.size())) {
        return app.net.sendStickerTo(app.friends[static_cast<std::size_t>(app.selectedFriend)].endpoint, stickerId);
    }
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 &&
        app.selectedGroup < static_cast<int>(app.groups.size())) {
        const auto& group = app.groups[static_cast<std::size_t>(app.selectedGroup)];
        return app.net.sendGroupSticker(group.name, group.members, stickerId);
    }
    return app.net.sendSticker(stickerId);
}

void sendCurrentMessage(App& app)
{
    std::wstring text = readEdit(app.edit);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    if (text.empty()) return;

    const std::string utf8 = wideToUtf8(text);
    if (!sendTextToTarget(app, utf8)) app.status = L"NETWORK SEND FAILED: " + utf8ToWide(app.net.lastError());

    ChatLine line;
    line.kind = ChatLine::Kind::Text;
    line.name = utf8ToWide(app.net.localName());
    line.text = text;
    line.local = true;
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 &&
        app.selectedGroup < static_cast<int>(app.groups.size())) {
        line.channel = utf8ToWide(app.groups[static_cast<std::size_t>(app.selectedGroup)].name);
    }
    app.lines.push_back(std::move(line));
    trimHistory(app);
    SetWindowTextW(app.edit, L"");
    app.sounds.play(sfx::Cue::Click);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void sendSticker(App& app, std::uint8_t stickerId)
{
    if (!sendStickerToTarget(app, stickerId)) {
        app.status = L"STICKER SEND FAILED: " + utf8ToWide(app.net.lastError());
        return;
    }

    ChatLine line;
    line.kind = ChatLine::Kind::Sticker;
    line.name = utf8ToWide(app.net.localName());
    line.stickerId = stickerId;
    line.local = true;
    if (app.targetMode == TargetMode::Group && app.selectedGroup >= 0 &&
        app.selectedGroup < static_cast<int>(app.groups.size())) {
        line.channel = utf8ToWide(app.groups[static_cast<std::size_t>(app.selectedGroup)].name);
    }
    app.lines.push_back(std::move(line));
    trimHistory(app);
    app.sounds.play(sfx::Cue::Sticker);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void addFriendFromEdit(App& app)
{
    const std::string endpoint = wideToUtf8(readEdit(app.friendEdit));
    if (endpoint.empty()) return;
    if (!app.net.addFriendAddress(endpoint)) {
        app.status = L"BAD ADDRESS: " + utf8ToWide(app.net.lastError());
        return;
    }
    ensureFriend(app, endpoint, endpoint);
    app.selectedFriend = findFriend(app, endpoint);
    app.selectedGroup = -1;
    app.targetMode = TargetMode::Friend;
    SetWindowTextW(app.friendEdit, L"");
    app.sounds.play(sfx::Cue::Click);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void addGroupFromEdit(App& app)
{
    const std::string name = wideToUtf8(readEdit(app.groupEdit));
    if (name.empty()) return;
    if (app.friends.empty()) {
        app.status = L"ADD FRIENDS BEFORE CREATING A GROUP";
        return;
    }

    int index = findGroup(app, name);
    if (index < 0) {
        GroupItem group;
        group.name = name.substr(0, 31);
        group.members = allFriendEndpoints(app);
        app.groups.push_back(std::move(group));
        index = static_cast<int>(app.groups.size()) - 1;
    } else {
        app.groups[static_cast<std::size_t>(index)].members = allFriendEndpoints(app);
    }

    app.selectedGroup = index;
    app.selectedFriend = -1;
    app.targetMode = TargetMode::Group;
    SetWindowTextW(app.groupEdit, L"");
    app.sounds.play(sfx::Cue::Click);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void updateNetwork(App& app)
{
    bool changed = false;
    for (const auto& event : app.net.update()) {
        if (!event.endpoint.empty()) ensureFriend(app, event.endpoint, event.name);
        if (!event.channel.empty()) ensureIncomingGroup(app, event.channel, event.endpoint);

        if (event.type == chatnet::EventType::Message) {
            ChatLine line;
            line.kind = ChatLine::Kind::Text;
            line.name = utf8ToWide(event.name);
            line.text = utf8ToWide(event.text);
            line.channel = utf8ToWide(event.channel);
            app.lines.push_back(std::move(line));
            trimHistory(app);
            app.sounds.play(sfx::Cue::Incoming);
            changed = true;
        } else if (event.type == chatnet::EventType::Sticker) {
            ChatLine line;
            line.kind = ChatLine::Kind::Sticker;
            line.name = utf8ToWide(event.name);
            line.stickerId = event.stickerId;
            line.channel = utf8ToWide(event.channel);
            app.lines.push_back(std::move(line));
            trimHistory(app);
            app.sounds.play(sfx::Cue::Sticker);
            changed = true;
        } else {
            changed = true;
        }
    }

    const auto peers = app.net.peers();
    const std::wstring publicAddress = utf8ToWide(app.net.publicEndpoint());
    std::wstring status = app.net.isRunning()
        ? (L"UDP  •  " + std::to_wstring(peers.size()) + L" ONLINE  •  " + targetLabel(app))
        : L"NETWORK OFFLINE";
    if (!publicAddress.empty()) status += L"  •  PUBLIC READY";

    if (status != app.status) {
        app.status = status;
        changed = true;
    }

    if (changed) InvalidateRect(app.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK editSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* app = reinterpret_cast<App*>(GetPropW(hwnd, L"GameChatApp"));
    if (app) {
        if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
            PostMessageW(app->hwnd, kSendMessageCommand, 0, 0);
            return 0;
        }
        if (msg == WM_CHAR && wParam == VK_RETURN) return 0;
    }
    return app && app->oldEditProc
        ? CallWindowProcW(app->oldEditProc, hwnd, msg, wParam, lParam)
        : DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND createEdit(HWND parent, HFONT font, int limit)
{
    HWND edit = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 10, 10, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(edit, EM_SETLIMITTEXT, limit, 0);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
    return edit;
}

void layout(App& app)
{
    app.closeRect = RECT{866, 16, 900, 50};
    app.copyRect = RECT{790, 88, 892, 120};
    app.allRect = RECT{636, 150, 884, 178};
    app.addFriendRect = RECT{810, 190, 884, 220};
    app.addGroupRect = RECT{810, 386, 884, 416};
    app.sendRect = RECT{786, 522, 892, 570};

    if (app.edit) SetWindowPos(app.edit, nullptr, 40, 531, 724, 29, SWP_NOZORDER | SWP_NOACTIVATE);
    if (app.friendEdit) SetWindowPos(app.friendEdit, nullptr, 638, 194, 160, 24, SWP_NOZORDER | SWP_NOACTIVATE);
    if (app.groupEdit) SetWindowPos(app.groupEdit, nullptr, 638, 390, 160, 24, SWP_NOZORDER | SWP_NOACTIVATE);

    for (int i = 0; i < 7; ++i) {
        app.friendRows[i] = RECT{638, 226 + i * 24, 884, 248 + i * 24};
    }
    for (int i = 0; i < 4; ++i) {
        app.groupRows[i] = RECT{638, 422 + i * 22, 884, 442 + i * 22};
    }

    const int stickerStart = 642;
    for (int i = 0; i < 4; ++i) {
        const int x = stickerStart + i * 52;
        app.stickerRects[i] = RECT{x, 474, x + 44, 518};
    }
}

void paint(App& app, HDC target)
{
    RECT client{};
    GetClientRect(app.hwnd, &client);

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    HPEN frame = CreatePen(PS_SOLID, 3, kOrange);
    HGDIOBJ oldPen = SelectObject(dc, frame);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, 6, 6, client.right - 6, client.bottom - 6, 22, 22);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(frame);

    RECT title{24, 14, 820, 58};
    drawText(dc, L"GAME CHAT", title, app.titleFont, kCyan, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitle{24, 56, 820, 80};
    drawText(dc, L"PIXEL SCI-FI  //  DIRECT INTERNET + FRIENDS + GROUPS", subtitle, app.smallFont, kOrange, DT_LEFT | DT_SINGLELINE);

    drawChamferedBox(dc, app.closeRect, RGB(48, 18, 18), kRed, 6);
    drawText(dc, L"X", app.closeRect, app.bodyFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT shareBox{24, 84, 900, 126};
    drawChamferedBox(dc, shareBox, kPanel, kCyan, 8);
    RECT shareLabel{38, 91, 152, 115};
    drawText(dc, L"MY ADDRESS", shareLabel, app.smallFont, kOrange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const std::string publicEndpoint = app.net.publicEndpoint();
    const std::string visibleEndpoint = publicEndpoint.empty() ? app.net.localEndpoint() : publicEndpoint;
    RECT shareValue{156, 91, 782, 115};
    drawText(dc, utf8ToWide(visibleEndpoint), shareValue, app.bodyFont,
             publicEndpoint.empty() ? kMuted : kCyanBright,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    drawChamferedBox(dc, app.copyRect, RGB(12, 93, 122), kCyanBright, 6);
    drawText(dc, L"COPY", app.copyRect, app.smallFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT messages{24, 140, 610, 506};
    drawChamferedBox(dc, messages, kPanel, kCyan, 10);
    RECT messagesTab{40, 130, 188, 158};
    drawChamferedBox(dc, messagesTab, kCyan, kCyanBright, 7);
    drawText(dc, L"MESSAGES", messagesTab, app.smallFont, kBg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT side{624, 140, 900, 506};
    drawChamferedBox(dc, side, kPanel, kCyan, 10);
    RECT sideTab{640, 130, 782, 158};
    drawChamferedBox(dc, sideTab, kCyan, kCyanBright, 7);
    drawText(dc, L"CONTACTS", sideTab, app.smallFont, kBg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const COLORREF allFill = app.targetMode == TargetMode::All ? RGB(18, 110, 137) : kPanel2;
    drawChamferedBox(dc, app.allRect, allFill, app.targetMode == TargetMode::All ? kCyanBright : kCyan, 6);
    drawText(dc, L"ALL FRIENDS", app.allRect, app.smallFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT friendLabel{638, 176, 798, 192};
    drawText(dc, L"ADD IP/HOST:PORT", friendLabel, app.smallFont, kOrange, DT_LEFT | DT_SINGLELINE);
    drawChamferedBox(dc, app.addFriendRect, RGB(12, 93, 122), kCyanBright, 6);
    drawText(dc, L"ADD", app.addFriendRect, app.smallFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    for (int i = 0; i < 7; ++i) {
        if (i >= static_cast<int>(app.friends.size())) break;
        const bool selected = app.targetMode == TargetMode::Friend && app.selectedFriend == i;
        drawChamferedBox(dc, app.friendRows[i], selected ? RGB(18, 110, 137) : RGB(5, 63, 74),
                         selected ? kCyanBright : kMuted, 5);
        RECT row = app.friendRows[i];
        row.left += 8;
        drawText(dc, utf8ToWide(app.friends[static_cast<std::size_t>(i)].label), row,
                 app.smallFont, selected ? kText : kCyanBright,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    RECT groupLabel{638, 370, 798, 388};
    drawText(dc, L"NEW GROUP (ALL FRIENDS)", groupLabel, app.smallFont, kOrange, DT_LEFT | DT_SINGLELINE);
    drawChamferedBox(dc, app.addGroupRect, RGB(12, 93, 122), kCyanBright, 6);
    drawText(dc, L"NEW", app.addGroupRect, app.smallFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    for (int i = 0; i < 4; ++i) {
        if (i >= static_cast<int>(app.groups.size())) break;
        const bool selected = app.targetMode == TargetMode::Group && app.selectedGroup == i;
        drawChamferedBox(dc, app.groupRows[i], selected ? RGB(102, 83, 20) : RGB(48, 43, 12),
                         selected ? kGold : RGB(130, 119, 43), 5);
        RECT row = app.groupRows[i];
        row.left += 8;
        drawText(dc, L"# " + utf8ToWide(app.groups[static_cast<std::size_t>(i)].name), row,
                 app.smallFont, selected ? kText : kGold,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    HRGN messageClip = CreateRectRgn(messages.left + 12, messages.top + 16, messages.right - 12, messages.bottom - 12);
    SelectClipRgn(dc, messageClip);

    const int maxEntries = 11;
    const int start = std::max(0, static_cast<int>(app.lines.size()) - maxEntries);
    int y = messages.top + 22;
    for (int i = start; i < static_cast<int>(app.lines.size()); ++i) {
        const auto& line = app.lines[static_cast<std::size_t>(i)];

        if (!line.channel.empty()) {
            RECT channelRect{messages.left + 16, y, messages.left + 128, y + 18};
            drawText(dc, L"#" + line.channel, channelRect, app.smallFont, kGold,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        RECT nameRect{messages.left + 132, y, messages.left + 262, y + 22};
        drawText(dc, line.name + L":", nameRect, app.smallFont, line.local ? kCyanBright : kGreen,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (line.kind == ChatLine::Kind::Text) {
            RECT textRect{messages.left + 266, y, messages.right - 16, y + 22};
            drawText(dc, line.text, textRect, app.smallFont, kText,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += 26;
        } else {
            RECT stickerRect{messages.left + 266, y - 4, messages.left + 316, y + 46};
            drawSticker(app, dc, stickerRect, line.stickerId);
            y += 54;
        }
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(messageClip);

    RECT stickerLabel{638, 454, 884, 473};
    drawText(dc, L"STICKERS  //  PNG JPG BMP SUPPORTED", stickerLabel, app.smallFont, kOrange,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < 4; ++i) drawSticker(app, dc, app.stickerRects[i], static_cast<std::uint8_t>(i + 1));

    RECT inputOuter{24, 518, 774, 578};
    drawChamferedBox(dc, inputOuter, kPanel2, kCyan, 9);
    RECT inputTab{40, 508, 214, 536};
    drawChamferedBox(dc, inputTab, kCyan, kCyanBright, 6);
    drawText(dc, targetLabel(app), inputTab, app.smallFont, kBg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    drawChamferedBox(dc, app.sendRect, RGB(12, 93, 122), kCyanBright, 8);
    drawText(dc, L"SEND", app.sendRect, app.smallFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT statusRect{24, 584, 892, 610};
    drawText(dc, app.status, statusRect, app.smallFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(cs->lpCreateParams);
        app->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }

    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_CREATE: {
        app->titleFont = makeFont(31, FW_BOLD);
        app->bodyFont = makeFont(18, FW_BOLD);
        app->smallFont = makeFont(14, FW_BOLD);
        app->editBrush = CreateSolidBrush(kPanel2);

        app->edit = createEdit(hwnd, app->bodyFont, 360);
        SetPropW(app->edit, L"GameChatApp", app);
        app->oldEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(app->edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editSubclass)));

        app->friendEdit = createEdit(hwnd, app->smallFont, 120);
        app->groupEdit = createEdit(hwnd, app->smallFont, 31);

        layout(*app);
        SetTimer(hwnd, kNetworkTimer, 60, nullptr);
        app->status = app->net.isRunning() ? L"NETWORK READY" : utf8ToWide(app->net.lastError());
        SetFocus(app->edit);
        return 0;
    }

    case WM_TIMER:
        if (wParam == kNetworkTimer) updateNetwork(*app);
        return 0;

    case kSendMessageCommand:
        sendCurrentMessage(*app);
        return 0;

    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), kText);
        SetBkColor(reinterpret_cast<HDC>(wParam), kPanel2);
        return reinterpret_cast<INT_PTR>(app->editBrush);

    case WM_LBUTTONUP: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        if (PtInRect(&app->closeRect, pt)) {
            app->sounds.play(sfx::Cue::Click);
            DestroyWindow(hwnd);
            return 0;
        }
        if (PtInRect(&app->copyRect, pt)) {
            const std::string value = app->net.publicEndpoint().empty()
                ? app->net.localEndpoint()
                : app->net.publicEndpoint();
            copyToClipboard(hwnd, utf8ToWide(value));
            app->status = L"ADDRESS COPIED: " + utf8ToWide(value);
            app->sounds.play(sfx::Cue::Click);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&app->addFriendRect, pt)) {
            addFriendFromEdit(*app);
            return 0;
        }
        if (PtInRect(&app->addGroupRect, pt)) {
            addGroupFromEdit(*app);
            return 0;
        }
        if (PtInRect(&app->allRect, pt)) {
            app->targetMode = TargetMode::All;
            app->selectedFriend = -1;
            app->selectedGroup = -1;
            app->sounds.play(sfx::Cue::Click);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&app->sendRect, pt)) {
            sendCurrentMessage(*app);
            SetFocus(app->edit);
            return 0;
        }

        for (int i = 0; i < 7; ++i) {
            if (i < static_cast<int>(app->friends.size()) && PtInRect(&app->friendRows[i], pt)) {
                app->targetMode = TargetMode::Friend;
                app->selectedFriend = i;
                app->selectedGroup = -1;
                app->sounds.play(sfx::Cue::Click);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        for (int i = 0; i < 4; ++i) {
            if (i < static_cast<int>(app->groups.size()) && PtInRect(&app->groupRows[i], pt)) {
                app->targetMode = TargetMode::Group;
                app->selectedGroup = i;
                app->selectedFriend = -1;
                app->sounds.play(sfx::Cue::Click);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        for (int i = 0; i < 4; ++i) {
            if (PtInRect(&app->stickerRects[i], pt)) {
                sendSticker(*app, static_cast<std::uint8_t>(i + 1));
                return 0;
            }
        }
        break;
    }

    case WM_NCHITTEST: {
        const LRESULT base = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (base != HTCLIENT) return base;
        POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &screen);
        if (screen.y < 76 && screen.x < 840) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        paint(*app, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        KillTimer(hwnd, kNetworkTimer);
        app->net.stop();
        if (app->edit) RemovePropW(app->edit, L"GameChatApp");
        if (app->editBrush) DeleteObject(app->editBrush);
        if (app->titleFont) DeleteObject(app->titleFont);
        if (app->bodyFont) DeleteObject(app->bodyFont);
        if (app->smallFont) DeleteObject(app->smallFont);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;

    App app;
    loadStickerAssets(app);
    const std::string name = defaultPlayerName();
    app.net.start(name, 34197);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = wndProc;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = std::max(work.left, work.right - kWindowWidth - 24);
    const int y = work.top + 24;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClass,
        L"Game Chat",
        WS_POPUP,
        x, y, kWindowWidth, kWindowHeight,
        nullptr, nullptr, instance, &app);

    if (!hwnd) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    HRGN region = CreateRoundRectRgn(0, 0, kWindowWidth + 1, kWindowHeight + 1, 24, 24);
    SetWindowRgn(hwnd, region, TRUE);

    const DWORD cornerPreference = 2;
    DwmSetWindowAttribute(hwnd, 33, &cornerPreference, sizeof(cornerPreference));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app.stickerImages = {};
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
