// LD_PRELOAD shim: serves Android system properties from a built-in table (no property service on this system).
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
static void lg(const char *a, const char *b){ int fd=open("/data/propshim.log",O_WRONLY|O_APPEND|O_CREAT,0666); if(fd>=0){ char m[300]; int n=snprintf(m,sizeof m,"%s %s\n",a,b?b:""); write(fd,m,n); close(fd);} }
struct prop { const char *name; const char *value; };
static struct prop T[] = {
 {"ro.hardware.egl","mtk"},{"ro.board.platform","mt6765"},{"ro.hardware","mt6765"},{"ro.build.version.sdk","31"},{"ro.build.version.release","12"},
 {"ro.product.cpu.abilist","arm64-v8a"},{"ro.product.cpu.abi","arm64-v8a"},{"ro.vendor.build.version.sdk","31"},{"ro.opengles.version","196610"},
 {"ro.hardware.vulkan","mt6765"},{"debug.egl.profiler","0"},{"ro.config.low_ram","false"},{"ro.debuggable","0"},
 {"ro.vndk.version","31"},{"ro.treble.enabled","true"},{"ro.product.vendor.device","tcl30se"},
 {"hwservicemanager.ready","true"},{"servicemanager.ready","true"},{"vendor.all.modules.ready","1"},{"sys.boot_completed","1"},
 {"ro.boot.hardware","mt6765"},{"ro.build.type","user"},{"ro.secure","1"},{"ro.kernel.qemu",""},
};
#define NT (sizeof(T)/sizeof(T[0]))
typedef struct prop prop_info;
const prop_info *__system_property_find(const char *name) { lg("find",name); for (unsigned i = 0; i < NT; i++) if (!strcmp(name, T[i].name)) return &T[i]; return NULL; }
int __system_property_get(const char *name, char *value) { const prop_info *p = __system_property_find(name); if (!p) { value[0] = 0; return 0; } strcpy(value, p->value); return strlen(value); }
int __system_property_read(const prop_info *pi, char *name, char *value) { if (name) strcpy(name, pi->name); strcpy(value, pi->value); return strlen(value); }
void __system_property_read_callback(const prop_info *pi, void (*cb)(void *, const char *, const char *, uint32_t), void *cookie) { cb(cookie, pi->name, pi->value, 2); }
uint32_t __system_property_serial(const prop_info *pi) { (void)pi; return 2; }
uint32_t __system_property_area_serial(void) { return 2; }
int __system_property_set(const char *name, const char *value) { (void)name; (void)value; return 0; }
int __system_property_wait(const prop_info *pi, uint32_t old, uint32_t *newv, const struct timespec *t) { lg("wait",pi?pi->name:"(global)"); usleep(20000); (void)pi; (void)t; if (newv) *newv = old + 1; return 1; }
const prop_info *__system_property_find_nth(unsigned n) { return n < NT ? &T[n] : NULL; }
int __system_property_foreach(void (*cb)(const prop_info *, void *), void *c) { for (unsigned i = 0; i < NT; i++) cb(&T[i], c); return 0; }
int __system_properties_init(void) { return 0; }
