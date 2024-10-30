#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE // don't include OpenGL stuff
#include <GLFW/glfw3.h>
#include <stdio.h>
#include "helpers.hpp"
#include <stb_image.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define MY_VULKAN_VERSION VK_API_VERSION_1_1

static constexpr u32 MIN_SWAPCHAIN_IMAGES = 2;

using glm::vec2;
using glm::vec3;
using glm::vec4;

GLFWwindow* window;

struct Buffer {
	VkBuffer buffer;
	VmaAllocation alloc;
	VmaAllocationInfo allocInfo;
};

struct StagingProcess {
	Buffer bufferInfo;
	VkCommandBuffer cmdBuffer;
	VkFence fence; // this fence will be sigaled once the transfer has finished, so we can delete the buffer
};

struct Img {
	VkImage img;
	VkImageView view;
	VmaAllocation alloc;
	VmaAllocationInfo allocInfo;
};

struct {
	VkInstance instance;
	VkSurfaceKHR surface;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties physicalDeviceProps;
	VkPhysicalDeviceMemoryProperties physicalDeviceMemProps;
	u32 queueFamily;
	VkDevice device;
	VkQueue queue;
	VmaAllocator allocator;
	vk::Swapchain swapchain;
	VkRenderPass renderPass;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	VkCommandPool cmdPool;
	std::vector<VkCommandBuffer> cmdBuffers;
	VkFramebuffer framebuffers[vk::Swapchain::MAX_IMAGES];
	Buffer vertexBuffer;
	std::vector<StagingProcess> stagingProcs;
	Img tentImg;
	VkSampler bilinearSampler;
	VkDescriptorPool descPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descSet;
} vkd;

static void onWindowResized(GLFWwindow* window, int w, int h)
{
	vkDeviceWaitIdle(vkd.device);
	vk::create_swapChain(vkd.swapchain, vkd.physicalDevice, vkd.device, vkd.surface, 2, VK_PRESENT_MODE_FIFO_KHR);
	for (u32 i = 0; i < vkd.swapchain.numImages; i++) {
		vkDestroyFramebuffer(vkd.device, vkd.framebuffers[i], nullptr);
		vkd.framebuffers[i] = VK_NULL_HANDLE;
	}
}

static void recordDrawCmdBuffer(u32 cmdBufferInd, u32 screenW, u32 screenH)
{
	VkCommandBuffer& cmdBuffer = vkd.cmdBuffers[cmdBufferInd];
	const VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VkResult vkRes = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
	vk::assertRes(vkRes);

	const VkClearValue clearVal = { .color = {0.5f, 0.5f, 0.5f, 1.f} };
	const VkRenderPassBeginInfo rpBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = vkd.renderPass,
		.framebuffer = vkd.framebuffers[cmdBufferInd],
		.renderArea = {{0, 0}, {screenW, screenH}},
		.clearValueCount = 1,
		.pClearValues = &clearVal,
	};

	vkCmdBeginRenderPass(cmdBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	{
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkd.pipeline);

		const VkViewport viewport = {
			.x = 0, .y = 0,
			.width = float(screenW), .height = float(screenH),
			.minDepth = 0, .maxDepth = 1,
		};
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

		const VkRect2D scissor = { {0, 0}, {screenW, screenH} };
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);


		size_t offset = 0;
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkd.pipelineLayout,
			0, 1, // firstSet, setCount
			&vkd.descSet,
			0, nullptr // dynamic offset
		);
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vkd.vertexBuffer.buffer, &offset);
		vkCmdDraw(cmdBuffer, 6, 1, 0, 0);
	}
	{
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuffer);
	}
	vkCmdEndRenderPass(cmdBuffer);

	vkEndCommandBuffer(cmdBuffer);
}

int main()
{
	int ok = glfwInit();
	assert(ok);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	window = glfwCreateWindow(800, 600, "example", nullptr, nullptr);

	glfwSetFramebufferSizeCallback(window, onWindowResized);

	u32 numRequiredExtensions;
	const CStr* requiredExtensions = glfwGetRequiredInstanceExtensions(&numRequiredExtensions);

	vkd.instance = vk::createInstance(MY_VULKAN_VERSION, {}, { requiredExtensions, numRequiredExtensions }, "example");
	
	VkResult vkRes = glfwCreateWindowSurface(vkd.instance, window, nullptr, &vkd.surface);
	vk::assertRes(vkRes);

	vk::findBestPhysicalDevice(vkd.instance, vkd.physicalDevice, vkd.physicalDeviceProps, vkd.physicalDeviceMemProps);

	vkd.queueFamily = vk::findGraphicsQueueFamily(vkd.physicalDevice, vkd.surface);
	const float queuePriorities[] = { 0.f };
	const vk::CreateQueues createQueues[] = { {vkd.queueFamily, queuePriorities} };
	ConstStr deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	vkd.device = vk::createDevice(vkd.physicalDevice, createQueues, deviceExtensions);
	vkGetDeviceQueue(vkd.device, vkd.queueFamily, 0, &vkd.queue);

	const VmaAllocatorCreateInfo allocatorInfo = {
		.flags = 0,
		.physicalDevice = vkd.physicalDevice,
		.device = vkd.device,
		.instance = vkd.instance,
		.vulkanApiVersion = MY_VULKAN_VERSION,
	};
	vkRes = vmaCreateAllocator(&allocatorInfo, &vkd.allocator);
	vk::assertRes(vkRes);

	vk::create_swapChain(vkd.swapchain, vkd.physicalDevice, vkd.device, vkd.surface, MIN_SWAPCHAIN_IMAGES, VK_PRESENT_MODE_FIFO_KHR);
	for (u32 i = 0; i < vkd.swapchain.numImages; i++)
		vkd.framebuffers[i] = VK_NULL_HANDLE;

	vkd.renderPass = vk::createSimpleRenderPass(vkd.device, vkd.swapchain.format.format);

	vk::ShaderStages shaderStages = {
		.vertex = {vk::loadShaderModule(vkd.device, "shaders/example_vert.spirv")},
		.fragment = {vk::loadShaderModule(vkd.device, "shaders/example_frag.spirv")},
	};

	struct Vert {
		vec2 pos;
		vec2 tc;
	};

	const VkVertexInputBindingDescription vertexInputBinding = {
		.binding = 0,
		.stride = sizeof(Vert),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkVertexInputAttributeDescription vertexInputAttribs[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vert, pos),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vert, tc),
		}
	};

	const VkPipelineColorBlendAttachmentState attachmentBlendInfos[] = { {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_RGBA_BITS,
	} };

	const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	const VkDescriptorSetLayoutBinding descSetBinding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.pImmutableSamplers = nullptr,
	};
	const VkDescriptorSetLayoutCreateInfo descSetInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &descSetBinding,
	};
	vkRes = vkCreateDescriptorSetLayout(vkd.device, &descSetInfo, nullptr, &vkd.descriptorSetLayout);
	vk::assertRes(vkRes);

	vkd.pipelineLayout = vk::createPipelineLayout(vkd.device, {&vkd.descriptorSetLayout, 1}, {});

	vkd.pipeline = vk::createGraphicsPipeline(vkd.device, {
		.shaderStages = shaderStages,
		.vertexInputBindings = {&vertexInputBinding, 1},
		.vertexInputAttribs = vertexInputAttribs,
		.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
		//.viewport = {}, // viewport: will be set with dynamic state
		//.scissor = {}, // scissor: will be set with dynamic state
		//.polygonMode = VK_POLYGON_MODE_LINE, // doesn't relly matter as we are not rendering polygons
		//.cullMode = VK_CULL_MODE_BACK_BIT, // doesn't relly matter as we are not rendering polygons
		.faceClockwise = false,
		.attachmentsBlendInfos = attachmentBlendInfos,
		.dynamicStates = dynamicStates,
		.pipelineLayout = vkd.pipelineLayout,
		.renderPass = vkd.renderPass,
		.subpass = 0,
	});

	vkd.cmdPool = vk::createCmdPool(vkd.device, vkd.queueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	const VkDescriptorPoolSize descPoolSizes[] = {
	{
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 64,
	}
	};
	vkd.descPool = vk::createDescriptorPool(vkd.device, 64, descPoolSizes);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplGlfw_InitForVulkan(window, true);

	ImGui_ImplVulkan_InitInfo imguiVkInitInfo = {
		.Instance = vkd.instance,
		.PhysicalDevice = vkd.physicalDevice,
		.Device = vkd.device,
		.QueueFamily = vkd.queueFamily,
		.Queue = vkd.queue,
		//.PipelineCache = ,
		.DescriptorPool = vkd.descPool,
		.RenderPass = vkd.renderPass,
		.MinImageCount = MIN_SWAPCHAIN_IMAGES,
		.ImageCount = vkd.swapchain.numImages,
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		.Subpass = 0,
		//.Allocator =,
		.CheckVkResultFn = [](VkResult vkRes) {
			vk::assertRes(vkRes);
		},
	};
	ImGui_ImplVulkan_Init(&imguiVkInitInfo);

	float dpiScaleX, dpiScaleY;
	glfwGetWindowContentScale(window, &dpiScaleX, &dpiScaleY);

	auto& imguiIO = ImGui::GetIO();
	imguiIO.Fonts->AddFontFromFileTTF("data/Roboto-Medium.ttf", 14 * dpiScaleX);

	vkd.cmdBuffers.resize(vkd.swapchain.numImages);
	vk::allocateCmdBuffers(vkd.device, vkd.cmdPool, vkd.cmdBuffers);

	const Vert verts[] = {
		{{-0.8, -0.8}, {0, 0}},
		{{-0.8, +0.8}, {0, 1}},
		{{+0.8, -0.8}, {1, 0}},
		{{+0.8, +0.8}, {1, 1}},
	};
	vk::createStaticVertexBuffer(vkd.device, vkd.allocator, sizeof(verts),
		vkd.vertexBuffer.buffer, vkd.vertexBuffer.alloc, &vkd.vertexBuffer.allocInfo);

	if (vkd.vertexBuffer.allocInfo.pMappedData) {
		memcpy(vkd.vertexBuffer.allocInfo.pMappedData, verts, sizeof(verts));
		vmaFlushAllocation(vkd.allocator, vkd.vertexBuffer.alloc, 0, VK_WHOLE_SIZE);
	}
	else { // the vertex buffer can't be directly mapped, we will have to create a staging buffer
		StagingProcess& stagingProc = vkd.stagingProcs.emplace_back();
		vk::createStagingBuffer(vkd.device, vkd.allocator, sizeof(verts),
			stagingProc.bufferInfo.buffer, stagingProc.bufferInfo.alloc, &stagingProc.bufferInfo.allocInfo);

		memcpy(stagingProc.bufferInfo.allocInfo.pMappedData, verts, sizeof(verts));
		vmaFlushAllocation(vkd.allocator, stagingProc.bufferInfo.alloc, 0, VK_WHOLE_SIZE);

		vk::allocateCmdBuffers(vkd.device, vkd.cmdPool, { &stagingProc.cmdBuffer, 1 });
		vk::createFences(vkd.device, false, { &stagingProc.fence, 1 });

		vk::beginCmdBuffer(stagingProc.cmdBuffer);
		{
			const VkBufferCopy copyRegion = { .size = sizeof(verts)};
			vkCmdCopyBuffer(stagingProc.cmdBuffer, stagingProc.bufferInfo.buffer, vkd.vertexBuffer.buffer, 1, &copyRegion);

			const VkMemoryBarrier memBarrier = {
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
			};
			vkCmdPipelineBarrier(stagingProc.cmdBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
				1, &memBarrier,
				0, nullptr,
				0, nullptr);
		}
		vkEndCommandBuffer(stagingProc.cmdBuffer);

		const VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &stagingProc.cmdBuffer,
		};
		vkRes = vkQueueSubmit(vkd.queue, 1, &submitInfo, stagingProc.fence);
		vk::assertRes(vkRes);
	}

	// load image
	{
		int w, h, nc;
		u8* data = stbi_load("data/tent.jpg", &w, &h, &nc, 4);
		vk::Img imgInfo = {
			.width = u32(w),
			.height = u32(h),
			.mipLevels = 1,
		};
		VmaAllocationInfo allocInfo;
		vk::createStaticImage(vkd.device, vkd.allocator, imgInfo, vkd.tentImg.img, vkd.tentImg.alloc, &vkd.tentImg.allocInfo, &vkd.tentImg.view);

		const size_t memSize = vkd.tentImg.allocInfo.size;
		StagingProcess& stagingProc = vkd.stagingProcs.emplace_back();
		vk::createStagingBuffer(vkd.device, vkd.allocator, memSize,
			stagingProc.bufferInfo.buffer, stagingProc.bufferInfo.alloc, &stagingProc.bufferInfo.allocInfo);
		memcpy(stagingProc.bufferInfo.allocInfo.pMappedData, data, memSize);
		vmaFlushAllocation(vkd.allocator, stagingProc.bufferInfo.alloc, 0, VK_WHOLE_SIZE);

		vk::allocateCmdBuffers(vkd.device, vkd.cmdPool, { &stagingProc.cmdBuffer, 1 });
		vk::createFences(vkd.device, false, { &stagingProc.fence, 1 });

		vk::beginCmdBuffer(stagingProc.cmdBuffer);
		{
			const VkImageSubresourceRange imgSubresRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			};

			const VkImageMemoryBarrier imgBarrier_transferDst = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_NONE,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.image = vkd.tentImg.img,
				.subresourceRange = imgSubresRange,
			};
			vkCmdPipelineBarrier(stagingProc.cmdBuffer,
				VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, // VkDependencyFlags
				0, nullptr, // memory barriers
				0, nullptr, // buffer barriers
				1, &imgBarrier_transferDst // img barriers
			);

			const VkBufferImageCopy copyRegion = {
				.bufferOffset = 0,
				.bufferRowLength = u32(w),
				.bufferImageHeight = u32(h),
				.imageSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.imageOffset = {},
				.imageExtent = {u32(w), u32(h), 1},
			};
			vkCmdCopyBufferToImage(stagingProc.cmdBuffer, stagingProc.bufferInfo.buffer, vkd.tentImg.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

			const VkImageMemoryBarrier imgBarrier_shaderOptimal = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image = vkd.tentImg.img,
				.subresourceRange = imgSubresRange,
			};

			vkCmdPipelineBarrier(
				stagingProc.cmdBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, // VkDependencyFlags
				0, nullptr, // memory barriers
				0, nullptr, // buffer barriers
				1, &imgBarrier_shaderOptimal // img barriers
			);
		}
		vkEndCommandBuffer(stagingProc.cmdBuffer);

		const VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &stagingProc.cmdBuffer,
		};
		vkRes = vkQueueSubmit(vkd.queue, 1, &submitInfo, stagingProc.fence);
		vk::assertRes(vkRes);
	}

	const VkSamplerCreateInfo samplerInfo = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		//.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		//.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
	};
	vkRes = vkCreateSampler(vkd.device, &samplerInfo, nullptr, &vkd.bilinearSampler);
	vk::assertRes(vkRes);

	vk::allocDescSets(vkd.device, vkd.descPool, { &vkd.descriptorSetLayout, 1 }, {&vkd.descSet, 1});

	vk::writeTextureDescriptor(vkd.device, vkd.descSet, 0, vkd.tentImg.view, vkd.bilinearSampler);

	u32 frameId = 0;
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		int screenW, screenH;
		glfwGetFramebufferSize(window, &screenW, &screenH);

		// Start the Dear ImGui frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::ShowDemoWindow();

		ImGui::Render();

		u32 swapchainImageInd;
		vkRes = vkAcquireNextImageKHR(vkd.device, vkd.swapchain.swapchain, -1,
			vkd.swapchain.semaphore_swapchainImgAvailable[frameId], VK_NULL_HANDLE, &swapchainImageInd);
		vk::assertRes(vkRes);

		if (vkd.framebuffers[swapchainImageInd] == VK_NULL_HANDLE) {
			const VkImageView attachments[] = { {vkd.swapchain.imageViews[swapchainImageInd]} };
			vkd.framebuffers[swapchainImageInd] = vk::createFramebuffer(vkd.device, vkd.renderPass, attachments, u32(screenW), u32(screenH));
		}

		vkRes = vkWaitForFences(vkd.device, 1, &vkd.swapchain.fence_queueWorkFinished[swapchainImageInd], VK_FALSE, -1);
		vk::assertRes(vkRes);
		vkRes = vkResetFences(vkd.device, 1, &vkd.swapchain.fence_queueWorkFinished[swapchainImageInd]);
		vk::assertRes(vkRes);

		recordDrawCmdBuffer(swapchainImageInd, u32(screenW), u32(screenH));

		const VkPipelineStageFlags semaphoreWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		const VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = vkd.swapchain.semaphore_swapchainImgAvailable + frameId,
			.pWaitDstStageMask = &semaphoreWaitStage,
			.commandBufferCount = 1,
			.pCommandBuffers = &vkd.cmdBuffers[swapchainImageInd],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = vkd.swapchain.semaphore_drawFinished + swapchainImageInd,
		};
		vkRes = vkQueueSubmit(vkd.queue, 1, &submitInfo, vkd.swapchain.fence_queueWorkFinished[swapchainImageInd]);
		vk::assertRes(vkRes);

		const VkPresentInfoKHR presentInfo = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = vkd.swapchain.semaphore_drawFinished + swapchainImageInd,
			.swapchainCount = 1,
			.pSwapchains = &vkd.swapchain.swapchain,
			.pImageIndices = &swapchainImageInd,
			.pResults = nullptr,
		};
		vkRes = vkQueuePresentKHR(vkd.queue, &presentInfo);
		vk::assertRes(vkRes);

		for (size_t i = 0; i < vkd.stagingProcs.size(); ) {
			auto& proc = vkd.stagingProcs[i];
			if (vk::fenceIsSignaled(vkd.device, proc.fence)) {
				vkDestroyFence(vkd.device, proc.fence, nullptr);
				vkFreeCommandBuffers(vkd.device, vkd.cmdPool, 1, &proc.cmdBuffer);
				vmaDestroyBuffer(vkd.allocator, proc.bufferInfo.buffer, proc.bufferInfo.alloc);
				if (vkd.stagingProcs.size() >= 2)
					proc = vkd.stagingProcs.back();
				vkd.stagingProcs.pop_back();
			}
			else
				i++;
		}

		frameId = (frameId + 1) % vkd.swapchain.numImages;
	}

	glfwDestroyWindow(window);
}