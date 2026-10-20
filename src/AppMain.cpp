#include "AppMain_part00.inc"

#define drawChamferedBox drawChamferedBoxLegacy
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
#undef drawChamferedBox
#include "AppMain_fontfix.inc"
#include "AppMain_polishfix.inc"

#define importSticker importStickerLibraryOnly
#define sendUserSticker sendUserStickerLegacy
#include "AppMain_part02.inc"
#undef sendUserSticker
#define unreadProc unreadProcLegacy
#include "AppMain_part03.inc"
#undef unreadProc
#undef importSticker
#include "AppMain_stickerfix.inc"
#include "AppMain_minbarfix.inc"

#define updateUnreadHud showCompactTrayBar
#define unreadProc unreadProcFixed
#define wWinMain wWinMainLegacy
#include "AppMain_part04.inc"
#undef wWinMain
#undef unreadProc
#undef updateUnreadHud

#define unreadProc unreadProcFixed
#include "AppMain_entryfix.inc"
#undef unreadProc
