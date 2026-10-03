# The usage guide's task-list feature, MVVM-shaped: the domain loop is
# identical to the Presenter version -- only the view adapter differs.
#
# Build from a checkout:
#   cmake -S examples/task-list-mvvm -B build-mvvm -G Ninja \
#         -DCMAKE_PREFIX_PATH=<Qt6>
#   cmake --build build-mvvm
#   ./build-mvvm/task-list-mvvm            # the app
#   ./build-mvvm/task-list-mvvm --smoke    # headless self-check, exit 0
cmake_minimum_required(VERSION 3.24)

project(task-list-mvvm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ordo)

add_executable(task-list-mvvm
    main.cpp
    model/task_list.cpp
    model/task_commands.cpp
    viewmodel/task_list_view_model.cpp
    view/task_list_window.cpp
)

target_include_directories(task-list-mvvm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(task-list-mvvm
    PRIVATE
        ordo::qt
        Qt6::Widgets
)
