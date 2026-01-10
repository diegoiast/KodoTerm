# SPDX-License-Identifier: MIT
# Author: Diego Iastrubni <diegoiast@gmail.com>

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
if(UNIX AND NOT APPLE)
    find_package(Qt6 COMPONENTS DBus)
endif()

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
endif()

add_executable(KodoTermSimple WIN32
    simple/main.cpp
)
target_link_libraries(KodoTermSimple PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KodoTerm::KodoTerm
)

add_executable(KodoShell WIN32
    tabbed/main.cpp
    tabbed/TabbedTerminal.cpp
    tabbed/TabbedTerminal.h
    tabbed/AppConfig.cpp
    tabbed/AppConfig.h
    tabbed/ConfigDialog.cpp
    tabbed/ConfigDialog.h
    tabbed/KodoShell.qrc
)
target_link_libraries(KodoShell PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KodoTerm::KodoTerm
)

if(Qt6DBus_FOUND)
    target_link_libraries(KodoShell PRIVATE Qt6::DBus)
    target_compile_definitions(KodoShell PRIVATE HAS_DBUS)
endif()

if(X11_FOUND)
    target_link_libraries(KodoShell PRIVATE X11::X11)
    target_compile_definitions(KodoShell PRIVATE HAS_X11)

    # Check for Qt 6 X11 native interface
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES Qt6::Gui)
    check_cxx_source_compiles("
        #include <QtGui/qguiapplication.h>
        #include <QtGui/QNativeInterface>
        int main() {
            auto interface = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
            return 0;
        }
    " HAS_X11_NATIVE_INTERFACE)
    
    if(HAS_X11_NATIVE_INTERFACE)
        target_compile_definitions(KodoShell PRIVATE HAS_X11_NATIVE_INTERFACE)
    endif()
endif()