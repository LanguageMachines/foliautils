AM_CPPFLAGS = -I ../include/
AM_CXXFLAGS = -std=c++14 -W -Wall -pedantic -g

bin_PROGRAMS = FoLiA-stats FoLiA-collect FoLiA-idf FoLiA-correct \
	FoLiA-alto FoLiA-langcat FoLiA-hocr FoLiA-page FoLiA-txt FoLiA-2text \
	FoLiA-pm FoLiA-clean FoLiA-wordtranslate FoLiA-abby FoLiA-benchmark

LDADD = libfoliautils.la
lib_LTLIBRARIES = libfoliautils.la
libfoliautils_la_LDFLAGS = -version-info 1:0:0

libfoliautils_la_SOURCES = common_code.cxx # nothing yet

FoLiA_stats_SOURCES = FoLiA-stats.cxx
FoLiA_collect_SOURCES = FoLiA-collect.cxx
FoLiA_idf_SOURCES = FoLiA-idf.cxx
FoLiA_correct_SOURCES = FoLiA-correct.cxx
FoLiA_alto_SOURCES = FoLiA-alto.cxx
FoLiA_langcat_SOURCES = FoLiA-langcat.cxx
FoLiA_hocr_SOURCES = FoLiA-hocr.cxx
FoLiA_page_SOURCES = FoLiA-page.cxx
FoLiA_abby_SOURCES = FoLiA-abby.cxx
FoLiA_txt_SOURCES = FoLiA-txt.cxx
FoLiA_2text_SOURCES = FoLiA-2text.cxx
FoLiA_pm_SOURCES = FoLiA-pm.cxx
FoLiA_clean_SOURCES = FoLiA-clean.cxx
FoLiA_wordtranslate_SOURCES = FoLiA-wordtranslate.cxx
FoLiA_benchmark_SOURCES = FoLiA-benchmark.cxx

TESTS = tst.sh
EXTRA_DIST = tst.sh
