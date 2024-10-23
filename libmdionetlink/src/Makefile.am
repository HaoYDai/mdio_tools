
include $(top_srcdir)/Make_global.am
lib_LTLIBRARIES = libmdionetlink.la

libmdionetlink_la_LDFLAGS = -Wl, -version-info $(LIBVERSION) $(mnl_LIBS)
libmdionetlink_la_SOURCES = mdio.c