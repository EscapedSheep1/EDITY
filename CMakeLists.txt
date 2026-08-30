cmake_minimum_required(VERSION 3.24)
project(EDITY VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(WEBVIEW2_VERSION "1.0.2849.39")
FetchContent_Declare(webview2
    URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(webview2)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(EDITY_SOURCES
    src/main.cpp
    src/app/Paths.cpp
    src/app/Utf8.cpp
    src/app/Util.cpp
    src/app/edity.rc
    src/bridge/AppBridge.cpp
    src/settings/SettingsStore.cpp
    src/settings/CredentialStore.cpp
    src/ftp/TransferClient.cpp
    src/market/JsonIO.cpp
    src/market/Validator.cpp
    src/workspace/Workspace.cpp
    src/backup/ZipArchive.cpp
    src/types/TypesCatalog.cpp
    src/types/TypesXmlIO.cpp
)

add_executable(EDITY WIN32 ${EDITY_SOURCES})
set_source_files_properties(src/app/edity.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/IconFile.ico"
)

target_include_directories(EDITY PRIVATE
    src
    ${webview2_SOURCE_DIR}/build/native/include
)

target_compile_definitions(EDITY PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    EDITY_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(EDITY PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus)
    target_link_options(EDITY PRIVATE /MANIFEST:NO)
endif()

set(WEBVIEW2_LIB_DIR "${webview2_SOURCE_DIR}/build/native/x64")
if(NOT EXISTS "${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib")
    set(WEBVIEW2_LIB_DIR "${webview2_SOURCE_DIR}/build/native/x64")
endif()

target_link_libraries(EDITY PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    "${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib"
    dwmapi
    comctl32
    advapi32
    ole32
    oleaut32
    user32
    gdi32
    shell32
    shlwapi
    version
    crypt32
    winhttp
    ws2_32
    wldap32
    normaliz
    bcrypt
    iphlpapi
)

add_executable(edity_tests
    tests/validator_tests.cpp
    src/app/Utf8.cpp
    src/app/Util.cpp
    src/app/Paths.cpp
    src/market/JsonIO.cpp
    src/market/Validator.cpp
    src/types/TypesCatalog.cpp
    src/types/TypesXmlIO.cpp
)

target_include_directories(edity_tests PRIVATE src)
target_compile_definitions(edity_tests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    EDITY_TEST_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
if(MSVC)
    target_compile_options(edity_tests PRIVATE /utf-8 /W4 /permissive-)
endif()
target_link_libraries(edity_tests PRIVATE nlohmann_json::nlohmann_json ole32 shell32)

add_custom_command(TARGET EDITY POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/ui"
        "$<TARGET_FILE_DIR:EDITY>/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/IconFile.png"
        "$<TARGET_FILE_DIR:EDITY>/ui/icon.png"
    COMMENT "Copying HUD assets next to EDITY.exe"
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT EDITY)
