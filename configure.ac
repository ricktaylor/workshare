AC_PREREQ(2.61)

AC_INIT(thehouse,0.0.1)
AC_CONFIG_SRCDIR([src/task.c])
AC_CONFIG_HEADERS([autoheader_dummy.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_FILES([
	Makefile
])

AM_INIT_AUTOMAKE([foreign 1.10 no-define subdir-objects])

# Use the latest short output macro if available
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# I like maintainer mode
AM_MAINTAINER_MODE

AC_CANONICAL_HOST

# Set the WIN32 conditional in the makefiles if the host is MinGW
# If the host is Cygwin, error
AS_CASE([$host_os],
	[*cygwin*],[AC_MSG_ERROR([Don't build for cygwin, use a native Win32 port such as MinGW])],
	[mingw*],[win32=true]
)
AM_CONDITIONAL([WIN32],[test "x$win32" = "xtrue"])

# Add the --enable-debug arg
AC_ARG_ENABLE([debug],AS_HELP_STRING([--enable-debug],[Turn on debugging]),[debug=true],[debug=false])
AM_CONDITIONAL([DEBUG], [test "x$debug" = "xtrue"])

OO_PROG_CC

AX_CC_FOR_BUILD

# Check the multi-threading flags
AS_CASE([$host_os],
	[mingw*],[PTHREAD_CFLAGS=-mthreads AC_SUBST(PTHREAD_LIBS) AC_SUBST(PTHREAD_CFLAGS)],
	[AX_PTHREAD]
)

AX_APPEND_COMPILE_FLAGS([-std=c11],[CFLAGS])

# Set sensible default CFLAGS if necessary
AS_IF([test "x$oo_test_CFLAGS" != "xset"],
[
  AS_IF([test "x$debug" = "xtrue"],[
    AX_APPEND_COMPILE_FLAGS([-ggdb -O2],[CFLAGS])
  ],[
    AX_APPEND_COMPILE_FLAGS([-O2],[CFLAGS])
  ])

  #AS_CASE([$host_os],
    #[mingw*],[AX_APPEND_COMPILE_FLAGS([-march=x86_64 -mtune=native],[CFLAGS])]
  #)
])

# Just for Win32 with GCC
AS_CASE([$host_os],[mingw*],
[
  AX_APPEND_COMPILE_FLAGS([-static-libgcc],[CFLAGS])
])

# Always try to hide symbols
AX_APPEND_COMPILE_FLAGS([-fvisibility=hidden -pipe],[CFLAGS])

# Always add full warnings
AX_CFLAGS_WARN_ALL

# Set up libtool correctly
#m4_ifdef([LT_PREREQ],,[AC_MSG_ERROR([Need libtool version 2.2.6 or later])])
#LT_PREREQ([2.2.6])
#LT_INIT([disable-shared])

AC_PROG_MAKE_SET

AC_OUTPUT
