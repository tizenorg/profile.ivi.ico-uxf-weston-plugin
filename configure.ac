AC_PREREQ([2.68])
AC_INIT([ico-uxf-weston-plugin],
        [0.9.21],
        [https://BUG-REPORT-ADDRESS])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz])

AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT([disable-static])

PKG_PROG_PKG_CONFIG()

AC_CHECK_FUNC([dlopen], [],
              AC_CHECK_LIB([dl], [dlopen], DLOPEN_LIBS="-ldl"))
AC_SUBST(DLOPEN_LIBS)

AC_CHECK_FUNC([aul_app_get_appid_bypid], [],
              AC_CHECK_LIB([aul], [aul_app_get_appid_bypid], AUL_LIBS="-laul"))
AC_SUBST(AUL_LIBS)

AC_CHECK_HEADERS([execinfo.h])

AC_CHECK_FUNCS([mkostemp strchrnul])

PKG_CHECK_MODULES([PLUGIN], [weston >= 1.1.1 pixman-1 xkbcommon])

if test "x$GCC" = "xyes"; then
	my_common_gcc_flags="-Wall -Wextra -Wno-unused-parameter \
		-Wno-missing-field-initializers -g -fvisibility=hidden"
	GCC_CFLAGS="$my_common_gcc_flags \
		-Wstrict-prototypes -Wmissing-prototypes"
	GCC_CXXFLAGS="$my_common_gcc_flags"
fi
AC_SUBST(GCC_CFLAGS)
AC_SUBST(GCC_CXXFLAGS)

WAYLAND_SCANNER_RULES(['$(top_srcdir)/protocol'])

AC_CONFIG_FILES([Makefile
		 src/Makefile
		 tools/Makefile
		 tests/Makefile])
AC_OUTPUT
