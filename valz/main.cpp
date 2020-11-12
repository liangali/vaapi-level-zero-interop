#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <new>
#include <stdlib.h>
#include <assert.h>

#include <iostream>
#include <string>
#include <sstream>
#include <utility>
#include <vector>
#include <fstream>
#include <memory>
#include <iomanip>

#include "va/va.h"
#include "va/va_drm.h"
#include "va/va_drmcommon.h"
#include "ze_api.h"

#include "video.h"

VADisplay va_dpy = NULL;
int va_fd = -1;
bool dump_decode_output = false;

int initVA()
{
    VADisplay disp;
    VAStatus va_res = VA_STATUS_SUCCESS;
    int major_version = 0, minor_version = 0;

    int adapter_num = 0;
    char adapterpath[256];
    snprintf(adapterpath,sizeof(adapterpath),"/dev/dri/renderD%d", adapter_num + 128);

    va_fd = open(adapterpath, O_RDWR);
    if (va_fd < 0) {
        printf("ERROR: failed to open adapter in %s\n", adapterpath);
        return -1;
    }

    va_dpy = vaGetDisplayDRM(va_fd);
    if (!va_dpy) {
        close(va_fd);
        va_fd = -1;
        printf("ERROR: failed in vaGetDisplayDRM\n");
        return -1;
    }

    va_res = vaInitialize(va_dpy, &major_version, &minor_version);
    if (VA_STATUS_SUCCESS != va_res) {
        close(va_fd);
        va_fd = -1;
        printf("ERROR: failed in vaInitialize with err = %d\n", va_res);
        return -1;
    }

    printf("INFO: vaInitialize done\n");

    return 0;
}

inline ze_device_handle_t findDevice(
    ze_driver_handle_t pDriver,
    ze_device_type_t type)
{
    // get all devices
    uint32_t deviceCount = 0;
    zeDeviceGet(pDriver, &deviceCount, nullptr);

    std::vector<ze_device_handle_t> devices(deviceCount);
    zeDeviceGet(pDriver, &deviceCount, devices.data());

    ze_device_handle_t found = nullptr;

    // for each device, find the first one matching the type
    for(uint32_t device = 0; device < deviceCount; ++device)
    {
        auto phDevice = devices[device];

        ze_device_properties_t device_properties = {};
        device_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES;
        zeDeviceGetProperties(phDevice, &device_properties);

        if(type == device_properties.type)
        {
            found = phDevice;

            ze_driver_properties_t driver_properties = {};
            driver_properties.stype = ZE_STRUCTURE_TYPE_DRIVER_PROPERTIES;
            zeDriverGetProperties(pDriver, &driver_properties);

            //std::cout << "Found "<< to_string(type) << " device..." << "\n";
            //std::cout << "Driver version: " << driver_properties.driverVersion << "\n";

            ze_api_version_t version = {};
            zeDriverGetApiVersion(pDriver, &version);
            //std::cout << "API version: " << to_string(version) << "\n";

            //std::cout << to_string(device_properties) << "\n";

            ze_device_compute_properties_t compute_properties = {};
            compute_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_COMPUTE_PROPERTIES;
            zeDeviceGetComputeProperties(phDevice, &compute_properties);
            //std::cout << to_string(compute_properties) << "\n";

            uint32_t memoryCount = 0;
            zeDeviceGetMemoryProperties(phDevice, &memoryCount, nullptr);
            auto pMemoryProperties = new ze_device_memory_properties_t[memoryCount];
            for( uint32_t mem = 0; mem < memoryCount; ++mem )
            {
                pMemoryProperties[mem].stype = ZE_STRUCTURE_TYPE_DEVICE_MEMORY_PROPERTIES;
                pMemoryProperties[mem].pNext = nullptr;
            }
            zeDeviceGetMemoryProperties(phDevice, &memoryCount, pMemoryProperties);
            for( uint32_t mem = 0; mem < memoryCount; ++mem )
            {
                //std::cout << to_string( pMemoryProperties[ mem ] ) << "\n";
            }
            delete[] pMemoryProperties;

            ze_device_memory_access_properties_t memory_access_properties = {};
            memory_access_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_MEMORY_ACCESS_PROPERTIES;
            zeDeviceGetMemoryAccessProperties(phDevice, &memory_access_properties);
            //std::cout << to_string( memory_access_properties ) << "\n";

            uint32_t cacheCount = 0;
            zeDeviceGetCacheProperties(phDevice, &cacheCount, nullptr );
            auto pCacheProperties = new ze_device_cache_properties_t[cacheCount];
            for( uint32_t cache = 0; cache < cacheCount; ++cache )
            {
                pCacheProperties[cache].stype = ZE_STRUCTURE_TYPE_DEVICE_CACHE_PROPERTIES;
                pCacheProperties[cache].pNext = nullptr;
            }
            zeDeviceGetCacheProperties(phDevice, &cacheCount, pCacheProperties);
            for( uint32_t cache = 0; cache < cacheCount; ++cache )
            {
                //std::cout << to_string( pCacheProperties[ cache ] ) << "\n";
            }
            delete[] pCacheProperties;

            ze_device_image_properties_t image_properties = {};
            image_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_IMAGE_PROPERTIES;
            zeDeviceGetImageProperties(phDevice, &image_properties);
            //std::cout << to_string( image_properties ) << "\n";

            break;
        }
    }

    return found;
}


#define CHECK_VASTATUS(va_status,func)                                    \
if (va_status != VA_STATUS_SUCCESS) {                                     \
    fprintf(stderr,"%s:%s (%d) failed, exit\n", __func__, func, __LINE__); \
    exit(1);                                                              \
}

int decodeFrame(VASurfaceID& frame)
{
    VAEntrypoint entrypoints[5];
    int num_entrypoints, vld_entrypoint;
    VAConfigAttrib attrib;
    VAConfigID config_id;
    VASurfaceID surface_id[RT_NUM];
    VAContextID context_id;
    VABufferID pic_param_buf, iqmatrix_buf, slice_param_buf, slice_data_buf;
    int major_ver, minor_ver;
    VAStatus va_status;
    int putsurface=0;

    va_status = vaQueryConfigEntrypoints(va_dpy, VAProfileH264Main, entrypoints, 
                             &num_entrypoints);
    CHECK_VASTATUS(va_status, "vaQueryConfigEntrypoints");

    for	(vld_entrypoint = 0; vld_entrypoint < num_entrypoints; vld_entrypoint++) {
        if (entrypoints[vld_entrypoint] == VAEntrypointVLD)
            break;
    }
    if (vld_entrypoint == num_entrypoints) {
        printf("ERROR: not find AVC VLD entry point\n");
        return -1;
    }

    /* find out the format for the render target */
    attrib.type = VAConfigAttribRTFormat;
    vaGetConfigAttributes(va_dpy, VAProfileH264Main, VAEntrypointVLD, &attrib, 1);
    if ((attrib.value & VA_RT_FORMAT_YUV420) == 0) {
        printf("ERROR: not find desired YUV420 RT format\n");
        return -1;
    }

    va_status = vaCreateConfig(va_dpy, VAProfileH264Main, VAEntrypointVLD, &attrib, 1, &config_id);
    CHECK_VASTATUS(va_status, "vaQueryConfigEntrypoints");

    for (size_t i = 0; i < RT_NUM; i++)
    {
        va_status = vaCreateSurfaces(va_dpy, VA_RT_FORMAT_YUV420, CLIP_WIDTH, CLIP_HEIGHT,  &surface_id[i], 1, NULL, 0 );
        CHECK_VASTATUS(va_status, "vaCreateSurfaces");
    }

    /* Create a context for this decode pipe */
    va_status = vaCreateContext(va_dpy, config_id, CLIP_WIDTH, ((CLIP_HEIGHT+15)/16)*16, VA_PROGRESSIVE, surface_id, RT_NUM, &context_id);
    CHECK_VASTATUS(va_status, "vaCreateContext");

    va_status = vaCreateBuffer(va_dpy, context_id, VAPictureParameterBufferType, pic_size, 1, &pic_param, &pic_param_buf);
    CHECK_VASTATUS(va_status, "vaCreateBuffer type = VAPictureParameterBufferType");
    va_status = vaCreateBuffer(va_dpy, context_id, VAIQMatrixBufferType, iq_size, 1, &iq_matrix, &iqmatrix_buf );
    CHECK_VASTATUS(va_status, "vaCreateBuffer type = VAIQMatrixBufferType");
    va_status = vaCreateBuffer(va_dpy, context_id, VASliceParameterBufferType, slc_size, 1, &slc_param, &slice_param_buf);
    CHECK_VASTATUS(va_status, "vaCreateBuffer type = VASliceParameterBufferType");
    va_status = vaCreateBuffer(va_dpy, context_id, VASliceDataBufferType, bs_size, 1, bs_data, &slice_data_buf);
    CHECK_VASTATUS(va_status, "vaCreateBuffer type = VASliceDataBufferType");

    /* send decode workload to GPU */
    va_status = vaBeginPicture(va_dpy, context_id, surface_id[RT_ID]);
    CHECK_VASTATUS(va_status, "vaBeginPicture");
    va_status = vaRenderPicture(va_dpy,context_id, &pic_param_buf, 1);
    CHECK_VASTATUS(va_status, "vaRenderPicture");
    va_status = vaRenderPicture(va_dpy,context_id, &iqmatrix_buf, 1);
    CHECK_VASTATUS(va_status, "vaRenderPicture");
    va_status = vaRenderPicture(va_dpy,context_id, &slice_param_buf, 1);
    CHECK_VASTATUS(va_status, "vaRenderPicture");
    va_status = vaRenderPicture(va_dpy,context_id, &slice_data_buf, 1);
    CHECK_VASTATUS(va_status, "vaRenderPicture");
    va_status = vaEndPicture(va_dpy,context_id);
    CHECK_VASTATUS(va_status, "vaEndPicture");

    va_status = vaSyncSurface(va_dpy, surface_id[RT_ID]);
    CHECK_VASTATUS(va_status, "vaSyncSurface");

    frame = surface_id[RT_ID];

    if (dump_decode_output)
    {
        VAImage output_image;
        va_status = vaDeriveImage(va_dpy, surface_id[RT_ID], &output_image);
        CHECK_VASTATUS(va_status, "vaDeriveImage");

        void *out_buf = nullptr;
        va_status = vaMapBuffer(va_dpy, output_image.buf, &out_buf);
        CHECK_VASTATUS(va_status, "vaMapBuffer");

        FILE *fp = fopen("out_224x224.nv12", "wb+");
        // dump y_plane
        char *y_buf = (char*)out_buf;
        int y_pitch = output_image.pitches[0];
        for (size_t i = 0; i < CLIP_HEIGHT; i++)
        {
            fwrite(y_buf + y_pitch*i, CLIP_WIDTH, 1, fp);
        }
        // dump uv_plane
        char *uv_buf = (char*)out_buf + output_image.offsets[1];
        int uv_pitch = output_image.pitches[1];
        for (size_t i = 0; i < CLIP_HEIGHT/2; i++)
        {
            fwrite(uv_buf + uv_pitch*i, CLIP_WIDTH, 1, fp);
        }

        // use below command line to convert nv12 surface as bmp image
        /* ffmpeg -s 224x224 -pix_fmt nv12 -f rawvideo -i out.nv12 out.bmp */

        fclose(fp);
    }

    vaDestroyConfig(va_dpy,config_id);
    vaDestroyContext(va_dpy,context_id);

    return 0;
}

void printDesc(VADRMPRIMESurfaceDescriptor &prime_desc)
{
    printf("INFO: width = %d\n", prime_desc.width);
    printf("INFO: height = %d\n", prime_desc.height);
    printf("INFO: fourcc = 0x%08x\n", prime_desc.fourcc);
    printf("INFO: num_objects = %d\n", prime_desc.num_objects);

    int num_objects = prime_desc.num_objects > 4 ? 4: prime_desc.num_objects;
    for (size_t i = 0; i < num_objects; i++)
    {
        printf("  INFO: dma_buf[%d] = %d\n", i, prime_desc.objects[i].fd);
        printf("  INFO: size[%d] = %d\n", i, prime_desc.objects[i].size);
        printf("  INFO: drm_format_modifier[%d] = %d\n", i, prime_desc.objects[i].drm_format_modifier);
    }

    int num_layers = prime_desc.num_layers > 4 ? 4: prime_desc.num_layers;
    printf("INFO: num_layers = %d\n", prime_desc.num_layers);
    for (size_t i = 0; i < num_layers; i++)
    {
        printf("  INFO: drm_format[%d] = 0x%08x\n", i, prime_desc.layers[i].drm_format);
        printf("  INFO: num_planes[%d] = %d\n", i, prime_desc.layers[i].num_planes);
        printf("  INFO: object_index[%d] = %d, %d, %d, %d\n", i, 
            prime_desc.layers[i].object_index[0], 
            prime_desc.layers[i].object_index[1], 
            prime_desc.layers[i].object_index[2], 
            prime_desc.layers[i].object_index[3]);
        printf("  INFO: offset[%d] = %d, %d, %d, %d\n", i, 
            prime_desc.layers[i].offset[0], 
            prime_desc.layers[i].offset[1], 
            prime_desc.layers[i].offset[2], 
            prime_desc.layers[i].offset[3]);
        printf("  INFO: pitch[%d] = %d, %d, %d, %d\n", i, 
            prime_desc.layers[i].pitch[0], 
            prime_desc.layers[i].pitch[1], 
            prime_desc.layers[i].pitch[2], 
            prime_desc.layers[i].pitch[3]);
    }
}

int main() 
{
    ze_result_t result;
    const ze_device_type_t type = ZE_DEVICE_TYPE_GPU;
    ze_driver_handle_t pDriver = nullptr;
    ze_device_handle_t pDevice = nullptr;

    VAStatus va_status;
    if (initVA()) {
        printf("ERROR: initVA failed!\n");
        return -1;
    }

    // get NV12 surface from HW decode output
    VASurfaceID va_frame;
    if(decodeFrame(va_frame)) {
        printf("ERROR: decode failed\n");
        return -1;
    }

    VADRMPRIMESurfaceDescriptor prime_desc = {};
    va_status = vaExportSurfaceHandle(va_dpy, va_frame, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, VA_EXPORT_SURFACE_READ_WRITE | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &prime_desc);
    if (va_status != VA_STATUS_SUCCESS) {
        printf("ERROR: vaExportSurfaceHandle failed!\n");
        return -1;
    }
    printDesc(prime_desc);

    int dma_buf_fd = prime_desc.objects[0].fd;
    uint32_t surf_size = prime_desc.objects[0].size;
    uint32_t surf_pitch = prime_desc.layers[0].pitch[0];
    

    result = zeInit(0);
    if(result != ZE_RESULT_SUCCESS) {
        printf("ERROR: zeInit failed with return code: 0x%08x\n", result);
        return -1;
    }

    uint32_t driverCount = 0;
    result = zeDriverGet(&driverCount, nullptr);
    if(result != ZE_RESULT_SUCCESS) {
        printf("ERROR: zeDriverGet Failed with return code: 0x%08x\n", result);
        return -1;
    }
    printf("INFO: driver count = %d\n", driverCount);

    std::vector<ze_driver_handle_t> drivers(driverCount);
    result = zeDriverGet( &driverCount, drivers.data() );
    if(result != ZE_RESULT_SUCCESS) {
        printf("ERROR: zeDriverGet Failed with return code: 0x%08x\n", result);
        return -1;
    }

    for( uint32_t driver = 0; driver < driverCount; ++driver ) {
        pDriver = drivers[driver];
        pDevice = findDevice(pDriver, type);
        if( pDevice ) {
            printf("INFO: find device handle = 0x%08llx\n", (uint64_t)pDevice);
            break;
        }
    }

    if( !pDevice ) {
        printf("ERROR: cannot find a proper device\n");
        return -1;
    }

    // Create the context
    ze_context_handle_t context;
    ze_context_desc_t context_desc = {};
    context_desc.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;
    result = zeContextCreate(pDriver, &context_desc, &context);
    if(result != ZE_RESULT_SUCCESS) {
        printf("ERROR: zeContextCreate Failed with return code: 0x%08x\n", result);
        return -1;
    }

    // Set up the request to import the external memory handle
    ze_external_memory_import_fd_t import_fd = {
        ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_FD,
        nullptr, // pNext
        ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF,
        dma_buf_fd
    };

    // allocate memory for results
    ze_device_mem_alloc_desc_t alloc_desc = {
        ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,
        nullptr,
        0, // flags
        0  // ordinal
    };

    size_t size = surf_size;
    size_t alignment = 4*1024;
    void* ptr = nullptr;
    // Link the request into the allocation descriptor and allocate
    alloc_desc.pNext = &import_fd;
    result = zeMemAllocDevice(context, &alloc_desc, size, alignment, pDevice, &ptr);
    if(result != ZE_RESULT_SUCCESS) {
        printf("ERROR: zeMemAllocDevice Failed with return code: 0x%08x\n", result);
        return -1;
    }

    zeContextDestroy(context);

    printf("done\n");
    return 0;
}