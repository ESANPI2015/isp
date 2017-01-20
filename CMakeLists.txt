cmake_minimum_required(VERSION 2.8)

project(isp)

option(ISP_ENABLE_TESTING
    "will enable testing, add the 'test' subdirectory and add some additional
    compiler-flags for debugging"
    OFF)

# we will need to execute binaries, which is not possible while cross-compiling
if(CMAKE_CROSSCOMPILING AND ISP_ENABLE_TESTING)
    message(FATAL_ERROR "${PROJECT_NAME}: cannot enable testing while in cross-compiling mode")
endif(CMAKE_CROSSCOMPILING AND ISP_ENABLE_TESTING)

if(ISP_ENABLE_TESTING)
    add_definitions(-g -O0 -Wall -Wpedantic)
endif(ISP_ENABLE_TESTING)

if(NOT TARGET ndlcom)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ndlcom ndlcom)
endif(NOT TARGET ndlcom)
if(NOT TARGET representations)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../lib/representations representations)
endif(NOT TARGET representations)

# nothing special here
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
# the following is needed because library 'representation' generates code!
#include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

SET(ENV{PKG_CONFIG_PATH}
    ${CMAKE_BINARY_DIR}:$ENV{PKG_CONFIG_PATH}
    )

find_package(PkgConfig)
pkg_check_modules(${PROJECT_NAME}_PKGCONFIG REQUIRED
    ndlcom representations
    )

include_directories(${${PROJECT_NAME}_PKGCONFIG_INCLUDE_DIRS})
link_directories(${${PROJECT_NAME}_PKGCONFIG_LIBRARY_DIRS})
add_definitions(${${PROJECT_NAME}_PKGCONFIG_CFLAGS_OTHER} -Wall -pedantic)

# the usual create-library-blocks
set(SOURCES_lib
    src/isp.c
)
set(HEADERS_lib
    include/${PROJECT_NAME}/isp.h
)

# define the lib
add_library(${PROJECT_NAME}
    ${SOURCES_lib}
    )
#
# installing:
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

if (NOT CMAKE_CROSSCOMPILING)
    # for use in shared libraries (MARS, Rock and so on). deliberately only
    # added in the "non-cross" case to keep codesizes small in embedded.
    add_definitions(-fPIC)
endif (NOT CMAKE_CROSSCOMPILING)

# testing has to be enabled explicitly!
if(NOT CMAKE_CROSSCOMPILING AND ISP_ENABLE_TESTING)
    enable_testing()
    add_subdirectory(test)
# some tooling for debugging and playing around
    add_subdirectory(tools)
endif(NOT CMAKE_CROSSCOMPILING AND ISP_ENABLE_TESTING)

# doxygen:
configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
add_custom_target(${PROJECT_NAME}-doc doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile && echo
    "-- ${PROJECT_NAME}: Documentation generated in ${CMAKE_CURRENT_BINARY_DIR}/html/index.html")
if(NOT TARGET doc)
    add_custom_target(doc)
endif(NOT TARGET doc)
add_dependencies(doc ${PROJECT_NAME}-doc)
# and install doxygen stuff as well.
# NOTE: cannot install documentation without calling "doc" before. looks like
# this bug: https://cmake.org/Bug/view.php?id=8438
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html
    DESTINATION share/doc/${PROJECT_NAME}
    OPTIONAL)

# pkg-config, to be installed:
configure_file(${PROJECT_NAME}.pc.in
    ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.pc @ONLY)
# pkg-config for use of this library inside the buildtree of another project
# (via add_subdirectory)
configure_file(${PROJECT_NAME}-uninstalled.pc.in
    ${CMAKE_BINARY_DIR}/${PROJECT_NAME}-uninstalled.pc @ONLY)

# all the headers to be installed (this includes the generated one)
install(FILES ${HEADERS_lib}
    DESTINATION include/${PROJECT_NAME})
# the pkg-config for external use has also to be installed
install(FILES ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.pc
    DESTINATION lib/pkgconfig)
