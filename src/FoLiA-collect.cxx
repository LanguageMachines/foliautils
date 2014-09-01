/*
  $Id$
  $URL$
  Copyright (c) 1998 - 2014
  TICC  -  Tilburg University

  This file is part of foliatools

  foliatools is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  foliatools is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.

  For questions and suggestions, see:
      http://ilk.uvt.nl/software.html
  or send mail to:
      Timbl@uvt.nl
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
  ofstream os( filename.c_str() );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  map<unsigned int, set<string> > wf;
  map<string,unsigned int >::const_iterator cit = wc.begin();
  while( cit != wc.end()  ){
    wf[cit->second].insert( cit->first );
    ++cit;
  }
  unsigned int sum=0;
  map<unsigned int, set<string> >::const_reverse_iterator wit = wf.rbegin();
  while ( wit != wf.rend() ){
    set<string>::const_iterator sit = wit->second.begin();
    while ( sit != wit->second.end() ){
      sum += wit->first;
      os << *sit << "\t" << wit->first << "\t" << sum << "\t"
	 << 100 * double(sum)/total << endl;
      ++sit;
    }
    ++wit;
  }
}

struct rec {
  unsigned int count;
  map<string,unsigned int> pc;
};

void create_lf_list( const map<string, unsigned int>& lc,
		     const string& filename, unsigned int total ){
  ofstream os( filename.c_str() );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  map<unsigned int, set<string> > lf;
  map<string,unsigned int >::const_iterator cit = lc.begin();
  while( cit != lc.end()  ){
    lf[cit->second].insert( cit->first );
    ++cit;
  }

  unsigned int sum=0;
  map<unsigned int, set<string> >::const_reverse_iterator wit = lf.rbegin();
  while ( wit != lf.rend() ){
    set<string>::const_iterator sit = wit->second.begin();
    while ( sit != wit->second.end() ){
      sum += wit->first;
      os << *sit << "\t" << wit->first << "\t" << sum << "\t"
	 << 100* double(sum)/total << endl;
      ++sit;
    }
    ++wit;
  }
}

void create_lpf_list( const multimap<string, rec>& lpc,
		      const string& filename, unsigned int total ){
  ofstream os( filename.c_str() );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  multimap<unsigned int, pair<string,string> > lpf;
  multimap<string,rec>::const_iterator cit = lpc.begin();
  while( cit != lpc.end()  ){
    map<string,unsigned int>::const_iterator pit = cit->second.pc.begin();
    while ( pit != cit->second.pc.end() ){
      lpf.insert( make_pair( pit->second,
			     make_pair( cit->first, pit->first ) ) );
      ++pit;
    }
    ++cit;
  }
  unsigned int sum =0;
  multimap<unsigned int, pair<string,string> >::const_reverse_iterator wit = lpf.rbegin();
  while ( wit != lpf.rend() ){
    sum += wit->first;
    os << wit->second.first << " " << wit->second.second << "\t"
       << wit->first << "\t" << sum << "\t" << 100 * double(sum)/total << endl;
    ++wit;
  }
}

size_t split_at( const string& src, vector<string>& results,
		 const string& sep ){
  // split a string into substrings, using sep as seperator
  // silently skip empty entries (e.g. when two or more seperators co-incide)
  results.clear();
  string::size_type pos = 0, p;
  string res;
  while ( pos != string::npos ){
    p = src.find( sep, pos );
    if ( p == string::npos ){
      res = src.substr( pos );
      pos = p;
    }
    else {
      res = src.substr( pos, p - pos );
      pos = p + sep.length();
    }
    if ( !res.empty() )
      results.push_back( res );
  }
  return results.size();
}

unsigned int fillWF( const string& fName,
		     map<string,unsigned int>& wf,
		     bool keepSingles ){
  unsigned int total = 0;
  ifstream is( fName.c_str() );
  while ( is ){
    string line;
    getline( is, line );
    vector<string> parts;
    int num = split_at( line, parts, "\t" );
    if ( num == 4 ){
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
  ifstream is( fName.c_str() );
  while ( is ){
    string line;
    getline( is, line );
    vector<string> parts;
    int num = split_at( line, parts, "\t" );
    if ( num == 4 ){
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
  ifstream is( fName.c_str() );
  while ( is ){
    string line;
    getline( is, line );
    vector<string> parts;
    unsigned int num = split_at( line, parts, "\t" );
    if ( num == 4 ){
      unsigned int cnt = TiCC::stringTo<unsigned int>( parts[1] );
      if ( keepSingles || cnt > 1 ){
	vector<string> lp;
	num = split_at( parts[0], lp, " " );
	if ( num != 2*ng ){
	  cerr << "suprise!" << endl;
	  cerr << parts[0] << endl;
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
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t collect the ngram statistics of a file" << endl;
  cerr << "\t or a whole directoy" << endl;
  cerr << "\t--ngram\t Ngram count " << endl;
  cerr << "\t--hapax also include HAPAXes (default is don't) " << endl;
  cerr << "\t-O\t outout directory." << endl;
  cerr << "\t-h\t this messages " << endl;
  cerr << "\t-V\t show version " << endl;
  cerr << "\t-v\t verbosity " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVhO:t:", "hapax,ngram:" );
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
  string value;
  if ( opts.extract( 'h' ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  verbose = opts.extract( 'v' );
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
  if ( opts.extract( 't', value ) ){
    if ( !TiCC::stringTo( value, numThreads ) ){
      cerr << "unsupported value for -t (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
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
    cerr << "missing file or directory to process!" << endl;
    exit( EXIT_FAILURE );
  }
  else if ( fileNames.size() >1 ){
    cerr << "Only 1 file or directory may be specified!" << endl;
    exit( EXIT_FAILURE );
  }
  string name = fileNames[0];
  if ( !TiCC::isFile(name) && !TiCC::isDir(name) ){
    cerr << "parameter '" << name << "' doesn't seem to be a file or directory"
	 << endl;
    exit(EXIT_FAILURE);
  }
  else {
    string::size_type pos = name.rfind( "/" );
    if ( pos != string::npos )
      name.erase(pos);
    if ( outDir.empty() )
      outDir = name;
    cout << "Processing dir '" << name << "'" << endl;
    vector<string> filenames;
    if ( nGv > 1 )
      filenames = TiCC::searchFilesMatch( name, nG + "-gram.tsv" );
    else
      filenames = TiCC::searchFilesMatch( name, "list.tsv" );
    cout << "found " << filenames.size() << " files to process" << endl;
    for ( size_t i=0; i < filenames.size(); ++ i ){
      string fullName = filenames[i];
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
  omp_set_num_threads( numThreads);
#endif

  cout << "start processing on " << numThreads << " threads" << endl;
#pragma omp parallel sections
  {

#pragma omp section
    {
      map<string,unsigned int> wf;
      unsigned int total = 0;
      for ( size_t fn=0; fn < wfNames.size(); ++fn ){
	string fName = wfNames[fn];
#pragma omp critical (log)
	{
	  cout << "\twords \t" << fName << endl;
	}
	total += fillWF( fName, wf, keepSingles );
      }
      if ( wf.empty() ){
	cerr << "no WordFrequencies found " << endl;
      }
      else {
#pragma omp critical (log)
	{
	  cout << "processed " << wfNames.size() << " wordfreq files." << endl;
	}
	string filename = outDir + ".wordfreqlist." + nG + "-gram.total.tsv";
	create_wf_list( wf, filename, total );
#pragma omp critical (log)
	{
	  cout << "created WordFreq list '" << filename << "'" << endl;
	}
      }
    }
#pragma omp section
    {
      map<string,unsigned int> lf;
      unsigned int total = 0;
      for ( size_t fn=0; fn < lfNames.size(); ++fn ){
	string fName = lfNames[fn];
#pragma omp critical (log)
	{
	  cout << "\tlemmas \t" << fName << endl;
	}
	total += fillLF( fName, lf, keepSingles );
      }
      if ( lf.empty() ){
	cerr << "no LemmaFrequencies found " << endl;
      }
      else {
#pragma omp critical (log)
	{
	  cout << "processed " << lfNames.size() << " lemmafreq files." << endl;
	}
	string filename = outDir + ".lemmafreqlist." + nG + "-gram.total.tsv";
	create_lf_list( lf, filename, total );
#pragma omp critical (log)
	{
	  cout << "created LemmaFreq list '" << filename << "'" << endl;
	}
      }
    }

#pragma omp section
    {
      multimap<string, rec> lpc;
      unsigned int total = 0;
      for ( size_t fn=0; fn < lpfNames.size(); ++fn ){
	string fName = lpfNames[fn];
#pragma omp critical (log)
	{
	  cout << "\tlemmapos \t" << fName << endl;
	}
	total += fillLPF( fName, nGv, lpc, keepSingles );
      }
      if ( lpc.empty() ){
	cerr << "no LemmaPosFrequencies found " << endl;
      }
      else {
#pragma omp critical (log)
	{
	  cout << "processed " << lpfNames.size() << " lemmaposfreq files." << endl;
	}
	string filename = outDir + ".lemmaposfreqlist." + nG + "-gram.total.tsv";
	create_lpf_list( lpc, filename, total );
#pragma omp critical (log)
	{
	  cout << "created LemmaPosFreq list '" << filename << "'" << endl;
	}
      }
    }
  }
}
