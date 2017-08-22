#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.50)
AC_INIT(freemoted, 1.4, randy.rossi@gmail.com)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/types.h])
AC_CONFIG_HEADERS([src/config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
AM_PROG_LIBTOOL

test -z "${WIN32}" && WIN32="no"
test -z "${CYGWIN}" && CYGWIN="no"
case "${host}" in
	*-cygwin*)
		AC_MSG_CHECKING([cygwin mode to use])
		CYGWIN="yes"
		WIN32="yes"
		AC_DEFINE([HAVE_WIN32], [1], [Define if you are on Cygwin])
		;;
esac

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netinet/in.h stdint.h stdlib.h string.h strings.h sys/socket.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memchr strcasecmp strncasecmp])

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_create],[],[
echo "libpthread is required"
exit -1]
)

AC_CHECK_LIB([bluetooth], [ba2str],[],[
echo ""
echo "Warning: libbluetooth not found.  Bluetooth connectivity will not be supported"
echo ""
])
AC_CHECK_LIB([X11], [XOpenDisplay])

if test "$WIN32" = "no"; then
AC_CHECK_LIB([Xtst], [XTestFakeKeyEvent],[],[
echo ""
echo "Warning: libXtst not found.  Keyboard and Mouse movements will not be supported"
echo ""
])
fi

AC_CHECK_LIB([giblib], [gib_imlib_free_image],[],[
echo ""
echo "Warning: libgiblib not found.  Remote Desktop viewing will not be supported"
echo ""
])

AC_CHECK_LIB([Imlib2], [imlib_context_get],[],[
echo ""
echo "Warning: libImlib2 not found.  Remote Desktop viewing will not be supported"
echo ""
])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 conf/Makefile
		])
AC_OUTPUT
