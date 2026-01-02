# SPDX-License-Identifier: MIT
# Author: Diego Iastrubni <diegoiast@gmail.com>

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

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
