# ordo_qt: Qt bridge layer built on top of ordo_core.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_AUTOMOC ON)

# Q_OBJECT headers are listed so AUTOMOC finds them: it only pairs a .cpp
# with a header in the same directory, and these live under include/.
add_library(ordo_qt STATIC
    src/view_adapter.cpp
    src/presenter.cpp
    src/view_host.cpp
    include/ordo/qt/view_adapter.h
    include/ordo/qt/presenter.h
    include/ordo/qt/view_model.h
)
add_library(ordo::qt ALIAS ordo_qt)
# Without EXPORT_NAME the installed target would be ordo::ordo_qt.
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

# --- Install & export -------------------------------------------------
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
