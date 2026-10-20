# chats-gamebased

Chat nativo para Windows inspirado en la capa de red multijugador preservada en el código de Red Alert, pero adaptado a Winsock/UDP moderno.

## Qué incluye

- Ventana flotante `always-on-top`, sin marco nativo y arrastrable desde la cabecera.
- GUI pixel/sci-fi basada en las referencias proporcionadas: fondo negro, marco naranja, paneles oscuros, cian brillante y controles chaflanados.
- UDP directo para LAN e Internet.
- Descubrimiento LAN automático por broadcast.
- Descubrimiento de la dirección pública mediante STUN (`stun.cloudflare.com:3478`).
- Botón **COPY** para compartir la dirección pública `IP:puerto`.
- Agregar amigos manualmente por `IP:puerto` o `host:puerto`.
- Chat directo seleccionando un amigo.
- Chat con todos los amigos conocidos.
- Grupos locales: se crea un grupo con la lista de amigos guardados y los mensajes se envían a todos sus miembros.
- Lista de contactos activos con expiración automática.
- Stickers compartidos por ID y cuatro slots de imágenes `PNG/JPG/BMP`.
- Efectos de sonido para clic, mensaje entrante y sticker; MP3 opcionales con tono de respaldo.
- Recepción no bloqueante y cola de eventos.
- Nombre automático basado en el nombre del equipo.
- Puerto por defecto: `34197/UDP`.

## Internet y NAT

La aplicación obtiene una dirección pública usando STUN desde el mismo socket UDP. Esa dirección se puede copiar y compartir con otro usuario para agregarlo como amigo.

El tráfico de chat es P2P directo. En NATs permisivos funciona mediante el mapeo UDP abierto por la propia aplicación; redes con NAT simétrico, CGNAT o firewalls restrictivos pueden necesitar un relay/TURN adicional. La capa está separada para poder agregar ese fallback sin rehacer el chat.

## Stickers

Los stickers usan cuatro slots compartidos. Coloca imágenes en:

```text
assets/stickers/1.png
assets/stickers/2.png
assets/stickers/3.png
assets/stickers/4.png
```

También acepta `.jpg` o `.bmp`. Si falta una imagen, aparece el sticker pixel-art integrado. Se transmite el ID del sticker, no el bitmap completo, por lo que todos los clientes deben tener el mismo pack.

## Sonidos

La aplicación busca:

```text
assets/sounds/ui-click.mp3
assets/sounds/incoming.mp3
assets/sounds/sticker.mp3
```

Si no existen, usa tonos generados. Los assets descargados desde terceros deben respetar sus términos de redistribución.

## Estructura

- `src/ChatNet.h` / `src/ChatNet.cpp`: transporte UDP, amigos, STUN, mensajes directos y fan-out de grupos.
- `src/SoundFx.h` / `src/SoundFx.cpp`: reproducción de efectos MP3 con fallback.
- `src/main.cpp`: ventana flotante Win32, contactos, grupos, stickers y GUI.
- `CMakeLists.txt`: build nativo C++17.
- `build.cmd`: compilación rápida con Visual Studio 2022.

## Compilar

```bat
build.cmd
```

El ejecutable queda en:

```text
build\bin\Release\chats-gamebased.exe
```
