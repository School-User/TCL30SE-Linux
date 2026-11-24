// LD_PRELOAD shim: send vendor liblog output to stderr (no logd in the chroot).
#include <stdarg.h>
#include <stdio.h>
static const char P[] = "VDSEIWF";
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap)
{
	fprintf(stderr, "%c/%s: ", (prio >= 2 && prio <= 7) ? P[prio - 1] : '?', tag ? tag : "");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	return 1;
}
int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	int r = __android_log_vprint(prio, tag, fmt, ap);
	va_end(ap); return r;
}
int __android_log_buf_print(int buf, int prio, const char *tag, const char *fmt, ...)
{
	(void)buf; va_list ap; va_start(ap, fmt);
	int r = __android_log_vprint(prio, tag, fmt, ap);
	va_end(ap); return r;
}
int __android_log_write(int prio, const char *tag, const char *msg)
{
	return __android_log_print(prio, tag, "%s", msg);
}
int __android_log_buf_write(int buf, int prio, const char *tag, const char *msg)
{
	(void)buf; return __android_log_print(prio, tag, "%s", msg);
}
int __android_log_is_loggable(int prio, const char *tag, int def) { (void)prio; (void)tag; (void)def; return 1; }
