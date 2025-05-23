set(EXTRA_FILES)
set(EXTRA_LIBS)
if(CMAKE_SYSTEM_NAME MATCHES "NintendoWii|NintendoGameCube")
    list(APPEND EXTRA_LIBS opengx)
else()
    list(APPEND EXTRA_LIBS PkgConfig::OPENGL)
endif()

pkg_check_modules(GLU IMPORTED_TARGET glu)
pkg_check_modules(GLM IMPORTED_TARGET glm)

macro(add_example name)
    add_executable(${name} ${EXTRA_FILES} ${ARGN})
    if (TARGET textures)
        list(APPEND EXTRA_LIBS textures)
    endif()
    if (TARGET PkgConfig::GLM)
        list(APPEND EXTRA_LIBS PkgConfig::GLM)
    endif()
    if (TARGET PkgConfig::GLU)
        list(APPEND EXTRA_LIBS PkgConfig::GLU)
    endif()
    target_link_libraries(${name} ${EXTRA_LIBS} m PkgConfig::SDL2)
endmacro()

add_subdirectory(opengl20)
add_subdirectory(opengl30)
