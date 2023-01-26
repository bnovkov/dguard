cmake_minimum_required(VERSION 3.4.3)

project(pass
  VERSION 1.0.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 14)

if (NOT PATH_TO_LLVM)
  message(FATAL_ERROR " 
  The cmake is supposed to be called with PATH_TO_LLVM pointing to
 a precompiled version of LLVM or to to the source code of LLVM
 Examples:
 cmake -G \"${CMAKE_GENERATOR}\" -DPATH_TO_LLVM=/opt/llvm-9.0.1 ${CMAKE_SOURCE_DIR}
 cmake -G \"${CMAKE_GENERATOR}\" -DPATH_TO_LLVM=/llvm-project/llvm ${CMAKE_SOURCE_DIR}
")
endif()

if (NOT IS_ABSOLUTE ${PATH_TO_LLVM})
  # Convert relative path to absolute path
  get_filename_component(PATH_TO_LLVM
    "${PATH_TO_LLVM}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()

set (BUILD_AGAINST_PRECOMPILED_LLVM TRUE)
if (EXISTS ${PATH_TO_LLVM}/CMakeLists.txt)
  set (BUILD_AGAINST_PRECOMPILED_LLVM FALSE)
endif()

string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Werror -Wall -Wextra -Wpedantic -fno-rtti")


if (${BUILD_AGAINST_PRECOMPILED_LLVM})
  set (search_paths
    ${PATH_TO_LLVM}
    ${PATH_TO_LLVM}/lib/cmake
    ${PATH_TO_LLVM}/lib/cmake/llvm
    ${PATH_TO_LLVM}/share/llvm/cmake/
  )

  find_package(LLVM REQUIRED CONFIG PATHS ${search_paths} NO_DEFAULT_PATH)

  list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

else()
  
  set (LLVM_TARGETS_TO_BUILD "host" CACHE STRING "Only build targets for host architecture" FORCE)

  add_subdirectory(${PATH_TO_LLVM} llvm-build)

  # Normally, include paths provided by LLVMConfig.cmake
  # In this case we can 'steal' them from real targets
  get_target_property(LLVM_INCLUDE_DIRS LLVMSupport INCLUDE_DIRECTORIES)

  # Manually add the llvm CMake module path
  list(APPEND CMAKE_MODULE_PATH
    "${PATH_TO_LLVM}/cmake/modules"
  )

  set(LLVM_MAIN_SRC_DIR ${PATH_TO_LLVM})

endif()

# CMake Modules used for llvm specific macros 
include(LLVM-Config)
include(HandleLLVMOptions)
include(AddLLVM)

include_directories(${LLVM_INCLUDE_DIRS})

add_library(DGuard MODULE
  ./src/pass.cpp
  ./src/plugins/func_list.cpp
)

target_include_directories(
  DGuard
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/./include"
)

add_library(dguard STATIC
#  ./src/lib/metadata_init.c
  ./src/lib/dguard_funcs.c
)

#target_include_directories(
#  dguard
#  PRIVATE
#  "${CMAKE_CURRENT_SOURCE_DIR}/./include"
#)

#target_link_libraries(dguard -ldl)
