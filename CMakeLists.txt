cmake_minimum_required(VERSION 3.13)
project(OpenGX VERSION 0.1)

option(BUILD_OPENGX "Build the opengx library" ON)
option(BUILD_DOCS "Build the documentation" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" "${CMAKE_MODULE_PATH}")

# This is ON by default, of course, but we allow switching it off in case one
# just needs to build the documentation
if(BUILD_OPENGX)

set(TARGET opengx)

include(GNUInstallDirs)

add_library(${TARGET} STATIC
    src/accum.c
    src/accum.h
    src/arrays.cpp
    src/arrays.h
    src/call_lists.c
    src/call_lists.h
    src/clip.c
    src/clip.h
    src/debug.c
    src/debug.h
    src/efb.c
    src/efb.h
    src/functions.c
    src/gc_gl.c
    src/getters.c
    src/gpu_resources.c
    src/gpu_resources.h
    src/image_DXT.c
    src/image_DXT.h
    src/murmurhash3.cpp
    src/murmurhash3.h
    src/opengx.h
    src/pixel_stream.cpp
    src/pixel_stream.h
    src/pixels.cpp
    src/pixels.h
    src/raster.cpp
    src/selection.c
    src/shader.c
    src/shader.h
    src/shader_api.c
    src/shader_attribute.cpp
    src/shader_functions.h
    src/shader_uniform.c
    src/state.h
    src/stencil.c
    src/stencil.h
    src/texel.h
    src/texture.c
    src/texture_gen_sw.c
    src/texture_gen_sw.h
    src/texture_unit.c
    src/texture_unit.h
    src/types.h
    src/utils.h
    src/vbo.c
    src/vertex.cpp
)
set_target_properties(${TARGET} PROPERTIES
    PUBLIC_HEADER src/opengx.h
)

target_include_directories(${TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

configure_file(opengl.pc.in opengl.pc @ONLY)

install(TARGETS ${TARGET}
    LIBRARY
    ARCHIVE
    RUNTIME
    PUBLIC_HEADER
)
install(DIRECTORY "include/GL" "include/KHR" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/opengl.pc
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
install(FILES OpenGLConfig.cmake
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenGL")

endif(BUILD_OPENGX)

if(BUILD_DOCS)
    include(UseLATEX)
    add_subdirectory(doc/src)
endif()
