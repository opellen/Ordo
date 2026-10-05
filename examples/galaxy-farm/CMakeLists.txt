# galaxylib (the C-ABI DLL), a console harness that exercises it directly,
# and the Qt/ordo app (domain, infra and view layers). Run the app with no
# args for the GUI, or with --smoke for the headless self-check. --render-probe
# and --gui-probe are developer checks that capture screenshots; they are not
# features of the example.
cmake_minimum_required(VERSION 3.24)

project(galaxy-farm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_library(galaxylib SHARED galaxylib/galaxylib.cpp)

target_compile_definitions(galaxylib PRIVATE GALAXYLIB_BUILD)

target_include_directories(galaxylib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/galaxylib)

add_executable(galaxy-farm-harness galaxylib/harness_main.cpp)

# The DLL lands next to the exe (default output dir).
target_link_libraries(galaxy-farm-harness PRIVATE galaxylib)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ordo)

add_executable(galaxy-farm
    main.cpp
    smoke.cpp
    probe.cpp
    checks.h
    domain/star_map.cpp
    domain/jump_commands.cpp
    domain/galaxy_job_commands.cpp
    infra/job_runner.cpp
    view/galaxy_gl_widget.cpp
    view/farm_window.cpp
    view/jump_presenter.cpp
)

target_include_directories(galaxy-farm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The DLL lands next to the exe (default output dir).
target_link_libraries(galaxy-farm PRIVATE galaxylib ordo::core ordo::qt Qt6::Core Qt6::Widgets Qt6::OpenGLWidgets)
