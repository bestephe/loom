cmake_minimum_required(VERSION 2.8)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project (libjsort)


find_package(JNI)
include_directories(. ${JNI_INCLUDE_DIRS})
set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Werror -mtune=native" )

FIND_PACKAGE(Boost 1.59 REQUIRED)
IF(NOT Boost_FOUND)
    message(FATAL_ERROR "-------S-O-L-U-T-I-O-N------------\nCopy include/boost/sort directory to ${CMAKE_SOURCE_DIR}/boost/sort for spreadsort from https://github.com/boostorg/sort.\n--------------------------------")
ENDIF(NOT Boost_FOUND)


add_library(jsort SHARED com_ibm_radixsort_NativeRadixSort.cpp)
#set_target_properties(jsort PROPERTIES SOVERSION "0.1")
#install(TARGETS jsort LIBRARY DESTINATION lib)

