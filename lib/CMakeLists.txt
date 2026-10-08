# libyukino -- portable screenshots
# Copyright (C) 2026 Paper
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.10)

project(
	libyukino
	LANGUAGES C
	VERSION 0.1.0)

find_package(PkgConfig REQUIRED)

pkg_check_modules(XCB xcb)
pkg_check_modules(ZLIB zlib)
pkg_check_modules(GIO gio-2.0 glib-2.0)
pkg_check_modules(DBUS dbus-1)
pkg_check_modules(WAYLAND wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner)
find_program(GDBUS_CODEGEN gdbus-codegen)

if(YUKINO_TEST)
	enable_testing()
	add_subdirectory(test)
endif()

# Make these empty for now
set(LIBRARIES "")
set(INCLUDE "")
set(DEFINES "")

set(SRC_FILES
    yukino.c
    crc32.c
    adler32.c
    uri.c
    random.c
    fmt/png.c
    fmt/ppm.c
    fmt/bmp.c)

if(XCB_FOUND)
	list(APPEND LIBRARIES ${XCB_LINK_LIBRARIES})
	list(APPEND INCLUDE ${XCB_INCLUDE_DIRS})
	list(APPEND SRC_FILES sys/xcb.c)
	list(APPEND DEFINES "-DYUKINO_XCB=1")
endif()

if(ZLIB_FOUND)
	list(APPEND LIBRARIES ${ZLIB_LINK_LIBRARIES})
	list(APPEND INCLUDE ${ZLIB_INCLUDE_DIRS})
	list(APPEND DEFINES "-DYUKINO_ZLIB=1")
endif()

# TODO should prob only do this if it's needed'
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})
list(APPEND INCLUDE ${GEN_DIR})

if(WAYLAND_FOUND AND WAYLAND_SCANNER)
	function(gen_protocol protocol)
		message(STATUS "generating ${GEN_DIR}/${protocol}.c")
		add_custom_command(
			OUTPUT ${GEN_DIR}/${protocol}.c ${GEN_DIR}/${protocol}.h
			COMMAND
				${WAYLAND_SCANNER} client-header
				${CMAKE_CURRENT_SOURCE_DIR}/protocol/${protocol}.xml
				${GEN_DIR}/${protocol}.h
			COMMAND
				${WAYLAND_SCANNER} private-code
				${CMAKE_CURRENT_SOURCE_DIR}/protocol/${protocol}.xml
				${GEN_DIR}/${protocol}.c
			DEPENDS
				${CMAKE_CURRENT_SOURCE_DIR}/protocol/${protocol}.xml
			VERBATIM)
		list(APPEND SRC_FILES "${GEN_DIR}/${protocol}.c"
		     "${GEN_DIR}/${protocol}.h")
		set(SRC_FILES
		    ${SRC_FILES}
		    PARENT_SCOPE)
	endfunction()

	gen_protocol(xdg-output-unstable-v1)

	list(APPEND LIBRARIES ${WAYLAND_LINK_LIBRARIES})
	list(APPEND INCLUDE ${WAYLAND_INCLUDE_DIRS})
	list(APPEND DEFINES "-DYUKINO_WAYLAND=1")
	list(APPEND SRC_FILES sys/wayland.c)
endif()

if(DBUS_FOUND)
	list(APPEND LIBRARIES ${DBUS_LINK_LIBRARIES})
	list(APPEND INCLUDE ${DBUS_INCLUDE_DIRS})
	list(APPEND DEFINES "-DYUKINO_XDG=1 -DYUKINO_KWIN=1")
	list(APPEND SRC_FILES sys/xdg.c sys/kwin.c)
endif()

if(DBUS_FOUND
   AND WAYLAND_FOUND
   AND WAYLAND_SCANNER)
	list(APPEND LIBRARIES m)
	list(APPEND DEFINES "-DYUKINO_GIO=1")
	list(APPEND SRC_FILES sys/gio.c)
endif()

add_library(libyukino ${SRC_FILES})

set_target_properties(libyukino PROPERTIES OUTPUT_NAME "yukino"
					   C_VISIBILITY_PRESET hidden)

target_include_directories(libyukino PRIVATE ${INCLUDE})
target_include_directories(libyukino PUBLIC .)
target_link_libraries(libyukino PRIVATE ${LIBRARIES})
target_compile_definitions(libyukino PRIVATE ${DEFINES})
