/*
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This is a basic passthrough layer used to verify the libvulan
 * layers_extensions.* code. This layer isn't thread safe and doesn't properly
 * clean up it's state so shouldn't be used for production.
 */

#include <android/log.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <bitset>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define VK_LAYER_EXPORT __attribute__((visibility("default")))

// TEST_LAYER_NAME is defined in build file
#define LAYER_NAME TEST_LAYER_NAME

static PFN_vkGetInstanceProcAddr g_nextGlobalGetInstanceProcAddr = nullptr;

// Per-instance dispatch. Keyed by the instance handle itself.
static std::unordered_map<VkInstance, PFN_vkGetInstanceProcAddr>
    g_instanceDispatch;

// Per-device dispatch. Keyed by the device handle itself.
static std::unordered_map<VkDevice, PFN_vkGetDeviceProcAddr> g_deviceDispatch;

// Maps to track which device a queue belongs to
static std::unordered_map<VkQueue, VkDevice> g_queueToDevice;

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                   VkLayerProperties* pProperties) {
    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pProperties == nullptr) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }

    if (*pPropertyCount < 1) {
        return VK_INCOMPLETE;
    }

    VkLayerProperties layerProps{};
    std::strcpy(layerProps.layerName, LAYER_NAME);
    std::strcpy(layerProps.description, "Custom Debug Layer for Android");
    layerProps.implementationVersion = 1;
    layerProps.specVersion = VK_API_VERSION_1_0;

    *pProperties = layerProps;
    *pPropertyCount = 1;
    return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice,
                                     const char*,
                                     uint32_t* pPropertyCount,
                                     VkExtensionProperties*) {
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL
vkEnumerateDeviceLayerProperties(VkPhysicalDevice,
                                 uint32_t* pPropertyCount,
                                 VkLayerProperties* pProperties) {
    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!pProperties) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }

    if (*pPropertyCount < 1) {
        return VK_INCOMPLETE;
    }

    VkLayerProperties props{};
    std::strcpy(props.layerName, LAYER_NAME);
    std::strcpy(props.description, "Custom Debug Layer for Android");
    props.implementationVersion = 1;
    props.specVersion = VK_API_VERSION_1_0;

    pProperties[0] = props;
    *pPropertyCount = 1;

    return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                       uint32_t* pPropertyCount,
                                       VkExtensionProperties* pProperties) {
    if (pLayerName && (std::strcmp(pLayerName, LAYER_NAME) == 0)) {
        const std::vector<VkExtensionProperties> myExtensions = {
            {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
             VK_MAKE_VERSION(1, 0, 0)},
        };

        if (!pProperties) {
            *pPropertyCount = static_cast<uint32_t>(myExtensions.size());
            return VK_SUCCESS;
        } else {
            uint32_t toCopy =
                std::min(*pPropertyCount, (uint32_t)myExtensions.size());
            for (uint32_t i = 0; i < toCopy; ++i) {
                pProperties[i] = myExtensions[i];
            }
            *pPropertyCount = toCopy;
            return (toCopy < myExtensions.size()) ? VK_INCOMPLETE : VK_SUCCESS;
        }
    }

    return VK_ERROR_LAYER_NOT_PRESENT;
}

static const VkLayerInstanceCreateInfo* GetLayerInstanceCreateInfo(
    const VkInstanceCreateInfo* pCreateInfo) {
    const VkBaseOutStructure* current =
        reinterpret_cast<const VkBaseOutStructure*>(pCreateInfo->pNext);
    while (current) {
        if (current->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO) {
            auto layerCreateInfo =
                reinterpret_cast<const VkLayerInstanceCreateInfo*>(current);
            if (layerCreateInfo->function == VK_LAYER_LINK_INFO) {
                return layerCreateInfo;
            }
        }
        current = current->pNext;
    }
    return nullptr;
}

static const VkLayerDeviceCreateInfo* GetLayerDeviceCreateInfo(
    const VkDeviceCreateInfo* pCreateInfo) {
    const VkBaseOutStructure* current =
        reinterpret_cast<const VkBaseOutStructure*>(pCreateInfo->pNext);
    while (current) {
        if (current->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            auto layerCreateInfo =
                reinterpret_cast<const VkLayerDeviceCreateInfo*>(current);
            if (layerCreateInfo->function == VK_LAYER_LINK_INFO) {
                return layerCreateInfo;
            }
        }
        current = current->pNext;
    }
    return nullptr;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                 const VkAllocationCallbacks* pAllocator,
                 VkInstance* pInstance) {
    const VkLayerInstanceCreateInfo* chainInfo =
        GetLayerInstanceCreateInfo(pCreateInfo);
    if (!chainInfo || !chainInfo->u.pLayerInfo ||
        !chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_nextGlobalGetInstanceProcAddr =
        chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;

    PFN_vkCreateInstance createFn = reinterpret_cast<PFN_vkCreateInstance>(
        g_nextGlobalGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    if (!createFn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = createFn(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) {
        return result;
    }

    PFN_vkGetInstanceProcAddr gipa =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            g_nextGlobalGetInstanceProcAddr(*pInstance,
                                            "vkGetInstanceProcAddr"));

    g_instanceDispatch[*pInstance] = gipa;

    return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice physicalDevice,
               const VkDeviceCreateInfo* pCreateInfo,
               const VkAllocationCallbacks* pAllocator,
               VkDevice* pDevice) {
    const VkLayerDeviceCreateInfo* chainInfo =
        GetLayerDeviceCreateInfo(pCreateInfo);
    if (!chainInfo || !chainInfo->u.pLayerInfo ||
        !chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetDeviceProcAddr nextGdpa =
        chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkGetInstanceProcAddr nextGdpaInstance =
        chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateDevice realCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(
        nextGdpaInstance(nullptr, "vkCreateDevice"));
    if (!realCreateDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result =
        realCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        return result;
    }

    // Store the device's dispatch pointer
    g_deviceDispatch[*pDevice] = nextGdpa;

    return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VkResult VKAPI_CALL
vkQueueSubmit(VkQueue queue,
              uint32_t submitCount,
              const VkSubmitInfo* pSubmits,
              VkFence fence) {
    auto it = g_queueToDevice.find(queue);
    if (it == g_queueToDevice.end()) {
        // We have not tracked this queue => layering error (or we didn't
        // intercept vkGetDeviceQueue)
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkDevice device = it->second;

    auto devDispatchIt = g_deviceDispatch.find(device);
    if (devDispatchIt == g_deviceDispatch.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkQueueSubmit realQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(
        devDispatchIt->second(device, "vkQueueSubmit"));

    if (!realQueueSubmit) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return realQueueSubmit(queue, submitCount, pSubmits, fence);
}

extern "C" VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (std::strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    }
    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    }
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            vkEnumerateInstanceLayerProperties);
    }
    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            vkEnumerateInstanceExtensionProperties);
    }

    // If we have a dispatch table for this instance, go there next
    if (instance != VK_NULL_HANDLE) {
        auto it = g_instanceDispatch.find(instance);
        if (it != g_instanceDispatch.end()) {
            return it->second(instance, pName);
        }
    }

    return nullptr;
}

extern "C" VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (std::strcmp(pName, "vkQueueSubmit") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit);
    }
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(
            vkEnumerateInstanceLayerProperties);
    }
    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    }

    // If we have a dispatch table for this device, go there next
    auto it = g_deviceDispatch.find(device);
    if (it != g_deviceDispatch.end()) {
        return it->second(device, pName);
    }

    return nullptr;
}
