#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include <iostream>
#include <stdexcept>
#include <functional>
#include <chrono>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <vector>
#include <cstring>
#include <array>
#include <set>

#define NOMINMAX

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <unordered_map>

#include <glm/gtx/hash.hpp>

#include <windows.h>

//variables to pass to the vertex shader
struct UniformBufferObject {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
	float time;
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	static VkVertexInputBindingDescription getBindingDescription() {
		VkVertexInputBindingDescription bindingDescription = {};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return bindingDescription;
	}

	static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
		std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};

		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, pos);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

		return attributeDescriptions;
	}

	bool operator==(const Vertex& other) const {
		return pos == other.pos && color == other.color && texCoord == other.texCoord;
	}
};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}

//Width and height of the window
const int WIDTH = 800;
const int HEIGHT = 600;
//paths for the model
const std::string MODEL_PATH = "models/test1.obj";
const std::string TEXTURE_PATH = "textures/binary_adder-RGBA.png";
//VK_LAYER_LUNARG_standard_validation  is a layer that enables a lot of other useful debugging layers
const std::vector<const char*> validationLayers = {
	"VK_LAYER_LUNARG_standard_validation"
};
//list of device extensions
const std::vector<const char*> deviceExtensions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) {
	auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pCallback);
	}
	else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
	auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
	if (func != nullptr) {
		func(instance, callback, pAllocator);
	}
}

//wrapper class to handle resource management, because there is very little automatic cleanup in vulkan, this class automatically cleans up vulkan objects when they go out of scope (applicaiton closed, etc), using RAII
template <typename T>
class VDeleter {
public:
	VDeleter() : VDeleter([](T, VkAllocationCallbacks*) {}) {}

	VDeleter(std::function<void(T, VkAllocationCallbacks*)> deletef) {
		this->deleter = [=](T obj) { deletef(obj, nullptr); };
	}

	VDeleter(const VDeleter<VkInstance>& instance, std::function<void(VkInstance, T, VkAllocationCallbacks*)> deletef) {
		this->deleter = [&instance, deletef](T obj) { deletef(instance, obj, nullptr); };
	}

	VDeleter(const VDeleter<VkDevice>& device, std::function<void(VkDevice, T, VkAllocationCallbacks*)> deletef) {
		this->deleter = [&device, deletef](T obj) { deletef(device, obj, nullptr); };
	}

	~VDeleter() {
		cleanup();
	}

	const T* operator &() const {
		return &object;
	}

T* replace() {
	cleanup();
	return &object;
}

operator T() const {
	return object;
}

void operator=(T rhs) {
	if (rhs != object) {
		cleanup();
		object = rhs;
	}
}

template<typename V>
bool operator==(V rhs) {
	return object == T(rhs);
}

private:
	T object{ VK_NULL_HANDLE };
	std::function<void(T)> deleter;

	void cleanup() {
		if (object != VK_NULL_HANDLE) {
			deleter(object);
		}
		object = VK_NULL_HANDLE;
	}
};

struct QueueFamilyIndices {
	int graphicsFamily = -1;
	int presentFamily = -1;

	bool isComplete() {
		return graphicsFamily >= 0 && presentFamily >= 0;
	}
};

struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};
glm::vec3 cameraPosition = glm::vec3(0, 0, 0);
glm::vec3 cameraAngle = glm::vec3(0, 0, 0);

struct keyValues { bool w = false; bool a = false; bool s = false; bool d = false; bool f = false; bool shift = false; bool space = false; bool ctrl = false; bool tab = false; bool n1 = false; bool n2 = false; bool n3 = false; bool n4 = false; bool n5 = false; bool n6 = false; bool n7 = false; bool n8 = false; bool n9 = false; bool n0 = false;   bool mouseLeft = false; bool mouseRight = false; };
keyValues keys;
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{

	if (key == GLFW_KEY_1 && action == GLFW_PRESS) {
		keys.n1 = true;
	}
	if (key == GLFW_KEY_1 && action == GLFW_RELEASE) {
		keys.n1 = false;
	}
	if (key == GLFW_KEY_2 && action == GLFW_PRESS) {
		keys.n2 = true;
	}
	if (key == GLFW_KEY_2 && action == GLFW_RELEASE) {
		keys.n2 = false;
	}
	if (key == GLFW_KEY_3 && action == GLFW_PRESS) {
		keys.n3 = true;
	}
	if (key == GLFW_KEY_3 && action == GLFW_RELEASE) {
		keys.n3 = false;
	}
	if (key == GLFW_KEY_4 && action == GLFW_PRESS) {
		keys.n4 = true;
	}
	if (key == GLFW_KEY_4 && action == GLFW_RELEASE) {
		keys.n4 = false;
	}
	if (key == GLFW_KEY_5 && action == GLFW_PRESS) {
		keys.n5 = true;
	}
	if (key == GLFW_KEY_5 && action == GLFW_RELEASE) {
		keys.n5 = false;
	}
	if (key == GLFW_KEY_6 && action == GLFW_PRESS) {
		keys.n6 = true;
	}
	if (key == GLFW_KEY_6 && action == GLFW_RELEASE) {
		keys.n6 = false;
	}
	if (key == GLFW_KEY_7 && action == GLFW_PRESS) {
		keys.n7 = true;
	}
	if (key == GLFW_KEY_7 && action == GLFW_RELEASE) {
		keys.n7 = false;
	}
	if (key == GLFW_KEY_8 && action == GLFW_PRESS) {
		keys.n8 = true;
	}
	if (key == GLFW_KEY_8 && action == GLFW_RELEASE) {
		keys.n8 = false;
	}
	if (key == GLFW_KEY_9 && action == GLFW_PRESS) {
		keys.n9 = true;
	}
	if (key == GLFW_KEY_9 && action == GLFW_RELEASE) {
		keys.n9 = false;
	}
	if (key == GLFW_KEY_0 && action == GLFW_PRESS) {
		keys.n0 = true;
	}
	if (key == GLFW_KEY_0 && action == GLFW_RELEASE) {
		keys.n0 = false;
	}

	if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
		keys.space = true;
	}
	if (key == GLFW_KEY_SPACE && action == GLFW_RELEASE) {
		keys.space = false;
	}

	if (key == GLFW_KEY_LEFT_SHIFT && action == GLFW_PRESS) {
		keys.shift = true;
	}
	if (key == GLFW_KEY_LEFT_SHIFT && action == GLFW_RELEASE) {
		keys.shift = false;
	}

	if (key == GLFW_KEY_LEFT_CONTROL && action == GLFW_PRESS) {
		keys.ctrl = true;
	}
	if (key == GLFW_KEY_LEFT_CONTROL && action == GLFW_RELEASE) {
		keys.ctrl = false;
	}

	if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
		keys.tab = true;
	}
	if (key == GLFW_KEY_TAB && action == GLFW_RELEASE) {
		keys.tab = false;
	}

	if (key == GLFW_KEY_W && action == GLFW_PRESS) {
		keys.w = true;
	}
	if (key == GLFW_KEY_A && action == GLFW_PRESS) {
		keys.a = true;
	}
	if (key == GLFW_KEY_S && action == GLFW_PRESS) {
		keys.s = true;
	}
	if (key == GLFW_KEY_D && action == GLFW_PRESS) {
		keys.d = true;
	}
	if (key == GLFW_KEY_F && action == GLFW_PRESS) {
		keys.f = true;
	}
	if (key == GLFW_KEY_W && action == GLFW_RELEASE) {
		keys.w = false;
	}
	if (key == GLFW_KEY_A && action == GLFW_RELEASE) {
		keys.a = false;
	}
	if (key == GLFW_KEY_S && action == GLFW_RELEASE) {
		keys.s = false;
	}
	if (key == GLFW_KEY_D && action == GLFW_RELEASE) {
		keys.d = false;
	}
	if (key == GLFW_KEY_F && action == GLFW_RELEASE) {
		keys.f = false;
	}
}
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		keys.mouseLeft = true;
	}
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
		keys.mouseLeft = false;
	}
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
		keys.mouseRight = true;
	}
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
		keys.mouseRight = false;
	}
}
bool inFocus = true;
void window_focus_callback(GLFWwindow* window, int focused)
{
	if (focused)
	{
		inFocus = true;
	}
	else
	{
		inFocus = false;
	}
}
void correctBoundingBox(glm::vec3* min1, glm::vec3* max1, glm::vec3* min2, glm::vec3* max2, glm::vec3* vel) {
	/**
	if (!(max1->x > min2->x && min1->x < max2->x)) { return; }
	if (!(max1->y > min2->y && min1->y < max2->y)) { return; }
	if (!(max1->z > min2->z && min1->z < max2->z)) { return; }*/
	float buffer = 0.001;
	if (max1->x > min2->x && min1->x < max2->x) {
		if (max1->y > min2->y && min1->y < max2->y) {
			//XY
			if (max1->z < min2->z  && max1->z + vel->z > min2->z) {
				min1->z = min2->z - buffer - (max1->z - min1->z);
				max1->z = min2->z - buffer;
				vel->z = 0;
			}
			if (min1->z > max2->z && min1->z + vel->z < max2->z) {
				max1->z = max2->z + buffer + (max1->z - min1->z);
				min1->z = max2->z + buffer;
				vel->z = 0;
			}
		}
		if (max1->z > min2->z && min1->z < max2->z) {
			//XZ
			if (max1->y < min2->y  && max1->y + vel->y > min2->y) {
				min1->y = min2->y - buffer - (max1->y - min1->y);
				max1->y = min2->y - buffer;
				vel->y = 0;
			}
			if (min1->y > max2->y && min1->y + vel->y < max2->y) {
				max1->y = max2->y + buffer + (max1->y - min1->y);
				min1->y = max2->y + buffer;
				vel->y = 0;
			}
		}
	}
	else {
		if (max1->y > min2->y && min1->y < max2->y && max1->z > min2->z && min1->z < max2->z) {
			//YZ
			if (max1->x < min2->x  && max1->x + vel->x > min2->x) {
				min1->x = min2->x - buffer - (max1->x - min1->x);
				max1->x = min2->x - buffer;
				vel->x = 0;
			}
			if (min1->x > max2->x && min1->x + vel->x < max2->x) {
				max1->x = max2->x + buffer + (max1->x - min1->x);
				min1->x = max2->x + buffer;
				vel->x = 0;
			}
		}
	}
}
struct primitive {
	std::vector<Vertex> verticesPX;
	std::vector<Vertex> verticesNX;
	std::vector<Vertex> verticesPY;
	std::vector<Vertex> verticesNY;
	std::vector<Vertex> verticesPZ;
	std::vector<Vertex> verticesNZ;
	std::vector<uint32_t> indices;
	std::string name;
};

enum blockType { wire, inverter, andGate, orGate, xorGate };
enum blockDirection { positiveX, negativeX, positiveY, negativeY, positiveZ, negativeZ };
struct Block { Block(int x, int y, int z, blockType _type, blockDirection _direction, VDeleter<VkDevice>* _blockDevice) { position = glm::vec3(x, y, z); type = _type; direction = _direction; blockDevice = _blockDevice; } glm::vec3 position; blockType type; blockDirection direction; VDeleter<VkDevice>* blockDevice;
VDeleter<VkBuffer> vertexBuffer{ *blockDevice, vkDestroyBuffer };
VDeleter<VkDeviceMemory> vertexBufferMemory{ *blockDevice, vkFreeMemory };
VDeleter<VkBuffer> indexBuffer{ *blockDevice, vkDestroyBuffer };
VDeleter<VkDeviceMemory> indexBufferMemory{ *blockDevice, vkFreeMemory };
};

class Blocks {
public:
	Blocks() {}

	void correctCameraWithBlocks(glm::vec3* cameraMin, glm::vec3* cameraMax, glm::vec3* cameraVel) {
		glm::vec3 test = glm::vec3(cameraMin->x, cameraMin->y, cameraMin->y);
		for (int i = 0; i < blocks.size(); i++) {
			correctBoundingBox(cameraMin, cameraMax, &blocks[i].position, &glm::vec3(blocks[i].position.x+1, blocks[i].position.y + 1, blocks[i].position.z + 1), cameraVel);
		}
	}
	VDeleter<VkDevice>* blocksDevice;
	void init(std::vector<Vertex>* _vertices, std::vector<uint32_t>* _indices) {
		vertices = _vertices;
		indices = _indices;
		//for (int i = 0; i < 200; i++) {
			addBlock(((double)rand() / (RAND_MAX)) * 120, ((double)rand() / (RAND_MAX)) * 120, ((double)rand() / (RAND_MAX)) * 120, inverter);
		//}
		/*
		for (int i = 0; i < 25600; i++) {
			addBlock(((double)rand() / (RAND_MAX)) * 400 - 600, ((double)rand() / (RAND_MAX)) * 400, ((double)rand() / (RAND_MAX)) * 400);
		}

		for (int i = 0; i < 25600; i++) {
			addBlock(((double)rand() / (RAND_MAX)) * 400 -600, ((double)rand() / (RAND_MAX)) * 400, ((double)rand() / (RAND_MAX)) * 400 - 600);
		}

		for (int i = 0; i < 25600; i++) {
			addBlock(((double)rand() / (RAND_MAX)) * 400, ((double)rand() / (RAND_MAX)) * 400, ((double)rand() / (RAND_MAX)) * 400 - 600);
		}*/
	}

	void addBlock(int x, int y, int z, blockType type) {
		if (!doesBlockExist(x, y, z)) {
			blocks.push_back(Block(x, y, z, type, positiveY, blocksDevice));
		}
	}
	void addBlock(int x, int y, int z, blockType type, blockDirection direction) {
		if (!doesBlockExist(x, y, z)) {
			blocks.push_back(Block(x, y, z, type, direction, blocksDevice));
		}
	}
	int getVectorSize() {
		return blocks.size();
	}
	bool removeBlock(int x, int y, int z) {
		if (doesBlockExist(x, y, z)) {
			int index = getBlock(x, y, z);
			blocks.erase(blocks.begin() + index);
			return true;
		}
		return false;
	}
	bool doesBlockExist(int x, int y, int z) {
		for (int i = 0; i < blocks.size(); i++) {
			if (blocks[i].position == glm::vec3(x, y, z)) {
				return true;
			}
		}
		return false;
	}
	int getBlock(int x, int y, int z) {
		for (int i = 0; i < blocks.size(); i++) {
			if (blocks[i].position == glm::vec3(x, y, z)) {
				return i;
			}
		}
		return NULL;
	}
	std::vector<Block> blocks;
private:
	std::vector<Vertex>* vertices;
	std::vector<uint32_t>* indices;

};

void addVectorsWithOffset(std::vector<Vertex>* originalVertices, std::vector<uint32_t>* originalIndices, std::vector<Vertex>* vertices, std::vector<uint32_t>* indices, glm::vec3 offset) {
	int temp = originalVertices->size();
	for (int i = 0; i < vertices->size(); i++) {
		vertices->at(i).pos += offset;
		originalVertices->push_back(vertices->at(i));
		vertices->at(i).pos -= offset;
	}
	for (int i = 0; i < indices->size(); i++) {
		originalIndices->push_back(indices->at(i) + temp);
	}
}

class WorkSpace {
public:
	void run() {
		initWindow();
		blocks.init(&vertices, &indices);
		//drawBlocks();
		initVulkan();

		mainLoop();
	}
	
private:
	GLFWwindow* window;

	VDeleter<VkInstance> instance{ vkDestroyInstance };
	VDeleter<VkDebugReportCallbackEXT> callback{ instance, DestroyDebugReportCallbackEXT };
	VDeleter<VkSurfaceKHR> surface{ instance, vkDestroySurfaceKHR };

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VDeleter<VkDevice> device{ vkDestroyDevice };

	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VDeleter<VkSwapchainKHR> swapChain{ device, vkDestroySwapchainKHR };
	std::vector<VkImage> swapChainImages;
	VkFormat swapChainImageFormat;
	VkExtent2D swapChainExtent;
	std::vector<VDeleter<VkImageView>> swapChainImageViews;
	std::vector<VDeleter<VkFramebuffer>> swapChainFramebuffers;

	VDeleter<VkRenderPass> renderPass{ device, vkDestroyRenderPass };
	VDeleter<VkDescriptorSetLayout> descriptorSetLayout{ device, vkDestroyDescriptorSetLayout };
	VDeleter<VkPipelineLayout> pipelineLayout{ device, vkDestroyPipelineLayout };
	VDeleter<VkPipeline> graphicsPipeline{ device, vkDestroyPipeline };

	VDeleter<VkCommandPool> commandPool{ device, vkDestroyCommandPool };

	VDeleter<VkImage> textureImage{ device, vkDestroyImage };
	VDeleter<VkDeviceMemory> textureImageMemory{ device, vkFreeMemory };
	VDeleter<VkImageView> textureImageView{ device, vkDestroyImageView };
	VDeleter<VkSampler> textureSampler{ device, vkDestroySampler };

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	VDeleter<VkBuffer> vertexBuffer{ device, vkDestroyBuffer };
	VDeleter<VkDeviceMemory> vertexBufferMemory{ device, vkFreeMemory };
	VDeleter<VkBuffer> indexBuffer{ device, vkDestroyBuffer };
	VDeleter<VkDeviceMemory> indexBufferMemory{ device, vkFreeMemory };

	VDeleter<VkBuffer> uniformStagingBuffer{ device, vkDestroyBuffer };
	VDeleter<VkDeviceMemory> uniformStagingBufferMemory{ device, vkFreeMemory };
	VDeleter<VkBuffer> uniformBuffer{ device, vkDestroyBuffer };
	VDeleter<VkDeviceMemory> uniformBufferMemory{ device, vkFreeMemory };

	VDeleter<VkDescriptorPool> descriptorPool{ device, vkDestroyDescriptorPool };
	VkDescriptorSet descriptorSet;

	std::vector<VkCommandBuffer> commandBuffers;

	VDeleter<VkSemaphore> imageAvailableSemaphore{ device, vkDestroySemaphore };
	VDeleter<VkSemaphore> renderFinishedSemaphore{ device, vkDestroySemaphore };

	VDeleter<VkImage> depthImage{ device, vkDestroyImage };
	VDeleter<VkDeviceMemory> depthImageMemory{ device, vkFreeMemory };

	VDeleter<VkImageView> depthImageView{ device, vkDestroyImageView };
	Blocks blocks;
	

	std::vector<primitive> primitives;

	void initWindow() {
		
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);//may need to remove this

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
		glfwSetKeyCallback(window, key_callback);
		glfwSetMouseButtonCallback(window, mouse_button_callback);
		glfwSetWindowFocusCallback(window, window_focus_callback);
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

		glfwSetWindowUserPointer(window, this);
		glfwSetWindowSizeCallback(window, WorkSpace::onWindowResized);
	}

	static void onWindowResized(GLFWwindow* window, int width, int height) {
		if (width == 0 || height == 0) return;

		WorkSpace* app = reinterpret_cast<WorkSpace*>(glfwGetWindowUserPointer(window));
		app->recreateSwapChain();
	}
	int x = 0;
	int y = 0;

	std::vector<Vertex> verticesInverterModel;
	std::vector<uint32_t> indicesInverterModel;

	std::vector<Vertex> verticesModelRotated;

	//initialises vulkan
	void initVulkan() {
		
		createInstance();
		setupDebugCallback();
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
		createSwapChain();
		createImageViews();
		createRenderPass();
		createDescriptorSetLayout();
		createGraphicsPipeline();
		createCommandPool();
		createDepthResources();
		createFramebuffers();
		createTextureImage();
		createTextureImageView();
		createTextureSampler();
		
		loadModel(&verticesInverterModel, &indicesInverterModel, "models/xyzOrigin.obj", glm::vec3(0.1, 0.9, 0.1));
		loadPrimitive("andGate", "models/AndGate.obj", glm::vec3(0.5, 0.5, 0.9));
		loadPrimitive("xorGate", "models/XorGate.obj", glm::vec3(0.5, 0.5, 0.9));
		loadPrimitive("orGate", "models/OrGate.obj", glm::vec3(0.5, 0.5, 0.9));
		loadPrimitive("wire", "models/wire2.obj", glm::vec3(0.9, 0.1, 0.1));
		loadPrimitive("wire_center", "models/wire_center.obj", glm::vec3(0.7, 0.1, 0.1));
		loadPrimitive("inverter", "models/inverter.obj", glm::vec3(0.1, 0.1, 0.9));

		Vertex temp = {};
		temp.texCoord = { 0, 0 };
		temp.pos = { 1, 1, 1 };
		temp.color = { 1, 0, 1 };
		vertices.push_back(temp);
		temp.texCoord = { 0, 0 };
		temp.pos = { 5, 1, 1 };
		temp.color = { 1, 0, 0 };
		vertices.push_back(temp);
		temp.texCoord = { 0, 0 };
		temp.pos = { 1, 5, 1 };
		temp.color = { 1, 1, 1 };
		vertices.push_back(temp);
		indices.push_back(vertices.size() - 3);
		indices.push_back(vertices.size() - 2);
		indices.push_back(vertices.size() - 1);
		//for (int i = 0; i < 500; i++) {
			//addVectors(&vertices, &indices, &verticesInverterModel, &indicesInverterModel);
		//}

		createVertexBuffer(vertices, vertexBuffer, vertexBufferMemory);
		createIndexBuffer(indices, indexBuffer, indexBufferMemory);
		createUniformBuffer();
		createDescriptorPool();
		createDescriptorSet();
		createCommandBuffers();
		createSemaphores();





		////////
		///////////
		//VkDevice* test;
		//test = device.replace();
		*blocks.blocksDevice = device;
	}
	bool drawBoxes = false;
	void drawBlocks() {
		glm::vec3 color;
		std::vector<Vertex> tempVertices;
		std::vector<uint32_t> tempIndices;
		for (int i = 0; i < blocks.blocks.size(); i++) {
			tempVertices.clear();
			tempIndices.clear();
			bool x = false;
			bool y = false;
			bool z = false;
			switch (blocks.blocks[i].type) {
			case wire:
				color = glm::vec3(0.6, 0, 0);



				//bottom Y front
				if (blocks.doesBlockExist(blocks.blocks[i].position.x, blocks.blocks[i].position.y - 1, blocks.blocks[i].position.z)) {
					addPrimitive("wire", blocks.blocks[i].position, NY, &tempVertices, &tempIndices);
					y = true;
				}
				//top Y back
				if (blocks.doesBlockExist(blocks.blocks[i].position.x, blocks.blocks[i].position.y + 1, blocks.blocks[i].position.z)) {
					addPrimitive("wire", blocks.blocks[i].position, PY, &tempVertices, &tempIndices);
					y = true;
				}
				//Z front
				if (blocks.doesBlockExist(blocks.blocks[i].position.x, blocks.blocks[i].position.y, blocks.blocks[i].position.z - 1)) {
					addPrimitive("wire", blocks.blocks[i].position, NZ, &tempVertices, &tempIndices);
					z = true;
				}
				//Z back
				if (blocks.doesBlockExist(blocks.blocks[i].position.x, blocks.blocks[i].position.y, blocks.blocks[i].position.z + 1)) {
					addPrimitive("wire", blocks.blocks[i].position, PZ, &tempVertices, &tempIndices);
					z = true;
				}
				//X front
				if (blocks.doesBlockExist(blocks.blocks[i].position.x - 1, blocks.blocks[i].position.y, blocks.blocks[i].position.z)) {
					addPrimitive("wire", blocks.blocks[i].position, NX, &tempVertices, &tempIndices);
					x = true;
				}
				//X back
				if (blocks.doesBlockExist(blocks.blocks[i].position.x + 1, blocks.blocks[i].position.y, blocks.blocks[i].position.z)) {
					addPrimitive("wire", blocks.blocks[i].position, PX, &tempVertices, &tempIndices);
					x = true;
				}
				if ( !x && !y && !z) {
					addPrimitive("wire_center", blocks.blocks[i].position, &tempVertices, &tempIndices);
				}
				//addPrimitive("wire", blocks.blocks[i].position);
				break;
			case inverter:
				switch (blocks.blocks[i].direction) {
				case positiveX:
					addPrimitive("inverter", blocks.blocks[i].position, NX, &tempVertices, &tempIndices);
					break;
				case negativeX:
					addPrimitive("inverter", blocks.blocks[i].position, PX, &tempVertices, &tempIndices);
					break;
				case positiveY:
					addPrimitive("inverter", blocks.blocks[i].position, NY, &tempVertices, &tempIndices);
					break;
				case negativeY:
					addPrimitive("inverter", blocks.blocks[i].position, PY, &tempVertices, &tempIndices);
					break;
				case positiveZ:
					addPrimitive("inverter", blocks.blocks[i].position, NZ, &tempVertices, &tempIndices);
					break;
				case negativeZ:
					addPrimitive("inverter", blocks.blocks[i].position, PZ, &tempVertices, &tempIndices);
					break;

				}
				break;
			case andGate:
				switch (blocks.blocks[i].direction) {
				case positiveX:
					addPrimitive("andGate", blocks.blocks[i].position, NX, &tempVertices, &tempIndices);
					break;
				case negativeX:
					addPrimitive("andGate", blocks.blocks[i].position, PX, &tempVertices, &tempIndices);
					break;
				case positiveY:
					addPrimitive("andGate", blocks.blocks[i].position, NY, &tempVertices, &tempIndices);
					break;
				case negativeY:
					addPrimitive("andGate", blocks.blocks[i].position, PY, &tempVertices, &tempIndices);
					break;
				case positiveZ:
					addPrimitive("andGate", blocks.blocks[i].position, NZ, &tempVertices, &tempIndices);
					break;
				case negativeZ:
					addPrimitive("andGate", blocks.blocks[i].position, PZ, &tempVertices, &tempIndices);
					break;

				}
				color = glm::vec3(0, 0, 0.8);
				break;
			case orGate:
				switch (blocks.blocks[i].direction) {
				case positiveX:
					addPrimitive("orGate", blocks.blocks[i].position, NX, &tempVertices, &tempIndices);
					break;
				case negativeX:
					addPrimitive("orGate", blocks.blocks[i].position, PX, &tempVertices, &tempIndices);
					break;
				case positiveY:
					addPrimitive("orGate", blocks.blocks[i].position, NY, &tempVertices, &tempIndices);
					break;
				case negativeY:
					addPrimitive("orGate", blocks.blocks[i].position, PY, &tempVertices, &tempIndices);
					break;
				case positiveZ:
					addPrimitive("orGate", blocks.blocks[i].position, NZ, &tempVertices, &tempIndices);
					break;
				case negativeZ:
					addPrimitive("orGate", blocks.blocks[i].position, PZ, &tempVertices, &tempIndices);
					break;

				}
				color = glm::vec3(0, 0, 0.8);
				break;
			case xorGate:
				switch (blocks.blocks[i].direction) {
				case positiveX:
					addPrimitive("xorGate", blocks.blocks[i].position, NX, &tempVertices, &tempIndices);
					break;
				case negativeX:
					addPrimitive("xorGate", blocks.blocks[i].position, PX, &tempVertices, &tempIndices);
					break;
				case positiveY:
					addPrimitive("xorGate", blocks.blocks[i].position, NY, &tempVertices, &tempIndices);
					break;
				case negativeY:
					addPrimitive("xorGate", blocks.blocks[i].position, PY, &tempVertices, &tempIndices);
					break;
				case positiveZ:
					addPrimitive("xorGate", blocks.blocks[i].position, NZ, &tempVertices, &tempIndices);
					break;
				case negativeZ:
					addPrimitive("xorGate", blocks.blocks[i].position, PZ, &tempVertices, &tempIndices);
					break;

				}
				color = glm::vec3(0, 0, 0.8);
				break;
			}
			/*
			if (blocks[i].type == wire) {
			addVectorsWithOffset()
			}
			*/
			//DO THIS WITH 8 VERTICES
			//HAVE IFS FOR ADDING INDICES

			/*if (drawBoxes) {

				//ADDING VERTICES:
				Vertex temp = {};
				temp.texCoord = { 0, 0 };
				temp.color = { color.r + 0.3, color.g + 0.3, color.b + 0.3 };
				temp.pos = blocks.blocks[i].position + glm::vec3(0, 0, 0);
				vertices.push_back(temp);
				temp.color = { color.r, color.g, color.b };
				temp.pos = blocks.blocks[i].position + glm::vec3(1, 0, 0); //
				vertices.push_back(temp);
				temp.pos = blocks.blocks[i].position + glm::vec3(1, 0, 1); //
				vertices.push_back(temp);
				temp.pos = blocks.blocks[i].position + glm::vec3(0, 0, 1);
				vertices.push_back(temp);
				temp.pos = blocks.blocks[i].position + glm::vec3(0, 1, 0);
				vertices.push_back(temp);
				temp.pos = blocks.blocks[i].position + glm::vec3(1, 1, 0); //
				vertices.push_back(temp);
				temp.color = { color.r - 0.3, color.g - 0.3, color.b - 0.3 };
				temp.pos = blocks.blocks[i].position + glm::vec3(1, 1, 1); //

				vertices.push_back(temp);
				temp.color = { color.r, color.g, color.b };
				temp.pos = blocks.blocks[i].position + glm::vec3(0, 1, 1);
				vertices.push_back(temp);

				//ADDING INDICES:
				//bottom Y front
				if (!blocks.doesBlockExist(blocks.blocks[i].position.x, blocks.blocks[i].position.y - 1, blocks.blocks[i].position.z)) {
					addFace(vertices.size() - 8, vertices.size() - 7, vertices.size() - 6, vertices.size() - 5);
				}
				//top Y back
				if (!blocks.doesBlockExist(blocks.blocks[i].position.x, blocks.blocks[i].position.y + 1, blocks.blocks[i].position.z)) {
					addFace(vertices.size() - 1, vertices.size() - 2, vertices.size() - 3, vertices.size() - 4);
				}
				//Z back
				if (!blocks.doesBlockExist(blocks.blocks[i].position.x, blocks.blocks[i].position.y, blocks.blocks[i].position.z + 1)) {
					addFace(vertices.size() - 5, vertices.size() - 6, vertices.size() - 2, vertices.size() - 1);
				}
				//Z front
				if (!blocks.doesBlockExist(blocks.blocks[i].position.x, blocks.blocks[i].position.y, blocks.blocks[i].position.z - 1)) {
					addFace(vertices.size() - 4, vertices.size() - 3, vertices.size() - 7, vertices.size() - 8);
				}
				//X front
				if (!blocks.doesBlockExist(blocks.blocks[i].position.x - 1, blocks.blocks[i].position.y, blocks.blocks[i].position.z)) {
					addFace(vertices.size() - 8, vertices.size() - 5, vertices.size() - 1, vertices.size() - 4);
				}
				//X back
				if (!blocks.doesBlockExist(blocks.blocks[i].position.x + 1, blocks.blocks[i].position.y, blocks.blocks[i].position.z)) {
					addFace(vertices.size() - 3, vertices.size() - 2, vertices.size() - 6, vertices.size() - 7);
				}
			}*/
			createVertexBuffer(tempVertices, blocks.blocks[i].vertexBuffer, blocks.blocks[i].vertexBufferMemory);
			createIndexBuffer(tempIndices, blocks.blocks[i].indexBuffer, blocks.blocks[i].indexBufferMemory);
		}
	}

	void addFace(int p1, int p2, int p3, int p4) {
		indices.push_back(p1);
		indices.push_back(p2);
		indices.push_back(p3);
		indices.push_back(p1);
		indices.push_back(p3);
		indices.push_back(p4);
	}



	void addVectors(std::vector<Vertex>* originalVertices, std::vector<uint32_t>* originalIndices, std::vector<Vertex>* vertices, std::vector<uint32_t>* indices) {
		int temp = originalVertices->size();
		for (int i = 0; i < vertices->size(); i++) {
			originalVertices->push_back(vertices->at(i));
		}
		for (int i = 0; i < indices->size(); i++) {
			originalIndices->push_back(indices->at(i) + temp);
		}
	}
	enum orientation {PX, NX, PY, NY, PZ, NZ};
	void addPrimitive(std::string name, glm::vec3 offset, orientation _orientation, std::vector<Vertex>* _vertices, std::vector<uint32_t>* _indices) {
		
		

		for (int i = 0; i < primitives.size(); i++) {
			if (primitives[i].name == name) {
			std::vector<Vertex>* vector =&primitives[i].verticesPX;
			switch (_orientation) {
			case PX:
				vector = &primitives[i].verticesPX;
				break;
			case NX:
				vector = &primitives[i].verticesNX;
				break;
			case PY:
				vector = &primitives[i].verticesPY;
				break;
			case NY:
				vector = &primitives[i].verticesNY;
				break;
			case PZ:
				vector = &primitives[i].verticesPZ;
				break;
			case NZ:
				vector = &primitives[i].verticesNZ;
				break;
			}
				addVectorsWithOffset(_vertices, _indices, vector, &primitives[i].indices, offset);
			}
		}
	}
	void addPrimitive(std::string name, glm::vec3 offset, std::vector<Vertex>* _vertices, std::vector<uint32_t>* _indices) {
		for (int i = 0; i < primitives.size(); i++) {
			if (primitives[i].name == name) {
				addVectorsWithOffset(_vertices, _indices, &primitives[i].verticesPX, &primitives[i].indices, offset);
			}
		}
	}

	 void loadPrimitive(std::string name, std::string path, glm::vec3 colour) {
		primitive temp;
		loadModel(&temp.verticesPX, &temp.indices, path, colour);
		temp.name = name;
		temp = rotatePrimitive(temp);
		primitives.push_back(temp);
	}
	 primitive rotatePrimitive(primitive _primitive) {
		 float PI = 3.1415926;
		 float angle = PI;
		 for (int i = 0; i < _primitive.verticesPX.size(); i++) {
			 _primitive.verticesPX[i].pos -= glm::vec3(0.5, 0.5, 0.5);
		 }
		 _primitive.verticesNX = _primitive.verticesPX;
		 _primitive.verticesNY = _primitive.verticesPX;
		 _primitive.verticesPY = _primitive.verticesPX;
		 _primitive.verticesNZ = _primitive.verticesPX;
		 _primitive.verticesPZ = _primitive.verticesPX;
		 for (int i = 0; i < _primitive.verticesPX.size(); i++) {
			 _primitive.verticesNX[i].pos = glm::rotateY(_primitive.verticesPX[i].pos, angle);
			 _primitive.verticesNX[i].pos += glm::vec3(0.5, 0.5, 0.5);
		 }
 		 angle = PI * 1.5;
		 for (int i = 0; i < _primitive.verticesPX.size(); i++) {
			 _primitive.verticesPZ[i].pos = glm::rotateY(_primitive.verticesPX[i].pos, angle);
			 _primitive.verticesPZ[i].pos += glm::vec3(0.5, 0.5, 0.5);
		 }
		 angle = PI / 2;
		 for (int i = 0; i < _primitive.verticesPX.size(); i++) {
			 _primitive.verticesNZ[i].pos = glm::rotateY(_primitive.verticesPX[i].pos, angle);
			 _primitive.verticesNZ[i].pos += glm::vec3(0.5, 0.5, 0.5);
		 }
		 angle = PI/2;
		 for (int i = 0; i < _primitive.verticesPX.size(); i++) {
			 _primitive.verticesPY[i].pos = glm::rotateZ(_primitive.verticesPX[i].pos, angle);
			 _primitive.verticesPY[i].pos += glm::vec3(0.5, 0.5, 0.5);
		 }
		 angle = PI * 1.5;
		 for (int i = 0; i < _primitive.verticesPX.size(); i++) {
			 _primitive.verticesNY[i].pos = glm::rotateZ(_primitive.verticesPX[i].pos, angle);
			 _primitive.verticesNY[i].pos += glm::vec3(0.5, 0.5, 0.5);
		 }
		 for (int i = 0; i < _primitive.verticesPX.size(); i++) {
			 _primitive.verticesPX[i].pos += glm::vec3(0.5, 0.5, 0.5);
		 }
		 return _primitive;
	}
	void loadModel(std::vector<Vertex>* _vertices, std::vector<uint32_t>* _indices, std::string path, glm::vec3 colour) {
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.c_str())) {
			throw std::runtime_error(err);
		}

		std::unordered_map<Vertex, int> uniqueVertices = {};

		for (const auto& shape : shapes) {
			for (const auto& index : shape.mesh.indices) {
				Vertex vertex = {};

				vertex.pos = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]
				};

				vertex.texCoord = {0,0};// {attrib.texcoords[2 * index.texcoord_index + 0],1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};

				vertex.color = { colour.r -0.2 + 0.4*((double)rand() / (RAND_MAX)), colour.g - 0.2 + 0.4*((double)rand() / (RAND_MAX)), colour.b - 0.2 + 0.4*((double)rand() / (RAND_MAX)) };

				if (uniqueVertices.count(vertex) == 0) {
					uniqueVertices[vertex] = _vertices->size();
					_vertices->push_back(vertex);
				}

				_indices->push_back(uniqueVertices[vertex]);
			}
		}

	}

	void createDepthResources() {
		VkFormat depthFormat = findDepthFormat();

		createImage(swapChainExtent.width, swapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
		createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, depthImageView);

		transitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}

	VkFormat findDepthFormat() {
		return findSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
	}

	VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
		for (VkFormat format : candidates) {
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
				return format;
			}
			else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
				return format;
			}
		}

		throw std::runtime_error("failed to find supported format!");
	}

	void createTextureSampler() {
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = 16;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		if (vkCreateSampler(device, &samplerInfo, nullptr, textureSampler.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create texture sampler!");
		}
	}

	void createTextureImageView() {
		createImageView(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, textureImageView);
	}

	void createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VDeleter<VkImageView>& imageView) {
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &viewInfo, nullptr, imageView.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create texture image view!");
		}
	}

	void createTextureImage() {
		int texWidth, texHeight, texChannels;
		stbi_uc* pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		VkDeviceSize imageSize = texWidth * texHeight * 4;

		if (!pixels) {
			throw std::runtime_error("failed to load texture image!");
		}

		VDeleter<VkImage> stagingImage{ device, vkDestroyImage };
		VDeleter<VkDeviceMemory> stagingImageMemory{ device, vkFreeMemory };
		createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingImage, stagingImageMemory);

		VkImageSubresource subresource = {};
		subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresource.mipLevel = 0;
		subresource.arrayLayer = 0;

		VkSubresourceLayout stagingImageLayout;
		vkGetImageSubresourceLayout(device, stagingImage, &subresource, &stagingImageLayout);

		void* data;
		vkMapMemory(device, stagingImageMemory, 0, imageSize, 0, &data);

		if (stagingImageLayout.rowPitch == texWidth * 4) {
			memcpy(data, pixels, (size_t)imageSize);
		}
		else {
			uint8_t* dataBytes = reinterpret_cast<uint8_t*>(data);

			for (int y = 0; y < texHeight; y++) {
				memcpy(&dataBytes[y * stagingImageLayout.rowPitch], &pixels[y * texWidth * 4], texWidth * 4);
			}
		}

		vkUnmapMemory(device, stagingImageMemory);

		stbi_image_free(pixels);

		createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

		transitionImageLayout(stagingImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		copyImage(stagingImage, textureImage, texWidth, texHeight);

		transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	//creating the image object
	void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VDeleter<VkImage>& image, VDeleter<VkDeviceMemory>& imageMemory) {
		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = tiling;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		imageInfo.usage = usage;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device, &imageInfo, nullptr, image.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create image!");
		}

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

		if (vkAllocateMemory(device, &allocInfo, nullptr, imageMemory.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate image memory!");
		}

		vkBindImageMemory(device, image, imageMemory, 0);
	}

	void copyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height) {
		VkCommandBuffer commandBuffer = beginSingleTimeCommands();

		VkImageSubresourceLayers subResource = {};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subResource.baseArrayLayer = 0;
		subResource.mipLevel = 0;
		subResource.layerCount = 1;

		VkImageCopy region = {};
		region.srcSubresource = subResource;
		region.dstSubresource = subResource;
		region.srcOffset = { 0, 0, 0 };
		region.dstOffset = { 0, 0, 0 };
		region.extent.width = width;
		region.extent.height = height;
		region.extent.depth = 1;

		vkCmdCopyImage(
			commandBuffer,
			srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region
		);

		endSingleTimeCommands(commandBuffer);
	}

	bool hasStencilComponent(VkFormat format) {
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
		VkCommandBuffer commandBuffer = beginSingleTimeCommands();

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

			if (hasStencilComponent(format)) {
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
		else {
			throw std::invalid_argument("unsupported layout transition!");
		}

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		endSingleTimeCommands(commandBuffer);
	}

	VkCommandBuffer beginSingleTimeCommands() {
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = commandPool;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		return commandBuffer;
	}

	void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(graphicsQueue);

		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
	}

	void createDescriptorSet() {
		VkDescriptorSetLayout layouts[] = { descriptorSetLayout };
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = layouts;

		if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate descriptor set!");
		}

		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = uniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = textureImageView;
		imageInfo.sampler = textureSampler;

		std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descriptorSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = descriptorSet;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
	}

	void createDescriptorPool() {
		std::array<VkDescriptorPoolSize, 2> poolSizes = {};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = poolSizes.size();
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, descriptorPool.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create descriptor pool!");
		}
	}

	void createUniformBuffer() {
		VkDeviceSize bufferSize = sizeof(UniformBufferObject);

		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformStagingBuffer, uniformStagingBufferMemory);
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, uniformBuffer, uniformBufferMemory);
	}

	void createDescriptorSetLayout() {
		VkDescriptorSetLayoutBinding uboLayoutBinding = {};
		uboLayoutBinding.binding = 0;
		uboLayoutBinding.descriptorCount = 1;
		uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboLayoutBinding.pImmutableSamplers = nullptr;
		uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
		samplerLayoutBinding.binding = 1;
		samplerLayoutBinding.descriptorCount = 1;
		samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding.pImmutableSamplers = nullptr;
		samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindings.size();
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, descriptorSetLayout.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create descriptor set layout!");
		}
	}

	//creating the index buffer
	void createIndexBuffer(std::vector<uint32_t> _indices, VDeleter<VkBuffer>& _indexBuffer, VDeleter<VkDeviceMemory>& _indexBufferMemory) {
		VkDeviceSize bufferSize = sizeof(_indices[0]) * _indices.size();

		VDeleter<VkBuffer> stagingBuffer{ device, vkDestroyBuffer };
		VDeleter<VkDeviceMemory> stagingBufferMemory{ device, vkFreeMemory };
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

		void* data;
		vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, _indices.data(), (size_t)bufferSize);
		vkUnmapMemory(device, stagingBufferMemory);

		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _indexBuffer, _indexBufferMemory);

		copyBuffer(stagingBuffer, _indexBuffer, bufferSize);
	}
	void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VDeleter<VkBuffer>& buffer, VDeleter<VkDeviceMemory>& bufferMemory) {
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &bufferInfo, nullptr, buffer.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create buffer!");
		}

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

		if (vkAllocateMemory(device, &allocInfo, nullptr, bufferMemory.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate buffer memory!");
		}

		vkBindBufferMemory(device, buffer, bufferMemory, 0);
	}

	void createVertexBuffer(std::vector<Vertex> _vertices, VDeleter<VkBuffer>& _vertexBuffer, VDeleter<VkDeviceMemory>& _vertexBufferMemory) {
		VkDeviceSize bufferSize = sizeof(_vertices[0]) * _vertices.size();

		VDeleter<VkBuffer> stagingBuffer{ device, vkDestroyBuffer };
		VDeleter<VkDeviceMemory> stagingBufferMemory{ device, vkFreeMemory };
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

		void* data;
		vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, _vertices.data(), (size_t)bufferSize);
		vkUnmapMemory(device, stagingBufferMemory);

		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _vertexBuffer, _vertexBufferMemory);

		copyBuffer(stagingBuffer, _vertexBuffer, bufferSize);
	}

	void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = commandPool;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0; // Optional
		copyRegion.dstOffset = 0; // Optional
		copyRegion.size = size;
		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(graphicsQueue);

		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
	}
	uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}

		throw std::runtime_error("failed to find suitable memory type!");
	}

	//re-creating the swap chain for when it becomes incompatible, like when the window is resized
	void recreateSwapChain() {
		vkDeviceWaitIdle(device);

		createSwapChain();
		createImageViews();
		createRenderPass();
		createGraphicsPipeline();
		createDepthResources();
		createFramebuffers();
		createCommandBuffers();
	}

	void createSemaphores() {
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, imageAvailableSemaphore.replace()) != VK_SUCCESS ||
			vkCreateSemaphore(device, &semaphoreInfo, nullptr, renderFinishedSemaphore.replace()) != VK_SUCCESS) {

			throw std::runtime_error("failed to create semaphores!");
		}
	}
	//allocates and records commands for every swapchain image
	void createCommandBuffers() {
		if (commandBuffers.size() > 0) {
			//vkFreeCommandBuffers(device, commandPool, commandBuffers.size(), commandBuffers.data());
		}

		commandBuffers.resize(swapChainFramebuffers.size());

		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

		if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate command buffers!");
		}

		for (size_t i = 0; i < commandBuffers.size(); i++) {
			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

			vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = swapChainFramebuffers[i];
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = swapChainExtent;

			std::array<VkClearValue, 2> clearValues = {};
			clearValues[0].color = { 0.9f, 0.9f, 0.9f, 1.0f };
			clearValues[1].depthStencil = { 1.0f, 0 };

			renderPassInfo.clearValueCount = clearValues.size();
			renderPassInfo.pClearValues = clearValues.data();

			vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

			//ADD MORE VERTEX BUFFERS HERE
			VkBuffer vertexBuffers[] = { vertexBuffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

			vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

			vkCmdDrawIndexed(commandBuffers[i], indices.size(), 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffers[i]);

			if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
				throw std::runtime_error("failed to record command buffer!");
			}
		}
	}
	//manages the memory that is used to store command buffers
	void createCommandPool() {
		QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
		poolInfo.flags = 0; // Optional

		if (vkCreateCommandPool(device, &poolInfo, nullptr, commandPool.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create command pool!");
		}
	}

	void createFramebuffers() {
		swapChainFramebuffers.resize(swapChainImageViews.size(), VDeleter<VkFramebuffer>{device, vkDestroyFramebuffer});

		for (size_t i = 0; i < swapChainImageViews.size(); i++) {
			std::array<VkImageView, 2> attachments = {
				swapChainImageViews[i],
				depthImageView
			};

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = attachments.size();
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = swapChainExtent.width;
			framebufferInfo.height = swapChainExtent.height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, swapChainFramebuffers[i].replace()) != VK_SUCCESS) {
				throw std::runtime_error("failed to create framebuffer!");
			}
		}
	}

	void createRenderPass() {
		VkAttachmentDescription colorAttachment = {};
		colorAttachment.format = swapChainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depthAttachment = {};
		depthAttachment.format = findDepthFormat();
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorAttachmentRef = {};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentRef = {};
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pDepthStencilAttachment = &depthAttachmentRef;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = attachments.size();
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, renderPass.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create render pass!");
		}
	}

	void createGraphicsPipeline() {
		//loading vertex and fragment shaders
		auto vertShaderCode = readFile("shaders/vert.spv");
		auto fragShaderCode = readFile("shaders/frag.spv");
		//only required in the pipeling creation proccess, so defined here instead of as a property of this class
		VDeleter<VkShaderModule> vertShaderModule{ device, vkDestroyShaderModule };
		VDeleter<VkShaderModule> fragShaderModule{ device, vkDestroyShaderModule };

		createShaderModule(vertShaderCode, vertShaderModule);
		createShaderModule(fragShaderCode, fragShaderModule);

		VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;

		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		auto bindingDescription = Vertex::getBindingDescription();
		auto attributeDescriptions = Vertex::getAttributeDescriptions();

		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)swapChainExtent.width;
		viewport.height = (float)swapChainExtent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = swapChainExtent;

		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		//multisampling
		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional
		//depth stencil
		VkPipelineDepthStencilStateCreateInfo depthStencil = {};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		//color blending
		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

		VkPipelineColorBlendStateCreateInfo colorBlending = {};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		VkDescriptorSetLayout setLayouts[] = { descriptorSetLayout };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = setLayouts;

		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
			pipelineLayout.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create pipeline layout!");
		}

		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineInfo.basePipelineIndex = -1; // Optional

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, graphicsPipeline.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create graphics pipeline!");
		}

	}
	//wrapping the shader code in a VkShaderModule object
	void createShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule) {
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = (uint32_t*)code.data();

		if (vkCreateShaderModule(device, &createInfo, nullptr, shaderModule.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create shader module!");
		}

	}
	void deleteBlock() {
		glm::vec3 temp = getSelectedBlock();
		blocks.removeBlock(temp.x, temp.y, temp.z);
		verticesChanged = true;
	}
	glm::vec3 getSelectedBlock() {
		glm::vec3 temp = cameraPosition;
		float radius = 150;
		for (int i = 0; i < radius; i++) {
			cameraPosition += glm::vec3(direction.x / 25, direction.y / 25, direction.z / 25);
			if (blocks.getBlock(cameraPosition.x, cameraPosition.y, cameraPosition.z)) {
				return glm::vec3(floor(cameraPosition.x), floor(cameraPosition.y), floor(cameraPosition.z));
			}
		}
		return glm::vec3(NULL, NULL, NULL);
	}
	glm::vec3 getLineFaceIntersect(glm::vec3 min, glm::vec3 max, glm::vec3 origin, glm::vec3 direction, int orientation) {
		//orientation 1: XZ, 2: ZY, 3: XY
		direction *= 100;
		direction += origin;
		glm::vec3 intersection;
		switch (orientation) {
		case 1:
			intersection.y = min.y;
			intersection.x = origin.x + abs(direction.x - origin.x)*((min.y - origin.y) / (direction.y - origin.y));
			intersection.z = origin.z + abs(direction.z - origin.z)*((min.y - origin.y) / (direction.y - origin.y));

			if (intersection.x <= max.x && intersection.x >= min.x && intersection.z <= max.z && intersection.z >= min.z) {
				return intersection;
			}
			break;
		case 2:
			intersection.x = min.x;
			intersection.y = origin.y + abs(direction.y - origin.y)*((min.x - origin.x) / (direction.x - origin.x));
			intersection.z = origin.z + abs(direction.z - origin.z)*((min.x - origin.x) / (direction.x - origin.x));

			if (intersection.y <= max.y && intersection.y >= min.y && intersection.z <= max.z && intersection.z >= min.z) {
				return intersection;
			}
			break;
		case 3:
			intersection.z = min.z;
			intersection.x = origin.x + abs(direction.x - origin.x)*((min.z - origin.z) / (direction.z - origin.z));
			intersection.y = origin.y + abs(direction.y - origin.y)*((min.z - origin.z) / (direction.z - origin.z));

			if (intersection.x <= max.x && intersection.x >= min.x && intersection.y <= max.y && intersection.y >= min.y) {
				return intersection;
			}
			break;
		}
		return glm::vec3(NULL, NULL, NULL);
	}
	float get3DDistance(glm::vec3 p1, glm::vec3 p2) {
		if (p1 == glm::vec3(NULL, NULL, NULL) || p2 == glm::vec3(NULL, NULL, NULL)) {
			return NULL;
		}
		return sqrt(pow((p1.x - p2.x), 2) + pow((p1.y - p2.y), 2) + pow((p1.z - p2.z), 2));
	}
	int getSide(glm::vec3 block) {
		std::vector<glm::vec3> intersections;
		//top XZ
		glm::vec3 min = glm::vec3(block.x, block.y + 1, block.z);
		glm::vec3 max = glm::vec3(block.x + 1, block.y + 1, block.z + 1);
		intersections.push_back(getLineFaceIntersect(min, max, cameraPosition, direction, 1));
		//bottom XZ
		min = glm::vec3(block.x, block.y, block.z);
		max = glm::vec3(block.x + 1, block.y, block.z + 1);
		intersections.push_back(getLineFaceIntersect(min, max, cameraPosition, direction, 1));
		//back ZY
		min = glm::vec3(block.x + 1, block.y, block.z);
		max = glm::vec3(block.x + 1, block.y + 1, block.z + 1);
		intersections.push_back(getLineFaceIntersect(min, max, cameraPosition, direction, 2));
		//front ZY
		min = glm::vec3(block.x, block.y, block.z);
		max = glm::vec3(block.x, block.y + 1, block.z + 1);
		intersections.push_back(getLineFaceIntersect(min, max, cameraPosition, direction, 2));
		//back XY
		min = glm::vec3(block.x, block.y, block.z + 1);
		max = glm::vec3(block.x + 1, block.y + 1, block.z + 1);
		intersections.push_back(getLineFaceIntersect(min, max, cameraPosition, direction, 3));
		//front XY
		min = glm::vec3(block.x, block.y, block.z);
		max = glm::vec3(block.x + 1, block.y + 1, block.z);
		intersections.push_back(getLineFaceIntersect(min, max, cameraPosition, direction, 3));

		int smallestDistanceIndex = 0;
		float smallestDistance = 1000000000;
		for (int i = 0; i < intersections.size(); i++) {
			float temp = get3DDistance(cameraPosition, intersections[i]);
			if (temp < smallestDistance && temp != NULL) {
				smallestDistance = temp;
				smallestDistanceIndex = i;
			}
		}
		if (smallestDistance == 1000000000) {
			return NULL;
		}
		return smallestDistanceIndex;
		//top XZ: 0
		//bottom XZ: 1
		//back ZY: 2
		//front ZY: 3
		//back XY: 4
		//front XY: 5
	}
	void placeBlock() {
		glm::vec3 block = getSelectedBlock();
		int face = getSide(block);
		//FACES:
		//top XZ: 0
		//bottom XZ: 1
		//back ZY: 2
		//front ZY: 3
		//back XY: 4
		//front XY: 5
		std::cout << face << std::endl;
 		switch (face) {
		case 0:
			blocks.addBlock(block.x, block.y+1, block.z, blockSelected, positiveY);
			break;
		case 1:
			blocks.addBlock(block.x, block.y-1, block.z, blockSelected, negativeY);
			break;
		case 2:
			blocks.addBlock(block.x+1, block.y, block.z, blockSelected, positiveX);
			break;
		case 3:
			blocks.addBlock(block.x-1, block.y, block.z, blockSelected, negativeX);
			break;
		case 4:
			blocks.addBlock(block.x, block.y, block.z+1, blockSelected, positiveZ);
			break;
		case 5:
			blocks.addBlock(block.x, block.y, block.z-1, blockSelected, negativeZ);
			break;
		}
		verticesChanged = true;
	}
	float velocity = 0.01;
	double xpos, ypos;
	glm::vec3 cameraMin = glm::vec3(-0.15, -0.25, -0.15);
	glm::vec3 cameraMax = glm::vec3(0.15, 0.7, 0.15);
	glm::vec3 cameraOffset = glm::vec3(0.15, 0.85, 0.15);
	glm::vec3 cameraVel = glm::vec3(0, 0, 0);
	float gVel = 0;
	bool t = false;
	float time = 0;
	bool mouseLeftLatch;
	bool mouseRightLatch;
	blockType blockSelected = wire;
	void mainLoop() {
		while (!glfwWindowShouldClose(window)) {
			
			//checks for window events (x button, etc)	
			glfwPollEvents();
			
			updateUniformBuffer();
			
			if (keys.n1) {
				blockSelected = wire;
			}
			else if (keys.n2) {
				blockSelected = inverter;
			}
			else if (keys.n3) {
				blockSelected = andGate;
			}
			else if (keys.n4) {
				blockSelected = orGate;
			}
			else if (keys.n5) {
				blockSelected = xorGate;
			}
			
			
			
			
			if (cameraVel.y != gVel) {
				if (t == true) {
					gVel = 0;
				}
			}
			gVel -= 0.006;
			for (int i = 0; i < 1; i++) {
				//blocks.addBlock(((double)rand() / (RAND_MAX)) * 120, ((double)rand() / (RAND_MAX)) * 120, ((double)rand() / (RAND_MAX)) * 120);
			}

			if (keys.ctrl) {
				velocity = 0.1;
			}
			else {
				velocity = 0.02;
			}
			t = false;
			if (keys.space) {
				cameraVel.y = velocity;
				gVel = 0;
			}
			else if (keys.shift) {
				cameraVel.y = -velocity;
				gVel = 0;
			}
			else {
				t = true;
				cameraVel.y = gVel;
				cameraVel.y = 0;
			}
			glm::vec3 forward = glm::vec3(sin(cameraAngle.x), 0, cos(cameraAngle.x));
			cameraVel.x *= 0.35;
			cameraVel.z *= 0.35;
			if (keys.w && !keys.s) {
				cameraVel += glm::vec3(forward.x, 0, forward.z) * velocity;
			}
			else if (keys.s) {
				cameraVel += glm::vec3(forward.x, 0, forward.z) * -velocity;
			}
			
			if (keys.a && !keys.d) {
				cameraVel += -right * velocity;
			}
			else if (keys.d) {
				cameraVel += right * velocity;;
			}

			if (keys.tab) {
				drawBoxes = true;
				//FOV = 20;
			}
			else {
				drawBoxes = false;
				//FOV = 75;
			}

			if (keys.mouseLeft) {
				if (mouseLeftLatch) {
					deleteBlock();
				}
				mouseLeftLatch = false;
			}
			else {
				mouseLeftLatch = true;
			}
			if (keys.mouseRight) {
				if (mouseRightLatch) {
					placeBlock();
				}
				mouseRightLatch = false;
			}
			else {
				mouseRightLatch = true;
			}

			if (keys.f) {
				//std::cout << "BLOCKS: " << blocks.getVectorSize() << std::endl;
				verticesChanged = true;
				blocks.addBlock(cameraMin.x, cameraMin.y - 1, cameraMin.z, blockSelected);
			}
			
			glm::vec3 test = cameraMin;
			//blocks.correctCameraWithBlocks(&cameraMin, &cameraMax, &cameraVel);
			cameraMax += cameraVel;
			cameraMin += cameraVel;
			cameraPosition = cameraMin + cameraOffset;
			if (inFocus) {
				glfwGetCursorPos(window, &xpos, &ypos);
				glfwSetCursorPos(window, WIDTH / 2, HEIGHT / 2);
				cameraAngle.x += 0.003 * float(WIDTH / 2 - xpos);
				cameraAngle.y += 0.003 * float(HEIGHT / 2 - ypos);
				if (cameraAngle.y < -3.14 / 2) {
					cameraAngle.y = -3.14 / 2;
				}
				if (cameraAngle.y > 3.14 / 2) {
					cameraAngle.y = 3.14 / 2;
				}
			}
			drawFrame();
			vkDeviceWaitIdle(device);
			//std::cout << "temp: " << temp << ", time: " << time << ", delay: " << delay << ", temp - time: " << temp - time << std::endl;
			float delay = 1.0f / 60.0f;
			float temp = glfwGetTime();
			
			
			if (temp - time < delay) {
				std::this_thread::sleep_for(std::chrono::milliseconds(int(1000 * (delay - (temp - time)))));
				
			}
			else {
				std::cout << "FRAME DROP" << std::endl;
				std::cout << "BLOCKS: " << blocks.getVectorSize() << std::endl;
			}
			time = glfwGetTime();
		}
	}

	glm::vec3 direction;

	glm::vec3 right;

	glm::vec3 up;
	
	float FOV = 90;

	void updateUniformBuffer() {
		static auto startTime = std::chrono::high_resolution_clock::now();

		auto currentTime = std::chrono::high_resolution_clock::now();
		float time = 0;// std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() / 1000.0f;

		direction = glm::vec3(
			cos(cameraAngle.y) * sin(cameraAngle.x),
			sin(cameraAngle.y),
			cos(cameraAngle.y) * cos(cameraAngle.x)
		);

		right = glm::vec3(
			sin(cameraAngle.x - 3.14f / 2.0f),
			0,
			cos(cameraAngle.x - 3.14f / 2.0f)
		);

		up = glm::cross(right, direction);

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(), 0.0f, glm::vec3(0, 0, 1));
		ubo.view = glm::lookAt(cameraPosition, cameraPosition + direction, up);
		ubo.proj = glm::perspective(glm::radians(FOV), swapChainExtent.width / (float)swapChainExtent.height, 0.001f, 1000.0f);
		ubo.proj[1][1] *= -1;
		ubo.time = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() / 1000.0f;
		void* data;
		vkMapMemory(device, uniformStagingBufferMemory, 0, sizeof(ubo), 0, &data);
		memcpy(data, &ubo, sizeof(ubo));
		vkUnmapMemory(device, uniformStagingBufferMemory);

		copyBuffer(uniformStagingBuffer, uniformBuffer, sizeof(ubo));
	}
	void drawRect(float x, float y, float width, float height, float r, float g, float b) {
		x /= swapChainExtent.width;
		width /= swapChainExtent.width;
		y /= swapChainExtent.height;
		height /= swapChainExtent.height;
		Vertex temp = {};
		temp.texCoord = { 0.111, 0 };
		temp.pos = glm::vec3(x, y, 0);
		temp.color = { r, g, b };
		vertices.push_back(temp);
		temp.pos = glm::vec3(x, y + height, 0);
		vertices.push_back(temp);
		temp.pos = glm::vec3(x + width, y + height, 0);
		vertices.push_back(temp);
		temp.pos = glm::vec3(x + width, y, 0);
		vertices.push_back(temp);
		indices.push_back(vertices.size() - 4);
		indices.push_back(vertices.size() - 3);
		indices.push_back(vertices.size() - 2);

		indices.push_back(vertices.size() - 4);
		indices.push_back(vertices.size() - 2);
		indices.push_back(vertices.size() - 1);
	}
	bool verticesChanged = false;
	//gets the image from the swap chain, executes the command buffer, with the swapchain image in the framebuffer, returns the image to the swap chain for presentation
	void drawFrame() {
		//if (verticesChanged) {
		if (keys.n6) {
			vertices.clear();
			indices.clear();
			drawBlocks();
		}
			//getting image from swap chain
			//drawRect(-20, -2.5, 40, 5, 0, 1, 0);
			//drawRect(-2.5, -20, 5, 40, 0, 1, 0);
			for (int i = 0; i < 500; i++) {
				//addVectors(&vertices, &indices, &verticesInverterModel, &indicesInverterModel);
			}


			createVertexBuffer(vertices, vertexBuffer, vertexBufferMemory);
			createIndexBuffer(indices, indexBuffer, indexBufferMemory);
			createCommandBuffers();
			verticesChanged = false;
		//}


		uint32_t imageIndex;

		//checking if the swap chain is out of date using vulkan
		VkResult result = vkAcquireNextImageKHR(device, swapChain, std::numeric_limits<uint64_t>::max(), imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapChain();
			return;
		}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("failed to acquire swap chain image!");
		}

		//submitting the command buffer
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit draw command buffer!");
		}
		//last step: submitting the result back to the swap chain so it is shown on the screen
		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { swapChain };

		//number of swapchains(should be 1 always)
		presentInfo.swapchainCount = 1;

		//the actualy swapchain being used
		presentInfo.pSwapchains = swapChains;

		//the index of the image to be presented from the swapchain
		presentInfo.pImageIndices = &imageIndex;

		//presenting the swapchain image to the presentation engine (vsync/mailbox)
		result = vkQueuePresentKHR(presentQueue, &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			recreateSwapChain();
		}
		else if (result != VK_SUCCESS) {
			throw std::runtime_error("failed to present swap chain image!");
		}
	}

	void createInstance() {
		if (enableValidationLayers && !checkValidationLayerSupport()) {
			throw std::runtime_error("validation layers requested, but not available!");
		}
		//optional struct with general infro about the application
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Hello Triangle";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "No Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;
		//non optional struct to define which global extensions and validation layers to use
		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		auto extensions = getRequiredExtensions();
		createInfo.enabledExtensionCount = extensions.size();
		createInfo.ppEnabledExtensionNames = extensions.data();
		//includes validation layer names if they are enabled
		if (enableValidationLayers) {
			createInfo.enabledLayerCount = validationLayers.size();
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else {
			createInfo.enabledLayerCount = 0;
		}
		//simple error check for success
		if (vkCreateInstance(&createInfo, nullptr, instance.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create instance!");
		}
	}

	void setupDebugCallback() {
		if (!enableValidationLayers) return;

		VkDebugReportCallbackCreateInfoEXT createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		createInfo.pfnCallback = debugCallback;

		if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, callback.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to set up debug callback!");
		}
	}

	void createSurface() {
		if (glfwCreateWindowSurface(instance, window, nullptr, surface.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface!");
		}
	}
	//picking a suitable physical device
	void pickPhysicalDevice() {
		//VkPhysicalDevice is a handle to a device(gfx card)
		//listing graphics cards
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		//error checking for if there is even a graphics card there
		if (deviceCount == 0) {
			throw std::runtime_error("failed to find GPUs with Vulkan support!");
		}
		//creating an vector to hold all the VkPhysicalDevice handles
		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
		//looping through the vector see see if each graphics card is suitable
		for (const auto& device : devices) {
			if (isDeviceSuitable(device)) {
				physicalDevice = device;
				break;
			}
		}
		//error handling for if no suitable card is found
		if (physicalDevice == VK_NULL_HANDLE) {
			throw std::runtime_error("failed to find a suitable GPU!");
		}
	}
	//creating a logical device to interface with the physical device
	void createLogicalDevice() {
		QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

		float queuePriority = 1.0f;
		for (int queueFamily : uniqueQueueFamilies) {
			VkDeviceQueueCreateInfo queueCreateInfo = {};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceFeatures deviceFeatures = {};

		VkDeviceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();

		createInfo.pEnabledFeatures = &deviceFeatures;

		createInfo.enabledExtensionCount = deviceExtensions.size();
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();

		if (enableValidationLayers) {
			createInfo.enabledLayerCount = validationLayers.size();
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else {
			createInfo.enabledLayerCount = 0;
		}
		//instantiating the logical device, with a small error check
		if (vkCreateDevice(physicalDevice, &createInfo, nullptr, device.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create logical device!");
		}

		vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
		vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
	}

	void createSwapChain() {
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

		VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
		VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
		VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
		//number of images in the swap chain
		uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
		if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
			imageCount = swapChainSupport.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		//swap chain details
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1; //always 1 unless stereoscopic 3D application
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; //what we will be usin g the images in the swapchain for

		QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
		uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphicsFamily, (uint32_t)indices.presentFamily };

		if (indices.graphicsFamily != indices.presentFamily) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT; //images can be used with multiple queue families without transfer
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;//images can not be used with multiple queue families unless transfered
			createInfo.queueFamilyIndexCount = 0; // Optional
			createInfo.pQueueFamilyIndices = nullptr; // Optional
		}

		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;

		VkSwapchainKHR oldSwapChain = swapChain;
		createInfo.oldSwapchain = oldSwapChain;

		VkSwapchainKHR newSwapChain;
		if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &newSwapChain) != VK_SUCCESS) {
			throw std::runtime_error("failed to create swap chain!");
		}

		swapChain = newSwapChain;

		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
		swapChainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

		swapChainImageFormat = surfaceFormat.format;
		swapChainExtent = extent;
	}

	void createImageViews() {
		swapChainImageViews.resize(swapChainImages.size(), VDeleter<VkImageView>{device, vkDestroyImageView});

		for (uint32_t i = 0; i < swapChainImages.size(); i++) {
			createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, swapChainImageViews[i]);
		}
	}

	//choosing a swap chain format
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
		if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
			return{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}

		for (const auto& availableFormat : availableFormats) {
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return availableFormat;
			}
		}

		return availableFormats[0];
	}
	//presentation mode: represents the actual conditions for showing images on the screen, 
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) {
		for (const auto& availablePresentMode : availablePresentModes) {
			if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return availablePresentMode;
			}
		}
		//this mode is always avaivable, last resort
		return VK_PRESENT_MODE_FIFO_KHR;
	}
	//choosing resolution of the swap chain
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return capabilities.currentExtent;
		}
		else {
			VkExtent2D actualExtent = { WIDTH, HEIGHT };

			actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
			actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

			return actualExtent;
		}
	}

	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
		SwapChainSupportDetails details;
		//filling the struct with the basic capibilities
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
		//getting supported surface formats
		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

		if (formatCount != 0) {
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
		}
		//getting supported presentation modes
		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

		if (presentModeCount != 0) {
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}
	//checking a graphics card to see if it suitable
	bool isDeviceSuitable(VkPhysicalDevice device) {
		//checking queue families
		QueueFamilyIndices indices = findQueueFamilies(device);
		//checking if extensions are supported
		bool extensionsSupported = checkDeviceExtensionSupport(device);
		//checking swap chain
		bool swapChainAdequate = false;
		if (extensionsSupported) {
			SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
			swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
		}

		return indices.isComplete() && extensionsSupported && swapChainAdequate;
	}
	//goes through all extensions and checks is the required ones are there
	bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

		for (const auto& extension : availableExtensions) {
			requiredExtensions.erase(extension.extensionName);
		}

		return requiredExtensions.empty();
	}

	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
		QueueFamilyIndices indices;
		//getting the list of queue families
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
		//filling a vector with queue families
		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
		//checking if atleast one of them is usable, supports VK_QUEUE_GRAPHICS_BIT
		int i = 0;
		for (const auto& queueFamily : queueFamilies) {
			if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				indices.graphicsFamily = i;
			}

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

			if (queueFamily.queueCount > 0 && presentSupport) {
				indices.presentFamily = i;
			}

			if (indices.isComplete()) {
				break;
			}

			i++;
		}

		return indices;
	}
	//returns the required list of extensions based on the validation layers are enabled or not 
	std::vector<const char*> getRequiredExtensions() {
		std::vector<const char*> extensions;

		unsigned int glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		for (unsigned int i = 0; i < glfwExtensionCount; i++) {
			extensions.push_back(glfwExtensions[i]);
		}

		if (enableValidationLayers) {
			extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		}

		return extensions;
	}

	bool checkValidationLayerSupport() {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char* layerName : validationLayers) {
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					layerFound = true;
					break;
				}
			}

			if (!layerFound) {
				return false;
			}
		}

		return true;
	}

	static std::vector<char> readFile(const std::string& filename) {
		std::ifstream file(filename, std::ios::ate | std::ios::binary);

		if (!file.is_open()) {
			throw std::runtime_error("failed to open file!");
		}
		//determining size and allocating a buffer
		size_t fileSize = (size_t)file.tellg();
		std::vector<char> buffer(fileSize);
		//reading the file
		file.seekg(0);
		file.read(buffer.data(), fileSize);
		//closing file and returning the bytes
		file.close();
		return buffer;
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData) {
		std::cerr << "validation layer: " << msg << std::endl;

		return VK_FALSE;
	}
};

int main() {
	WorkSpace app;
	try {
		app.run();
	}
	catch (const std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}