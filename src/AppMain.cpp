#include <windows.h>
#include <mmsystem.h>

#define defaultPlayerName defaultPlayerNameLegacy
#include "AppMain_part00.inc"
#undef defaultPlayerName

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
#define sendCurrentMessage sendCurrentMessageLegacy
#define sendAudioToTarget sendAudioToTargetLegacy
#define sendAudioFromPicker sendAudioFromPickerLegacy
#define updateNetwork updateNetworkLegacy
#include "AppMain_part02.inc"
#undef updateNetwork
#undef sendAudioFromPicker
#undef sendAudioToTarget
#undef sendCurrentMessage
#undef sendUserSticker
#define unreadProc unreadProcLegacy
#define paint paintLegacy
#include "AppMain_part03.inc"
#undef paint
#undef unreadProc
#undef importSticker

#define importSticker importStickerPrevious
#define sendUserSticker sendUserStickerPrevious
#include "AppMain_stickerfix.inc"
#undef sendUserSticker
#undef importSticker

#define sendAudioFromPicker sendAudioFromPickerWithoutRecipient
#define sendUserSticker sendUserStickerWithoutRecipient
#include "AppMain_attachmentfix.inc"
#undef sendUserSticker
#undef sendAudioFromPicker
#include "AppMain_audiorecipientfix.inc"
#include "AppMain_recipientfix.inc"
#include "AppMain_minbarfix.inc"

#define paint paintWithSenderIdentities
#include "AppMain_senderfix.inc"
#undef paint

#include "AppMain_audioplayerfix.inc"

#define updateUnreadHud showCompactTrayBar
#define unreadProc unreadProcFixed
#define wWinMain wWinMainLegacy
#define ShellExecuteW internalAudioShellExecuteW
#define updateNetwork updateNetworkWithAudioPlayer
#define removeTrayIcon removeTrayIconWithAudioShutdown
#include "AppMain_part04.inc"
#undef removeTrayIcon
#undef updateNetwork
#undef ShellExecuteW
#undef wWinMain
#undef unreadProc
#undef updateUnreadHud

#define unreadProc unreadProcFixed
#include "AppMain_entryfix.inc"
#undef unreadProc
