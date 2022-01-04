/*
  Copyright (c) 2014 - 2022
  CLST  - Radboud University
  ILK   - Tilburg University

  This file is part of foliautils

  foliautils is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  foliautils is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.

  For questions and suggestions, see:
      https://github.com/LanguageMachines/foliautils/issues
  or send mail to:
      lamasoftware (at ) science.ru.nl
*/

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sys/time.h>

#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"

#include "config.h"

using namespace std;


///==================== BEGIN MEMORY PROFILING CODE ============

/*
 * Author:  David Robert Nadeau
 * Site:    http://NadeauSoftware.com/
 * License: Creative Commons Attribution 3.0 Unported License
 *          http://creativecommons.org/licenses/by/3.0/deed.en_US
 */

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <sys/resource.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
#include <fcntl.h>
#include <procfs.h>

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
#include <stdio.h>

#endif

#else
#error "Cannot define getPeakRSS( ) or getCurrentRSS( ) for an unknown OS."
#endif


/**
 * Returns the peak (maximum so far) resident set size (physical
 * memory use) measured in bytes, or zero if the value cannot be
 * determined on this OS.
 */
size_t getPeakRSS( )
{
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo( GetCurrentProcess( ), &info, sizeof(info) );
    return (size_t)info.PeakWorkingSetSize;

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
    /* AIX and Solaris ------------------------------------------ */
    struct psinfo psinfo;
    int fd = -1;
    if ( (fd = open( "/proc/self/psinfo", O_RDONLY )) == -1 )
        return (size_t)0L;      /* Can't open? */
    if ( read( fd, &psinfo, sizeof(psinfo) ) != sizeof(psinfo) )
    {
        close( fd );
        return (size_t)0L;      /* Can't read? */
    }
    close( fd );
    return (size_t)(psinfo.pr_rssize * 1024L);

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    /* BSD, Linux, and OSX -------------------------------------- */
    struct rusage rusage;
    getrusage( RUSAGE_SELF, &rusage );
#if defined(__APPLE__) && defined(__MACH__)
    return (size_t)rusage.ru_maxrss;
#else
    return (size_t)(rusage.ru_maxrss * 1024L);
#endif

#else
    /* Unknown OS ----------------------------------------------- */
    return (size_t)0L;          /* Unsupported. */
#endif
}


/**
 * Returns the current resident set size (physical memory use) measured
 * in bytes, or zero if the value cannot be determined on this OS.
 */
size_t getCurrentRSS( )
{
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo( GetCurrentProcess( ), &info, sizeof(info) );
    return (size_t)info.WorkingSetSize;

#elif defined(__APPLE__) && defined(__MACH__)
    /* OSX ------------------------------------------------------ */
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if ( task_info( mach_task_self( ), MACH_TASK_BASIC_INFO,
        (task_info_t)&info, &infoCount ) != KERN_SUCCESS )
        return (size_t)0L;      /* Can't access? */
    return (size_t)info.resident_size;

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
    /* Linux ---------------------------------------------------- */
    long rss = 0L;
    FILE* fp = NULL;
    if ( (fp = fopen( "/proc/self/statm", "r" )) == NULL )
        return (size_t)0L;      /* Can't open? */
    if ( fscanf( fp, "%*s%ld", &rss ) != 1 )
    {
        fclose( fp );
        return (size_t)0L;      /* Can't read? */
    }
    fclose( fp );
    return (size_t)rss * (size_t)sysconf( _SC_PAGESIZE);

#else
    /* AIX, BSD, Solaris, and Unknown OS ------------------------ */
    return (size_t)0L;          /* Unsupported. */
#endif
}

//===== END MEMORY PROFILING CODE ========




struct Measurement {
    clock_t begintime;
    double duration;
    size_t beginmem;
    size_t endmem;
};

Measurement begin() {
    Measurement m;
    m.begintime = clock();
	m.beginmem = getCurrentRSS();
	return m;
}

void end(Measurement& m, const string & test_id, const string & filename, const string & title) {
    m.duration = (clock() - m.begintime) / (double) CLOCKS_PER_SEC;
	m.endmem = getCurrentRSS();
	const size_t peakmem = getPeakRSS();
	const double peak = peakmem / 1024.0 / 1024.0;
	const size_t memdiff = m.endmem-m.beginmem;
    const double mem = memdiff / 1024.0 / 1024.0;
    cout << filename << " - [" << test_id << "] " << title << " - time: " <<  (m.duration*1000) << " ms res: " << mem << " MB peak: " << peak << " MB" << endl << endl;
}

void test(const string & test_id, const string & filename) {
    if (test_id == "parse") {
            const string title = "Parse XML from file into full memory representation";
            Measurement m = begin();
            folia::Document doc( "file='"+ filename + "'" );
            end(m, test_id, filename, title);
    } else if (test_id == "serialise") {
            const string title = "Serialise to XML";
            folia::Document doc( "file='"+ filename + "'" );
            Measurement m = begin();
            doc.xmlstring();
            end(m, test_id, filename, title);
    } else if (test_id == "select") {
            const string title = "Select and iterate over all words";
            folia::Document doc( "file='"+ filename + "'" );
            Measurement m = begin();
            vector<folia::Word*> selection = doc.words();
            cerr << "found " << selection.size() << " words" << endl;
            end(m, test_id, filename, title);
    } else {
            cerr << "ERROR: No such test: " << test_id << endl;
    }
}

void usage( const string& name ){
  cerr << "Usage: " << name << " [options] file/dir" << endl;
  cerr << "\t FoLiA-benchmark runs benchmarks on libfolia given one or more FoLiA documents" << endl;
  //cerr << "\t-i\t Number of iterations" << endl;
  cerr << "\t-t\t Comma separated list of tests to run" << endl;
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-V or --version \t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "hVt:i:", "help,version" );
  try {
    opts.init(argc,argv);
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage(argv[0]);
    exit( EXIT_FAILURE );
  }
  string progname = opts.prog_name();
  if ( opts.empty() ){
    usage( progname );
    exit(EXIT_FAILURE);
  }

  vector<string> filenames = opts.getMassOpts();
  if ( filenames.empty() ){
    cerr << "no file or dir specified!" << endl;
    exit(EXIT_FAILURE);
  }
  string tests_string = "parse";
  opts.extract( 't', tests_string);
  vector<string> tests = TiCC::split_at( tests_string, "," );
  for ( size_t tn=0; tn < tests.size(); tn++) {
      const string test_id = tests[tn];
      for ( size_t fn=0; fn < filenames.size(); fn++ ){
        const string filename  = filenames[fn];
        test(test_id, filename);
      }
  }
}
