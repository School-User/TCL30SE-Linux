#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
int main(int argc, char **argv) {
  setenv("LD_PRELOAD", "/data/libpropshim.so", 1);
  setenv("LD_LIBRARY_PATH", "/vendor/lib64/egl:/vendor/lib64:/system/lib64:/apex/com.android.vndk.v31/lib64:/apex/com.android.runtime/lib64/bionic:/apex/com.android.runtime/lib64", 1);
  char *a[32]; int n = 0; a[n++] = "/system/bin/linker64"; for (int i = 1; i < argc && n < 30; i++) a[n++] = argv[i]; a[n] = 0;
  execv(a[0], a); perror("execv"); return 1;
}
