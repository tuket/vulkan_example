Simple vulkan example that draws a textured quad.

![Imgur](https://i.imgur.com/lxKDqOw.png)

Code is contained in a few files:
- [src/main.cpp](https://github.com/tuket/vulkan_example/blob/b18cfca886e93a2acc41e3b8f75b1c33db1a7282/src/main.cpp): entry point and main loop
- [src/helpers.hpp](https://github.com/tuket/vulkan_example/blob/b18cfca886e93a2acc41e3b8f75b1c33db1a7282/src/helpers.hpp): simple helpers to reduce bit the code in main.cpp, so you can see the basic structure of a Vulkan program more clearly
- [shaders/example_vert.glsl](https://github.com/tuket/vulkan_example/blob/b18cfca886e93a2acc41e3b8f75b1c33db1a7282/shaders/example_vert.glsl), [shaders/example_frag.glsl](https://github.com/tuket/vulkan_example/blob/b18cfca886e93a2acc41e3b8f75b1c33db1a7282/shaders/example_frag.glsl)

Written in simple C++, without any sofisticated code style. However, C++20 is used for [designated initializers](https://www.cppstories.com/2021/designated-init-cpp20/), as it improves code readability.

Covers the most basic Vulkan concepts:
- Set up a window using GLFW
- Create vulkan instance
- Select the best GPU (VkPhysicalDevice)
- Create a logical device
- Use [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) to allocate memory
- Swapchain creation and synchronization
- Pipeline creation
- Staging buffers: creating, mapping host memory, flushing
- Vertex buffers: creating, initializing with data
- Images: creation, initializing with data, layout transition using barriers
- Descriptors, descriptor pool, descriptor sets, etc
- Compile shaders to SPIRV: uses CMake to automate the compilation of shaders
- Record cmdBuffers
- Framebuffer recreation when window is resized

Uses the following libraries:
- Vulkan (C bindings)
- [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [glm](https://github.com/g-truc/glm)
- [GLFW](https://www.glfw.org/)
- [stbi](https://github.com/nothings/stb)
