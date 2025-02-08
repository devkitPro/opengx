pkg_check_modules(PNG IMPORTED_TARGET libpng)

set(EXTRA_LIBS)
if(CMAKE_SYSTEM_NAME MATCHES "NintendoWii|NintendoGameCube")
    list(APPEND EXTRA_LIBS opengx)
else()
    list(APPEND EXTRA_LIBS PkgConfig::OPENGL)
endif()

function(add_resource out_var)
    set(result)
    foreach(in_f ${ARGN})
        string(MAKE_C_IDENTIFIER ${in_f} input_identifier)
        set(out_f "${CMAKE_CURRENT_BINARY_DIR}/${input_identifier}.o")

        add_custom_command(
            OUTPUT ${out_f}
            COMMAND ${CMAKE_LINKER} -z noexecstack --relocatable --format binary --output ${out_f} ${in_f}
            DEPENDS ${in_f}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Embedding ${in_f} as a resource"
        )
        list(APPEND result ${out_f})
    endforeach()
    set(${out_var} ${result} PARENT_SCOPE)
endfunction()

if(PNG_FOUND)
    add_resource(TEXTURES
        grid512.png
        mix256.png
    )
    add_library(textures STATIC
        ${TEXTURES}
        textures.c
        textures.h
    )
    target_link_libraries(textures PUBLIC ${EXTRA_LIBS} PkgConfig::PNG)
    target_include_directories(textures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
