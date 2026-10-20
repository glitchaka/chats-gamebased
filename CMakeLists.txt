cmake_minimum_required(VERSION 3.20)
project(chats_gamebased LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(chats-gamebased WIN32
    src/main.cpp
    src/ChatNet.cpp
)

target_include_directories(chats-gamebased PRIVATE src)
target_compile_definitions(chats-gamebased PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(chats-gamebased PRIVATE ws2_32 dwmapi)

if(MSVC)
    target_compile_options(chats-gamebased PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(chats-gamebased PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
