# Ordo's own suite. GoogleTest is fetched at configure time. The core tests
# do not link Qt; the two Qt tests are at the bottom.
include(FetchContent)

# Match the runtime library gtest is built with to avoid ABI mismatches on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# Don't install googletest along with ordo.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

foreach(name version dispatcher agent command kernel)
    if(name STREQUAL "version")
        set(src ordo_version_test.cpp)
    else()
        set(src ${name}_test.cpp)
    endif()
    add_executable(ordo_${name}_test ${src})
    target_link_libraries(ordo_${name}_test PRIVATE ordo_core GTest::gtest_main)
    gtest_discover_tests(ordo_${name}_test)
endforeach()

if(ORDO_BUILD_QT)
    set(CMAKE_AUTOMOC ON)  # view_model_test.cpp defines a Q_OBJECT class
    add_executable(ordo_presenter_test presenter_test.cpp)
    target_link_libraries(ordo_presenter_test PRIVATE ordo_qt GTest::gtest_main)
    # PRE_TEST: discover at ctest time, when the Qt DLLs are on PATH.
    gtest_discover_tests(ordo_presenter_test DISCOVERY_MODE PRE_TEST)

    add_executable(ordo_view_model_test view_model_test.cpp)
    target_link_libraries(ordo_view_model_test PRIVATE ordo_qt GTest::gtest_main)
    gtest_discover_tests(ordo_view_model_test DISCOVERY_MODE PRE_TEST)
endif()
