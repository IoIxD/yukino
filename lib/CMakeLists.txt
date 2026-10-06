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

cmake_minimum_required(VERSION 3.5)

project(libyukino LANGUAGES C VERSION 0.1.0)

find_package(PkgConfig REQUIRED)

pkg_check_modules(XCB xcb)
pkg_check_modules(ZLIB zlib)
pkg_check_modules(GIO gio-2.0)

# Make these empty for now
set(LIBRARIES "")
set(INCLUDE "")
set(DEFINES "")

set(SRC_FILES yukino.c crc32.c adler32.c fmt/png.c fmt/ppm.c fmt/bmp.c)

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

if(GIO_FOUND)
  find_program(GDBUS_CODEGEN gdbus-codegen REQUIRED)

  set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  file(MAKE_DIRECTORY ${GEN_DIR})

  function(gen_iface iface)
    message(STATUS "generating ${GEN_DIR}/${iface}.c")
    add_custom_target(${iface}
      OUTPUT ${GEN_DIR}/${iface}.c ${GEN_DIR}/${iface}.h
      COMMAND ${GDBUS_CODEGEN}
              --generate-c-code ${GEN_DIR}/${iface}
              /usr/share/dbus-1/interfaces/${iface}.xml
      DEPENDS /usr/share/dbus-1/interfaces/${iface}.xml
      COMMENT "Generating GDBus code"
    )
    list(APPEND SRC_FILES "${GEN_DIR}/${iface}.c")
    set(SRC_FILES ${SRC_FILES} PARENT_SCOPE)
  endfunction()

  gen_iface(org.freedesktop.portal.Screenshot)
  gen_iface(org.freedesktop.portal.Request)

  list(APPEND LIBRARIES ${GIO_LINK_LIBRARIES})
  list(APPEND LIBRARIES m)
  list(APPEND INCLUDE ${GIO_INCLUDE_DIRS})
  list(APPEND INCLUDE ${GEN_DIR})
  list(APPEND DEFINES "-DYUKINO_GIO=1")
  list(APPEND SRC_FILES sys/gio.c)

  message(STATUS "${SRC_FILES}")

endif()

add_library(libyukino ${SRC_FILES})

set_target_properties(libyukino PROPERTIES OUTPUT_NAME "yukino" C_VISIBILITY_PRESET hidden)

target_include_directories(libyukino PRIVATE ${INCLUDE})
target_include_directories(libyukino PUBLIC .)
target_link_libraries(libyukino PRIVATE ${LIBRARIES})
target_compile_definitions(libyukino PRIVATE ${DEFINES})
