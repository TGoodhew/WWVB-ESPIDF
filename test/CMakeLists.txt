# WWVB Unit Tests CMakeLists.txt
#
# This is a test project that builds and runs unit tests for WWVB encoder
# and DST calculation modules using ESP-IDF Unity test framework.

cmake_minimum_required(VERSION 3.5)

# Add the main project source directory as a component
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../main")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(wwvb_test)
