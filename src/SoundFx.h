#pragma once

namespace sfx {

enum class Cue {
    Click,
    Incoming,
    Sticker
};

class SoundFx {
public:
    SoundFx();
    ~SoundFx();

    SoundFx(const SoundFx&) = delete;
    SoundFx& operator=(const SoundFx&) = delete;

    void play(Cue cue);

private:
    void playAlias(const wchar_t* alias, const wchar_t* filename, unsigned fallbackHz);
};

} // namespace sfx
