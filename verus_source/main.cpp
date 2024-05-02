#include <nvvk/context_vk.hpp>
#include <nvvk/structs_vk.hpp>           // initialize vulkan structures with nvvk::make
#include <nvvk/resourceallocator_vk.hpp> // NVKK memory allocator

// rendered image size
static const uint64_t render_width = 800;
static const uint64_t render_height = 600;

int main(int argc, const char** argv)
{
	// context information
	nvvk::ContextCreateInfo deviceInfo;
	deviceInfo.apiMajor = 1;
	deviceInfo.apiMinor = 3;

	// context instance
	nvvk::Context           context;    // represent a single physical device and its information

	// enable device extensions for ray tracing
	deviceInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME); // enable deferred host operations

	// acceleration structure, ray query
	auto as_features = nvvk::make<VkPhysicalDeviceAccelerationStructureFeaturesKHR>();
	deviceInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &as_features); // false parameter means that the extension is required.
	auto rayquery_features = nvvk::make<VkPhysicalDeviceRayQueryFeaturesKHR>();
	deviceInfo.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &rayquery_features); // false parameter means that the extension is required.

	context.init(deviceInfo);
	// check if the device supports the ray tracing extensions
	assert(as_features.accelerationStructure == VK_TRUE && rayquery_features.rayQuery == VK_TRUE);

	nvvk::ResourceAllocatorDedicated allocator; // memory allocator
	allocator.init(context, context.m_physicalDevice);

	// create a storage buffer
	VkDeviceSize buffer_size = render_height * render_width * 3 * sizeof(float);
	auto buffer_create_info = nvvk::make<VkBufferCreateInfo>();
	buffer_create_info.size = buffer_size;
	buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	
	nvvk::Buffer storage_buffer = allocator.createBuffer(
		buffer_create_info,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT // memory properties
	);

	// retrieve data from GPU to CPU
	void* data = allocator.map(storage_buffer);         // map memory to CPU
	float* float_data = reinterpret_cast<float*>(data); // cast void data type to float pointer type
	printf("%f, %f, %f", float_data[0], float_data[1], float_data[2]);
	allocator.unmap(storage_buffer);                    // unmap memory

	
	// clean up
	{
		allocator.destroy(storage_buffer);  // clean up storage buffer
		allocator.deinit();                 // clean up allocator
		context.deinit();                   // clean up context
	}
}

