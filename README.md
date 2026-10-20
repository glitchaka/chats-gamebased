# chats-gamebased

Chat nativo para Windows inspirado en la capa de red multijugador preservada en el código de Red Alert, pero adaptado a Winsock/UDP moderno.

## Qué incluye

- Ventana flotante `always-on-top`, sin marco nativo y arrastrable desde la cabecera.
- GUI pixel/sci-fi basada en las referencias proporcionadas: fondo negro, marco naranja, paneles oscuros, cian brillante y controles biselados/chaflanados.
- Chat LAN por UDP broadcast.
- Descubrimiento automático de usuarios en la red local.
- Lista de jugadores activos con expiración automática.
- Recepción no bloqueante y cola de eventos.
- Nombre automático basado en el nombre del equipo.
- Puerto por defecto: `34197/UDP`.
- Sin servidor central.

## Estructura

- `src/ChatNet.h` / `src/ChatNet.cpp`: capa de red reutilizable. Conserva el mismo modelo conceptual del código IPX original: abrir transporte, anunciar, enviar, escuchar, recuperar paquetes y cerrar.
- `src/main.cpp`: ventana flotante Win32 y GUI del chat.
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

Abre el ejecutable en dos equipos de la misma LAN. Se descubrirán automáticamente y los mensajes se enviarán a todos los clientes del chat.
