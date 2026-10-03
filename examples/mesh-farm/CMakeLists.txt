# v3: + the QOpenGLWidget shelf view
cmake_minimum_required(VERSION 3.24)

project(mesh-farm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_library(worklib SHARED worklib/worklib.cpp)

target_compile_definitions(worklib PRIVATE WORKLIB_BUILD)

target_include_directories(worklib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/worklib)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ordo)

add_executable(mesh-farm
    main.cpp
    domain/part_shelf.cpp
    domain/regenerate_command.cpp
    domain/bake_job_commands.cpp
    infra/job_runner.cpp
    view/shelf_gl_widget.cpp
    view/shelf_presenter.cpp
    view/farm_window.cpp
)

target_include_directories(mesh-farm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# worklib and mesh-farm land in the same output directory (this is a
# single-directory build, no per-target output dirs configured), so
# worklib's DLL is right beside mesh-farm.exe at runtime -- no PATH/install
# step needed for the loader to find it.
target_link_libraries(mesh-farm PRIVATE worklib ordo::core ordo::qt Qt6::Core Qt6::Widgets Qt6::OpenGLWidgets)
