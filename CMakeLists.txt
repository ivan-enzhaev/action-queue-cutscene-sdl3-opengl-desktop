cmake_minimum_required(VERSION 3.21)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

project(action-queue-cutscene-sdl3-opengl-desktop CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)

if(WIN32)
    set(SDL3_DIR "C:/libs/sdl3-3.4.20-mingw/lib/cmake/SDL3")
    set(CGLM_INCLUDE_DIR "C:/libs/cglm-0.9.6/include")
    set(GLAD_INCLUDE_DIR "C:/libs/glad-0.1.36/opengl-3.3/include")
    set(GLAD_SOURCE "C:/libs/glad-0.1.36/opengl-3.3/src/glad.c")
else()
    set(SDL3_DIR "$ENV{HOME}/libs/sdl3-3.4.20-ubuntu/lib/cmake/SDL3")
    set(CGLM_INCLUDE_DIR "$ENV{HOME}/libs/cglm-0.9.6/include")
    set(GLAD_INCLUDE_DIR "$ENV{HOME}/libs/glad-0.1.36/opengl-3.3/include")
    set(GLAD_SOURCE "$ENV{HOME}/libs/glad-0.1.36/opengl-3.3/src/glad.c")
endif()

find_package(SDL3 REQUIRED CONFIG)

add_executable(app
    src/main.cpp
    ${GLAD_SOURCE}
)

target_include_directories(app PRIVATE
    ${CGLM_INCLUDE_DIR}
    ${GLAD_INCLUDE_DIR}
)

if(WIN32)
    target_compile_options(app PRIVATE -Os)
    target_link_options(app PRIVATE -static -s -static-libgcc)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(app PRIVATE -mconsole)
    else()
        target_link_options(app PRIVATE -mwindows)
    endif()

    target_link_libraries(app PRIVATE
        SDL3::SDL3-static
        OpenGL::GL
    )
else()
    target_link_libraries(app PRIVATE
        SDL3::SDL3-static
        OpenGL::GL
    )
endif()
