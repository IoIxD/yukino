# yukino -- portable screenshot utility
# Copyright (C) 2026 Paper
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.5)

project(yukino LANGUAGES C VERSION 0.1.0)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL3 REQUIRED sdl3)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib)

set(LIBRARIES libyukino ${SDL3_LINK_LIBRARIES})

set(INCLUDE lib ${SDL3_INCLUDE_DIRS})

set(SRC_FILES src/main.c)

set(DEFINES)

if(XCB_FOUND)
  list(APPEND LIBRARIES ${XCB_LINK_LIBRARIES})
  list(APPEND INCLUDE ${XCB_INCLUDE_DIRS})
endif()

add_executable(yukino ${SRC_FILES})
set_property(TARGET yukino PROPERTY C_STANDARD 99)

target_include_directories(yukino PRIVATE ${INCLUDE})
target_link_libraries(yukino PRIVATE ${LIBRARIES})
target_compile_definitions(yukino PRIVATE ${DEFINES})
