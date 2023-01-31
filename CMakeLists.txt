cmake_minimum_required(VERSION 3.21) # TODO: change to 3.24 once it is released
project(vulkan_example)

set (CMAKE_CXX_STANDARD 20)

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)

add_subdirectory(libs/glm)
add_subdirectory(libs/glfw)
add_subdirectory(libs/vma)
add_subdirectory(libs/imgui)

file(GLOB_RECURSE glsl_files "${CMAKE_SOURCE_DIR}/shaders/*.glsl")
foreach(glsl_file ${glsl_files})
    string(REGEX REPLACE "[.]glsl$" ".spirv" spirv_file ${glsl_file})
    message(${spirv_file})
    add_custom_command(
        DEPENDS ${glsl_file}
        OUTPUT ${spirv_file}
        COMMAND ${GLSLC} ${glsl_file} -o ${spirv_file}
    )
    list(APPEND spirv_files ${spirv_file})
endforeach()
add_custom_target(shaders_target DEPENDS ${spirv_files})

set(SRCS
    src/main.cpp
    src/helpers.hpp
)
add_executable(vulkan_example ${SRCS})
#target_link_libraries(vulkan_example Vulkan::Vulkan Vulkan::shaderc_combined glm glfw)
target_link_libraries(vulkan_example Vulkan::Vulkan ${SHADERC_LIBRARIES} glm glfw vma imgui)
add_dependencies(vulkan_example shaders_target)
set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT vulkan_example)
set_target_properties(
    vulkan_example PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
source_group("" FILES ${SRCS})
