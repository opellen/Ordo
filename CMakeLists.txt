# Ordo — a typed-event application framework for Modern C++.
#
# Standalone: configure this directory directly to build the libraries and run
# the test suite. Embedded: add_subdirectory() it from a consumer, where
# ORDO_BUILD_TESTS defaults OFF and the consumer owns its own test wiring.
# Installed: `cmake --install` + a consumer's `find_package(ordo CONFIG)` --
# see README's Build / Install / Consume section. All three stay equally
# supported; nothing below changes the add_subdirectory() contract.
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

# --- Consumer skill: Claude Code (opt-in) -------------------------------
# ordo-skill-claude copies docs/skills/ordo-use into the *consuming*
# project's .claude/skills/, so Claude Code's native skill loader can find
# it -- never wired into ALL, so it only runs when a user explicitly asks
# for it. CMAKE_SOURCE_DIR (not CMAKE_CURRENT_SOURCE_DIR) is deliberate:
# when ordo is vendored via add_subdirectory()/FetchContent, that is the
# consumer's own project root -- the whole point of this target. When ordo
# is configured standalone (top-level), CMAKE_SOURCE_DIR is ordo's own
# root, so the copy lands in ordo's own .claude/ instead -- harmless,
# explicit-invocation-only dogfooding. Codex/Cursor users need no copy at
# all: point their AGENTS.md at
# docs/skills/ordo-use/integration/AGENTS-snippet.md instead. This target
# must never edit a user's AGENTS.md or CLAUDE.md -- guidance goes to the
# console only, for the user to paste by hand.
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
# ordoTargets (the EXPORT set core/CMakeLists.txt and qt/CMakeLists.txt's own
# install(TARGETS ... EXPORT ordoTargets) calls feed) becomes an importable
# ordo::core / ordo::qt package here. ordo_qt is only ever in the set when
# ORDO_BUILD_QT was ON at install time -- see cmake/ordoConfig.cmake.in for
# how a consumer's find_package(ordo) detects that and skips Qt otherwise.
set(ORDO_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/ordo")

include(CMakePackageConfigHelpers)

# SameMinorVersion while pre-1.0: minor releases may still move the API, so a
# consumer asking for 0.1 must not silently match an incompatible 0.2 install.
# Switch back to SameMajorVersion at 1.0.
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
# Minimal on purpose: archive generators only, no NSIS/installer tooling.
set(CPACK_PACKAGE_NAME "ordo")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VENDOR "San")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A typed-event application framework for Modern C++")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_SOURCE_GENERATOR "ZIP;TGZ")
# Four backslashes (not two): CPack re-serializes this value into a generated
# CPackSourceConfig.cmake that is itself re-parsed by CMake, so the string
# has to survive an extra round of \\ -> \ unescaping before it reaches the
# actual ignore regex -- two backslashes here would leave a bare "\." in the
# generated file, which CMake (correctly) warns is not a recognized escape.
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/build[^/]*/"
    "/\\\\.vs/"
    "/\\\\.vscode/"
    "/\\\\.idea/"
)

set(CPACK_GENERATOR "ZIP")

include(CPack)
