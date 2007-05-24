bin_PROGRAMS=gtkmogo2
gtkmogo2_SOURCES=gtkmogo2.c
AM_CPPFLAGS=-DDATA_DIR=\"$(pkgdatadir)\" -DLOCALE_DIR=\"$(datadir)/locale\"
INCLUDES=${GTK_CFLAGS}
gtkmogo2_LDADD=${GTK_LIBS}
dist_pkgdata_DATA=data/mogo2.png data/loading.gif data/reload.png data/post.png data/home.png data/logo.png
EXTRA_DIST=gtkmogo2.spec
