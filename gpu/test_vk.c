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
int main(void){
  setvbuf(stdout,0,_IONBF,0); struct sigaction sa={0}; sa.sa_sigaction=segv; sa.sa_flags=SA_SIGINFO; sigaction(SIGSEGV,&sa,0);
  void *h=dlopen("/vendor/lib64/hw/vulkan.mt6765.so",RTLD_NOW); if(!h){printf("dlopen: %s\n",dlerror());return 1;}
  hw_module_t *m=dlsym(h,"HMI"); if(!m){printf("no HMI\n");return 1;} printf("module id=%s name=%s\n",m->id,m->name);
  hw_device_t *dev=0; int r=m->methods->open(m,"vk0",&dev); printf("open -> %d dev=%p\n",r,dev); if(r) return 1;
  hwvulkan_device_t *vd=(hwvulkan_device_t*)dev; PFN_vkGetInstanceProcAddr gipa=vd->GetInstanceProcAddr;
  VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO,0,"t",1,"e",1,VK_API_VERSION_1_0}; VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,0,0,&ai,0,0,0,0};
  VkInstance inst; CK(vd->CreateInstance(&ici,0,&inst));
  PFN_vkEnumeratePhysicalDevices enumpd=(void*)gipa(inst,"vkEnumeratePhysicalDevices"); uint32_t n=0; CK(enumpd(inst,&n,0)); printf("physical devices: %u\n",n); if(!n) return 1;
  VkPhysicalDevice pd; n=1; enumpd(inst,&n,&pd);
  PFN_vkGetPhysicalDeviceProperties gpp=(void*)gipa(inst,"vkGetPhysicalDeviceProperties"); VkPhysicalDeviceProperties pp; gpp(pd,&pp);
  printf("device: %s vendor=0x%x id=0x%x api=%u.%u.%u driver=0x%x\n",pp.deviceName,pp.vendorID,pp.deviceID,VK_VERSION_MAJOR(pp.apiVersion),VK_VERSION_MINOR(pp.apiVersion),VK_VERSION_PATCH(pp.apiVersion),pp.driverVersion);
  PFN_vkGetPhysicalDeviceQueueFamilyProperties gq=(void*)gipa(inst,"vkGetPhysicalDeviceQueueFamilyProperties"); uint32_t qn=0; gq(pd,&qn,0); VkQueueFamilyProperties qp[8]; if(qn>8) qn=8; gq(pd,&qn,qp);
  uint32_t qf=0; for(uint32_t i=0;i<qn;i++){ printf("queue family %u flags=0x%x count=%u\n",i,qp[i].queueFlags,qp[i].queueCount); if(qp[i].queueFlags&(VK_QUEUE_TRANSFER_BIT|VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){qf=i;} }
  float prio=1; VkDeviceQueueCreateInfo dq={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,0,0,qf,1,&prio}; VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,0,0,1,&dq,0,0,0,0,0};
  PFN_vkCreateDevice cd=(void*)gipa(inst,"vkCreateDevice"); VkDevice dev2; CK(cd(pd,&dci,0,&dev2));
  PFN_vkGetDeviceProcAddr gdpa=(void*)gipa(inst,"vkGetDeviceProcAddr");
#define D(n) PFN_##n n=(void*)gdpa(dev2,#n); if(!n){printf("missing %s\n",#n);return 1;}
  D(vkGetDeviceQueue) D(vkCreateBuffer) D(vkGetBufferMemoryRequirements) D(vkAllocateMemory) D(vkBindBufferMemory) D(vkMapMemory) D(vkCreateCommandPool) D(vkAllocateCommandBuffers)
  D(vkBeginCommandBuffer) D(vkCmdFillBuffer) D(vkEndCommandBuffer) D(vkQueueSubmit) D(vkQueueWaitIdle) D(vkCreateFence) D(vkWaitForFences)
  PFN_vkGetPhysicalDeviceMemoryProperties gmp=(void*)gipa(inst,"vkGetPhysicalDeviceMemoryProperties"); VkPhysicalDeviceMemoryProperties mp; gmp(pd,&mp);
  VkQueue q; vkGetDeviceQueue(dev2,qf,0,&q);
  VkBufferCreateInfo bci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,0,0,4096,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_SHARING_MODE_EXCLUSIVE,0,0}; VkBuffer buf; CK(vkCreateBuffer(dev2,&bci,0,&buf));
  VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev2,buf,&mr); uint32_t mt=0xffff; for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((mr.memoryTypeBits&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)){mt=i;break;}
  printf("memtype %u size %llu\n",mt,(unsigned long long)mr.size); if(mt==0xffff) return 1;
  VkMemoryAllocateInfo mai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,0,mr.size,mt}; VkDeviceMemory mem; CK(vkAllocateMemory(dev2,&mai,0,&mem)); CK(vkBindBufferMemory(dev2,buf,mem,0));
  uint32_t *p; CK(vkMapMemory(dev2,mem,0,4096,0,(void**)&p)); memset(p,0,4096);
  VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,0,0,qf}; VkCommandPool cp; CK(vkCreateCommandPool(dev2,&cpi,0,&cp));
  VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,0,cp,VK_COMMAND_BUFFER_LEVEL_PRIMARY,1}; VkCommandBuffer cb2; CK(vkAllocateCommandBuffers(dev2,&cai,&cb2));
  VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,0,0,0}; CK(vkBeginCommandBuffer(cb2,&bi)); vkCmdFillBuffer(cb2,buf,0,4096,0xDEADBEEF); CK(vkEndCommandBuffer(cb2));
  VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,0,0}; VkFence f; CK(vkCreateFence(dev2,&fi,0,&f));
  VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,0,0,0,0,1,&cb2,0,0}; CK(vkQueueSubmit(q,1,&si,f)); CK(vkWaitForFences(dev2,1,&f,1,5000000000ull));
  printf("buffer[0]=0x%08x buffer[1023]=0x%08x (expect 0xdeadbeef)\n",p[0],p[1023]);
  return p[0]==0xDEADBEEF?0:3;
}
