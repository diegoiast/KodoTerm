# SPDX-License-Identifier: MIT
# Author: Diego Iastrubni <diegoiast@gmail.com>

cmake_minimum_required(VERSION 3.16)

project(codoterm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
option(BUILD_EXAMPLES "Build the examples" ON)

include(FetchContent)

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

add_library(KodoTerm
    src/KodoTerm.cpp
    include/KodoTerm/KodoTerm.hpp
)
target_include_directories(KodoTerm PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(KodoTerm PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    vterm
)
add_library(KodoTerm::KodoTerm ALIAS KodoTerm)


if(BUILD_EXAMPLES)
add_executable(KodoTermDemo
  examples/main.cpp
)
target_link_libraries(KodoTermDemo PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  vterm
  KodoTerm::KodoTerm
)
endif()
