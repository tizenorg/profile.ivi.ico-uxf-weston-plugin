TESTS_ENVIRONMENT = $(SHELL) $(top_srcdir)/tests/weston-plugin-test

export abs_builddir

EFL_INCLUDE = `pkg-config --cflags ecore-evas ecore evas ecore-wayland elementary`
AM_CFLAGS = $(GCC_CFLAGS) $(EFL_INCLUDE)
AM_CPPFLAGS = -I$(top_srcdir)/src -DUNIT_TEST $(COMPOSITOR_CFLAGS)

noinst_PROGRAMS =		\
	test-send_event		\
	test-homescreen		\
	test-client			\
	test-eflapp

check_LTLIBRARIES = $(TESTS)
check_PROGRAMS = test-homescreen test-client test-eflapp test-send_event

AM_LDFLAGS = -module -avoid-version -rpath $(libdir) -lwayland-egl -lEGL -lGLESv2

test_common_src = test-common.c test-common.h
test_protocol_lib = ../src/.libs/libico-uxf-weston-plugin.so
test_efl_libs = -lecore_evas -lecore -levas -lecore_wayland -lelementary
test_wayland_client = -lwayland-client

test_send_event_SOURCES = test-send_event.c $(test_common_src)
test_send_event_LDADD = $(SIMPLE_CLIENT_LIBS) $(test_wayland_client)

test_homescreen_SOURCES = test-homescreen.c $(test_common_src) test-thumbnail.c
test_homescreen_LDADD = $(SIMPLE_CLIENT_LIBS) $(test_protocol_lib) $(test_wayland_client)

test_client_SOURCES = test-client.c $(test_common_src)
test_client_LDADD = $(SIMPLE_CLIENT_LIBS) $(test_protocol_lib) $(test_wayland_client)

test_eflapp_SOURCES = test-eflapp.c
test_eflapp_LDADD = $(SIMPLE_CLIENT_LIBS) $(test_efl_libs)

EXTRA_DIST = weston-plugin-test

