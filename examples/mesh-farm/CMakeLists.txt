# mesh-farm: thread-pool bake of procedural parts, shown in a QOpenGLWidget shelf.
# --smoke is the headless self-check; --gui-probe is a developer check that captures screenshots.
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
    smoke.cpp
    probe.cpp
    checks.h
    domain/part_shelf.cpp
    domain/regenerate_command.cpp
    domain/bake_job_commands.cpp
    infra/job_runner.cpp
    view/shelf_gl_widget.cpp
    view/shelf_presenter.cpp
    view/farm_window.cpp
)

target_include_directories(mesh-farm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The DLL lands next to the exe (default output dir).
target_link_libraries(mesh-farm PRIVATE worklib ordo::core ordo::qt Qt6::Core Qt6::Widgets Qt6::OpenGLWidgets)
