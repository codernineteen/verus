#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include <stb_image_write.h>

#ifndef TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#endif
#include <tiny_obj_loader.h>

#include <array>

#include <nvvk/context_vk.hpp>
#include <nvvk/structs_vk.hpp>           // initialize vulkan structures with nvvk::make
#include <nvvk/resourceallocator_vk.hpp> // NVKK memory allocator
#include <nvvk/error_vk.hpp>             // NVVK_CHECK macro
#include <nvvk/shaders_vk.hpp>           
#include <nvvk/descriptorsets_vk.hpp>
#include <nvvk/raytraceKHR_vk.hpp>
#include <nvh/fileoperations.hpp>

// rendered image size
static const uint64_t render_width = 1920;
static const uint64_t render_height = 1280;

// work group resolution (16 x 8 = 128)
static const uint32_t workgroup_width = 16;
static const uint32_t workgroup_height = 8;

static VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool cmd_pool)
{
	// allocate command buffer
	VkCommandBufferAllocateInfo cmd_buffer_info = nvvk::make<VkCommandBufferAllocateInfo>();
	cmd_buffer_info.commandPool                 = cmd_pool;
	cmd_buffer_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buffer_info.commandBufferCount          = 1;
	VkCommandBuffer cmd_buffer;
	NVVK_CHECK(vkAllocateCommandBuffers(device, &cmd_buffer_info, &cmd_buffer));

	// begin command buffer recording
	VkCommandBufferBeginInfo cmd_buffer_begin_info = nvvk::make<VkCommandBufferBeginInfo>();
	cmd_buffer_begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	NVVK_CHECK(vkBeginCommandBuffer(cmd_buffer, &cmd_buffer_begin_info));

	return cmd_buffer;
}

static void endSingleTimeCommands(VkDevice device, VkQueue queue, VkCommandBuffer cmd_buffer, VkCommandPool cmd_pool)
{
	NVVK_CHECK(vkEndCommandBuffer(cmd_buffer));

	// submit command buffer.
	VkSubmitInfo submit_info       = nvvk::make<VkSubmitInfo>();
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &cmd_buffer;
	NVVK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));

	// wait for gpu working to finish
	NVVK_CHECK(vkQueueWaitIdle(queue));

	vkFreeCommandBuffers(device, cmd_pool, 1, &cmd_buffer);
}

static VkDeviceAddress GetBufferDeviceAddress(VkDevice device, nvvk::Buffer& buffer)
{
	VkBufferDeviceAddressInfo buffer_device_address_info = nvvk::make<VkBufferDeviceAddressInfo>();
	buffer_device_address_info.buffer                    = buffer.buffer;
	return vkGetBufferDeviceAddress(device, &buffer_device_address_info);
}

int main(int argc, const char** argv)
{
	// context information
	nvvk::ContextCreateInfo device_info;
	device_info.apiMajor = 1;
	device_info.apiMinor = 3;

	// enable device extensions for ray tracing
	device_info.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME); // enable deferred host operations
	// acceleration structure, ray query
	auto as_features = nvvk::make<VkPhysicalDeviceAccelerationStructureFeaturesKHR>();
	device_info.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &as_features); // false parameter means that the extension is required.
	auto rayquery_features = nvvk::make<VkPhysicalDeviceRayQueryFeaturesKHR>();
	device_info.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &rayquery_features); // false parameter means that the extension is required.

	// context instance
	nvvk::Context           context;    // represent a single physical device and its information
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
	tinyobj::ObjReader reader;
	reader.ParseFromFile(nvh::findFile("scenes/cornellbox_original_merged.obj", search_paths));
	assert(reader.Valid());

	// get all vertices
	const std::vector<tinyobj::real_t> obj_vertices = reader.GetAttrib().GetVertices();
	// get shape (shape includes mesh, a set of lines and a set of points)
	const std::vector<tinyobj::shape_t>& obj_shapes = reader.GetShapes();
	assert(obj_shapes.size() == 1);
	const tinyobj::shape_t& shape = obj_shapes[0];

	// get indices
	std::vector<uint32_t> obj_indices;
	obj_indices.reserve(shape.mesh.indices.size());
	for (const tinyobj::index_t& index : shape.mesh.indices) 
	{
		obj_indices.push_back(index.vertex_index);
	}

	// create command pool and buffer
	VkCommandPoolCreateInfo cmd_pool_info = nvvk::make<VkCommandPoolCreateInfo>();
	cmd_pool_info.queueFamilyIndex = context.m_queueGCT;
	VkCommandPool cmd_pool;
	NVVK_CHECK(vkCreateCommandPool(context, &cmd_pool_info, /*default cpu allocator*/ nullptr, &cmd_pool));

	nvvk::Buffer vertex_buffer, index_buffer;
	{
		// start recording command buffer
		VkCommandBuffer cmd_buffer = beginSingleTimeCommands(context, cmd_pool);

		const VkBufferUsageFlags buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		vertex_buffer = allocator.createBuffer(cmd_buffer, obj_vertices, buffer_usage_flags);
		index_buffer = allocator.createBuffer(cmd_buffer, obj_indices, buffer_usage_flags);

		endSingleTimeCommands(context, context.m_queueGCT, cmd_buffer, cmd_pool);
		allocator.finalizeAndReleaseStaging();
	}


	// build bottom level acceleration structure
	std::vector<nvvk::RaytracingBuilderKHR::BlasInput> blases;
	{
		nvvk::RaytracingBuilderKHR::BlasInput blas;
		auto vertex_address = GetBufferDeviceAddress(context, vertex_buffer);
		auto index_address = GetBufferDeviceAddress(context, index_buffer);

		// define triangle
		VkAccelerationStructureGeometryTrianglesDataKHR triangles_data = nvvk::make<VkAccelerationStructureGeometryTrianglesDataKHR>();
		triangles_data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		triangles_data.vertexData.deviceAddress = vertex_address;
		triangles_data.vertexStride = sizeof(float) * 3;
		triangles_data.maxVertex = static_cast<uint32_t>(obj_vertices.size() / 3 - 1);
		triangles_data.indexType = VK_INDEX_TYPE_UINT32;
		triangles_data.indexData.deviceAddress = index_address;
		triangles_data.transformData.deviceAddress = 0;

		// define geometry
		VkAccelerationStructureGeometryKHR geometry = nvvk::make<VkAccelerationStructureGeometryKHR>();
		geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		geometry.geometry.triangles = triangles_data;
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		blas.asGeometry.push_back(geometry);

		// define build range
		VkAccelerationStructureBuildRangeInfoKHR build_range = nvvk::make<VkAccelerationStructureBuildRangeInfoKHR>();
		build_range.firstVertex                              = 0;
		build_range.primitiveCount                           = static_cast<uint32_t>(obj_indices.size() / 3);
		build_range.primitiveOffset                          = 0;
		build_range.transformOffset                          = 0;
		blas.asBuildOffsetInfo.push_back(build_range);
		blases.push_back(blas);
	}

	nvvk::RaytracingBuilderKHR raytracing_builder;
	raytracing_builder.setup(context, &allocator, context.m_queueGCT);
	raytracing_builder.buildBlas(blases, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

	// create an instance
	std::vector<VkAccelerationStructureInstanceKHR> instances;
	{
		VkAccelerationStructureInstanceKHR instance{};
		instance.accelerationStructureReference         = raytracing_builder.getBlasDeviceAddress(0);
		instance.instanceCustomIndex                    = 0;
		instance.mask                                   = 0xFF;
		instance.instanceShaderBindingTableRecordOffset = 0;
		instance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance.transform.matrix[0][0] = instance.transform.matrix[1][1] = instance.transform.matrix[2][2] = 1.0f;

		instances.push_back(instance);
	}
	raytracing_builder.buildTlas(instances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

	// create a descriptor set container
	nvvk::DescriptorSetContainer descriptor_container(context);
	descriptor_container.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
	descriptor_container.addBinding(1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT);
	descriptor_container.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
	descriptor_container.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
	descriptor_container.initLayout();
	descriptor_container.initPool(1);
	descriptor_container.initPipeLayout();

	// update descriptor set
	std::array<VkWriteDescriptorSet, 4> write_desc_sets{};
	// buffer write 
	VkDescriptorBufferInfo buffer_desc_info = nvvk::make<VkDescriptorBufferInfo>();
	buffer_desc_info.buffer                 = storage_buffer.buffer;
	buffer_desc_info.range                  = buffer_size;
	write_desc_sets[0]                      = descriptor_container.makeWrite(0, 0, &buffer_desc_info);

	// acceleration structure write
	VkWriteDescriptorSetAccelerationStructureKHR as_write = nvvk::make<VkWriteDescriptorSetAccelerationStructureKHR>();
	VkAccelerationStructureKHR tlas_copy                  = raytracing_builder.getAccelerationStructure();
	as_write.accelerationStructureCount                   = 1;
	as_write.pAccelerationStructures                      = &tlas_copy;
	write_desc_sets[1]                                    = descriptor_container.makeWrite(0, 1, &as_write);

	// buffer write 
	VkDescriptorBufferInfo vertex_buffer_desc_info = nvvk::make<VkDescriptorBufferInfo>();
	vertex_buffer_desc_info.buffer                 = vertex_buffer.buffer;
	vertex_buffer_desc_info.range                  = VK_WHOLE_SIZE;
	write_desc_sets[2] = descriptor_container.makeWrite(0, 2, &vertex_buffer_desc_info);

	// buffer write 
	VkDescriptorBufferInfo index_buffer_desc_info = nvvk::make<VkDescriptorBufferInfo>();
	index_buffer_desc_info.buffer                 = index_buffer.buffer;
	index_buffer_desc_info.range                  = VK_WHOLE_SIZE;
	write_desc_sets[3] = descriptor_container.makeWrite(0, 3, &index_buffer_desc_info);

	vkUpdateDescriptorSets(context, static_cast<uint32_t>(write_desc_sets.size()), write_desc_sets.data(), 0, nullptr);


	// compute shader module
	VkShaderModule ray_trace_module = nvvk::createShaderModule(context, nvh::loadFile("shaders/raytrace.comp.glsl.spv", true, search_paths));

	VkPipelineShaderStageCreateInfo shader_stage_info = nvvk::make<VkPipelineShaderStageCreateInfo>();
	shader_stage_info.stage                           = VK_SHADER_STAGE_COMPUTE_BIT;
	shader_stage_info.module                          = ray_trace_module;
	shader_stage_info.pName                           = "main"; // spir-v file can contain multiple entry points

	// create compute pipeline
	VkComputePipelineCreateInfo pipeline_info = nvvk::make<VkComputePipelineCreateInfo>();
	pipeline_info.stage                       = shader_stage_info;
	pipeline_info.layout                      = descriptor_container.getPipeLayout();
	
	VkPipeline compute_pipeline;
	NVVK_CHECK(vkCreateComputePipelines(context, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &compute_pipeline));
	
	// create command buffer
	VkCommandBuffer cmd_buffer = beginSingleTimeCommands(context, cmd_pool);

	vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
	
	VkDescriptorSet descriptor_set = descriptor_container.getSet(0);
	vkCmdBindDescriptorSets(
		cmd_buffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		descriptor_container.getPipeLayout(),
		0, 1,
		&descriptor_set,
		0, nullptr);

	// each workgroup will process a 16 x 8 pixel block
	// total workgroup count is [ceil(render_width / 16) x ceil(render_height / 8) x 1]
	auto workgroup_count_x = (uint32_t(render_width) + workgroup_width - 1) / workgroup_width;
	auto workgroup_count_y = (uint32_t(render_height) + workgroup_height - 1) / workgroup_height;

;	vkCmdDispatch(cmd_buffer, workgroup_count_x, workgroup_count_y, 1);
	
	// memory barrier from computer shader to host 
	VkMemoryBarrier memory_barrier = nvvk::make<VkMemoryBarrier>();
	memory_barrier.srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT;
	memory_barrier.dstAccessMask   = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(
		cmd_buffer, 
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
		VK_PIPELINE_STAGE_HOST_BIT,
		0, 
		1, &memory_barrier, 
		0, nullptr,
		0, nullptr);

	// stop recording command buffer and destroy resources
	endSingleTimeCommands(context, context.m_queueGCT, cmd_buffer, cmd_pool);
	

	// retrieve data from GPU to CPU
	void* data = allocator.map(storage_buffer);         // map memory to CPU
	stbi_write_hdr("output.hdr", render_width, render_height, 3, reinterpret_cast<float*>(data)); // write data to file
	allocator.unmap(storage_buffer);                    // unmap memory
	
	// clean up
	{
		vkDestroyPipeline(context, compute_pipeline, nullptr);
		vkDestroyShaderModule(context, ray_trace_module, nullptr);
		vkDestroyCommandPool(context, cmd_pool, nullptr);

		raytracing_builder.destroy();
		allocator.destroy(vertex_buffer);
		allocator.destroy(index_buffer);
		descriptor_container.deinit();      // clean up descriptor set container
		allocator.destroy(storage_buffer);  // clean up storage buffer
		allocator.deinit();                 // clean up allocator
		context.deinit();                   // clean up context
	}
}


