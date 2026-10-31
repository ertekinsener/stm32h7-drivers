# Path resolution for stm32h7-drivers and its dependencies, in the style of
# stm32-cmake's STM32_CUBE_<FAMILY>_PATH: every location can be given as a
# CMake variable or an environment variable, and nothing is an absolute path
# inside this repository.
include_guard(GLOBAL)

# stm32h7_drivers_resolve_path(<var> <default> <description>)
#
# Sets the cache path <var> (and the variable in the caller's scope) from, in
# order: the CMake variable <var> (-D<var>=... or set() before), the
# environment variable <var>, <default>. Relative paths are taken relative to
# the current source directory. An empty <default> leaves <var> empty when
# neither is set.
function(stm32h7_drivers_resolve_path var default description)
  if(NOT "${${var}}" STREQUAL "")
    set(value "${${var}}")
  elseif(NOT "$ENV{${var}}" STREQUAL "")
    set(value "$ENV{${var}}")
  else()
    set(value "${default}")
  endif()
  if(NOT value STREQUAL "")
    cmake_path(ABSOLUTE_PATH value BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
               NORMALIZE)
    string(REGEX REPLACE "/$" "" value "${value}")
  endif()
  set(${var} "${value}" CACHE PATH "${description}" FORCE)
  set(${var} "${value}" PARENT_SCOPE)
endfunction()

# stm32h7_drivers_require_project(<var> <name> <hint>)
#
# Fails with <hint> unless the path in <var> is a CMake project.
function(stm32h7_drivers_require_project var name hint)
  if(NOT EXISTS "${${var}}/CMakeLists.txt")
    message(FATAL_ERROR "stm32h7-drivers: ${name} not found at ${var}="
                        "'${${var}}'. ${hint}")
  endif()
endfunction()
