cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

# HTTP and WebSockets (Twitch EventSub, Kick, the OBSPETS backend). OBS ships libcurl.
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

# Lua 5.4 (MIT), only to read character packs (pack.lua) as data.
file(GLOB LUA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/deps/lua/src/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua)\\.c$")
add_library(obspet-lua STATIC ${LUA_SOURCES})
target_include_directories(obspet-lua PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/deps/lua/src")
set_property(TARGET obspet-lua PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(obspet-lua PRIVATE /W0 /wd4244 /wd4310)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE obspet-lua)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 shell32 crypt32 gdi32 ole32 bcrypt)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/util.cpp
    src/audio.cpp
    src/pack.cpp
    src/net.cpp
    src/hub.cpp
    src/pet.cpp
    src/pet-render.cpp
    src/pet-source.cpp
    src/chat-server.cpp
    src/chat-source.cpp
    src/editor.cpp
    src/settings.cpp
    src/updater.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
