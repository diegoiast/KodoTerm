# SPDX-License-Identifier: MIT
# Author: Diego Iastrubni <diegoiast@gmail.com>

cmake_minimum_required(VERSION 3.24)

project(codoterm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Quick)
if(UNIX AND NOT APPLE)
    find_package(Qt6 COMPONENTS DBus)
endif()
option(BUILD_EXAMPLES "Build the examples" ON)

include(FetchContent)


###################################################
# Fetch libvterm
FetchContent_Declare(
  libvterm
  GIT_REPOSITORY https://github.com/neovim/libvterm.git
  GIT_TAG        934bc2fbf21800ac3458a499df8820ca5fb45fd3
)
FetchContent_MakeAvailable(libvterm)

# Manually define the vterm library since it lacks CMakeLists.txt
file(GLOB VTERM_SOURCES
    "${libvterm_SOURCE_DIR}/src/*.c"
)

add_library(vterm STATIC ${VTERM_SOURCES})
target_include_directories(vterm PUBLIC
    "${libvterm_SOURCE_DIR}/include"
    "${libvterm_SOURCE_DIR}/src"
)

###################################################
# Main library

set(KODOTERM_SOURCES
    src/KodoTerm.cpp
    src/KodoTermConfig.cpp
    src/KodoTermSession.cpp
    src/KodoTermRenderer.cpp
    src/KodoQuickTerm.cpp
    include/KodoTerm/KodoTerm.hpp
    include/KodoTerm/KodoTermSession.hpp
    include/KodoTerm/KodoTermRenderer.hpp
    include/KodoTerm/KodoQuickTerm.hpp
    src/PtyProcess.cpp
    src/PtyProcess.h
    KodoTermThemes.qrc
)

if(UNIX)
    list(APPEND KODOTERM_SOURCES
        src/PtyProcess_unix.cpp
        src/PtyProcess_unix.h
    )
    # Check for util library (needed for forkpty on some systems)
    #find_library(UTIL_LIB util)
elseif(WIN32)
    list(APPEND KODOTERM_SOURCES
        src/PtyProcess_win.cpp
        src/PtyProcess_win.h
    )
endif()
add_library(KodoTerm ${KODOTERM_SOURCES})
add_library(KodoTerm::KodoTerm ALIAS KodoTerm)
target_include_directories(KodoTerm PRIVATE src)
target_include_directories(KodoTerm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(KodoTerm PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    vterm
)


if(BUILD_EXAMPLES)
    add_subdirectory(examples)
    if(UNIX AND NOT APPLE)
        install(FILES examples/tabbed/KodoShell.desktop DESTINATION share/applications)
    endif()

    # why this? because WIX does not like files without extensions.
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_BINARY_DIR}/license.txt"
        COPYONLY
    )

    set(CPACK_WIX_UPGRADE_GUID "2f231866-b181-4671-9d5e-bb2fdcec7cc4")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/diegoiast/KodoTerm/")
    set(CPACK_PACKAGE_NAME "KodoShell")
    set(CPACK_PACKAGE_VENDOR "Diego Iastrubni")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "KodoShell Terminal Emulator (based on KodoTerm)")
    set(CPACK_PACKAGE_VERSION "0.0.1")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/license.txt")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

    # Append suffix to VERSION only on non-Windows
    if(CPACK_PACKAGE_VERSION_SUFFIX AND NOT WIN32)
        set(CPACK_PACKAGE_VERSION
            "${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_VERSION_SUFFIX}")
    endif()

    # Always append suffix to the file name
    if(CPACK_PACKAGE_VERSION_SUFFIX)
        set(CPACK_PACKAGE_FILE_NAME
            "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_VERSION_SUFFIX}")
    endif()

    # WiX-only metadata
    if(WIN32 AND CPACK_PACKAGE_VERSION_SUFFIX)
        set(CPACK_WIX_PROPERTY_GIT_SHA "${CPACK_PACKAGE_VERSION_SUFFIX}")
    endif()
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "KodoShell")

    if(WIN32)
        set(CPACK_GENERATOR "WIX;ZIP")
        set(CPACK_WIX_UPGRADE_GUID "B2A1B2A1-B2A1-B2A1-B2A1-B2A1B2A1B2A1")
        set(CPACK_WIX_PROGRAM_MENU_FOLDER "KodoShell")
        set(CPACK_PACKAGE_EXECUTABLES "KodoShell" "KodoShell")
        set(CPACK_NSIS_INSTALLED_ICON_NAME "Resources/KodoShell.ico")
        set(CPACK_WIX_SHORTCUTS "KodoShell.ico")
    elseif(APPLE)
        set(CPACK_GENERATOR "DragNDrop")
        set(CPACK_BUNDLE_NAME "KodoShell")
        set(CPACK_APPLE_GUI_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
        set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.diegoiast.kodoshell")
    else()
        set(CPACK_GENERATOR "TGZ")
    endif()

    include(CPack)
endif()

