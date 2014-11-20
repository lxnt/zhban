cmake_minimum_required(VERSION 2.8)
project(libhbf)

set(ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/:${CMAKE_INSTALL_PREFIX}/lib/x86_64-linux-gnu/pkgconfig/:")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ARCH64 TRUE)
else()
  set(ARCH64 FALSE)
endif()

set(CMAKE_C_FLAGS "-std=c99 -Wall -Wextra -fvisibility=hidden -msse -m3dnow -mmmx")
set(CMAKE_C_FLAGS_DEBUG "-ggdb3")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -ggdb3")
set(CMAKE_C_FLAGS_RELEASE "-O3 -ggdb3")

if (ARCH64)
    option(BUILD_32BIT "Compile 32-bit version" OFF)
    if(BUILD_32BIT)
        set(CMAKE_LIBRARY_PATH /usr/lib32 /usr/lib/i386-linux-gnu)
        set(CMAKE_C_FLAGS "-m32 ${CMAKE_C_FLAGS}")
    endif()
endif()

option(BUILD_STATIC "Build libzhban_s.a" OFF)

include(FindPkgConfig)
pkg_check_modules(PKG_FT2 REQUIRED freetype2)
link_directories(${PKG_FT2_LIBRARY_DIRS})
include_directories( ${PKG_FT2_INCLUDE_DIRS} )

pkg_check_modules(PKG_HBZ REQUIRED harfbuzz)
link_directories(${PKG_HBZ_LIBRARY_DIRS})
include_directories( ${PKG_HBZ_INCLUDE_DIRS} )

if (BUILD_STATIC)
    add_library(zhban_s STATIC zhban.c utf.c logging.c)
    install(TARGETS zhban_s ARCHIVE DESTINATION lib)
endif()

add_library(zhban SHARED zhban.c utf.c logging.c)
target_link_libraries(zhban ${PKG_HBZ_LIBRARIES} ${PKG_FT2_LIBRARIES})
install(TARGETS zhban LIBRARY DESTINATION lib)

add_executable(zhbantest test.c)
target_link_libraries(zhbantest zhban)
install(TARGETS zhbantest RUNTIME DESTINATION bin/)

install(FILES zhban.h DESTINATION include)
install(FILES zhban.py DESTINATION lib/python3.2/site-packages)

