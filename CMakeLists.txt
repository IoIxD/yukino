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

project(
	yukino
	LANGUAGES C
	VERSION 0.1.0)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL3 REQUIRED sdl3)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib)

set(LIBRARIES libyukino ${SDL3_LINK_LIBRARIES})

set(INCLUDE lib ${SDL3_INCLUDE_DIRS})

set(SRC_FILES src/main.c)

set(DEFINES)

pkg_check_modules(WAYLAND wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols)
find_program(WAYLAND_SCANNER wayland-scanner)

if(WAYLAND_FOUND
   AND WAYLAND_PROTOCOLS_FOUND
   AND WAYLAND_SCANNER)
	pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

	set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
	file(MAKE_DIRECTORY ${GEN_DIR})

	function(gen_protocol name)
		add_custom_command(
			OUTPUT ${GEN_DIR}/${name}.c ${GEN_DIR}/${name}.h
			COMMAND
				${WAYLAND_SCANNER} client-header
				${CMAKE_CURRENT_SOURCE_DIR}/protocol/${name}.xml
				${GEN_DIR}/${name}.h
			COMMAND
				${WAYLAND_SCANNER} private-code
				${CMAKE_CURRENT_SOURCE_DIR}/protocol/${name}.xml
				${GEN_DIR}/${name}.c
			DEPENDS ${xml}
			VERBATIM)
		list(APPEND SRC_FILES ${GEN_DIR}/${name}.c ${GEN_DIR}/${name}.h)
		set(SRC_FILES
		    ${SRC_FILES}
		    PARENT_SCOPE)
	endfunction()

	# layer shell's get_popup references xdg_popup, so xdg-shell is needed
	# too
	gen_protocol(wlr-layer-shell-unstable-v1)
	gen_protocol(xdg-shell)
	gen_protocol(xdg-output-unstable-v1)

	list(APPEND SRC_FILES src/layer_shell.c)
	list(APPEND LIBRARIES ${WAYLAND_LINK_LIBRARIES})
	list(APPEND INCLUDE ${WAYLAND_INCLUDE_DIRS} ${GEN_DIR})
	list(APPEND DEFINES YUKINO_LAYER_SHELL=1)
endif()

if(XCB_FOUND)
	list(APPEND LIBRARIES ${XCB_LINK_LIBRARIES})
	list(APPEND INCLUDE ${XCB_INCLUDE_DIRS})
endif()

add_executable(yukino ${SRC_FILES})
set_property(TARGET yukino PROPERTY C_STANDARD 99)

target_include_directories(yukino PRIVATE ${INCLUDE})
target_link_libraries(yukino PRIVATE ${LIBRARIES})
target_compile_definitions(yukino PRIVATE ${DEFINES})
