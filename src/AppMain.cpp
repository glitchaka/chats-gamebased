#define kUnreadWidth kUnreadWidthLegacy
#define kUnreadHeight kUnreadHeightLegacy
#include "AppMain_part00.inc"
#undef kUnreadHeight
#undef kUnreadWidth
constexpr int kUnreadWidth = 360;
constexpr int kUnreadHeight = 36;

#define sourceGlyphIndex sourceGlyphIndexLegacy
#define glyphAdvanceFor glyphAdvanceForLegacy
#define glyphAdvance glyphAdvanceLegacy
#define pixelTextWidth pixelTextWidthLegacy
#define fitPixelText fitPixelTextLegacy
#define drawPixelMark drawPixelMarkLegacy
#define drawPixelText drawPixelTextLegacy
#define drawPixelCentered drawPixelCenteredLegacy
#include "AppMain_part01.inc"
#undef drawPixelCentered
#undef drawPixelText
#undef drawPixelMark
#undef fitPixelText
#undef pixelTextWidth
#undef glyphAdvance
#undef glyphAdvanceFor
#undef sourceGlyphIndex
#include "AppMain_fontfix.inc"

#define importSticker importStickerLibraryOnly
#include "AppMain_part02.inc"
#define addTrayIcon addTrayIconLegacy
#define unreadProc unreadProcLegacy
#include "AppMain_part03.inc"
#undef unreadProc
#undef addTrayIcon
#undef importSticker
#include "AppMain_stickerfix.inc"
#include "AppMain_iconfix.inc"
#include "AppMain_minbarfix.inc"

#define updateUnreadHud showCompactTrayBar
#define wWinMain wWinMainLegacy
#include "AppMain_part04.inc"
#undef wWinMain
#undef updateUnreadHud

#define unreadProc unreadProcFixed
#include "AppMain_entryfix.inc"
#undef unreadProc
