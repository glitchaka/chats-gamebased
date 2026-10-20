#include "ChatNet.h"
#include "SoundFx.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"GameBasedChatWindow";
constexpr UINT_PTR kNetworkTimer = 1;
constexpr UINT kSendMessageCommand = WM_APP + 1;
constexpr int kWindowWidth = 760;
constexpr int kWindowHeight = 520;

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
    std::uint8_t stickerId{};
    bool local{};
};

struct App {
    HWND hwnd{};
    HWND edit{};
    WNDPROC oldEditProc{};
    HBRUSH editBrush{};
    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    chatnet::ChatNet net;
    sfx::SoundFx sounds;
    std::vector<ChatLine> lines;
    RECT sendRect{};
    RECT closeRect{};
    RECT stickerRects[4]{};
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

void fillRect(HDC dc, const RECT& r, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void drawSticker(HDC dc, const RECT& bounds, std::uint8_t id)
{
    const int w = bounds.right - bounds.left;
    const int h = bounds.bottom - bounds.top;
    const int s = std::max(2, std::min(w, h) / 10);
    const int cx = (bounds.left + bounds.right) / 2;
    const int cy = (bounds.top + bounds.bottom) / 2;

    drawChamferedBox(dc, bounds, RGB(7, 36, 43), kCyan, std::max(4, s));

    auto px = [&](int x, int y, int ww, int hh, COLORREF color) {
        RECT r{bounds.left + x * s, bounds.top + y * s,
               bounds.left + (x + ww) * s, bounds.top + (y + hh) * s};
        fillRect(dc, r, color);
    };

    switch (id) {
    case 1: // heart
        px(2, 2, 2, 2, kRed); px(6, 2, 2, 2, kRed);
        px(1, 3, 8, 3, kRed); px(2, 6, 6, 1, kRed);
        px(3, 7, 4, 1, kRed); px(4, 8, 2, 1, kRed);
        break;
    case 2: // bolt
        px(5, 1, 3, 2, kGold); px(4, 3, 3, 2, kGold);
        px(3, 5, 4, 2, kGold); px(2, 7, 3, 1, kGold);
        px(4, 7, 2, 2, kGold); px(3, 9, 1, 1, kGold);
        break;
    case 3: // smile terminal
        px(2, 2, 6, 1, kGreen); px(1, 3, 1, 5, kGreen); px(8, 3, 1, 5, kGreen);
        px(2, 8, 6, 1, kGreen); px(3, 4, 1, 1, kCyanBright); px(6, 4, 1, 1, kCyanBright);
        px(3, 6, 1, 1, kCyanBright); px(4, 7, 2, 1, kCyanBright); px(6, 6, 1, 1, kCyanBright);
        break;
    case 4: // skull
        px(3, 2, 4, 1, kText); px(2, 3, 6, 4, kText); px(3, 7, 4, 2, kText);
        px(3, 4, 1, 2, kBg); px(6, 4, 1, 2, kBg); px(4, 6, 2, 1, kBg);
        px(4, 8, 1, 1, kBg); px(6, 8, 1, 1, kBg);
        break;
    default:
        SetPixel(dc, cx, cy, kCyanBright);
        break;
    }
}

void trimHistory(App& app)
{
    constexpr std::size_t kMaxHistory = 100;
    if (app.lines.size() > kMaxHistory) {
        app.lines.erase(app.lines.begin(), app.lines.begin() + static_cast<std::ptrdiff_t>(app.lines.size() - kMaxHistory));
    }
}

void sendCurrentMessage(App& app)
{
    const int len = GetWindowTextLengthW(app.edit);
    if (len <= 0) return;

    std::wstring text(static_cast<std::size_t>(len + 1), L'\0');
    const int copied = GetWindowTextW(app.edit, text.data(), len + 1);
    if (copied <= 0) return;
    text.resize(static_cast<std::size_t>(copied));

    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    if (text.empty()) return;

    const std::string utf8 = wideToUtf8(text);
    if (!app.net.sendChat(utf8)) app.status = L"NETWORK SEND FAILED";

    ChatLine line;
    line.kind = ChatLine::Kind::Text;
    line.name = utf8ToWide(app.net.localName());
    line.text = text;
    line.local = true;
    app.lines.push_back(std::move(line));
    trimHistory(app);
    SetWindowTextW(app.edit, L"");
    app.sounds.play(sfx::Cue::Click);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void sendSticker(App& app, std::uint8_t stickerId)
{
    if (!app.net.sendSticker(stickerId)) {
        app.status = L"STICKER SEND FAILED";
        return;
    }

    ChatLine line;
    line.kind = ChatLine::Kind::Sticker;
    line.name = utf8ToWide(app.net.localName());
    line.stickerId = stickerId;
    line.local = true;
    app.lines.push_back(std::move(line));
    trimHistory(app);
    app.sounds.play(sfx::Cue::Sticker);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void updateNetwork(App& app)
{
    bool changed = false;
    for (const auto& event : app.net.update()) {
        if (event.type == chatnet::EventType::Message) {
            ChatLine line;
            line.kind = ChatLine::Kind::Text;
            line.name = utf8ToWide(event.name);
            line.text = utf8ToWide(event.text);
            app.lines.push_back(std::move(line));
            trimHistory(app);
            app.sounds.play(sfx::Cue::Incoming);
            changed = true;
        } else if (event.type == chatnet::EventType::Sticker) {
            ChatLine line;
            line.kind = ChatLine::Kind::Sticker;
            line.name = utf8ToWide(event.name);
            line.stickerId = event.stickerId;
            app.lines.push_back(std::move(line));
            trimHistory(app);
            app.sounds.play(sfx::Cue::Sticker);
            changed = true;
        } else {
            changed = true;
        }
    }

    const auto peers = app.net.peers();
    const std::wstring status = app.net.isRunning()
        ? (L"LAN  •  PORT " + std::to_wstring(app.net.port()) + L"  •  " + std::to_wstring(peers.size()) + L" ONLINE")
        : L"NETWORK OFFLINE";
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

void layout(App& app)
{
    app.sendRect = RECT{646, 431, 730, 477};
    app.closeRect = RECT{706, 14, 738, 46};
    if (app.edit) {
        SetWindowPos(app.edit, nullptr, 38, 440, 588, 27, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    const int startX = 562;
    for (int i = 0; i < 4; ++i) {
        const int x = startX + i * 41;
        app.stickerRects[i] = RECT{x, 352, x + 34, 386};
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

    RECT title{24, 16, 690, 60};
    drawText(dc, L"GAME CHAT", title, app.titleFont, kCyan, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT subtitle{24, 58, 520, 82};
    drawText(dc, L"RED ALERT NETWORK STYLE  //  FLOATING LAN CHAT", subtitle, app.smallFont, kOrange, DT_LEFT | DT_SINGLELINE);

    drawChamferedBox(dc, app.closeRect, RGB(48, 18, 18), kRed, 6);
    RECT closeText = app.closeRect;
    drawText(dc, L"X", closeText, app.bodyFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT messages{24, 90, 532, 410};
    drawChamferedBox(dc, messages, kPanel, kCyan, 10);
    RECT messagesTab{38, 80, 184, 108};
    drawChamferedBox(dc, messagesTab, kCyan, kCyanBright, 7);
    drawText(dc, L"MESSAGES", messagesTab, app.smallFont, kBg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT peersRect{550, 90, 736, 410};
    drawChamferedBox(dc, peersRect, kPanel, kCyan, 10);
    RECT peersTab{564, 80, 700, 108};
    drawChamferedBox(dc, peersTab, kCyan, kCyanBright, 7);
    drawText(dc, L"PLAYERS", peersTab, app.smallFont, kBg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    HRGN messageClip = CreateRectRgn(messages.left + 12, messages.top + 16, messages.right - 12, messages.bottom - 12);
    SelectClipRgn(dc, messageClip);

    const int maxEntries = 9;
    const int start = std::max(0, static_cast<int>(app.lines.size()) - maxEntries);
    int y = messages.top + 20;
    for (int i = start; i < static_cast<int>(app.lines.size()); ++i) {
        const auto& line = app.lines[static_cast<std::size_t>(i)];
        RECT nameRect{messages.left + 16, y, messages.left + 148, y + 22};
        drawText(dc, line.name + L":", nameRect, app.smallFont, line.local ? kCyanBright : kGreen,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (line.kind == ChatLine::Kind::Text) {
            RECT textRect{messages.left + 154, y, messages.right - 16, y + 22};
            drawText(dc, line.text, textRect, app.smallFont, kText,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += 25;
        } else {
            RECT stickerRect{messages.left + 154, y - 3, messages.left + 198, y + 41};
            drawSticker(dc, stickerRect, line.stickerId);
            y += 50;
        }
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(messageClip);

    const auto peers = app.net.peers();
    int py = peersRect.top + 20;
    for (std::size_t i = 0; i < peers.size() && i < 7; ++i) {
        HBRUSH dot = CreateSolidBrush(kGreen);
        HGDIOBJ oldDotBrush = SelectObject(dc, dot);
        HGDIOBJ oldDotPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, peersRect.left + 16, py + 6, peersRect.left + 24, py + 14);
        SelectObject(dc, oldDotPen);
        SelectObject(dc, oldDotBrush);
        DeleteObject(dot);

        RECT peerName{peersRect.left + 30, py, peersRect.right - 12, py + 22};
        drawText(dc, utf8ToWide(peers[i].name), peerName, app.smallFont, kText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        py += 25;
    }
    if (peers.empty()) {
        RECT empty{peersRect.left + 16, peersRect.top + 22, peersRect.right - 12, peersRect.top + 62};
        drawText(dc, L"SEARCHING\nLAN...", empty, app.smallFont, kMuted, DT_LEFT | DT_WORDBREAK);
    }

    RECT stickerLabel{562, 326, 724, 347};
    drawText(dc, L"STICKERS", stickerLabel, app.smallFont, kOrange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < 4; ++i) drawSticker(dc, app.stickerRects[i], static_cast<std::uint8_t>(i + 1));

    RECT inputOuter{24, 426, 636, 482};
    drawChamferedBox(dc, inputOuter, kPanel2, kCyan, 9);
    RECT inputTab{38, 417, 170, 443};
    drawChamferedBox(dc, inputTab, kCyan, kCyanBright, 6);
    drawText(dc, L"YOUR TEXT", inputTab, app.smallFont, kBg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    drawChamferedBox(dc, app.sendRect, RGB(12, 93, 122), kCyanBright, 8);
    drawText(dc, L"SEND", app.sendRect, app.smallFont, kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT statusRect{24, 490, 730, 512};
    drawText(dc, app.status, statusRect, app.smallFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

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
        app->smallFont = makeFont(15, FW_BOLD);
        app->editBrush = CreateSolidBrush(kPanel2);

        app->edit = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 10, 10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(app->edit, WM_SETFONT, reinterpret_cast<WPARAM>(app->bodyFont), TRUE);
        SendMessageW(app->edit, EM_SETLIMITTEXT, 360, 0);
        SendMessageW(app->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        SetPropW(app->edit, L"GameChatApp", app);
        app->oldEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(app->edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editSubclass)));

        layout(*app);
        SetTimer(hwnd, kNetworkTimer, 60, nullptr);
        app->status = app->net.isRunning() ? L"LAN READY" : utf8ToWide(app->net.lastError());
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
        if (PtInRect(&app->sendRect, pt)) {
            sendCurrentMessage(*app);
            SetFocus(app->edit);
            return 0;
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
        if (screen.y < 76 && screen.x < 690) return HTCAPTION;
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

    App app;
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

    if (!hwnd) return 1;

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
    return static_cast<int>(msg.wParam);
}
