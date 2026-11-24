// Stub libselinux for running the Android HIDL service stack on a kernel without SELinux: everything is allowed.
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
static int dummy;
int getcon(char **c) { *c = strdup("u:r:su:s0"); return 0; }
int getpidcon(pid_t p, char **c) { (void)p; *c = strdup("u:r:su:s0"); return 0; }
int getpeercon(int fd, char **c) { (void)fd; *c = strdup("u:r:su:s0"); return 0; }
int getfilecon(const char *p, char **c) { (void)p; *c = strdup("u:object_r:default:s0"); return 0; }
void freecon(char *c) { free(c); }
int selabel_lookup(void *h, char **c, const char *k, int t) { (void)h; (void)k; (void)t; *c = strdup("u:object_r:default:s0"); return 0; }
int selabel_lookup_raw(void *h, char **c, const char *k, int t) { return selabel_lookup(h, c, k, t); }
void selabel_close(void *h) { (void)h; }
void *selinux_android_hw_service_context_handle(void) { return &dummy; }
void *selinux_android_service_context_handle(void) { return &dummy; }
void *selinux_android_vendor_service_context_handle(void) { return &dummy; }
void *selinux_android_file_context_handle(void) { return &dummy; }
int selinux_check_access(const char *s, const char *t, const char *c, const char *p, void *a) { (void)s;(void)t;(void)c;(void)p;(void)a; return 0; }
int selinux_log_callback(int type, const char *fmt, ...) { (void)type; (void)fmt; return 0; }
void selinux_set_callback(int type, void *cb) { (void)type; (void)cb; }
int selinux_status_open(int f) { (void)f; return 0; }
int selinux_status_updated(void) { return 0; }
void selinux_status_close(void) {}
int is_selinux_enabled(void) { return 0; }
int is_selinux_mls_enabled(void) { return 0; }
int security_getenforce(void) { return 0; }
int setcon(const char *c) { (void)c; return 0; }
int setexeccon(const char *c) { (void)c; return 0; }
int setfscreatecon(const char *c) { (void)c; return 0; }
int fgetfilecon(int fd, char **c) { (void)fd; *c = strdup("u:object_r:default:s0"); return 0; }
int fgetfilecon_raw(int fd, char **c) { (void)fd; *c = strdup("u:object_r:default:s0"); return 0; }
int lsetfilecon(const char *p, const char *c) { (void)p; (void)c; return 0; }
int setfilecon(const char *p, const char *c) { (void)p; (void)c; return 0; }
int security_policyvers(void) { return 33; }
int selinux_android_restorecon(const char *p, unsigned f) { (void)p; (void)f; return 0; }
int selinux_android_seapp_context_init(void) { return 0; }
int selinux_android_setcon(const char *c) { (void)c; return 0; }
int selinux_android_setcontext(int u, int s, const char *i, const char *n) { (void)u;(void)s;(void)i;(void)n; return 0; }
int setsockcreatecon_raw(const char *c) { (void)c; return 0; }
