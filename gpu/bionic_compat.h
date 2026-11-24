/* glibc build of libhybris against Android 12 NDK headers: neutralise bionic-only annotations. */
#ifndef BIONIC_COMPAT_H
#define BIONIC_COMPAT_H
#define __INTRODUCED_IN(x)
#define __INTRODUCED_IN_32(x)
#define __INTRODUCED_IN_64(x)
#define __DEPRECATED_IN(x, ...)
#define __REMOVED_IN(x, ...)
#define __BIONIC_AVAILABILITY_GUARD(x) 1
#define __ANDROID_API_FUTURE__ 10000
#define _Nullable
#define _Nonnull
#define _Null_unspecified
#endif
