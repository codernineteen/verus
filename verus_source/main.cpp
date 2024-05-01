#include <cassert>

#include <nvvk/context_vk.hpp>
#include <nvvk/structs_vk.hpp> // initialize vulkan structures with nvvk::make


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

	context.deinit();                   // clean up context
}

