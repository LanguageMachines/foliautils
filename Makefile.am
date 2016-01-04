ACLOCAL_AMFLAGS =-I m4 --install

SUBDIRS = src docs m4

EXTRA_DIST = bootstrap.sh AUTHORS TODO NEWS

ChangeLog: NEWS
	svn update; svn2cl --include-rev
