cmake_minimum_required(VERSION 3.20)
project(chats_gamebased LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(chats-gamebased WIN32
    src/main.cpp
    src/ChatNet.cpp
    src/SoundFx.cpp
)

target_include_directories(chats-gamebased PRIVATE src)
target_compile_definitions(chats-gamebased PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(chats-gamebased PRIVATE ws2_32 dwmapi winmm gdiplus ole32)

if(MSVC)
    target_compile_options(chats-gamebased PRIVATE /W4 /permissive- /utf-8)
    set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "/FIobjidl.h")
    set_property(TARGET chats-gamebased PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(MINGW)
    set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "-include;objidl.h")
endif()

set_target_properties(chats-gamebased PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Portable distribution layout:
# dist/chats-gamebased/
#   chats-gamebased.exe
#   assets/...
install(TARGETS chats-gamebased RUNTIME DESTINATION ".")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/" DESTINATION "assets")
