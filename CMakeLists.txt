cmake_minimum_required(VERSION 3.21)
project(IncoBrowser VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebEngineWidgets)
find_package(OpenSSL REQUIRED)

add_executable(IncoBrowser
    src/main.cpp
    src/SettingsStore.hpp
    src/SettingsStore.cpp
    src/PasswordStore.hpp
    src/PasswordStore.cpp
    src/BookmarkStore.hpp
    src/BookmarkStore.cpp
    src/BrowserProfile.hpp
    src/BrowserProfile.cpp
    src/MainWindow.hpp
    src/MainWindow.cpp
    src/SettingsPage.hpp
    src/SettingsPage.cpp
    resources/resources.qrc
)

target_include_directories(IncoBrowser PRIVATE src)
target_link_libraries(IncoBrowser PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebEngineWidgets
    OpenSSL::Crypto
)

add_custom_command(TARGET IncoBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:IncoBrowser>/tor
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/tor $<TARGET_FILE_DIR:IncoBrowser>/tor
    COMMENT "Copy bundled tor next to IncoBrowser"
)

add_custom_command(TARGET IncoBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/scripts/fetch-tor.sh
            $<TARGET_FILE_DIR:IncoBrowser>/fetch-tor.sh
)
