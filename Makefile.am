
NULL =

SUBDIRS = data src doc po

# introspection and docs doesn't work with src != build
DISTCHECK_CONFIGURE_FLAGS=--disable-introspection --disable-gtk-doc

MAINTAINERCLEANFILES = \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/compile \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	$(srcdir)/gtk-doc.make \
	`find "$(srcdir)" -type f -name Makefile.in -print` \
	$(NULL)

GITIGNOREFILES = \
	m4 \
	$(NULL)

-include $(top_srcdir)/git.mk
