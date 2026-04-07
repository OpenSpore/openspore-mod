cmake_minimum_required(VERSION 3.16)
project(OpenSporeMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to Spore ModAPI SDK - set via environment or CMake variable
if(NOT DEFINED SPORE_MODAPI_PATH)
    set(SPORE_MODAPI_PATH "$ENV{SPORE_MODAPI_PATH}" CACHE PATH "Path to Spore ModAPI SDK")
endif()

if(NOT EXISTS "${SPORE_MODAPI_PATH}")
    message(FATAL_ERROR "SPORE_MODAPI_PATH not set or does not exist. "
        "Set it to the root of your Spore ModAPI Development Kit folder.")
endif()

# Determine SDK lib directory based on game version
set(SPORE_VERSION "disk" CACHE STRING "Spore version: disk, steam, or steam_patched")
set_property(CACHE SPORE_VERSION PROPERTY STRINGS disk steam steam_patched)

if(SPORE_VERSION STREQUAL "disk")
    set(SPORE_LIB_DIR "${SPORE_MODAPI_PATH}/lib/disk")
elseif(SPORE_VERSION STREQUAL "steam_patched")
    set(SPORE_LIB_DIR "${SPORE_MODAPI_PATH}/lib/steam_patched")
else()
    set(SPORE_LIB_DIR "${SPORE_MODAPI_PATH}/lib/steam")
endif()

# nlohmann/json - fetched automatically
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

# Source files
set(SOURCES
    src/dllmain.cpp
    src/OpenSporeMod.cpp
    src/net/HttpClient.cpp
    src/net/ApiClient.cpp
    src/net/SseClient.cpp
)

set(HEADERS
    src/OpenSporeMod.h
    src/net/HttpClient.h
    src/net/ApiClient.h
    src/net/SseClient.h
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SPORE_MODAPI_PATH}/Spore ModAPI
)

target_link_directories(${PROJECT_NAME} PRIVATE ${SPORE_LIB_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE
    SporeModAPI
    nlohmann_json::nlohmann_json
    winhttp
    shlwapi
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    _WINDLL
    UNICODE
    _UNICODE
)

# Output the DLL with the correct name for the ModAPI Launcher
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "OpenSporeMod"
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
