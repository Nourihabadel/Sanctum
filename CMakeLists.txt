cmake_minimum_required(VERSION 3.16)
project(SanctumNavigation LANGUAGES CXX)

# Reusable navigation and overlay logic; this library does not need a window system.
add_library(sanctum_navigation
    src/NavigationController.cpp
    src/ViewOverlay.cpp
)
target_include_directories(sanctum_navigation PUBLIC include)
target_compile_features(sanctum_navigation PUBLIC cxx_std_17)

include(CTest)
if(BUILD_TESTING)
    # Headless behavior tests run without opening the interactive demo.
    add_executable(sanctum_navigation_tests tests/NavigationControllerTests.cpp)
    target_link_libraries(sanctum_navigation_tests PRIVATE sanctum_navigation)
    add_test(NAME sanctum_navigation_tests COMMAND sanctum_navigation_tests)
endif()

if(WIN32)
    # The optional Windows demo uses native GDI, so no extra graphics packages are needed.
    add_executable(sanctum_navigation_demo WIN32 src/NavigationDemoWin32.cpp)
    target_link_libraries(sanctum_navigation_demo PRIVATE sanctum_navigation gdi32 user32)
endif()
