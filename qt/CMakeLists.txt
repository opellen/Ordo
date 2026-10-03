# ordo_qt: Qt bridge layer built on top of ordo_core.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_AUTOMOC ON)

# Q_OBJECT headers are listed explicitly because
# CMake AUTOMOC's basename-pairing only looks for a matching header in the same
# directory as its .cpp; this target splits public include/ from src/, so
# without this a Q_OBJECT class in a listed header is silently never moc'd.
add_library(ordo_qt STATIC
    src/view_adapter.cpp
    src/presenter.cpp
    src/view_host.cpp
    include/ordo/qt/view_adapter.h
    include/ordo/qt/presenter.h
    include/ordo/qt/view_model.h
)
add_library(ordo::qt ALIAS ordo_qt)
# See core/CMakeLists.txt's comment on EXPORT_NAME -- same reason: without
# this, the installed/find_package target would be ordo::ordo_qt, not
# ordo::qt.
set_target_properties(ordo_qt PROPERTIES EXPORT_NAME qt)

target_include_directories(ordo_qt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(ordo_qt
    PUBLIC
        ordo_core
        Qt6::Core
        Qt6::Widgets
)

# --- Install & export (see core/CMakeLists.txt's comment -- same contract:
# a no-op unless someone runs `cmake --install`) -------------------------
install(
    TARGETS ordo_qt
    EXPORT ordoTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
