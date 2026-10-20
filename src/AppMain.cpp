#include "AppMain_part00.inc"

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
#undef importSticker
#include "AppMain_stickerfix.inc"

#define addTrayIcon addTrayIconLegacy
#include "AppMain_part03.inc"
#undef addTrayIcon
#include "AppMain_iconfix.inc"

#define wWinMain wWinMainLegacy
#include "AppMain_part04.inc"
#undef wWinMain
#include "AppMain_entryfix.inc"
