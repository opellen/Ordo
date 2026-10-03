# Consumer smoke test for the INSTALLED ordo package -- deliberately NOT
# add_subdirectory()'d by tests/CMakeLists.txt or anything else in the main
# build. Ordo is consumed exactly the way an external project would: only
# through find_package(ordo CONFIG), never a relative path back into the repo.
#
# Manual usage, against an install produced by `cmake --install`:
#
#   cmake -S tests/package -B <build-dir> -G Ninja \
#       -DCMAKE_PREFIX_PATH="<ordo-install-prefix>;<Qt6-prefix>"
#   cmake --build <build-dir>
#   <build-dir>/smoke_core.exe
#   <build-dir>/smoke_qt.exe     # only exists if the install has ordo::qt
#
# <Qt6-prefix> is only needed when the ordo install actually has ordo::qt --
# ordoConfig.cmake's own find_dependency(Qt6) call fails fast otherwise.
cmake_minimum_required(VERSION 3.24)

project(ordo_package_smoke_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ordo CONFIG REQUIRED)

add_executable(smoke_core main_core.cpp)
target_link_libraries(smoke_core PRIVATE ordo::core)

# ordo::qt exists in the imported config only when the ordo install being
# consumed was built with ORDO_BUILD_QT=ON (core-only proof, see README).
# Gating on the target -- not a separate option here -- means this project
# configures and builds cleanly against either kind of install unmodified.
if(TARGET ordo::qt)
    add_executable(smoke_qt main_qt.cpp)
    target_link_libraries(smoke_qt PRIVATE ordo::qt)
endif()
