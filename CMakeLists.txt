cmake_minimum_required(VERSION 3.12)
project(Visualizador)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm QUIET)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)

if(EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(STATUS "Dear ImGui --- Soporte de interfaz grafica pal punto extra.")
    set(SOURCES
        main.cpp
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    add_executable(visualizador ${SOURCES})
    target_include_directories(visualizador PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_compile_definitions(visualizador PRIVATE USE_IMGUI)
else()
    message(STATUS "Dear ImGui no encontrado, se usara mouse y tec")
    add_executable(visualizador main.cpp)
endif()
target_link_libraries(visualizador PRIVATE OpenGL::GL GLEW::GLEW glfw)