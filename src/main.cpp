#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE // don't include OpenGL stuff
#include <GLFW/glfw3.h>
#include <stdio.h>
#include "helpers.hpp"

#define MY_VULKAN_VERSION VK_API_VERSION_1_1

using glm::vec2;
using glm::vec3;
using glm::vec4;

GLFWwindow* window;

struct BufferInfo {
	VkBuffer buffer;
	VmaAllocation alloc;
	VmaAllocationInfo allocInfo;
};

struct StagingProcess {
	BufferInfo bufferInfo;
	VkCommandBuffer cmdBuffer;
	VkFence fence; // this fence will be sigaled once the transfer has finished, so we can delete the buffer
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
	BufferInfo vertexBuffer;
	std::vector<StagingProcess> stagingProcs;

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

static void recordCmdBuffer(u32 cmdBufferInd, u32 screenW, u32 screenH)
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
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vkd.vertexBuffer.buffer, &offset);
		vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
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

	vk::create_swapChain(vkd.swapchain, vkd.physicalDevice, vkd.device, vkd.surface, 2, VK_PRESENT_MODE_FIFO_KHR);
	for (u32 i = 0; i < vkd.swapchain.numImages; i++)
		vkd.framebuffers[i] = VK_NULL_HANDLE;

	vkd.renderPass = vk::createSimpleRenderPass(vkd.device, vkd.swapchain.format.format);

	vk::ShaderStages shaderStages = {
		.vertex = {vk::loadShaderModule(vkd.device, "shaders/example_vert.spirv")},
		.fragment = {vk::loadShaderModule(vkd.device, "shaders/example_frag.spirv")},
	};

	struct Vert {
		vec2 pos;
		glm::u8vec4 color;
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
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.offset = offsetof(Vert, color),
		}
	};

	const VkPipelineColorBlendAttachmentState attachmentBlendInfos[] = { {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_RGBA_BITS,
	} };

	const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	vkd.pipelineLayout = vk::createPipelineLayout(vkd.device, {}, {});

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

	vkd.cmdBuffers.resize(vkd.swapchain.numImages);
	vk::allocateCmdBuffers(vkd.device, vkd.cmdPool, vkd.cmdBuffers);

	const Vert verts[] = {
		{{-0.8, +0.8}, {255,0,0, 255}},
		{{+0.8, +0.8}, {0,255,0, 255}},
		{{0.0, -0.8}, {0,0,255, 255}},
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

	bool pendingStaging = true;

	u32 frameId = 0;
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		int screenW, screenH;
		glfwGetFramebufferSize(window, &screenW, &screenH);


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

		recordCmdBuffer(swapchainImageInd, u32(screenW), u32(screenH));

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