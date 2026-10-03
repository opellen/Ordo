# The classic TodoMVC feature set as a Qt Widgets app on ordo, translated
# from the PureMVC JS demo (https://github.com/PureMVC/puremvc-js-demo-todomvc).
# See README.md for the full translation map.
#
# Build from a checkout:
#   cmake -S examples/todomvc -B build-todomvc -G Ninja \
#         -DCMAKE_PREFIX_PATH=<Qt6>
#   cmake --build build-todomvc
#   ./build-todomvc/todomvc            # the app
#   ./build-todomvc/todomvc --smoke    # headless self-check, exit 0
cmake_minimum_required(VERSION 3.24)

project(todomvc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ordo)

add_executable(todomvc
    main.cpp
    model/todo_list.cpp
    controller/todo_commands.cpp
    controller/persistence_commands.cpp
    infra/db_worker.cpp
    view/todo_view_model.cpp
    view/todo_window.cpp
)

target_include_directories(todomvc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(todomvc
    PRIVATE
        ordo::qt
        Qt6::Widgets
        Qt6::Sql
)
