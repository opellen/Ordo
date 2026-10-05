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
# Without EXPORT_NAME the installed target would be ordo::ordo_core.
set_target_properties(ordo_core PROPERTIES EXPORT_NAME core)

# version.h is generated from version.h.in; the version comes from project()
# in the root CMakeLists.txt.
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
# Only used by `cmake --install`.
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

# The generated header is in the build tree, so install it separately.
install(
    FILES "${CMAKE_BINARY_DIR}/generated/ordo/core/version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ordo/core
)
