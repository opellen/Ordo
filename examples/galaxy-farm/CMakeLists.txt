# galaxylib (the C-ABI DLL) plus a console harness that exercises it
# directly, plus the Qt/ordo app target -- domain + infra layers wired to
# StarMap/JobRunner, plus the view layer (window, presenter, GL viewport):
# run with no args for the interactive GUI, or --smoke / --render-probe /
# --gui-probe for the headless checks.
cmake_minimum_required(VERSION 3.24)

project(galaxy-farm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_library(galaxylib SHARED galaxylib/galaxylib.cpp)

target_compile_definitions(galaxylib PRIVATE GALAXYLIB_BUILD)

target_include_directories(galaxylib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/galaxylib)

add_executable(galaxy-farm-harness galaxylib/harness_main.cpp)

# galaxy-farm-harness and galaxylib land in the same output directory (this
# is a single-directory build, no per-target output dirs configured), so
# galaxylib's DLL is right beside the harness exe at runtime -- no
# PATH/install step needed for the loader to find it.
target_link_libraries(galaxy-farm-harness PRIVATE galaxylib)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ordo)

add_executable(galaxy-farm
    main.cpp
    domain/star_map.cpp
    domain/jump_commands.cpp
    domain/galaxy_job_commands.cpp
    infra/job_runner.cpp
    view/galaxy_gl_widget.cpp
    view/farm_window.cpp
    view/jump_presenter.cpp
)

target_include_directories(galaxy-farm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# galaxy-farm and galaxylib land in the same output directory (same
# single-directory build as the harness above), so galaxylib's DLL is right
# beside galaxy-farm.exe at runtime -- no PATH/install step needed for the
# loader to find it.
target_link_libraries(galaxy-farm PRIVATE galaxylib ordo::core ordo::qt Qt6::Core Qt6::Widgets Qt6::OpenGLWidgets)
