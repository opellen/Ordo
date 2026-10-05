# Ordo: a typed-event application framework for C++20.
#
# Standalone: configure this directory to build the libraries and run the
# tests. Embedded: add_subdirectory() it from a consumer (ORDO_BUILD_TESTS
# defaults OFF there). Installed: `cmake --install`, then
# find_package(ordo CONFIG). See the README's Build / Install / Consume section.
cmake_minimum_required(VERSION 3.24)

project(ordo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ORDO_BUILD_QT "Build the ordo_qt bridge layer (requires Qt6)" ON)
option(ORDO_BUILD_TESTS "Build ordo's own test suite" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)

add_subdirectory(core)

if(ORDO_BUILD_QT)
    add_subdirectory(qt)
endif()

if(ORDO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Agent skill for Claude Code (opt-in) --------------------------------
# `cmake --build <dir> --target ordo-skill-claude` copies docs/skills/ordo-use
# into the top-level project's .claude/skills/. Not part of ALL. It never
# edits AGENTS.md or CLAUDE.md; Codex and Cursor users point AGENTS.md at
# docs/skills/ordo-use/integration/AGENTS-snippet.md instead.
set(ORDO_SKILL_CLAUDE_DEST "${CMAKE_SOURCE_DIR}/.claude/skills/ordo-use")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ordo-use-skill-version.md"
"# ordo-use skill version\n\nordo ${PROJECT_VERSION}\n\nThis copy was installed by the ordo-skill-claude target from the vendored ordo source.\n"
)

add_custom_target(ordo-skill-claude
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/skills/ordo-use"
        "${ORDO_SKILL_CLAUDE_DEST}"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/ordo-use-skill-version.md"
        "${ORDO_SKILL_CLAUDE_DEST}/ordo-use-skill-version.md"
    COMMAND "${CMAKE_COMMAND}" -E echo
        "ordo: installed the ordo-use skill to ${ORDO_SKILL_CLAUDE_DEST}"
    COMMAND "${CMAKE_COMMAND}" -E echo
        "ordo: AGENTS.md-based tools (Codex/Cursor) need no copy -- see docs/skills/ordo-use/integration/AGENTS-snippet.md instead"
    VERBATIM
    COMMENT "Opt-in copy of the ordo-use Claude Code skill into the consumer's .claude/skills/; never in ALL, never edits AGENTS.md/CLAUDE.md"
)

# --- Install & export ---------------------------------------------------
# Exports ordo::core and ordo::qt for find_package(ordo). ordo::qt is exported
# only when ORDO_BUILD_QT is ON.
set(ORDO_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/ordo")

include(CMakePackageConfigHelpers)

# Pre-1.0 a minor release may break the API, so 0.1 must not match 0.2.
# SameMajorVersion from 1.0 on.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ordoConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ordoConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ordoConfig.cmake"
    INSTALL_DESTINATION "${ORDO_INSTALL_CMAKEDIR}"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ordoConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ordoConfigVersion.cmake"
    DESTINATION "${ORDO_INSTALL_CMAKEDIR}"
)

install(
    EXPORT ordoTargets
    NAMESPACE ordo::
    DESTINATION "${ORDO_INSTALL_CMAKEDIR}"
)

# --- CPack: source + binary archives ------------------------------------
# Archive generators only.
set(CPACK_PACKAGE_NAME "ordo")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VENDOR "San")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A typed-event application framework for Modern C++")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_SOURCE_GENERATOR "ZIP;TGZ")
# Backslashes are doubled twice: CPack writes this value into a generated
# file that CMake parses again.
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/build[^/]*/"
    "/\\\\.vs/"
    "/\\\\.vscode/"
    "/\\\\.idea/"
)

set(CPACK_GENERATOR "ZIP")

include(CPack)
