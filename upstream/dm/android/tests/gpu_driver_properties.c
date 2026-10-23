// 独立验证所选driver身份、物理能力及GIPA/GDPA分发表，不运行游戏。
// Independently verify selected-driver identity, physical capabilities and GIPA/GDPA dispatch without running the game.
#include "../native/gpu_driver.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
    dm_vk_driver driver={0};VkInstance instance=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;
    int result=1;VkPhysicalDevice *physical_devices=NULL;VkQueueFamilyProperties *queues=NULL;
    if(argc>2){fprintf(stderr,"usage: gpu-driver-properties [offline-shader-cache-directory]\n");return 2;}
    if(!dm_vk_driver_open(&driver)){fprintf(stderr,"loader: %s\n",dm_vk_driver_error(&driver));goto finish;}
    if(driver.requested_custom&&!driver.using_custom){fprintf(stderr,"FAIL: requested custom driver fell back to system\n");goto finish;}
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="D2R driver properties",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo create={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkResult vk_result=driver.vkCreateInstance(&create,NULL,&instance);
    if(vk_result!=VK_SUCCESS){fprintf(stderr,"vkCreateInstance: %d\n",vk_result);goto finish;}
    if(!dm_vk_driver_load_instance(&driver,instance,0)){fprintf(stderr,"instance dispatch: %s\n",dm_vk_driver_error(&driver));goto finish;}
    uint32_t count=0;if(driver.vkEnumeratePhysicalDevices(instance,&count,NULL)!=VK_SUCCESS||!count)goto finish;
    physical_devices=calloc(count,sizeof *physical_devices);if(!physical_devices)goto finish;
    if(driver.vkEnumeratePhysicalDevices(instance,&count,physical_devices)!=VK_SUCCESS)goto finish;
    VkPhysicalDevice physical=VK_NULL_HANDLE;uint32_t family=UINT32_MAX;
    for(uint32_t i=0;i<count&&!physical;i++){
        uint32_t queue_count=0;driver.vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i],&queue_count,NULL);
        queues=calloc(queue_count,sizeof *queues);if(queue_count&&!queues)goto finish;
        driver.vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i],&queue_count,queues);
        for(uint32_t j=0;j<queue_count;j++){
            if((queues[j].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){
                physical=physical_devices[i];family=j;
                printf("queue family=%u timestamp_bits=%u\n",j,queues[j].timestampValidBits);break;
            }
        }
        free(queues);queues=NULL;
    }
    if(!physical)goto finish;
    if(!dm_vk_driver_validate(&driver,physical,argc==2?argv[1]:NULL)){fprintf(stderr,"driver validation: %s\n",dm_vk_driver_error(&driver));goto finish;}
    VkPhysicalDeviceProperties properties;VkPhysicalDeviceFeatures features;
    driver.vkGetPhysicalDeviceProperties(physical,&properties);driver.vkGetPhysicalDeviceFeatures(physical,&features);
    printf("device=%s vendor=%#x device_id=%#x API=%u.%u.%u driver_version=%u driver_id=%u subgroup=%u\n",properties.deviceName,properties.vendorID,properties.deviceID,VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),VK_VERSION_PATCH(properties.apiVersion),properties.driverVersion,driver.actual_driver_id,driver.subgroup_size);
    printf("features BC=%u ASTC=%u independent_blend=%u gather=%u fragment_stores=%u vertex_stores=%u storage_read_without_format=%u storage_write_without_format=%u precise_occlusion=%u timestamp_period=%g\n",features.textureCompressionBC,features.textureCompressionASTC_LDR,features.independentBlend,features.shaderImageGatherExtended,features.fragmentStoresAndAtomics,features.vertexPipelineStoresAndAtomics,features.shaderStorageImageReadWithoutFormat,features.shaderStorageImageWriteWithoutFormat,features.occlusionQueryPrecise,properties.limits.timestampPeriod);
    const VkFormat formats[]={VK_FORMAT_BC1_RGBA_UNORM_BLOCK,VK_FORMAT_BC3_UNORM_BLOCK,VK_FORMAT_R16_UNORM,VK_FORMAT_D16_UNORM};
    for(unsigned i=0;i<sizeof formats/sizeof formats[0];i++){
        VkFormatProperties format;driver.vkGetPhysicalDeviceFormatProperties(physical,formats[i],&format);
        printf("format=%u optimal=%#x linear=%#x buffer=%#x",formats[i],format.optimalTilingFeatures,format.linearTilingFeatures,format.bufferFeatures);
        if(properties.apiVersion>=VK_API_VERSION_1_3){
            VkFormatProperties3 flags={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
            VkFormatProperties2 properties2={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,.pNext=&flags};
            driver.vkGetPhysicalDeviceFormatProperties2(physical,formats[i],&properties2);
            printf(" sampled_compare=%u",!!(flags.optimalTilingFeatures&VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT));
        }
        putchar('\n');
    }
    float priority=1;VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo device_create={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue};
    if(!dm_vk_driver_prepare_device(&driver,&device_create)){fprintf(stderr,"device subgroup policy: %s\n",dm_vk_driver_error(&driver));goto finish;}
    vk_result=driver.vkCreateDevice(physical,&device_create,NULL,&device);
    if(vk_result!=VK_SUCCESS){fprintf(stderr,"vkCreateDevice: %d\n",vk_result);goto finish;}
    if(!dm_vk_driver_load_device(&driver,device)){fprintf(stderr,"device dispatch: %s\n",dm_vk_driver_error(&driver));goto finish;}
    printf("subgroup physical_default=%u explicit_request=%u shader_selected=%u device_policy_enabled=%d\n",driver.subgroup_size,driver.requested_subgroup_size,driver.effective_shader_subgroup_size,driver.subgroup_policy_enabled);
    VkQueue graphics_queue;driver.vkGetDeviceQueue(device,family,0,&graphics_queue);
    if(!graphics_queue||driver.vkDeviceWaitIdle(device)!=VK_SUCCESS)goto finish;
    puts("GPU driver properties and dispatch PASS");result=0;
finish:
    free(queues);free(physical_devices);
    if(device&&driver.vkDestroyDevice)driver.vkDestroyDevice(device,NULL);
    if(instance&&driver.vkDestroyInstance)driver.vkDestroyInstance(instance,NULL);
    dm_vk_driver_close(&driver);return result;
}
