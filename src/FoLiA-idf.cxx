/*
  Copyright (c) 2014 - 2016
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
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>

#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;

bool verbose = false;

void create_idf_list( const map<string, unsigned int>& wc,
		      const string& filename, unsigned int clip ){
  ofstream os( filename.c_str() );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  map<string,unsigned int >::const_iterator cit = wc.begin();
  while( cit != wc.end()  ){
    if ( cit->second > clip ){
      os << cit->first << "\t" << cit->second << endl;
    }
    ++cit;
  }
#pragma omp critical
  {
    cout << "created IDF list '" << filename << "'" << endl;
  }
}

size_t inventory( const Document *doc,
		  bool lowercase,
		  map<string,unsigned int>& wc ){
  vector<Word*> words = doc->words();
  set<string> ws;
  for ( unsigned int i=0; i < words.size(); ++i ){
    string word;
    try {
      if ( lowercase ){
	UnicodeString uword = UTF8ToUnicode( words[i]->str() );
	uword.toLower();
	word = UnicodeToUTF8( uword );
      }
      else
	word = words[i]->str();
    }
    catch(...){
#pragma omp critical
      {
	cerr << "missing text for word " << words[i]->id() << endl;
      }
      break;
    }
    ws.insert( word );
  }

  set<string>::const_iterator it = ws.begin();
  while ( it != ws.end() ){
#pragma omp critical
    {
      ++wc[*it];
    }
    ++it;
  }
  return ws.size();
}

void usage(){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t FoLiA-idf will produce IDF statistics for a directory of FoLiA files " << endl;
  cerr << "\t--clip\t clipping factor. " << endl;
  cerr << "\t\t\t (entries with frequency <= this factor will be ignored). " << endl;
  cerr << "\t--lower\t Lowercase all words" << endl;
  cerr << "\t-t\t number of threads" << endl;
  cerr << "\t-h\t this message " << endl;
  cerr << "\t-V\t show version " << endl;
  cerr << "\t-e\t expr: specify the expression all files should match with." << endl;
  cerr << "\t-O\t output prefix" << endl;
  cerr << "\t-R\t search the dirs recursively (when appropriate)." << endl;
  exit(EXIT_SUCCESS);
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVt:O:Rhe:", "clip:,lower" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
  int clip = 0;
  int numThreads = 1;
  bool recursiveDirs = false;
  bool lowercase = false;
  string expression;
  string outPrefix;
  string value;
  if ( opts.extract('h' ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('V' ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  verbose = opts.extract( 'v' );
  opts.extract( 'e', expression );
  recursiveDirs = opts.extract( 'R' );
  opts.extract( 'O', outPrefix );
  if ( opts.extract( 't', value ) ){
    if ( !TiCC::stringTo( value, numThreads ) ){
      cerr << "unsupported value for -t (" << value << ")" << endl;
      exit(EXIT_FAILURE);  }
  }
  if ( opts.extract( "clip", value ) ){
    if ( !TiCC::stringTo( value, clip ) ){
      cerr << "illegal --clip value (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
  }
  lowercase = opts.extract( "lowercase" );
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage();
    exit(EXIT_FAILURE);
  }
#ifdef HAVE_OPENMP
  if ( numThreads != 1 )
    omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 )
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
#endif

  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.size() == 0 ){
    cerr << "missing input file or directory" << endl;
    exit( EXIT_FAILURE );
  }
  else if ( fileNames.size() > 1 ){
    cerr << "currently only 1 file or directory is supported" << endl;
    exit( EXIT_FAILURE );
  }

  string dirName = fileNames[0];
  fileNames = TiCC::searchFilesMatch( dirName, expression, recursiveDirs );
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }
  if ( outPrefix.empty() ){
    string::size_type pos = fileNames[0].find( "/" );
    if ( pos != string::npos )
      outPrefix = fileNames[0].substr(0,pos);
    else
      outPrefix = "current";
  }

  if ( toDo > 1 ){
#ifdef HAVE_OPENMP
    folia::initMT();
#endif
    cout << "start processing of " << toDo << " files " << endl;
  }


  map<string,unsigned int> wc;
  unsigned int wordTotal =0;

#pragma omp parallel for shared(fileNames,wordTotal,wc )
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    string docName = fileNames[fn];
    Document *d = 0;
    try {
      d = new Document( "file='"+ docName + "'" );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "failed to load document '" << docName << "'" << endl;
	cerr << "reason: " << e.what() << endl;
      }
      continue;
    }
    size_t count = inventory( d, lowercase, wc );
    wordTotal += count;
#pragma omp critical
    {
      cout << "Processed :" << docName << " with " << count << " unique words."
	   << " still " << --toDo << " files to go." << endl;
    }
    delete d;
  }

  if ( toDo > 1 ){
    cout << "done processsing directory '" << dirName << "' in total "
	 << wordTotal << " unique words were found." << endl;
  }
  cout << "start calculating the results" << endl;
  string filename = outPrefix + ".idf.tsv";
  create_idf_list( wc, filename, clip );
  cout << "done: " << endl;
}
