# SPDX-License-Identifier: MIT
# Author: Diego Iastrubni <diegoiast@gmail.com>

cmake_minimum_required(VERSION 3.16)

project(codoterm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

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

add_executable(KodoTermDemo
  main.cpp
  KodoTerm.cpp
  KodoTerm.hpp
)

target_link_libraries(KodoTermDemo PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  vterm
)
