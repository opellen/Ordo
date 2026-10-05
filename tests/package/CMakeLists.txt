# Smoke test for an installed ordo package. Not part of the main build: it
# consumes ordo only through find_package(ordo CONFIG).
#
# Usage, against an install produced by `cmake --install`:
#
#   cmake -S tests/package -B <build-dir> -G Ninja \
#       -DCMAKE_PREFIX_PATH="<ordo-install-prefix>;<Qt6-prefix>"
#   cmake --build <build-dir>
#   <build-dir>/smoke_core.exe
#   <build-dir>/smoke_qt.exe     # only exists if the install has ordo::qt
#
# <Qt6-prefix> is needed only when the install has ordo::qt.
cmake_minimum_required(VERSION 3.24)

project(ordo_package_smoke_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ordo CONFIG REQUIRED)

add_executable(smoke_core main_core.cpp)
target_link_libraries(smoke_core PRIVATE ordo::core)

# ordo::qt exists only if the install was built with ORDO_BUILD_QT=ON.
if(TARGET ordo::qt)
    add_executable(smoke_qt main_qt.cpp)
    target_link_libraries(smoke_qt PRIVATE ordo::qt)
endif()
