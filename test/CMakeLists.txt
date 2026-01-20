# WWVB Unit Tests CMakeLists.txt
#
# This is a test project that builds and runs unit tests for WWVB encoder
# and DST calculation modules using ESP-IDF Unity test framework.

cmake_minimum_required(VERSION 3.5)

# Test project uses its own components; production sources added explicitly in test/main/CMakeLists.txt

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(wwvb_test)
