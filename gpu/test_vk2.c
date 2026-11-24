// Offscreen Vulkan smoke test for the MTK/PowerVR HAL driver on bionic: fill a buffer on the GPU and read it back.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>
#include <vulkan/vulkan.h>
struct hw_module_t; struct hw_device_t;
typedef struct hw_module_methods_t { int (*open)(const struct hw_module_t *m, const char *id, struct hw_device_t **dev); } hw_module_methods_t;
typedef struct hw_module_t { uint32_t tag; uint16_t module_api_version; uint16_t hal_api_version; const char *id; const char *name; const char *author; hw_module_methods_t *methods; void *dso; uint64_t reserved[32 - 7]; } hw_module_t;
typedef struct hw_device_t { uint32_t tag; uint32_t version; struct hw_module_t *module; uint64_t reserved[12]; int (*close)(struct hw_device_t *); } hw_device_t;
typedef struct { hw_device_t common; PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties; PFN_vkCreateInstance CreateInstance; PFN_vkGetInstanceProcAddr GetInstanceProcAddr; } hwvulkan_device_t;
struct bt { int n; };
static _Unwind_Reason_Code cb(struct _Unwind_Context *c, void *a){ struct bt *b=a; uintptr_t pc=_Unwind_GetIP(c); Dl_info i; if(dladdr((void*)pc,&i)) printf("  #%d %p %s (%s)\n",b->n,(void*)pc,i.dli_sname?i.dli_sname:"?",i.dli_fname); b->n++; return b->n>20?_URC_END_OF_STACK:_URC_NO_REASON; }
static void segv(int s,siginfo_t*si,void*u){ printf("SIGSEGV addr=%p\n",si->si_addr); struct bt b={0}; _Unwind_Backtrace(cb,&b); _exit(139); }
#define CK(x) do{ VkResult r=(x); printf("%-40s -> %d\n",#x,r); if(r){return 1;} }while(0)
#include "tri_vert.h"
#include "tri_frag.h"
#define W 256
#define H 256
int main(void){
  setvbuf(stdout,0,_IONBF,0); struct sigaction sa={0}; sa.sa_sigaction=segv; sa.sa_flags=SA_SIGINFO; sigaction(SIGSEGV,&sa,0);
  void *h=dlopen("/vendor/lib64/hw/vulkan.mt6765.so",RTLD_NOW); if(!h){printf("dlopen: %s\n",dlerror());return 1;}
  hw_module_t *m=dlsym(h,"HMI"); hw_device_t *dev=0; if(m->methods->open(m,"vk0",&dev)) return 1;
  hwvulkan_device_t *vd=(hwvulkan_device_t*)dev; PFN_vkGetInstanceProcAddr gipa=vd->GetInstanceProcAddr;
  VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO,0,"t",1,"e",1,VK_API_VERSION_1_0}; VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,0,0,&ai,0,0,0,0};
  VkInstance inst; CK(vd->CreateInstance(&ici,0,&inst));
#define I(n) PFN_##n n=(void*)gipa(inst,#n); if(!n){printf("missing %s\n",#n);return 1;}
  I(vkEnumeratePhysicalDevices) I(vkGetPhysicalDeviceMemoryProperties) I(vkCreateDevice) I(vkGetDeviceProcAddr)
  VkPhysicalDevice pd; uint32_t n=1; vkEnumeratePhysicalDevices(inst,&n,&pd);
  float prio=1; VkDeviceQueueCreateInfo dq={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,0,0,0,1,&prio}; VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,0,0,1,&dq,0,0,0,0,0};
  VkDevice d; CK(vkCreateDevice(pd,&dci,0,&d));
#define D(n) PFN_##n n=(void*)vkGetDeviceProcAddr(d,#n); if(!n){printf("missing %s\n",#n);return 1;}
  D(vkGetDeviceQueue) D(vkCreateImage) D(vkGetImageMemoryRequirements) D(vkAllocateMemory) D(vkBindImageMemory) D(vkCreateImageView) D(vkCreateRenderPass) D(vkCreateFramebuffer)
  D(vkCreateShaderModule) D(vkCreatePipelineLayout) D(vkCreateGraphicsPipelines) D(vkCreateBuffer) D(vkGetBufferMemoryRequirements) D(vkBindBufferMemory) D(vkMapMemory)
  D(vkCreateCommandPool) D(vkAllocateCommandBuffers) D(vkBeginCommandBuffer) D(vkCmdBeginRenderPass) D(vkCmdBindPipeline) D(vkCmdDraw) D(vkCmdEndRenderPass) D(vkCmdCopyImageToBuffer)
  D(vkEndCommandBuffer) D(vkQueueSubmit) D(vkCreateFence) D(vkWaitForFences)
  VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd,&mp);
  VkQueue q; vkGetDeviceQueue(d,0,0,&q);
  VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,0,0,VK_IMAGE_TYPE_2D,VK_FORMAT_R8G8B8A8_UNORM,{W,H,1},1,1,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_SHARING_MODE_EXCLUSIVE,0,0,VK_IMAGE_LAYOUT_UNDEFINED};
  VkImage img; CK(vkCreateImage(d,&ii,0,&img)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(d,img,&mr);
  uint32_t mt=0xffff; for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((mr.memoryTypeBits&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){mt=i;break;}
  VkMemoryAllocateInfo mai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,mr.size,mt}; VkDeviceMemory im; CK(vkAllocateMemory(d,&mai,0,&im)); CK(vkBindImageMemory(d,img,im,0));
  VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,0,0,img,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_R8G8B8A8_UNORM,{0,0,0,0},{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}; VkImageView iv; CK(vkCreateImageView(d,&vi,0,&iv));
  VkAttachmentDescription ad={0,VK_FORMAT_R8G8B8A8_UNORM,VK_SAMPLE_COUNT_1_BIT,VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,VK_ATTACHMENT_LOAD_OP_DONT_CARE,VK_ATTACHMENT_STORE_OP_DONT_CARE,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
  VkAttachmentReference ar={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; VkSubpassDescription sd={0,VK_PIPELINE_BIND_POINT_GRAPHICS,0,0,1,&ar,0,0,0,0};
  VkRenderPassCreateInfo rpi={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,0,0,1,&ad,1,&sd,0,0}; VkRenderPass rp; CK(vkCreateRenderPass(d,&rpi,0,&rp));
  VkFramebufferCreateInfo fbi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,0,0,rp,1,&iv,W,H,1}; VkFramebuffer fb; CK(vkCreateFramebuffer(d,&fbi,0,&fb));
  VkShaderModuleCreateInfo smv={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,0,0,sizeof(tri_vert),(const uint32_t*)tri_vert}, smf={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,0,0,sizeof(tri_frag),(const uint32_t*)tri_frag};
  VkShaderModule vs,fs; CK(vkCreateShaderModule(d,&smv,0,&vs)); CK(vkCreateShaderModule(d,&smf,0,&fs));
  VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,0,0,0,0,0,0}; VkPipelineLayout pl; CK(vkCreatePipelineLayout(d,&pli,0,&pl));
  VkPipelineShaderStageCreateInfo st[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,0,0,VK_SHADER_STAGE_VERTEX_BIT,vs,"main",0},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,0,0,VK_SHADER_STAGE_FRAGMENT_BIT,fs,"main",0}};
  VkPipelineVertexInputStateCreateInfo vis={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ias={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,0,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,0};
  VkViewport vp={0,0,W,H,0,1}; VkRect2D sc={{0,0},{W,H}}; VkPipelineViewportStateCreateInfo vps={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,0,0,1,&vp,1,&sc};
  VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,0,0,0,0,VK_POLYGON_MODE_FILL,VK_CULL_MODE_NONE,VK_FRONT_FACE_COUNTER_CLOCKWISE,0,0,0,0,1.0f};
  VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,0,0,VK_SAMPLE_COUNT_1_BIT};
  VkPipelineColorBlendAttachmentState cba={0,0,0,0,0,0,0,0xF}; VkPipelineColorBlendStateCreateInfo cbs={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,0,0,0,0,1,&cba};
  VkGraphicsPipelineCreateInfo gpi={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,0,0,2,st,&vis,&ias,0,&vps,&rs,&ms,0,&cbs,0,pl,rp,0,0,-1};
  VkPipeline pipe; CK(vkCreateGraphicsPipelines(d,0,1,&gpi,0,&pipe));
  VkBufferCreateInfo bci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,0,0,W*H*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_SHARING_MODE_EXCLUSIVE,0,0}; VkBuffer buf; CK(vkCreateBuffer(d,&bci,0,&buf));
  VkMemoryRequirements br; vkGetBufferMemoryRequirements(d,buf,&br); uint32_t bt=0xffff; for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((br.memoryTypeBits&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)){bt=i;break;}
  VkMemoryAllocateInfo bai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,br.size,bt}; VkDeviceMemory bm; CK(vkAllocateMemory(d,&bai,0,&bm)); CK(vkBindBufferMemory(d,buf,bm,0));
  uint8_t *px; CK(vkMapMemory(d,bm,0,W*H*4,0,(void**)&px)); memset(px,0x55,W*H*4);
  VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,0,0,0}; VkCommandPool cp; CK(vkCreateCommandPool(d,&cpi,0,&cp));
  VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,0,cp,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1}; VkCommandBuffer cb; CK(vkAllocateCommandBuffers(d,&cai,&cb));
  VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CK(vkBeginCommandBuffer(cb,&bi));
  VkClearValue cv={.color={{0.1f,0.1f,0.1f,1}}}; VkRenderPassBeginInfo rbi={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,0,rp,fb,{{0,0},{W,H}},1,&cv};
  vkCmdBeginRenderPass(cb,&rbi,VK_SUBPASS_CONTENTS_INLINE); vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe); vkCmdDraw(cb,3,1,0,0); vkCmdEndRenderPass(cb);
  VkBufferImageCopy bc={0,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0,0,0},{W,H,1}}; vkCmdCopyImageToBuffer(cb,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buf,1,&bc); CK(vkEndCommandBuffer(cb));
  VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence f; CK(vkCreateFence(d,&fi,0,&f)); VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,0,0,0,0,1,&cb,0,0};
  CK(vkQueueSubmit(q,1,&si,f)); CK(vkWaitForFences(d,1,&f,1,10000000000ull));
  uint8_t *c=px+(H/2*W+W/2)*4, *e=px+(5*W+5)*4, *t=px+(200*W+128)*4;
  printf("center px = %u %u %u %u, corner = %u %u %u %u, low-mid = %u %u %u %u\n",c[0],c[1],c[2],c[3],e[0],e[1],e[2],e[3],t[0],t[1],t[2],t[3]);
  FILE *o=fopen("/data/tri.ppm","wb"); fprintf(o,"P6\n%d %d\n255\n",W,H); for(int i=0;i<W*H;i++) fwrite(px+i*4,1,3,o); fclose(o);
  return 0;
}
