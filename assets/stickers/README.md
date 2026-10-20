# Sticker image slots

The chat supports four image-backed sticker slots.

Put any of these files beside the executable under `assets/stickers/`:

- `1.png`, `1.jpg` or `1.bmp`
- `2.png`, `2.jpg` or `2.bmp`
- `3.png`, `3.jpg` or `3.bmp`
- `4.png`, `4.jpg` or `4.bmp`

The sender transmits the sticker ID, not the bitmap bytes, so every client should ship with the same sticker pack. If an image is missing, the app renders a built-in pixel sticker instead.
