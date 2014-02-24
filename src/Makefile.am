lib_LTLIBRARIES = libico-uxf-weston-plugin.la

pkginclude_HEADERS =               \
	ico_window_mgr-client-protocol.h	\
	ico_input_mgr-client-protocol.h		\
	ico_input_mgr.h

libico_uxf_weston_plugin_la_LIBADD = -ldrm_intel -lrt -lm
libico_uxf_weston_plugin_la_LDFLAGS = -version-info 0:9:0
libico_uxf_weston_plugin_la_SOURCES =          \
	ivi-application-protocol.c		\
	ivi-controller-protocol.c		\
	ico_window_mgr-protocol.c		\
	ico_input_mgr-protocol.c

AM_CPPFLAGS =					\
	-DDATADIR='"$(datadir)"'		\
	-DMODULEDIR='"$(moduledir)"'		\
	-DLIBEXECDIR='"$(libexecdir)"'

EXT_CFLAGS =

git-version.h : .FORCE
	$(AM_V_GEN)(echo "#define BUILD_ID \"$(shell git --work-tree=$(top_srcdir) describe --always --dirty) $(shell git --work-tree=$(top_srcdir) log -1 --format='%s (%ci)')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

.FORCE :

moduledir = @libdir@/weston
module_LTLIBRARIES =		\
	$(ico_plugin_loader)	\
	$(ico_window_mgr)		\
	$(ico_window_animation)	\
	$(ico_input_mgr)

# Weston Plugin Loader
ico_plugin_loader = ico_plugin_loader.la
ico_plugin_loader_la_LDFLAGS = -module -avoid-version
ico_plugin_loader_la_LIBADD = $(PLUGIN_LIBS)
ico_plugin_loader_la_CFLAGS = $(GCC_CFLAGS) $(EXT_CFLAGS) $(PLUGIN_CFLAGS)
ico_plugin_loader_la_SOURCES =		\
	ico_plugin_loader.c				\
	ico_ivi_common_private.h		\
	ico_plugin_version.h

# Multi Window Manager
ico_window_mgr = ico_window_mgr.la
ico_window_mgr_la_LDFLAGS = -module -avoid-version
ico_window_mgr_la_LIBADD = -ldrm_intel $(PLUGIN_LIBS) $(AUL_LIBS)
ico_window_mgr_la_CFLAGS = -I/usr/include/libdrm $(GCC_CFLAGS) $(EXT_CFLAGS) $(PLUGIN_CFLAGS)
ico_window_mgr_la_SOURCES =			\
	ico_window_mgr.c				\
	ico_window_mgr_private.h		\
	ico_ivi_common_private.h		\
	ico_window_mgr-protocol.c		\
	ico_window_mgr-server-protocol.h
#
# Window Animation
ico_window_animation = ico_window_animation.la
ico_window_animation_la_LDFLAGS = -module -avoid-version
ico_window_animation_la_LIBADD = $(PLUGIN_LIBS) $(AUL_LIBS)
ico_window_animation_la_CFLAGS = $(GCC_CFLAGS) $(EXT_CFLAGS) $(PLUGIN_CFLAGS)
ico_window_animation_la_SOURCES =	\
	ico_window_animation.c			\
	ico_window_mgr_private.h		\
	ico_ivi_common._privateh

# Multi Input Manager
ico_input_mgr = ico_input_mgr.la
ico_input_mgr_la_LDFLAGS = -module -avoid-version
ico_input_mgr_la_LIBADD = $(PLUGIN_LIBS)
ico_input_mgr_la_CFLAGS = $(GCC_CFLAGS) $(EXT_CFLAGS) $(PLUGIN_CFLAGS)
ico_input_mgr_la_SOURCES =			\
	ico_input_mgr.c					\
	ico_input_mgr.h					\
	ico_window_mgr_private.h		\
	ico_ivi_common._privateh		\
	ico_input_mgr-protocol.c		\
	ico_input_mgr-server-protocol.h

# Wayland protocol(Build by wayland-scanner)
BUILT_SOURCES =						\
	ivi-application-protocol.c		\
	ivi-application-server-protocol.h	\
	ivi-application-client-protocol.h	\
	ivi-controller-protocol.c		\
	ivi-controller-server-protocol.h	\
	ivi-controller-client-protocol.h	\
	ico_window_mgr-protocol.c		\
	ico_window_mgr-server-protocol.h	\
	ico_window_mgr-client-protocol.h	\
	ico_input_mgr-protocol.c		\
	ico_input_mgr-server-protocol.h	\
	ico_input_mgr-client-protocol.h

CLEANFILES = $(BUILT_SOURCES)

@wayland_scanner_rules@
