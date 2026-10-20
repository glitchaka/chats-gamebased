#include "SoundFx.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <filesystem>
#include <string>

namespace sfx {
namespace {

std::filesystem::path executableDirectory()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD count = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (count == 0 || count >= MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buffer, buffer + count)).parent_path();
}

std::filesystem::path soundPath(const wchar_t* filename)
{
    return executableDirectory() / L"assets" / L"sounds" / filename;
}

void closeAlias(const wchar_t* alias)
{
    std::wstring command = L"close ";
    command += alias;
    mciSendStringW(command.c_str(), nullptr, 0, nullptr);
}

} // namespace

SoundFx::SoundFx() = default;

SoundFx::~SoundFx()
{
    closeAlias(L"chat_click");
    closeAlias(L"chat_incoming");
    closeAlias(L"chat_sticker");
}

void SoundFx::playAlias(const wchar_t* alias, const wchar_t* filename, unsigned fallbackHz)
{
    const auto path = soundPath(filename);
    closeAlias(alias);

    if (std::filesystem::exists(path)) {
        std::wstring open = L"open \"" + path.wstring() + L"\" type mpegvideo alias " + alias;
        if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0) {
            std::wstring play = L"play ";
            play += alias;
            play += L" from 0";
            if (mciSendStringW(play.c_str(), nullptr, 0, nullptr) == 0) return;
        }
    }

    Beep(fallbackHz, 28);
}

void SoundFx::play(Cue cue)
{
    switch (cue) {
    case Cue::Click:
        playAlias(L"chat_click", L"ui-click.mp3", 820);
        break;
    case Cue::Incoming:
        playAlias(L"chat_incoming", L"incoming.mp3", 620);
        break;
    case Cue::Sticker:
        playAlias(L"chat_sticker", L"sticker.mp3", 980);
        break;
    }
}

} // namespace sfx
