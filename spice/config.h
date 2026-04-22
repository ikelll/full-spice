/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define to one of `_getb67', `GETB67', `getb67' for Cray-2 and Cray-YMP
   systems. This function is required for `alloca.c' support on those systems.
   */
/* #undef CRAY_STACKSEG_END */

/* Define to 1 if using `alloca.c'. */
/* #undef C_ALLOCA */

/* Define if the agent-interface instrumentation is enabled */
/* #undef ENABLE_AGENT_INTERFACE */

/* Enable extra checks on code */
/* #undef ENABLE_EXTRA_CHECKS */

/* Define if the recorder instrumentation is enabled */
/* #undef ENABLE_RECORDER */

/* Enable compile-time and run-time bounds-checking, and some warnings. */
#if !defined _FORTIFY_SOURCE &&  defined __OPTIMIZE__ && __OPTIMIZE__
# define _FORTIFY_SOURCE 2
#endif


/* Define to 1 if you have `alloca', as a function or macro. */
#define HAVE_ALLOCA 1

/* Define to 1 if you have <alloca.h> and it should be used (not on Ultrix).
   */
#define HAVE_ALLOCA_H 1

/* define if the compiler supports basic C++11 syntax */
#define HAVE_CXX11 1

/* Define to 1 if you have the declaration of `SSL_CTX_set_ecdh_auto', and to
   0 if you don't. */
#define HAVE_DECL_SSL_CTX_SET_ECDH_AUTO 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <drm/drm_fourcc.h> header file. */
#define HAVE_DRM_DRM_FOURCC_H 1

/* Define to 1 if you have the <execinfo.h> header file. */
#define HAVE_EXECINFO_H 1

/* Define if supporting GStreamer 1.0 */
#define HAVE_GSTREAMER_1_0 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <linux/sockios.h> header file. */
#define HAVE_LINUX_SOCKIOS_H 1

/* Define to 1 if you have the `LZ4_compress_fast_continue' function. */
#define HAVE_LZ4_COMPRESS_FAST_CONTINUE 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define if you have POSIX threads libraries and header files. */
#define HAVE_PTHREAD 1

/* Define to 1 if you have the <pthread_np.h> header file. */
/* #undef HAVE_PTHREAD_NP_H */

/* Have PTHREAD_PRIO_INHERIT. */
#define HAVE_PTHREAD_PRIO_INHERIT 1

/* whether Cyrus SASL is available for authentication */
/* #undef HAVE_SASL */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if <netinet/tcp.h> has a TCP_KEEPIDLE definition */
#define HAVE_TCP_KEEPIDLE 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "spice"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "spice-devel@lists.freedesktop.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "spice"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "spice 0.16.0.5-4a07d-dirty"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "spice"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.16.0.5-4a07d-dirty"

/* Define to necessary symbol if this constant uses a non-standard name on
   your system. */
/* #undef PTHREAD_CREATE_JOINABLE */

/* Enable SPICE statistics */
/* #undef RED_STATISTICS */

/* Enable more type safe version of SPICE_CONTAINEROF */
#define SPICE_USE_SAFER_CONTAINEROF 1

/* If using the C implementation of alloca, define if you know the
   direction of stack growth for your system; otherwise it will be
   automatically deduced at runtime.
	STACK_DIRECTION > 0 => grows toward higher addresses
	STACK_DIRECTION < 0 => grows toward lower addresses
	STACK_DIRECTION = 0 => direction of growth unknown */
/* #undef STACK_DIRECTION */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to build with lz4 support */
#define USE_LZ4 1

/* Define if supporting smartcard proxying */
#define USE_SMARTCARD 1

/* Version number of package */
#define VERSION "0.16.0.5-4a07d-dirty"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Minimal Win32 version */
/* #undef _WIN32_WINNT */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */
