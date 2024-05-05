#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include <stb_image_write.h>

#include <nvvk/context_vk.hpp>
#include <nvvk/structs_vk.hpp>           // initialize vulkan structures with nvvk::make
#include <nvvk/resourceallocator_vk.hpp> // NVKK memory allocator
#include <nvvk/error_vk.hpp>             // NVVK_CHECK macro
#include <nvvk/shaders_vk.hpp>           
#include <nvh/fileoperations.hpp>

// rendered image size
static const uint64_t render_width = 800;
static const uint64_t render_height = 600;

// work group size (16 x 8 = 128)
static const uint32_t workgroup_width = 16;
static const uint32_t workgroup_height = 8;

int main(int argc, const char** argv)
{
	// context information
	nvvk::ContextCreateInfo device_info;
	device_info.apiMajor = 1;
	device_info.apiMinor = 3;

	// context instance
	nvvk::Context           context;    // represent a single physical device and its information

	// enable device extensions for ray tracing
	device_info.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME); // enable deferred host operations
	device_info.addDeviceExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
	VkValidationFeaturesEXT      validation_info            = nvvk::make<VkValidationFeaturesEXT>();
	VkValidationFeatureEnableEXT validation_feature = VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT;
	validation_info.enabledValidationFeatureCount           = 1;
	validation_info.pEnabledValidationFeatures              = &validation_feature;
	device_info.instanceCreateInfoExt                       = &validation_info;

#ifdef _WIN32
	_putenv_s("DEBUG_PRINTF_TO_STDOUT", "1");
#else 
	static char putenvString[] = "DEBUG_PRINTF_TO_STDOUT=1";
	putenv(putenvString);
#endif  

	// acceleration structure, ray query
	auto as_features = nvvk::make<VkPhysicalDeviceAccelerationStructureFeaturesKHR>();
	device_info.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &as_features); // false parameter means that the extension is required.
	auto rayquery_features = nvvk::make<VkPhysicalDeviceRayQueryFeaturesKHR>();
	device_info.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &rayquery_features); // false parameter means that the extension is required.

	context.init(device_info);
	// check if the device supports the ray tracing extensions
	assert(as_features.accelerationStructure == VK_TRUE && rayquery_features.rayQuery == VK_TRUE);

	nvvk::ResourceAllocatorDedicated allocator; // memory allocator
	allocator.init(context, context.m_physicalDevice);

	// create a storage buffer
	VkDeviceSize buffer_size = render_height * render_width * 3 * sizeof(float);
	auto buffer_create_info  = nvvk::make<VkBufferCreateInfo>();
	buffer_create_info.size  = buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	
	nvvk::Buffer storage_buffer = allocator.createBuffer(
		buffer_create_info,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT // memory properties
	);

	const std::string exe_path(argv[0], std::string(argv[0]).find_last_of("/\\") + 1);
	std::vector<std::string> search_paths = { 
		exe_path + PROJECT_RELDIRECTORY,
		exe_path + PROJECT_RELDIRECTORY "..",
		exe_path + PROJECT_RELDIRECTORY "../..",
		exe_path + PROJECT_NAME
	};

	// create command pool and buffer
	VkCommandPoolCreateInfo cmd_pool_info = nvvk::make<VkCommandPoolCreateInfo>();
	cmd_pool_info.queueFamilyIndex        = context.m_queueGCT;
	VkCommandPool cmd_pool;
	NVVK_CHECK(vkCreateCommandPool(context, &cmd_pool_info, /*default cpu allocator*/ nullptr, &cmd_pool));

	// compute shader module
	VkShaderModule ray_trace_module = nvvk::createShaderModule(context, nvh::loadFile("shaders/raytrace.comp.glsl.spv", true, search_paths));

	VkPipelineShaderStageCreateInfo shader_stage_info = nvvk::make<VkPipelineShaderStageCreateInfo>();
	shader_stage_info.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
	shader_stage_info.module = ray_trace_module;
	shader_stage_info.pName  = "main"; // spir-v file can contain multiple entry points

	// specify the pipeline layout
	VkPipelineLayoutCreateInfo pipeline_layout_info = nvvk::make<VkPipelineLayoutCreateInfo>();
	pipeline_layout_info.setLayoutCount             = 0;
	pipeline_layout_info.pushConstantRangeCount     = 0;
	VkPipelineLayout pipeline_layout; // empty pipeline layout
	NVVK_CHECK(vkCreatePipelineLayout(context, &pipeline_layout_info, nullptr, &pipeline_layout));

	// create compute pipeline
	VkComputePipelineCreateInfo pipeline_info = nvvk::make<VkComputePipelineCreateInfo>();
	pipeline_info.stage  = shader_stage_info;
	pipeline_info.layout = pipeline_layout;
	
	VkPipeline compute_pipeline;
	NVVK_CHECK(vkCreateComputePipelines(context, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &compute_pipeline));

	// allocate command buffer
	VkCommandBufferAllocateInfo cmd_buffer_info = nvvk::make<VkCommandBufferAllocateInfo>();
	cmd_buffer_info.commandPool                 = cmd_pool;
	cmd_buffer_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buffer_info.commandBufferCount          = 1;
	VkCommandBuffer cmd_buffer;
	NVVK_CHECK(vkAllocateCommandBuffers(context, &cmd_buffer_info, &cmd_buffer));

	// begin command buffer recording
	VkCommandBufferBeginInfo cmd_buffer_begin_info = nvvk::make<VkCommandBufferBeginInfo>();
	cmd_buffer_begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	NVVK_CHECK(vkBeginCommandBuffer(cmd_buffer, &cmd_buffer_begin_info));

	vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
	vkCmdDispatch(cmd_buffer, 1, 1, 1); // grid : 1 x 1 x 1
	
	// memory barrier from computer shader to host 
	VkMemoryBarrier memory_barrier = nvvk::make<VkMemoryBarrier>();
	memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	memory_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(
		cmd_buffer, 
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
		VK_PIPELINE_STAGE_HOST_BIT,
		0, 
		1, &memory_barrier, 
		0, nullptr,
		0, nullptr);

	NVVK_CHECK(vkEndCommandBuffer(cmd_buffer));

	// submit command buffer.
	VkSubmitInfo submit_info       = nvvk::make<VkSubmitInfo>();
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &cmd_buffer;
	NVVK_CHECK(vkQueueSubmit(context.m_queueGCT, 1, &submit_info, VK_NULL_HANDLE));

	// wait for gpu working to finish
	NVVK_CHECK(vkQueueWaitIdle(context.m_queueGCT));

	// retrieve data from GPU to CPU
	void* data = allocator.map(storage_buffer);         // map memory to CPU
	stbi_write_hdr("output.hdr", render_width, render_height, 3, reinterpret_cast<float*>(data)); // write data to file
	allocator.unmap(storage_buffer);                    // unmap memory
	
	// clean up
	{

		vkFreeCommandBuffers(context, cmd_pool, 1, &cmd_buffer);
		vkDestroyCommandPool(context, cmd_pool, /*default cpu allocator*/ nullptr);

		vkDestroyPipeline(context, compute_pipeline, nullptr);
		vkDestroyShaderModule(context, ray_trace_module, nullptr);
		vkDestroyPipelineLayout(context, pipeline_layout, nullptr);


		allocator.destroy(storage_buffer);  // clean up storage buffer
		allocator.deinit();                 // clean up allocator
		context.deinit();                   // clean up context
	}
}

