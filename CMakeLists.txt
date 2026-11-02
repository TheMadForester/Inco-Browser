cmake_minimum_required(VERSION 3.21)
project(IncoBrowser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 6.10: QWebEngineExtensionManager
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets)
# 3.2: Argon2id through EVP_KDF (the vault falls back to PBKDF2 at runtime
# if the provider lacks it).
find_package(OpenSSL 3.2 REQUIRED)
# Optional: lock the vault on suspend and screen lock.
find_package(Qt6 QUIET COMPONENTS DBus)
find_package(ZLIB REQUIRED)

# Tor bundle that gets hashed into the binary and copied next to it.
set(INCO_TOR_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tor" CACHE PATH "Tor expert bundle to bake and ship")
set(INCO_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${INCO_GEN_DIR}")

# Re-hash on every build; the .inc is only rewritten when the bundle changed,
# so an unchanged tor/ does not trigger a recompile.
add_custom_target(inco_tor_manifest
    COMMAND ${CMAKE_COMMAND}
            -DTOR_DIR=${INCO_TOR_SOURCE_DIR}
            -DOUT=${INCO_GEN_DIR}/TorManifest.inc
            -P ${CMAKE_SOURCE_DIR}/cmake/GenTorManifest.cmake
    BYPRODUCTS ${INCO_GEN_DIR}/TorManifest.inc
    COMMENT "Baking Tor bundle hashes (integrity check)"
    VERBATIM
)

add_executable(IncoBrowser
    src/main.cpp
    src/SettingsStore.hpp
    src/SettingsStore.cpp
    src/SearchEngine.hpp
    src/SearchEngine.cpp
    src/SecureKey.hpp
    src/SecureKey.cpp
    src/CryptoBox.hpp
    src/CryptoBox.cpp
    src/PrivatePaths.hpp
    src/PrivatePaths.cpp
    src/PasswordStore.hpp
    src/PasswordStore.cpp
    src/BookmarkStore.hpp
    src/BookmarkStore.cpp
    src/BlockList.hpp
    src/BlockList.cpp
    src/BrowserProfile.hpp
    src/BrowserProfile.cpp
    src/ExtensionArchive.hpp
    src/ExtensionArchive.cpp
    src/MainWindow.hpp
    src/MainWindow.cpp
    src/SettingsPage.hpp
    src/SettingsPage.cpp
    src/TorIntegrity.hpp
    src/TorIntegrity.cpp
    src/Motion.hpp
    src/OnionRoute.hpp
    src/OnionRoute.cpp
    src/TorControl.hpp
    src/TorControl.cpp
    resources/resources.qrc
)
add_dependencies(IncoBrowser inco_tor_manifest)

target_include_directories(IncoBrowser PRIVATE src ${INCO_GEN_DIR})
target_compile_definitions(IncoBrowser PRIVATE INCO_VERSION="${PROJECT_VERSION}")
target_link_libraries(IncoBrowser PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network
    OpenSSL::Crypto ZLIB::ZLIB
)
if(TARGET Qt6::DBus)
    target_link_libraries(IncoBrowser PRIVATE Qt6::DBus)
    target_compile_definitions(IncoBrowser PRIVATE INCO_HAVE_DBUS=1)
endif()

# Fresh copy each build so stale files never linger in build/tor
# (stray files make the integrity check fail on purpose).
add_custom_command(TARGET IncoBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:IncoBrowser>/tor
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:IncoBrowser>/tor
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${INCO_TOR_SOURCE_DIR} $<TARGET_FILE_DIR:IncoBrowser>/tor
    COMMENT "Copy bundled tor next to IncoBrowser"
)

add_custom_command(TARGET IncoBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/scripts/fetch-tor.sh
            $<TARGET_FILE_DIR:IncoBrowser>/fetch-tor.sh
)
