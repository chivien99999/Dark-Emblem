// Minimal standard Khronos Vulkan 1.3 definitions for runtime GPU capability probing
// via LoadLibraryA("vulkan-1.dll") without requiring the external Vulkan SDK.
// Derived from Vulkan-Headers (Apache-2.0 / MIT).
#ifndef DE_VK_PROBE_DEFS_H
#define DE_VK_PROBE_DEFS_H

#include <stdint.h>

#define VKAPI_ATTR
#define VKAPI_CALL __stdcall
#define VKAPI_PTR VKAPI_CALL

typedef uint32_t VkBool32;
typedef uint32_t VkFlags;
typedef uint64_t VkDeviceSize;
typedef int32_t  VkResult;

typedef struct VkInstance_T       *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;

#define VK_SUCCESS 0
#define VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
#define VK_UUID_SIZE 16
#define VK_MAX_EXTENSION_NAME_SIZE 256
#define VK_MAX_MEMORY_TYPES 32
#define VK_MAX_MEMORY_HEAPS 16

#define VK_MAKE_API_VERSION(variant, major, minor, patch) \
    ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))
#define VK_API_VERSION_1_3 VK_MAKE_API_VERSION(0, 1, 3, 0)

#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 1000059000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT 1000352000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT 1000482000

#define VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU 2
#define VK_MEMORY_HEAP_DEVICE_LOCAL_BIT 0x00000001U

#define VK_EXT_SHADER_OBJECT_EXTENSION_NAME "VK_EXT_shader_object"
#define VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME "VK_EXT_extended_dynamic_state3"
#define VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME "VK_EXT_vertex_input_dynamic_state"

typedef struct VkApplicationInfo {
    int32_t sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    int32_t  deviceType;
    char     deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t  pipelineCacheUUID[VK_UUID_SIZE];
    uint8_t  limits_and_sparse[544]; // Opaque padding for VkPhysicalDeviceLimits + SparseProperties
} VkPhysicalDeviceProperties;

typedef struct VkMemoryType {
    VkFlags  propertyFlags;
    uint32_t heapIndex;
} VkMemoryType;

typedef struct VkMemoryHeap {
    VkDeviceSize size;
    VkFlags      flags;
} VkMemoryHeap;

typedef struct VkPhysicalDeviceMemoryProperties {
    uint32_t     memoryTypeCount;
    VkMemoryType memoryTypes[VK_MAX_MEMORY_TYPES];
    uint32_t     memoryHeapCount;
    VkMemoryHeap memoryHeaps[VK_MAX_MEMORY_HEAPS];
} VkPhysicalDeviceMemoryProperties;

typedef struct VkExtensionProperties {
    char     extensionName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
} VkExtensionProperties;

typedef struct VkPhysicalDeviceFeatures {
    VkBool32 features[55];
} VkPhysicalDeviceFeatures;

typedef struct VkPhysicalDeviceFeatures2 {
    int32_t sType;
    void *pNext;
    VkPhysicalDeviceFeatures features;
} VkPhysicalDeviceFeatures2;

typedef struct VkPhysicalDeviceShaderObjectFeaturesEXT {
    int32_t  sType;
    void    *pNext;
    VkBool32 shaderObject;
} VkPhysicalDeviceShaderObjectFeaturesEXT;

typedef struct VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT {
    int32_t  sType;
    void    *pNext;
    VkBool32 vertexInputDynamicState;
} VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT;

typedef void (VKAPI_PTR *PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetInstanceProcAddr)(VkInstance, const char *);
typedef VkResult (VKAPI_PTR *PFN_vkCreateInstance)(const VkInstanceCreateInfo *, const void *, VkInstance *);
typedef void (VKAPI_PTR *PFN_vkDestroyInstance)(VkInstance, const void *);
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t *, VkPhysicalDevice *);
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties *);
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceMemoryProperties)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *);
typedef VkResult (VKAPI_PTR *PFN_vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties *);
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceFeatures2)(VkPhysicalDevice, VkPhysicalDeviceFeatures2 *);

#endif
