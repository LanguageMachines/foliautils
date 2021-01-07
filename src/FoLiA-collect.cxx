/*
  Copyright (c) 2014 - 2021
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

#include <getopt.h>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <map>
#include <vector>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/StringOps.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;

bool verbose = false; // not yest used

void create_wf_list( const map<string, unsigned int>& wc,
		     const string& filename, unsigned int total ){
  ofstream os( filename );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  map<unsigned int, set<string> > wf;
  for ( const auto& cit : wc ){
    wf[cit.second].insert( cit.first );
  }
  unsigned int sum=0;
  unsigned int types=0;
  map<unsigned int, set<string> >::const_reverse_iterator wit = wf.rbegin();
  while ( wit != wf.rend() ){
    for ( const auto& st : wit->second ){
      sum += wit->first;
      os << st << "\t" << wit->first << "\t" << sum << "\t"
	 << 100 * double(sum)/total << endl;
      ++types;
    }
    ++wit;
  }
#pragma omp critical (log)
  {
    cout << "created WordFreq list '" << filename << "'" << endl;
    cout << " Stored " << sum << " tokens and " << types << " types, TTR= "
	 << (double)types/sum << endl;
  }
}

struct rec {
  unsigned int count;
  map<string,unsigned int> pc;
};

void create_lf_list( const map<string, unsigned int>& lc,
		     const string& filename, unsigned int total ){
  ofstream os( filename );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  map<unsigned int, set<string> > lf;
  for ( const auto& cit : lc ){
    lf[cit.second].insert( cit.first );
  }

  unsigned int sum=0;
  unsigned int types=0;
  map<unsigned int, set<string> >::const_reverse_iterator wit = lf.rbegin();
  while ( wit != lf.rend() ){
    for ( const auto& st : wit->second ){
      sum += wit->first;
      os << st << "\t" << wit->first << "\t" << sum << "\t"
	 << 100* double(sum)/total << endl;
      ++types;
    }
    ++wit;
  }
#pragma omp critical (log)
  {
    cout << "created LemmaFreq list '" << filename << "'" << endl;
    cout << " Stored " << sum << " tokens and " << types << " types, TTR= "
	 << (double)types/sum << endl;
  }
}

void create_lpf_list( const multimap<string, rec>& lpc,
		      const string& filename, unsigned int total ){
  ofstream os( filename );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  multimap<unsigned int, pair<string,string> > lpf;
  for ( const auto& cit : lpc ){
    for ( const auto& pit : cit.second.pc ){
      lpf.insert( make_pair( pit.second,
			     make_pair( cit.first, pit.first ) ) );
    }
  }
  unsigned int sum =0;
  multimap<unsigned int, pair<string,string> >::const_reverse_iterator wit = lpf.rbegin();
  unsigned int types = 0;
  while ( wit != lpf.rend() ){
    sum += wit->first;
    os << wit->second.first << " " << wit->second.second << "\t"
       << wit->first << "\t" << sum << "\t" << 100 * double(sum)/total << endl;
    ++types;
    ++wit;
  }
#pragma omp critical (log)
  {
    cout << "created LemmaPosFreq list '" << filename << "'" << endl;
    cout << " Stored " << sum << " tokens and " << types << " types, TTR= "
	 << (double)types/sum << endl;
  }
}

unsigned int fillWF( const string& fName,
		     map<string,unsigned int>& wf,
		     bool keepSingles ){
  unsigned int total = 0;
  ifstream is( fName );
  while ( is ){
    string line;
    getline( is, line );
    vector<string> parts = TiCC::split_at( line, "\t" );
    int num = parts.size();
    if ( num == 4 || num == 2 ){
      unsigned int cnt = TiCC::stringTo<unsigned int>( parts[1] );
      if ( keepSingles || cnt > 1 ){
	string word = parts[0];
	total += cnt;
	wf[word] += cnt;
      }
    }
  }
  return total;
}

unsigned int fillLF( const string& fName,
		     map<string,unsigned int>& lf,
		     bool keepSingles ){
  unsigned int total = 0;
  ifstream is( fName );
  while ( is ){
    string line;
    getline( is, line );
    vector<string> parts = TiCC::split_at( line, "\t" );
    int num = parts.size();
    if ( num == 4 || num == 2 ){
      unsigned int cnt = TiCC::stringTo<unsigned int>( parts[1] );
      if ( keepSingles || cnt > 1 ){
	string lemma = parts[0];
	total += cnt;
	lf[lemma] += cnt;
      }
    }
  }
  return total;
}

unsigned int fillLPF( const string& fName, unsigned int ng,
		      multimap<string, rec>& lpc,
		      bool keepSingles ){
  unsigned int total = 0;
  ifstream is( fName );
  while ( is ){
    string line;
    getline( is, line );
    vector<string> parts = TiCC::split_at( line, "\t" );
    unsigned int num = parts.size();
    if ( num == 2 || num == 4 ){
      unsigned int cnt = TiCC::stringTo<unsigned int>( parts[1] );
      if ( keepSingles || cnt > 1 ){
	vector<string> lp = TiCC::split_at( parts[0], " " );
	if ( lp.size() != 2*ng ){
	  cerr << "suprise. expected " << 2*ng << " parts, got " << lp.size()
	       << ". IN " << parts[0] << endl;
	  exit( EXIT_FAILURE);
	}
	string lemma;
	string pos;
	for ( size_t i=0; i < ng; ++i ){
	  lemma += lp[i];
	  pos += lp[ng+i];
	  if ( i != ng-1 ){
	    lemma += " ";
	    pos += " ";
	  }
	}
	multimap<string, rec >::iterator it = lpc.find(lemma);
	if ( it == lpc.end() ){
	  rec tmp;
	  tmp.count = cnt;
	  tmp.pc[pos]=cnt;
	  lpc.insert( make_pair(lemma,tmp) );
	}
	else {
	  it->second.count += cnt;
	  it->second.pc[pos] += cnt;
	}
	total += cnt;
      }
    }
  }
  return total;
}

void usage(){
  cerr << "Usage: [options] dir" << endl;
  cerr << "\t collect the ngram statistics of a directory containing" << endl;
  cerr << "\t files produced by FoLiA-stats." << endl;
  cerr << "\t--ngram\t Ngram count " << endl;
  cerr << "\t--hapax also include HAPAXes (default is don't) " << endl;
  cerr << "\t-O\t output directory." << endl;
  cerr << "\t-R\t recurse into the input directory." << endl;
  cerr << "\t-t <threads>\n\t--threads <threads> Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-h or --help\t this messages " << endl;
  cerr << "\t-V or --version\t show version " << endl;
  cerr << "\t-v\t verbosity " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVhO:t:R", "hapax,ngram:,help,version,threads:" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
  string nG = "1";
  int nGv = 1;
  int numThreads=1;
  string outDir;
  bool keepSingles = false;
  bool recurse = false;
  string value;
  if ( opts.extract( 'h' ) || opts.extract( "help" ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  verbose = opts.extract( 'v' );
  recurse = opts.extract( 'R' );
  if ( opts.extract( "ngram", nG ) ){
    if ( !TiCC::stringTo( nG, nGv ) ){
      cerr << "unsupported value for --ngram (" << nG << ")" << endl;
      exit(EXIT_FAILURE);
    }
    if ( nGv <= 0 || nGv >= 10 ){
      cerr << "unsupported value for --ngram (" << nG << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  opts.extract( 'O', outDir );
  keepSingles = opts.extract( "hapax" );
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
#ifdef HAVE_OPENMP
    if ( TiCC::lowercase(value) == "max" ){
      numThreads = omp_get_max_threads() - 2;
    }
    else if ( !TiCC::stringTo(value,numThreads) ) {
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
#else
    cerr << "FoLiA-collect: OpenMP support is missing. -t option is not supported" << endl;
    exit(EXIT_FAILURE);
#endif
  }
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage();
    exit(EXIT_FAILURE);
  }

  vector<string> lfNames;
  vector<string> lpfNames;
  vector<string> wfNames;

  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.size() == 0 ){
    cerr << "missing a directory to process!" << endl;
    exit( EXIT_FAILURE );
  }
  else if ( fileNames.size() >1 ){
    cerr << "Only 1 directory may be specified!" << endl;
    exit( EXIT_FAILURE );
  }
  string name = fileNames[0];
  if ( !TiCC::isDir(name) ){
    cerr << "parameter '" << name << "' doesn't seem to be a directory" << endl;
    exit(EXIT_FAILURE);
  }
  else {
    string::size_type pos = name.rfind( "/" );
    if ( pos != string::npos )
      name.erase(pos);
    if ( outDir.empty() ){
      outDir = name;
    }
    else {
      TiCC::createPath(outDir);
    }
    cout << "Processing dir '" << name << "'" << endl;
    vector<string> filenames;
    if ( nGv > 1 )
      filenames = TiCC::searchFilesMatch( name, nG + "-gram.tsv", recurse );
    else
      filenames = TiCC::searchFilesMatch( name, "list.tsv", recurse );
    cout << "found " << filenames.size() << " files to process" << endl;
    for ( const auto& fullName : filenames ){
      string::size_type pos = fullName.find( ".lemmafreqlist" );
      if ( pos != string::npos ){
	lfNames.push_back( fullName );
      }
      pos = fullName.find( ".lemmaposfreqlist" );
      if ( pos != string::npos ){
	lpfNames.push_back( fullName );
      }
      pos = fullName.find( ".wordfreqlist" );
      if ( pos != string::npos ){
	wfNames.push_back( fullName );
      }
    }
  }

#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
  omp_set_nested( 1);
#endif

  cout << "start processing on " << numThreads << " threads" << endl;
#pragma omp parallel sections
  {

#pragma omp section
    {
      map<string,unsigned int> wf;
      unsigned int total = 0;

#pragma omp parallel for shared(wfNames)
      for ( unsigned int i=0; i < wfNames.size(); ++ i ){
	const auto& fName = wfNames[i];
#pragma omp critical (log)
	{
	  cout << "\twords \t" << fName << endl;
	}
	total += fillWF( fName, wf, keepSingles );
      }
      if ( wf.empty() ){
#pragma omp critical (log)
	{
	  cerr << "no WordFrequencies found " << endl;
	}
      }
      else {
#pragma omp critical (log)
	{
	  cout << "processed " << wfNames.size() << " wordfreq files." << endl;
	}
	string filename = outDir + ".wordfreqlist." + nG + "-gram.total.tsv";
	create_wf_list( wf, filename, total );
      }
    }
#pragma omp section
    {
      map<string,unsigned int> lf;
      unsigned int total = 0;
#pragma omp parallel for shared(lfNames)
      for ( unsigned int i=0; i < lfNames.size(); ++ i ){
	const auto& fName = lfNames[i];
#pragma omp critical (log)
	{
	  cout << "\tlemmas \t" << fName << endl;
	}
	total += fillLF( fName, lf, keepSingles );
      }
      if ( lf.empty() ){
#pragma omp critical (log)
	{
	  cerr << "no LemmaFrequencies found " << endl;
	}
      }
      else {
#pragma omp critical (log)
	{
	  cout << "processed " << lfNames.size() << " lemmafreq files." << endl;
	}
	string filename = outDir + ".lemmafreqlist." + nG + "-gram.total.tsv";
	create_lf_list( lf, filename, total );
      }
    }

#pragma omp section
    {
      multimap<string, rec> lpc;
      unsigned int total = 0;
#pragma omp parallel for shared(lpfNames)
      for ( unsigned int i=0; i < lpfNames.size(); ++ i ){
	const auto& fName = lpfNames[i];
#pragma omp critical (log)
	{
	  cout << "\tlemmapos \t" << fName << endl;
	}
	total += fillLPF( fName, nGv, lpc, keepSingles );
      }
      if ( lpc.empty() ){
#pragma omp critical (log)
	{
	  cerr << "no LemmaPosFrequencies found " << endl;
	}
      }
      else {
#pragma omp critical (log)
	{
	  cout << "processed " << lpfNames.size() << " lemmaposfreq files." << endl;
	}
	string filename = outDir + ".lemmaposfreqlist." + nG + "-gram.total.tsv";
	create_lpf_list( lpc, filename, total );
      }
    }
  }
  return EXIT_SUCCESS;
}
