export abs_builddir

AM_CFLAGS = $(GCC_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/src -DUNIT_TEST $(COMPOSITOR_CFLAGS)

bin_PROGRAMS =		\
	ico_send_inputevent

check_LTLIBRARIES = $(TESTS)
check_PROGRAMS = ico_send_inputevent

AM_LDFLAGS = -module -avoid-version -rpath $(libdir)

ico_send_inputevent_SOURCES = ico_send_inputevent.c
ico_send_inputevent_LDADD = $(SIMPLE_CLIENT_LIBS)
