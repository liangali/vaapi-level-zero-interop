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

using namespace std;

#include "va/va.h"
#include "va/va_drm.h"
#include "va/va_drmcommon.h"
#include "ze_api.h"

#include "video.h"

const char* kernel_spv_file = "../add_kernel.spv";
const char* kernel_func_name = "cmdlist_add_constant";

VADisplay va_dpy = NULL;
int va_fd = -1;
bool dump_decode_output = false;

#define CHECK_VA_STATUS(va_status, func)                                    \
if (va_status != VA_STATUS_SUCCESS) {                                     \
    fprintf(stderr,"%s:%s (%d) failed, exit\n", __func__, func, __LINE__); \
    exit(1);                                                              \
} else { \
    printf("INFO[VA]: %s succeed\n", func); \
}

#define CHECK_ZE_STATUS(err, msg) \
if (err < 0 ) { \
    printf("ERROR: %s failed with err = 0x%08x, in function %s, line %d\n", msg, err, __FUNCTION__, __LINE__); \
    exit(0); \
} else { \
    printf("INFO[ZE]: %s succeed\n", msg); \
}

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
    CHECK_VA_STATUS(va_status, "vaQueryConfigEntrypoints");

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
    CHECK_VA_STATUS(va_status, "vaQueryConfigEntrypoints");

    for (size_t i = 0; i < RT_NUM; i++) {
        va_status = vaCreateSurfaces(va_dpy, VA_RT_FORMAT_YUV420, CLIP_WIDTH, CLIP_HEIGHT,  &surface_id[i], 1, NULL, 0 );
        CHECK_VA_STATUS(va_status, "vaCreateSurfaces");
    }

    /* Create a context for this decode pipe */
    va_status = vaCreateContext(va_dpy, config_id, CLIP_WIDTH, ((CLIP_HEIGHT+15)/16)*16, VA_PROGRESSIVE, surface_id, RT_NUM, &context_id);
    CHECK_VA_STATUS(va_status, "vaCreateContext");

    va_status = vaCreateBuffer(va_dpy, context_id, VAPictureParameterBufferType, pic_size, 1, &pic_param, &pic_param_buf);
    CHECK_VA_STATUS(va_status, "vaCreateBuffer type = VAPictureParameterBufferType");
    va_status = vaCreateBuffer(va_dpy, context_id, VAIQMatrixBufferType, iq_size, 1, &iq_matrix, &iqmatrix_buf );
    CHECK_VA_STATUS(va_status, "vaCreateBuffer type = VAIQMatrixBufferType");
    va_status = vaCreateBuffer(va_dpy, context_id, VASliceParameterBufferType, slc_size, 1, &slc_param, &slice_param_buf);
    CHECK_VA_STATUS(va_status, "vaCreateBuffer type = VASliceParameterBufferType");
    va_status = vaCreateBuffer(va_dpy, context_id, VASliceDataBufferType, bs_size, 1, bs_data, &slice_data_buf);
    CHECK_VA_STATUS(va_status, "vaCreateBuffer type = VASliceDataBufferType");

    /* send decode workload to GPU */
    va_status = vaBeginPicture(va_dpy, context_id, surface_id[RT_ID]);
    CHECK_VA_STATUS(va_status, "vaBeginPicture");
    va_status = vaRenderPicture(va_dpy,context_id, &pic_param_buf, 1);
    CHECK_VA_STATUS(va_status, "vaRenderPicture");
    va_status = vaRenderPicture(va_dpy,context_id, &iqmatrix_buf, 1);
    CHECK_VA_STATUS(va_status, "vaRenderPicture");
    va_status = vaRenderPicture(va_dpy,context_id, &slice_param_buf, 1);
    CHECK_VA_STATUS(va_status, "vaRenderPicture");
    va_status = vaRenderPicture(va_dpy,context_id, &slice_data_buf, 1);
    CHECK_VA_STATUS(va_status, "vaRenderPicture");
    va_status = vaEndPicture(va_dpy,context_id);
    CHECK_VA_STATUS(va_status, "vaEndPicture");

    va_status = vaSyncSurface(va_dpy, surface_id[RT_ID]);
    CHECK_VA_STATUS(va_status, "vaSyncSurface");

    frame = surface_id[RT_ID];

    if (dump_decode_output) {
        VAImage output_image;
        va_status = vaDeriveImage(va_dpy, surface_id[RT_ID], &output_image);
        CHECK_VA_STATUS(va_status, "vaDeriveImage");

        void *out_buf = nullptr;
        va_status = vaMapBuffer(va_dpy, output_image.buf, &out_buf);
        CHECK_VA_STATUS(va_status, "vaMapBuffer");

        FILE *fp = fopen("out_224x224.nv12", "wb+");
        // dump y_plane
        char *y_buf = (char*)out_buf;
        int y_pitch = output_image.pitches[0];
        for (size_t i = 0; i < CLIP_HEIGHT; i++) {
            fwrite(y_buf + y_pitch*i, CLIP_WIDTH, 1, fp);
        }
        // dump uv_plane
        char *uv_buf = (char*)out_buf + output_image.offsets[1];
        int uv_pitch = output_image.pitches[1];
        for (size_t i = 0; i < CLIP_HEIGHT/2; i++) {
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

void printVAImage(VAImage &img)
{
    printf("\nVAImage: ================\n");
    printf("VAImage: image_id = %d\n", img.image_id);
    printf("VAImage: buf_id   = %d\n", img.buf);
    printf("VAImage: format: \n");
    printf("            fourcc         = 0x%08x\n", img.format.fourcc);
    printf("            byte_order     = %d\n", img.format.byte_order);
    printf("            bits_per_pixel = %d\n", img.format.bits_per_pixel);
    printf("            depth          = %d\n", img.format.depth);
    printf("            red_mask       = %d\n", img.format.red_mask);
    printf("            green_mask     = %d\n", img.format.green_mask);
    printf("            blue_mask      = %d\n", img.format.blue_mask);
    printf("            alpha_mask     = %d\n", img.format.alpha_mask);
    printf("VAImage: width = %d\n", img.width);
    printf("VAImage: height = %d\n", img.height);
    printf("VAImage: data_size = %d\n", img.data_size);
    printf("VAImage: num_planes = %d\n", img.num_planes);
    printf("VAImage: pitches = [%d, %d, %d]\n", img.pitches[0], img.pitches[1], img.pitches[2]);
    printf("VAImage: offsets = [%d, %d, %d]\n", img.offsets[0], img.offsets[1], img.offsets[2]);
    printf("VAImage: num_palette_entries = %d\n", img.num_palette_entries);
    printf("VAImage: entry_bytes = %d\n", img.entry_bytes);
    printf("VAImage: component_order = [%d, %d, %d, %d]\n", img.component_order[0], img.component_order[1], img.component_order[2], img.component_order[3]);
    printf("VAImage: ================\n");
}

void printSurface(VASurfaceID frame)
{
    VAStatus va_status;
    VAImage va_img = {};
    void *surf_ptr = nullptr;

    va_status = vaDeriveImage(va_dpy, frame, &va_img);
    CHECK_VA_STATUS(va_status, "vaDeriveImage");
    uint16_t w = va_img.width;
    uint16_t h = va_img.height;
    uint32_t pitch = va_img.pitches[0];
    uint32_t uv_offset = va_img.offsets[1];
    printVAImage(va_img);
    
    va_status = vaMapBuffer(va_dpy, va_img.buf, &surf_ptr);
    CHECK_VA_STATUS(va_status, "vaMapBuffer");

    char* src = (char*)surf_ptr;
    vector<char> dst(w*h*3/2, 0);

    // Y plane
    for (size_t i = 0; i < h; i++)
        memcpy(dst.data()+i*w, src+i*pitch, w);
    // UV plane
    for (size_t i = 0; i < h/2; i++)
        memcpy(dst.data()+(h+i)*w, src+uv_offset+i*pitch, w);

    FILE* fp = fopen("dec_out.nv12", "wb");
    fwrite(dst.data(), w*h*3/2, 1, fp);
    fclose(fp);

    // ffmpeg -s 224x224 -pix_fmt nv12 -f rawvideo -i dec_out.nv12 dec_out.bmp -y

    printf("\n");
    for (size_t i = 0; i < 256; i++)
    {
        printf("%d, ", (uint8_t)dst[i]);
    }
    printf("\n");

    vaUnmapBuffer(va_dpy, va_img.buf);
    vaDestroyImage(va_dpy, va_img.image_id);
}

void printDesc(VADRMPRIMESurfaceDescriptor &prime_desc)
{
    printf("INFO: width = %d\n", prime_desc.width);
    printf("INFO: height = %d\n", prime_desc.height);
    printf("INFO: fourcc = 0x%08x\n", prime_desc.fourcc);
    printf("INFO: num_objects = %d\n", prime_desc.num_objects);

    int num_objects = prime_desc.num_objects > 4 ? 4: prime_desc.num_objects;
    for (size_t i = 0; i < num_objects; i++) {
        printf("   dma_buf[%d] = %d\n", i, prime_desc.objects[i].fd);
        printf("   size[%d] = %d\n", i, prime_desc.objects[i].size);
        printf("   drm_format_modifier[%d] = %d\n", i, prime_desc.objects[i].drm_format_modifier);
    }

    int num_layers = prime_desc.num_layers > 4 ? 4: prime_desc.num_layers;
    printf("INFO: num_layers = %d\n", prime_desc.num_layers);
    for (size_t i = 0; i < num_layers; i++) {
        printf("   drm_format[%d] = 0x%08x\n", i, prime_desc.layers[i].drm_format);
        printf("   num_planes[%d] = %d\n", i, prime_desc.layers[i].num_planes);
        printf("   object_index[%d] = %d, %d, %d, %d\n", i, 
            prime_desc.layers[i].object_index[0], 
            prime_desc.layers[i].object_index[1], 
            prime_desc.layers[i].object_index[2], 
            prime_desc.layers[i].object_index[3]);
        printf("   offset[%d] = %d, %d, %d, %d\n", i, 
            prime_desc.layers[i].offset[0], 
            prime_desc.layers[i].offset[1], 
            prime_desc.layers[i].offset[2], 
            prime_desc.layers[i].offset[3]);
        printf("   pitch[%d] = %d, %d, %d, %d\n", i, 
            prime_desc.layers[i].pitch[0], 
            prime_desc.layers[i].pitch[1], 
            prime_desc.layers[i].pitch[2], 
            prime_desc.layers[i].pitch[3]);
    }
}

int readKernel(vector<char>& binary)
{
    FILE *fp = nullptr;
    size_t nsize = 0;
    fp = fopen(kernel_spv_file, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        nsize = (size_t)ftell(fp);
        fseek(fp, 0, SEEK_SET);

        binary.resize(nsize+1);
        memset(binary.data(), 0, binary.size());
        fread(binary.data(), sizeof(unsigned char), nsize, fp);

        fclose(fp);
        return 0;
    }
    
    printf("ERROR: cannot open kernel spv file %\n", kernel_spv_file);
    return -1;
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
    for(uint32_t device = 0; device < deviceCount; ++device) {
        auto phDevice = devices[device];

        ze_device_properties_t device_properties = {};
        device_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES;
        zeDeviceGetProperties(phDevice, &device_properties);

        if(type == device_properties.type) {
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
            for( uint32_t mem = 0; mem < memoryCount; ++mem ) {
                pMemoryProperties[mem].stype = ZE_STRUCTURE_TYPE_DEVICE_MEMORY_PROPERTIES;
                pMemoryProperties[mem].pNext = nullptr;
            }
            zeDeviceGetMemoryProperties(phDevice, &memoryCount, pMemoryProperties);
            for( uint32_t mem = 0; mem < memoryCount; ++mem ) {
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
            for( uint32_t cache = 0; cache < cacheCount; ++cache ) {
                pCacheProperties[cache].stype = ZE_STRUCTURE_TYPE_DEVICE_CACHE_PROPERTIES;
                pCacheProperties[cache].pNext = nullptr;
            }
            zeDeviceGetCacheProperties(phDevice, &cacheCount, pCacheProperties);
            for( uint32_t cache = 0; cache < cacheCount; ++cache ) {
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

    printSurface(va_frame);

    VADRMPRIMESurfaceDescriptor prime_desc = {};
    va_status = vaExportSurfaceHandle(va_dpy, va_frame, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, VA_EXPORT_SURFACE_READ_WRITE | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &prime_desc);
    CHECK_VA_STATUS(va_status, "vaExportSurfaceHandle");
    printDesc(prime_desc);

    int dma_buf_fd = prime_desc.objects[0].fd;
    uint32_t surf_size = prime_desc.objects[0].size;
    uint32_t surf_pitch = prime_desc.layers[0].pitch[0];

    size_t size = 0;
    size_t alignment = 0;
    result = zeInit(0);
    CHECK_ZE_STATUS(result, "zeInit");

    uint32_t driverCount = 0;
    result = zeDriverGet(&driverCount, nullptr);
    CHECK_ZE_STATUS(result, "zeDriverGet");
    printf("INFO: driver count = %d\n", driverCount);

    std::vector<ze_driver_handle_t> drivers(driverCount);
    result = zeDriverGet( &driverCount, drivers.data() );
    CHECK_ZE_STATUS(result, "zeDriverGet");

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
    CHECK_ZE_STATUS(result, "zeContextCreate");

#if 0
    // Create module
    vector<char> kernel_binary;
    if (readKernel(kernel_binary) != 0)
        return -1;
    ze_module_handle_t module = nullptr;
    ze_module_desc_t module_desc = {};
    module_desc.stype = ZE_STRUCTURE_TYPE_MODULE_DESC;
    module_desc.pNext = nullptr;
    module_desc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    module_desc.inputSize = static_cast<uint32_t>(kernel_binary.size());
    module_desc.pInputModule = reinterpret_cast<const uint8_t *>(kernel_binary.data());
    module_desc.pBuildFlags = nullptr;
    result = zeModuleCreate(context, pDevice, &module_desc, &module, nullptr);
    CHECK_ZE_STATUS(result, "zeModuleCreate");

    // Create kernel
    ze_kernel_handle_t function = nullptr;
    ze_kernel_desc_t function_desc = {};
    function_desc.stype = ZE_STRUCTURE_TYPE_KERNEL_DESC;
    function_desc.pNext = nullptr;
    function_desc.flags = 0;
    function_desc.pKernelName = kernel_func_name;
    result = zeKernelCreate(module, &function_desc, &function);
    CHECK_ZE_STATUS(result, "zeKernelCreate");

    result = zeKernelSetGroupSize(function, 1, 1, 1);
    CHECK_ZE_STATUS(result, "zeKernelSetGroupSize");

    size = 256;
    alignment = 1024;
    void *memory = nullptr;
    ze_device_mem_alloc_desc_t device_desc = {};
    device_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
    device_desc.ordinal = 0;
    device_desc.flags = 0;
    device_desc.pNext = nullptr;
    ze_host_mem_alloc_desc_t host_desc = {};
    host_desc.stype = ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC;
    host_desc.flags = 0;
    result = zeMemAllocShared(context, &device_desc, &host_desc, size, alignment, pDevice, &memory);
    CHECK_ZE_STATUS(result, "zeMemAllocShared");

    result = zeKernelSetArgumentValue(function, 0, sizeof(memory), &memory);
    CHECK_ZE_STATUS(result, "zeKernelSetArgumentValue");

    const int addval = 10;
    result = zeKernelSetArgumentValue(function, 1, sizeof(addval), &addval);
    CHECK_ZE_STATUS(result, "zeKernelSetArgumentValue");
#endif

    // Create command list
    ze_command_list_handle_t command_list = nullptr;
    ze_command_list_desc_t descriptor_cmdlist = {};
    descriptor_cmdlist.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
    descriptor_cmdlist.pNext = nullptr;
    descriptor_cmdlist.flags = 0;
    descriptor_cmdlist.commandQueueGroupOrdinal = 0;
    result = zeCommandListCreate(context, pDevice, &descriptor_cmdlist, &command_list);
    CHECK_ZE_STATUS(result, "zeCommandListCreate");

    // Create command queue
    ze_command_queue_handle_t command_queue = nullptr;
    ze_command_queue_desc_t descriptor_cmdqueue = {};
    descriptor_cmdqueue.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
    descriptor_cmdqueue.pNext = nullptr;
    descriptor_cmdqueue.flags = 0;
    descriptor_cmdqueue.mode = ZE_COMMAND_QUEUE_MODE_DEFAULT;
    descriptor_cmdqueue.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
    descriptor_cmdqueue.ordinal = 0;
    descriptor_cmdqueue.index = 0;
    ze_device_properties_t properties = {};
    result = zeDeviceGetProperties(pDevice, &properties);
    CHECK_ZE_STATUS(result, "zeDeviceGetProperties");
    result = zeCommandQueueCreate(context, pDevice, &descriptor_cmdqueue, &command_queue);
    CHECK_ZE_STATUS(result, "zeCommandQueueCreate");

#if 1
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

    void* shared_fd_mem = nullptr;
    // Link the request into the allocation descriptor and allocate
    alloc_desc.pNext = &import_fd;
    result = zeMemAllocDevice(context, &alloc_desc, surf_size, 1, pDevice, &shared_fd_mem);
    CHECK_ZE_STATUS(result, "zeMemAllocDevice");
    ze_memory_allocation_properties_t props = {};
    result = zeMemGetAllocProperties(context, shared_fd_mem, &props, nullptr);
    CHECK_ZE_STATUS(result, "zeMemGetAllocProperties");
    printf("MemAllocINFO: memory = 0x%08x, stype = %d, pNext = 0x%08x, type = %d, id = 0x%08x, pagesize = %d\n", 
        shared_fd_mem, props.stype, (uint64_t)props.pNext, props.type, props.id, props.pageSize);
#endif

    void* tmp_mem = nullptr;
    ze_device_mem_alloc_desc_t tmp_desc = {
        ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,
        nullptr,
        0, // flags
        0  // ordinal
    };
    result = zeMemAllocDevice(context, &tmp_desc, 1*1024*1024, 1, pDevice, &tmp_mem);
    CHECK_ZE_STATUS(result, "zeMemAllocDevice");

    const size_t buf_size = 1 * 1024;
    std::vector<uint8_t> host_src(buf_size, 0);
    std::vector<uint8_t> host_dst(buf_size, 0);
    for (size_t i = 0; i < buf_size; i++)
    {
        host_src[i] = i % 256;
    }

    // host_src --> tmp_mem
    result = zeCommandListAppendMemoryCopy(command_list, tmp_mem, host_src.data(), buf_size, nullptr, 0, nullptr);
    CHECK_ZE_STATUS(result, "zeCommandListAppendMemoryCopy");

    result = zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr);
    CHECK_ZE_STATUS(result, "zeCommandListAppendBarrier");

    // tmp_mem --> host_dst
    result = zeCommandListAppendMemoryCopy(command_list, host_dst.data(), tmp_mem , buf_size, nullptr, 0, nullptr);
    CHECK_ZE_STATUS(result, "zeCommandListAppendMemoryCopy");

    result = zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr);
    CHECK_ZE_STATUS(result, "zeCommandListAppendBarrier");

    result = zeCommandListClose(command_list);
    CHECK_ZE_STATUS(result, "zeCommandListClose");

    result = zeCommandQueueExecuteCommandLists(command_queue, 1, &command_list, nullptr);
    CHECK_ZE_STATUS(result, "zeCommandQueueExecuteCommandLists");

    result = zeCommandQueueSynchronize(command_queue, UINT64_MAX);
    CHECK_ZE_STATUS(result, "zeCommandQueueSynchronize");

    bool copy_passed = true;
    for (size_t i = 0; i < buf_size; i++)
    {
        if (host_dst[i] != (i%256))
        {
            copy_passed = false;
            break;
        }
    }
    if (copy_passed)
        printf("INFO: ================ Copy test passed ================ \n");
    else
        printf("INFO: !!!!!!!!!!!!!!!! Copy test failed !!!!!!!!!!!!!!!! \n");
    
    memset(host_dst.data(), 0, host_dst.size());

    result = zeCommandListAppendMemoryCopy(command_list, host_dst.data(), shared_fd_mem, buf_size, nullptr, 0, nullptr);
    CHECK_ZE_STATUS(result, "zeCommandListAppendMemoryCopy");

    result = zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr);
    CHECK_ZE_STATUS(result, "zeCommandListAppendBarrier");

    result = zeCommandListClose(command_list);
    CHECK_ZE_STATUS(result, "zeCommandListClose");

    result = zeCommandQueueExecuteCommandLists(command_queue, 1, &command_list, nullptr);
    CHECK_ZE_STATUS(result, "zeCommandQueueExecuteCommandLists");

    result = zeCommandQueueSynchronize(command_queue, UINT64_MAX);
    CHECK_ZE_STATUS(result, "zeCommandQueueSynchronize");

    printf("\n");
    for (size_t i = 0; i < 256; i++)
    {
        printf("%d, ", host_dst[i]);
    }
    printf("\n");
    
    zeContextDestroy(context);

    printf("done\n");
    return 0;
}