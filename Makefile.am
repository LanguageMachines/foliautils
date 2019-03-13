ACLOCAL_AMFLAGS =-I m4 --install

SUBDIRS = src include m4 docs tests

EXTRA_DIST = bootstrap.sh AUTHORS TODO NEWS README.md codemeta.json

ChangeLog: NEWS
	git pull; git2cl > ChangeLog
