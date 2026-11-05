cmake_minimum_required(VERSION 3.5...4.4.2)

project(sponza-simulation LANGUAGES CXX)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Enabling ASan and UBSan")

    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)

    add_link_options(-fsanitize=address,undefined)
endif()

if(MSVC)
    add_compile_options(/EHs-c- /GR-)
    add_compile_definitions(_HAS_EXCEPTIONS=0)
else()
    add_compile_options(-fno-exceptions -fno-rtti)
endif()

find_package(Git REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(stb REQUIRED)
find_package(glm REQUIRED)
find_package(tinyobjloader REQUIRED)
find_package(vk-bootstrap REQUIRED)

find_package(caldera REQUIRED)

# # # # #

add_executable(sponza-simulation
        src/main.cpp
        src/vma_impl.cpp
        src/utility.cpp
        src/pipeline_builder.cpp
        src/descriptor_builder.cpp
        src/context.cpp
        src/renderer.cpp
        src/resources.cpp
        src/pipeline.cpp)

target_compile_definitions(sponza-simulation PUBLIC VULKAN_HPP_NO_EXCEPTIONS)
target_link_libraries(sponza-simulation PUBLIC
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        stb::stb
        tinyobjloader::tinyobjloader
        vk-bootstrap::vk-bootstrap
        glm::glm-header-only
        caldera::caldera)

target_include_directories(sponza-simulation PUBLIC include)

set_target_properties(sponza-simulation PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        COMPILE_WARNING_AS_ERROR ON)

# # # # #

find_package(Python3 REQUIRED COMPONENTS Interpreter)

execute_process(
    COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive external/shader-link
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND_ERROR_IS_FATAL ANY)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/compiled-shaders/${TARGET_NAME}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/sponza/shaders/compiled")

add_custom_target(sponza-shader-compilation
        COMMAND Python3::Interpreter -m shader_link
        --input  "${CMAKE_CURRENT_LIST_DIR}/include/sponza/shaders/src"
        --output "${CMAKE_BINARY_DIR}/compiled-shaders/${TARGET_NAME}"
        --export "${CMAKE_CURRENT_LIST_DIR}/include/sponza/shaders/compiled"
        --args   "-O --target-env=vulkan1.4"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/external/shader-link"
        VERBATIM)

add_dependencies(sponza-simulation sponza-shader-compilation)
