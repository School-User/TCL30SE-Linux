// rb [arg]: reboot(LINUX_REBOOT_CMD_RESTART2, arg). Static, no libc. "rb bootloader" reaches fastboot on the stock kernel.
static long sc(long n,long a,long b,long c,long d){register long x8 __asm__("x8")=n,x0 __asm__("x0")=a,x1 __asm__("x1")=b,x2 __asm__("x2")=c,x3 __asm__("x3")=d;__asm__("svc 0":"=r"(x0):"r"(x8),"r"(x0),"r"(x1),"r"(x2),"r"(x3):"memory");return x0;}
void _start_c(long *sp){char **argv=(char**)(sp+1);sc(142,0xfee1dead,672274793,0xA1B2C3D4,(long)(sp[0]>1?argv[1]:"bootloader"));sc(93,1,0,0,0);}
__asm__(".global _start\n_start:\n mov x0,sp\n bl _start_c\n");
