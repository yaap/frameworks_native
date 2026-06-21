/*
 * Copyright 2026 The Android Open Source Project
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

#include "vulkan/vulkan_core.h"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <log/log.h>
#include <utils/Trace.h>

#include <algorithm>
#include <vector>

#include "driver.h"

#include <com_android_graphics_libvulkan_flags.h>

using namespace com::android::graphics::libvulkan;

namespace vulkan {
namespace driver {

// Note: two versions of each of these entrypoints, and we need to
// forward to the matching driver function for anything we're not
// handling ourselves.

namespace {

VkResult CreatePrivateDataSlotInternal(
        VkDevice device,
        const VkPrivateDataSlotCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkPrivateDataSlot* pPrivateDataSlot,
        bool useExt)
{
    auto& device_data = GetData(device);
    auto result = (useExt ? device_data.driver.CreatePrivateDataSlotEXT : device_data.driver.CreatePrivateDataSlot)(
            device, pCreateInfo, pAllocator, pPrivateDataSlot);

    if (result != VK_SUCCESS)
        return result;

    auto* slot = new PrivateDataSlot(*pPrivateDataSlot);

    {
        std::lock_guard lock(device_data.private_data_mutex);
        device_data.private_data_slots.push_back(slot);

        // if we haven't exhausted all the preallocated slots, use one for this.
        if (device_data.next_preallocated_private_data_slot <
               device_data.num_preallocated_private_data_slots) {
            slot->preallocated_slot = device_data.next_preallocated_private_data_slot++;
        }
    }

    *pPrivateDataSlot = slot->as_handle();
    return result;
}

void DestroyPrivateDataSlotInternal(
        VkDevice device,
        VkPrivateDataSlot privateDataSlot,
        const VkAllocationCallbacks* pAllocator,
        bool useExt) {

    if (privateDataSlot == VK_NULL_HANDLE) {
        return;
    }

    auto& device_data = GetData(device);
    auto* slot = PrivateDataSlot::from_handle(privateDataSlot);
    (useExt ? device_data.driver.DestroyPrivateDataSlotEXT : device_data.driver.DestroyPrivateDataSlot)(
            device, slot->driver_object, pAllocator);

    {
        std::lock_guard lock(device_data.private_data_mutex);
        std::erase(device_data.private_data_slots, slot);
    }

    delete slot;
}

VKAPI_ATTR void GetPrivateDataInternal(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t* pData,
        bool useExt) {

    const auto& device_data = GetData(device);
    auto* slot = PrivateDataSlot::from_handle(privateDataSlot);

    if (objectType == VK_OBJECT_TYPE_SWAPCHAIN_KHR) {
        // we handle swapchain objects directly instead of in the driver.

        // if this is a preallocated slot, use the storage in the object itself.
        // otherwise, fall back to the map with all its locking etc.
        if (slot->preallocated_slot >= 0)
            *pData = GetSwapchainPreallocatedDataSlot(VkSwapchainKHR(objectHandle), slot->preallocated_slot);
        else
            *pData = slot->get(objectHandle);
        return;
    }

    if (objectType == VK_OBJECT_TYPE_PRIVATE_DATA_SLOT) {
        // if the requested object is itself a private data slot,
        // unwrap it so we ask the driver about an object _it_ knows about.
        auto *o = PrivateDataSlot::from_handle(VkPrivateDataSlot(objectHandle));
        objectHandle = reinterpret_cast<uint64_t>(o->driver_object);
    }

    (useExt ? device_data.driver.GetPrivateDataEXT : device_data.driver.GetPrivateData)(
            device, objectType, objectHandle, slot->driver_object, pData);
}

VKAPI_ATTR VkResult SetPrivateDataInternal(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t data,
        bool useExt) {

    const auto& device_data = GetData(device);
    auto* slot = PrivateDataSlot::from_handle(privateDataSlot);

    if (objectType == VK_OBJECT_TYPE_SWAPCHAIN_KHR) {
        // we handle swapchain objects directly instead of in the driver.

        // if this is a preallocated slot, use the storage in the object itself.
        // otherwise, fall back to the map with all its locking etc.
        if (slot->preallocated_slot >= 0)
            SetSwapchainPreallocatedDataSlot(VkSwapchainKHR(objectHandle), slot->preallocated_slot, data);
        else
            slot->set(objectHandle, data);
        return VK_SUCCESS;
    }

    if (objectType == VK_OBJECT_TYPE_PRIVATE_DATA_SLOT) {
        // if the requested object is itself a private data slot,
        // unwrap it so we ask the driver about an object _it_ knows about.
        auto *o = PrivateDataSlot::from_handle(VkPrivateDataSlot(objectHandle));
        objectHandle = reinterpret_cast<uint64_t>(o->driver_object);
    }

    return (useExt ? device_data.driver.SetPrivateDataEXT : device_data.driver.SetPrivateData)(
            device, objectType, objectHandle, slot->driver_object, data);
}

} // namespace

VKAPI_ATTR VkResult CreatePrivateDataSlotEXT(
        VkDevice device,
        const VkPrivateDataSlotCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkPrivateDataSlot* pPrivateDataSlot) {

    if (!flags::ext_private_data_swapchain()) {
        auto& device_data = GetData(device);
        return device_data.driver.CreatePrivateDataSlotEXT(device, pCreateInfo, pAllocator, pPrivateDataSlot);
    }

    return CreatePrivateDataSlotInternal(device, pCreateInfo, pAllocator, pPrivateDataSlot, true);
}

VKAPI_ATTR VkResult CreatePrivateDataSlot(
        VkDevice device,
        const VkPrivateDataSlotCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkPrivateDataSlot* pPrivateDataSlot) {

    if (!flags::ext_private_data_swapchain()) {
        auto& device_data = GetData(device);
        return device_data.driver.CreatePrivateDataSlot(device, pCreateInfo, pAllocator, pPrivateDataSlot);
    }

    return CreatePrivateDataSlotInternal(device, pCreateInfo, pAllocator, pPrivateDataSlot, false);
}

VKAPI_ATTR void DestroyPrivateDataSlotEXT(
        VkDevice device,
        VkPrivateDataSlot privateDataSlot,
        const VkAllocationCallbacks* pAllocator) {

    if (!flags::ext_private_data_swapchain()) {
        auto& device_data = GetData(device);
        device_data.driver.DestroyPrivateDataSlotEXT(device, privateDataSlot, pAllocator);
        return;
    }

    DestroyPrivateDataSlotInternal(device, privateDataSlot, pAllocator, true);
}

VKAPI_ATTR void DestroyPrivateDataSlot(
        VkDevice device,
        VkPrivateDataSlot privateDataSlot,
        const VkAllocationCallbacks* pAllocator) {

    if (!flags::ext_private_data_swapchain()) {
        auto& device_data = GetData(device);
        device_data.driver.DestroyPrivateDataSlot(device, privateDataSlot, pAllocator);
        return;
    }

    DestroyPrivateDataSlotInternal(device, privateDataSlot, pAllocator, false);
}

VKAPI_ATTR void GetPrivateDataEXT(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t* pData) {

    if (!flags::ext_private_data_swapchain()) {
        auto& device_data = GetData(device);
        device_data.driver.GetPrivateDataEXT(device, objectType, objectHandle, privateDataSlot, pData);
        return;
    }

    GetPrivateDataInternal(device, objectType, objectHandle, privateDataSlot, pData, true);
}

VKAPI_ATTR void GetPrivateData(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t* pData) {

    if (!flags::ext_private_data_swapchain()) {
        auto& device_data = GetData(device);
        device_data.driver.GetPrivateData(device, objectType, objectHandle, privateDataSlot, pData);
        return;
    }

    GetPrivateDataInternal(device, objectType, objectHandle, privateDataSlot, pData, false);
}

VKAPI_ATTR VkResult SetPrivateDataEXT(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t data) {

    if (!flags::ext_private_data_swapchain()) {
        auto& device_data = GetData(device);
        return device_data.driver.SetPrivateDataEXT(device, objectType, objectHandle, privateDataSlot, data);
    }

    return SetPrivateDataInternal(device, objectType, objectHandle, privateDataSlot, data, true);
}

VKAPI_ATTR VkResult SetPrivateData(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        VkPrivateDataSlot privateDataSlot,
        uint64_t data) {

    if (!flags::ext_private_data_swapchain()) {
        auto& device_data = GetData(device);
        return device_data.driver.SetPrivateData(device, objectType, objectHandle, privateDataSlot, data);
    }

    return SetPrivateDataInternal(device, objectType, objectHandle, privateDataSlot, data, false);
}


}  // namespace driver
}  // namespace vulkan

