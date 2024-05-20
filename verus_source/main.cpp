// third party
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <nvpsystem.hpp>

// nvpro
#include <nvh/cameramanipulator.hpp>
#include <nvvk/context_vk.hpp>

// define
#define UNUSED(x) (void)(x)

// global 
std::vector<std::string> search_paths;

// error handler for glfw
static void onErrorCallback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error : %d: %s\n", error, description);
}

static int const RENDER_WIDTH = 1680;
static int const RENDER_HEIGHT = 1050;

int main(int argc, const char** argv)
{
	UNUSED(argc);

	// Setup GLFW window
	glfwSetErrorCallback(onErrorCallback);
	if (!glfwInit())
	{
		fprintf(stderr, "Failed to initialize GLFW\n");
		return 1;
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(RENDER_WIDTH, RENDER_HEIGHT, PROJECT_NAME, nullptr, nullptr);

	// Setup camera
	CameraManip.setWindowSize(RENDER_WIDTH, RENDER_HEIGHT);
	CameraManip.setLookat(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));

	// Setup basing things for application
	NVPSystem system(PROJECT_NAME);

	// Setup search paths
	const std::string exe_path(argv[0], std::string(argv[0]).find_last_of("/\\") + 1);
	search_paths = {
		exe_path + PROJECT_RELDIRECTORY,
		exe_path + PROJECT_RELDIRECTORY "..",
		exe_path + PROJECT_RELDIRECTORY "../..",
		exe_path + PROJECT_NAME
	};

	// Assert glfw vulkan is supported
	assert(glfwVulkanSupported() == 1);
	uint32_t count{ 0 };
	auto required_extensions_glfw = glfwGetRequiredInstanceExtensions(&count);

	// Configuring Vulkan extensions and Layers
	nvvk::ContextCreateInfo context_info;
	context_info.setVersion(1, 3);
	for (uint32_t extension_id = 0; extension_id < count; extension_id++)
	{
		context_info.addDeviceExtension(required_extensions_glfw[extension_id]);
	}
	context_info.addInstanceLayer("VK_LAYER_LUNARG_monitor", true);
	context_info.addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);
	context_info.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);


}


