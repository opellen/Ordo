# The usage guide's task-list feature, MVVM-shaped for a QML view instead of
# widgets: the domain loop is identical to the widgets sibling -- only the
# view adapter (declarative QML + a QAbstractItemModel-backed projection)
# differs.
#
# Build from a checkout:
#   cmake -S examples/task-list-mvvm-qml -B build-mvvm-qml -G Ninja \
#         -DCMAKE_PREFIX_PATH=<Qt6>
#   cmake --build build-mvvm-qml
#   ./build-mvvm-qml/task-list-mvvm-qml               # the app
#   ./build-mvvm-qml/task-list-mvvm-qml --smoke       # headless self-check, exit 0
#   ./build-mvvm-qml/task-list-mvvm-qml --qml-smoke   # offscreen QML load check, exit 0
cmake_minimum_required(VERSION 3.24)

project(task-list-mvvm-qml LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)

qt_standard_project_setup()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ordo)

qt_add_executable(task-list-mvvm-qml
    main.cpp
    model/task_list.cpp
    model/task_commands.cpp
    viewmodel/task_list_view_model.cpp
)

qt_add_qml_module(task-list-mvvm-qml
    URI TaskListMvvmQml
    VERSION 1.0
    QML_FILES view/Main.qml
)

target_include_directories(task-list-mvvm-qml PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(task-list-mvvm-qml
    PRIVATE
        ordo::qt
        Qt6::Gui
        Qt6::Quick
)
