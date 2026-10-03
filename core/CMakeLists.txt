# ordo_core: pure C++ core library. Must never link or include Qt.
add_library(ordo_core STATIC
    src/version.cpp
    src/dispatcher.cpp
    src/agent.cpp
    src/agent_context.cpp
    src/command_context.cpp
    src/presenter_context.cpp
    src/kernel.cpp
)
add_library(ordo::core ALIAS ordo_core)
# NAMESPACE in install(EXPORT ...) below is just a string prefix on the
# target's own name -- without EXPORT_NAME the imported target would surface
# as ordo::ordo_core, not ordo::core. This makes the installed/find_package
# name match the ALIAS used for add_subdirectory() consumers.
set_target_properties(ordo_core PROPERTIES EXPORT_NAME core)

# version.h is generated from version.h.in (single source of truth: this
# project's VERSION, set once at the root) into the build tree's
# generated/ordo/core/ -- never hand-edit the generated file.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ordo/core/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/ordo/core/version.h"
    @ONLY
)

target_include_directories(ordo_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(ordo_core PUBLIC cxx_std_20)

# --- Install & export -------------------------------------------------
# A no-op for the add_subdirectory() embedding contract: these rules only
# fire when someone runs `cmake --install` on a build that includes this
# directory. ordo_core/ordo_qt keep existing as plain target names either way.
install(
    TARGETS ordo_core
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

# The generated header lives under the build tree, not under include/, so the
# DIRECTORY install above never sees it -- install it explicitly.
install(
    FILES "${CMAKE_BINARY_DIR}/generated/ordo/core/version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ordo/core
)
