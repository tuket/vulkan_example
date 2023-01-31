#pragma once

#include <vulkan/vulkan.h>
#include <vma.h>
#include <span>
#include <vector>
#include <stdio.h>
#include <assert.h>
#include <glm/glm.hpp>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef const char* CStr;
typedef const char* const ConstStr;

namespace
{

const auto VK_COLOR_COMPONENT_RGBA_BITS =
	VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

bool loadBinaryFile(CStr fileName, std::vector<u8>& buffer)
{
	FILE* file = fopen(fileName, "rb");
	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	const auto len = ftell(file);
	fseek(file, 0, SEEK_SET);
	buffer.resize(len);
	fread(buffer.data(), 1, len, file);
	fread(buffer.data(), 1, len, file);
	fclose(file);
	return true;
}

namespace vk { 

void assertRes(VkResult r)
{
	assert(r == VK_SUCCESS);
}

void createFences(VkDevice device, bool signaled, std::span<VkFence> fences)
{
	const VkFenceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VkFenceCreateFlags(signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0),
	};
	for (size_t i = 0; i < fences.size(); i++) {
		VkResult vkRes = vkCreateFence(device, &info, nullptr, &fences[i]);
		assertRes(vkRes);
	}
}

[[nodiscard]]
bool fenceIsSignaled(VkDevice device, VkFence fence)
{
	VkResult vkRes = vkWaitForFences(device, 1, &fence, VK_FALSE, 0);
	assertRes(vkRes);
	return vkRes == VK_SUCCESS;
}

[[nodiscard]]
VkInstance createInstance(u32 apiVersion, std::span<ConstStr> layerNames, std::span<ConstStr> extensionNames, CStr appName)
{
	const VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = appName,
		.applicationVersion = 0,
		.pEngineName = "",
		.engineVersion = 0,
		.apiVersion = apiVersion
	};

	VkInstanceCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = u32(layerNames.size()),
		.ppEnabledLayerNames = layerNames.data(),
		.enabledExtensionCount = u32(extensionNames.size()),
		.ppEnabledExtensionNames = &extensionNames[0],
	};
	VkInstance instance;
	VkResult vkRes = vkCreateInstance(&info, nullptr, &instance);
	assertRes(vkRes);
	return instance;
}

void findBestPhysicalDevice(VkInstance instance, VkPhysicalDevice& bestDevice,
	VkPhysicalDeviceProperties& bestDeviceProps, VkPhysicalDeviceMemoryProperties& bestDeviceMemProps)
{
	VkPhysicalDevice devices[16];
	u32 numDevices = std::size(devices);
	VkResult vkRes = vkEnumeratePhysicalDevices(instance, &numDevices, devices);
	assert(vkRes == VK_SUCCESS && numDevices > 0);

	auto calcDeviceLocalMemoryKB = [](const VkPhysicalDeviceMemoryProperties& memProps) -> size_t
	{
		size_t sum = 0;
		for (size_t i = 0; i < memProps.memoryHeapCount; i++) {
			auto& heap = memProps.memoryHeaps[i];
			if (heap.flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
				sum += heap.size;
		}
		sum >>= 10; // B -> KB
		return sum;
	};

	auto calcScore = [&](VkPhysicalDevice device, VkPhysicalDeviceProperties& props, VkPhysicalDeviceMemoryProperties& memProps) -> u64
	{
		vkGetPhysicalDeviceProperties(device, &props);
		vkGetPhysicalDeviceMemoryProperties(device, &memProps);

		size_t score = 0;
		switch (props.deviceType) {
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 5; break;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 4; break;
			case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 3; break;
			case VK_PHYSICAL_DEVICE_TYPE_CPU: score = 2; break;
			case VK_PHYSICAL_DEVICE_TYPE_OTHER: score = 1; break;
			default: score = 0;
		}
		score <<= 32;

		const size_t KB = calcDeviceLocalMemoryKB(memProps);
		score |= KB;

		return score;
	};

	u64 bestDeviceScore = 0;
	u32 bestDeviceInd = 0;

	for (u32 i = 0; i < numDevices; i++) {
		auto& device = devices[i];
		VkPhysicalDeviceProperties props;
		VkPhysicalDeviceMemoryProperties memProps;
		const u64 score = calcScore(device, props, memProps);
		if (score > bestDeviceScore) {
			bestDeviceInd = i;
			bestDevice = device;
			bestDeviceScore = score;
			bestDeviceProps = props;
			bestDeviceMemProps = memProps;
		}
	}
}

[[nodiscard]]
u32 findGraphicsQueueFamily(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
	VkQueueFamilyProperties props[32];
	u32 numQueueFamilies = std::size(props);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &numQueueFamilies, props);

	for (u32 i = 0; i < numQueueFamilies; i++) {
		const bool supportsGraphics = props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
		VkBool32 supportsSurface;
		VkResult vkRes = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsSurface);
		assertRes(vkRes);
		if (supportsGraphics && supportsSurface)
			return i;
	}
	assert(false);
	return 0;
}

struct CreateQueues {
	u32 familyIndex;
	std::span<const float> priorities;
};
[[nodiscard]]
VkDevice createDevice(VkPhysicalDevice physicalDevice, std::span<const CreateQueues> createQueues, std::span<ConstStr> extensionNames)
{
	const float queuePriority = 0;
	VkDeviceQueueCreateInfo queueCreateInfos[16];
	assert(createQueues.size() <= std::size(queueCreateInfos));
	for (size_t i = 0; i < createQueues.size(); i++) {
		const auto& cq = createQueues[i];
		queueCreateInfos[i] = VkDeviceQueueCreateInfo {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = cq.familyIndex,
			.queueCount = u32(cq.priorities.size()),
			.pQueuePriorities = &cq.priorities[0],
		};
	}
	
	VkDeviceCreateInfo deviceCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = u32(createQueues.size()),
		.pQueueCreateInfos = queueCreateInfos,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = nullptr,
		.enabledExtensionCount = u32(extensionNames.size()),
		.ppEnabledExtensionNames = &extensionNames[0],
	};
	VkDevice device;
	VkResult vkRes = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
	assertRes(vkRes);
	return device;
}

struct Swapchain {
	static constexpr u32 MAX_IMAGES = 16;
	u32 numImages = 0;
	u32 w, h;
	VkSwapchainKHR swapchain = nullptr;
	VkSurfaceFormatKHR format;
	VkImageView imageViews[MAX_IMAGES];
	VkSemaphore semaphore_swapchainImgAvailable[MAX_IMAGES];
	VkSemaphore semaphore_drawFinished[MAX_IMAGES];
	VkFence fence_queueWorkFinished[MAX_IMAGES];
};

void create_swapChain(Swapchain& o, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, u32 minImages, VkPresentModeKHR presentMode)
{
	VkSurfaceCapabilitiesKHR surfaceCaps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);
	o.w = surfaceCaps.currentExtent.width;
	o.h = surfaceCaps.currentExtent.height;
	const VkSwapchainKHR oldSwapchain = o.swapchain;

	VkSurfaceFormatKHR supportedFormats[64];
	u32 numSupportedFormats = std::size(supportedFormats);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &numSupportedFormats, supportedFormats);
	u32 formatInd = 0;
	for (u32 i = 1; i < numSupportedFormats; i++) {
		const auto& format = supportedFormats[i];
		if (format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
			if (format.format == VK_FORMAT_R8G8B8A8_SRGB || format.format == VK_FORMAT_B8G8R8A8_SRGB) {
				formatInd = i;
				if (format.format == VK_FORMAT_B8G8R8A8_SRGB)
					break;
			}
		}
	}
	o.format = supportedFormats[formatInd];

	const VkSwapchainCreateInfoKHR swapchainInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = minImages,
		.imageFormat = o.format.format,
		.imageColorSpace = o.format.colorSpace,
		.imageExtent = {o.w, o.h},
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, // might be used for transparent window compositing
		.presentMode = presentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = oldSwapchain,
	};
	VkResult vkRes = vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &o.swapchain);
	assertRes(vkRes);

	// destroy old swapchain stuff
	if (oldSwapchain != VK_NULL_HANDLE) {
		//vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
		for (u32 i = 0; i < o.numImages; i++) {
			vkDestroySemaphore(device, o.semaphore_swapchainImgAvailable[i], nullptr);
			vkDestroySemaphore(device, o.semaphore_drawFinished[i], nullptr);
			vkDestroyFence(device, o.fence_queueWorkFinished[i], nullptr);

			vkDestroyImageView(device, o.imageViews[i], nullptr);
		}
	}

	// create image views
	VkImage images[Swapchain::MAX_IMAGES];
#ifndef NDEBUG
	vkRes = vkGetSwapchainImagesKHR(device, o.swapchain, &o.numImages, nullptr);
	assert(vkRes == VK_SUCCESS && o.numImages <= Swapchain::MAX_IMAGES);
#endif
	o.numImages = Swapchain::MAX_IMAGES;
	vkRes = vkGetSwapchainImagesKHR(device, o.swapchain, &o.numImages, images);
	assertRes(vkRes);
	for (u32 i = 0; i < o.numImages; i++) {
		const VkImageViewCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = o.format.format,
			.components = VK_COMPONENT_SWIZZLE_IDENTITY,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};
		vkRes = vkCreateImageView(device, &info, nullptr, o.imageViews + i);
		assertRes(vkRes);
	}

	// create semaphores
	const VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, };
	for (u32 i = 0; i < o.numImages; i++) {
		vkRes = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &o.semaphore_swapchainImgAvailable[i]);
		assertRes(vkRes);
		vkRes = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &o.semaphore_drawFinished[i]);
		assertRes(vkRes);
	}

	// create fences
	createFences(device, true, { o.fence_queueWorkFinished, o.numImages });
}

[[nodiscard]]
VkRenderPass createSimpleRenderPass(VkDevice device, VkFormat colorAttachmentFormat)
{
	const VkAttachmentDescription attachment = {
		.flags = 0,
		.format = colorAttachmentFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		//.stencilLoadOp = ,
		//.stencilStoreOp = ,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};
	const VkAttachmentReference attachmentRef = {
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	const VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachments = &attachmentRef,

	};
	const VkRenderPassCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 0,
		.pDependencies = nullptr,
	};

	VkRenderPass rp;
	VkResult vkRes = vkCreateRenderPass(device, &info, nullptr, &rp);
	assertRes(vkRes);
	return rp;
}

struct ShaderStageInfo {
	VkShaderModule module = VK_NULL_HANDLE;
	const VkSpecializationInfo* specialization = nullptr;
};
struct ShaderStages {
	ShaderStageInfo vertex = {};
	ShaderStageInfo fragment = {};
};

VkShaderModule createShaderModule(VkDevice device, std::span<u8> spirv)
{
	assert((spirv.size() & 3) == 0);
	const VkShaderModuleCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = spirv.size(),
		.pCode = (const u32*)spirv.data(),
	};
	VkShaderModule module;
	VkResult vkRes = vkCreateShaderModule(device, &info, nullptr, &module);
	assertRes(vkRes);
	return module;
}

VkShaderModule loadShaderModule(VkDevice device, CStr fileName)
{
	std::vector<u8> spirv;
	const bool ok = loadBinaryFile(fileName, spirv);
	assert(ok);
	return createShaderModule(device, spirv);
}

[[nodiscard]]
VkPipelineLayout createPipelineLayout(VkDevice device,
	std::span<const VkDescriptorSetLayout> setLayouts,
	std::span<const VkPushConstantRange> pushConstantRanges)
{
	const VkPipelineLayoutCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = u32(setLayouts.size()),
		.pSetLayouts = setLayouts.data(),
		.pushConstantRangeCount = u32(pushConstantRanges.size()),
		.pPushConstantRanges = pushConstantRanges.data(),
	};
	VkPipelineLayout layout;
	VkResult vkRes = vkCreatePipelineLayout(device, &info, nullptr, &layout);
	assertRes(vkRes);
	return  layout;
}

struct CreateGraphicsPipeline {
	ShaderStages shaderStages;
	std::span<const VkVertexInputBindingDescription> vertexInputBindings;
	std::span<const VkVertexInputAttributeDescription> vertexInputAttribs;
	VkPrimitiveTopology primitiveTopology;
	VkViewport viewport = { 0,0, 1,1, 1,1 }; // just some feasible values so validation layers don't complain
	VkRect2D scissor = { {0, 0}, {1, 1} };
	VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
	VkCullModeFlagBits cullMode = VK_CULL_MODE_BACK_BIT;
	bool faceClockwise = false;
	std::span<const VkPipelineColorBlendAttachmentState> attachmentsBlendInfos;
	glm::vec4 blendConstants;
	std::span<const VkDynamicState> dynamicStates;
	VkPipelineLayout pipelineLayout;
	VkRenderPass renderPass; // pipeline will be compatible with similar renderPassses
	u32 subpass; // subpass index in the previous renderPass
};

[[nodiscard]]
VkPipeline createGraphicsPipeline(VkDevice device, const CreateGraphicsPipeline& params)
{
	VkPipelineShaderStageCreateInfo shaderStageCreateInfos[sizeof(ShaderStages) / sizeof(ShaderStageInfo)];
	u32 numStages = 0;
	auto appendShaderStageCreateInfo = [&](const ShaderStageInfo& stageInfo, VkShaderStageFlagBits stage)
	{
		if (stageInfo.module == VK_NULL_HANDLE)
			return;

		shaderStageCreateInfos[numStages] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.flags = 0,
			.stage = stage,
			.module = stageInfo.module,
			.pName = "main",
			.pSpecializationInfo = stageInfo.specialization,
		};

		numStages++;
	};
		
	appendShaderStageCreateInfo(params.shaderStages.vertex, VK_SHADER_STAGE_VERTEX_BIT);
	appendShaderStageCreateInfo(params.shaderStages.fragment, VK_SHADER_STAGE_FRAGMENT_BIT);

	const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = u32(params.vertexInputBindings.size()),
		.pVertexBindingDescriptions = params.vertexInputBindings.data(),
		.vertexAttributeDescriptionCount = u32(params.vertexInputAttribs.size()),
		.pVertexAttributeDescriptions = params.vertexInputAttribs.data(),
	};

	const VkPipelineInputAssemblyStateCreateInfo inputAssemplyInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = params.primitiveTopology,
		.primitiveRestartEnable = VK_FALSE,
	};

	const VkPipelineViewportStateCreateInfo viewportInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &params.viewport,
		.scissorCount = 1,
		.pScissors = &params.scissor,
	};

	const VkPipelineRasterizationStateCreateInfo rasterizationInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = params.polygonMode,
		.cullMode = VkCullModeFlags(params.cullMode),
		.frontFace = params.faceClockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		//.depthBiasConstantFactor = 0,
		//.depthBiasClamp = 0,
		//.depthBiasSlopeFactor = 0,
		.lineWidth = 1,
	};

	const auto& c = params.blendConstants;
	const VkPipelineColorBlendStateCreateInfo colorBlendInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		//.logicOp = ,
		.attachmentCount = u32(params.attachmentsBlendInfos.size()),
		.pAttachments = params.attachmentsBlendInfos.data(),
		.blendConstants = {c.r, c.g, c.b, c.a},
	};

	const VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = u32(params.dynamicStates.size()),
		.pDynamicStates = params.dynamicStates.data(),
	};

	const VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.flags = 0,
		.stageCount = numStages,
		.pStages = shaderStageCreateInfos,
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssemplyInfo,
		.pViewportState = &viewportInfo,
		.pRasterizationState = &rasterizationInfo,
		.pMultisampleState = nullptr,
		.pDepthStencilState = nullptr,
		.pColorBlendState = &colorBlendInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = params.pipelineLayout,
		.renderPass = params.renderPass,
		.subpass = params.subpass,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = 0,
	};

	VkPipeline pipeline;
	VkResult vkRes = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
	assertRes(vkRes);
	return pipeline;
}

VkCommandPool createCmdPool(VkDevice device, u32 queueFamilyInd, VkCommandPoolCreateFlags flags)
{
	const VkCommandPoolCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = flags,
		.queueFamilyIndex = queueFamilyInd,
	};
	VkCommandPool pool;
	VkResult vkRes = vkCreateCommandPool(device, &info, nullptr, &pool);
	assertRes(vkRes);
	return pool;
}

void allocateCmdBuffers(VkDevice device, VkCommandPool pool, std::span<VkCommandBuffer> buffers)
{
	const VkCommandBufferAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.commandBufferCount = u32(buffers.size())
	};
	VkResult vkRes = vkAllocateCommandBuffers(device, &info, buffers.data());
	assertRes(vkRes);
}

void beginCmdBuffer(VkCommandBuffer cmdBuffer, bool oneTimeSubmit = true)
{
	VkCommandBufferUsageFlags usageFlags = 0;
	if (oneTimeSubmit)
		usageFlags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	const VkCommandBufferBeginInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = usageFlags,
		.pInheritanceInfo = nullptr, // this field is used when working with secondary cmd buffers
	};
	VkResult vkRes = vkBeginCommandBuffer(cmdBuffer, &info);
	assertRes(vkRes);
}

VkFramebuffer createFramebuffer(VkDevice device, VkRenderPass renderPass,
	std::span<const VkImageView> attachments, u32 w, u32 h)
{
	const VkFramebufferCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = renderPass, // just for stating compatibility
		.attachmentCount = u32(attachments.size()),
		.pAttachments = attachments.data(),
		.width = w,
		.height = h,
		.layers = 1,
	};
	VkFramebuffer fb;
	VkResult vkRes = vkCreateFramebuffer(device, &info, nullptr, &fb);
	assertRes(vkRes);
	return fb;
}

u32 findMemTypeForStaticVertexBuffer(VmaAllocator allocator, size_t size, VkBufferCreateInfo& bufferInfo)
{
	bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};
	VmaAllocationCreateInfo allocCreateInfo = {
		.usage = VMA_MEMORY_USAGE_AUTO,
	};
	u32 memType;
	VkResult vkRes = vmaFindMemoryTypeIndexForBufferInfo(allocator, &bufferInfo, &allocCreateInfo, &memType);
	assertRes(vkRes);

	VkMemoryPropertyFlags memPropFlags;
	vmaGetMemoryTypeProperties(allocator, memType, &memPropFlags);
	if (memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		// if the chosen memory type is HOST_VISIBLE we don't actually need the TRANSFER_DST_BIT
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; // probably not need but just in case
		u32 memType;
		VkResult vkRes = vmaFindMemoryTypeIndexForBufferInfo(allocator, &bufferInfo, &allocCreateInfo, &memType);
		assertRes(vkRes);
	}
	return memType;
}

void createStaticVertexBuffer(VkDevice device, VmaAllocator allocator, size_t size, VkBuffer& buffer, VmaAllocation& allocation, VmaAllocationInfo* allocInfo = nullptr)
{
	VkBufferCreateInfo bufferInfo;
	const u32 memType = findMemTypeForStaticVertexBuffer(allocator, size, bufferInfo);

	const VmaAllocationCreateInfo allocCreateInfo = {
		.usage = VMA_MEMORY_USAGE_AUTO,
		.memoryTypeBits = u32(1) << memType
	};
	VmaAllocationInfo allocInfoVar;
	allocInfo = allocInfo ? allocInfo : &allocInfoVar;
	VkResult vkRes = vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &buffer, &allocation, allocInfo);
	assertRes(vkRes);

	VkMemoryPropertyFlags memPropFlags;
	vmaGetAllocationMemoryProperties(allocator, allocation, &memPropFlags);
	if (memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vmaMapMemory(allocator, allocation, &allocInfo->pMappedData);
	}
}

#if 0
void createStaticImage(VkDevice device, VmaAllocator, u32 width, u32 height, u32 layers, VkFormat format, u32 mipLevels, VkImage& img, VmaAllocation& allocation, VmaAllocationInfo* allocInfo = nullptr)
{
	u32 maxMipLevels = 1;
	u32 hw = width;
	while()
	mipLevels = glm::clamp(mipLevels, 1, )
	VkImageCreateInfo imgInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {width, height, 1},
		.mipLevels = 
	}
}
#endif

void createStagingBuffer(VkDevice device, VmaAllocator allocator, size_t size, VkBuffer& buffer, VmaAllocation& allocation, VmaAllocationInfo* allocInfo = nullptr)
{
	const VkBufferCreateInfo bufferInfo = {
	.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	.size = size,
	.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	};
	const VmaAllocationCreateInfo allocCreateInfo = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
				 VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};
	VkResult vkRes = vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &buffer, &allocation, allocInfo);
	assertRes(vkRes);
}

} // namesapce vk

}