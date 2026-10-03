# The usage guide's task-list feature, Clean-Architecture-shaped: the same
# domain loop as the MVVM twin, reorganized so the file layout follows
# Clean's vocabulary (entities / use_cases / interface_adapters) and an
# imperative Presenter stands in for the ViewModel.
#
# Build from a checkout:
#   cmake -S examples/task-list-clean -B build-clean -G Ninja \
#         -DCMAKE_PREFIX_PATH=<Qt6>
#   cmake --build build-clean
#   ./build-clean/task-list-clean            # the app
#   ./build-clean/task-list-clean --smoke    # headless self-check, exit 0
cmake_minimum_required(VERSION 3.24)

project(task-list-clean LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ordo)

add_executable(task-list-clean
    main.cpp
    interface_adapters/task_list_presenter.cpp
)

target_include_directories(task-list-clean PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(task-list-clean
    PRIVATE
        ordo::qt
        Qt6::Widgets
)
