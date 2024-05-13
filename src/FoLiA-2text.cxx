/*
  Copyright (c) 2014 - 2024
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

#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"
#include "foliautils/common_code.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;
using namespace	folia;

void usage( const string& name ){
  cerr << "Usage: " << name << " [options] file/dir" << endl;
  cerr << "\t FoLiA-2text will produce a text from a FoLiA file, " << endl;
  cerr << "\t or a whole directory of FoLiA files " << endl;
  cerr << "\t-c OR --class='name'\t use 'name' as the folia class for <t> nodes. (default is 'current')" << endl;
  cerr << "\t--retaintok\t Retain tokenization. Default is attempt to remove." << endl;
  cerr << "\t--restore-formatting \tAttempt to restore the original formatting." << endl;
  cerr << "\t\t\t Will insert (soft-)hypens and such." << endl;
  cerr << "\t-t 'threads' or\n\t--threads='threads' Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-v increment the verbosity level." << endl;
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-V or --version \t show version " << endl;
  cerr << "\t-e\t\t expr: specify the expression all input files should match with." << endl;
  cerr << "\t-o\t\t name of the output file(s) prefix." << endl;
}

UnicodeString handle_token_tag( const folia::FoliaElement *d,
				const folia::TextPolicy& tp ){
  UnicodeString tmp_result = text( d, tp );
  tmp_result = ZWJ + tmp_result;
  tmp_result += ZWJ;
  return tmp_result;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "hVvpe:t:o:c:",
			 "class:,help,version,retaintok,threads:,"
			 "restore-formatting,"
			 "honour-tags,correction-handling:" );
  try {
    opts.init(argc,argv);
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage(argv[0]);
    exit( EXIT_FAILURE );
  }
  string progname = opts.prog_name();
  // if ( opts.empty() ){
  //   usage( progname );
  //   exit(EXIT_FAILURE);
  // }
  string expression;
  string outputPrefix;
  int verbosity = 0;
  string value;
  if ( opts.extract('V') || opts.extract("version") ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('h') || opts.extract("help") ){
    usage(progname);
    exit(EXIT_SUCCESS);
  }
  opts.extract( 'o', outputPrefix );
  bool retaintok = opts.extract( "retaintok" );
  bool restore = opts.extract( "restore-formatting" );
  bool honour_tags = opts.extract( "honour-tags" );
  CORRECTION_HANDLING ch = CORRECTION_HANDLING::CURRENT;
  string handling;
  opts.extract( "correction-handling", handling );
  if ( !handling.empty() ) {
    if ( handling == "original" ){
      ch = CORRECTION_HANDLING::ORIGINAL;
    }
    else if ( handling == "current" ){
      ch = CORRECTION_HANDLING::CURRENT;
    }
    else if ( handling == "either" ){
      ch = CORRECTION_HANDLING::EITHER;
    }
    else {
      cerr << "invalid value for option '--correction-handling' " << endl
	   << "\t use 'current', original' or ''either'" << endl;
    }
  }
  if ( opts.extract('t', value ) || opts.extract("threads", value ) ){
#ifdef HAVE_OPENMP
    int numThreads = 1;
    if ( TiCC::lowercase(value) == "max" ){
      numThreads = omp_get_max_threads() - 2;
    }
    else if ( !TiCC::stringTo(value,numThreads) ) {
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
    omp_set_num_threads( numThreads );
#else
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
    exit(EXIT_FAILURE);
#endif
  }
  while ( opts.extract( 'v' ) ){
    ++verbosity;
  }
  opts.extract( 'e', expression );
  string class_name = "current";
  opts.extract( "class", class_name ) || opts.extract( 'c', class_name );



  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    cerr << "no file or dir specified!" << endl;
    usage(progname);
    exit(EXIT_FAILURE);
  }

  if ( fileNames.size() == 1 && TiCC::isDir( fileNames[0] ) ){
    fileNames = TiCC::searchFilesMatch( fileNames[0], expression );
  }
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }

  if ( !outputPrefix.empty() ){
    string::size_type pos = outputPrefix.find( "/" );
    if ( pos != string::npos && pos == outputPrefix.length()-1 ){
      // outputname ends with a /
    }
    else {
      outputPrefix += "/";
    }
    if ( !TiCC::createPath( outputPrefix ) ){
      cerr << "Output to '" << outputPrefix << "' is impossible" << endl;
    }
  }
  else {
    string pf = TiCC::dirname( fileNames[0] );
    if ( pf != "." ){
      outputPrefix = TiCC::dirname( fileNames[0] ) + "/";
    }
  }

  if ( toDo > 1 ){
    cout << "start processing of " << toDo << " files " << endl;
  }

#pragma omp parallel for shared(fileNames) schedule(dynamic)
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
    string outname = outputPrefix + TiCC::basename(docName) + ".txt";
    if ( !TiCC::createPath( outname ) ){
#pragma omp critical
      {
	cerr << "Output to '" << outname << "' is impossible" << endl;
      }
    }
    else {
      folia::TextPolicy tp( class_name );
      if ( retaintok ){
	tp.set( folia::TEXT_FLAGS::RETAIN );
      }
      if ( restore ){
	tp.set( folia::TEXT_FLAGS::ADD_FORMATTING );
      }
      tp.set_correction_handling( ch );
      tp.set_debug( verbosity > 0 );
      if ( honour_tags ){
	tp.add_handler("token", &handle_token_tag );
      }
      UnicodeString us;
      try {
	us = d->text( tp );
      }
      catch( ... ){
	cout << "document '" << docName << "' contains no text in class="
	     << class_name << endl;
      }
      if ( honour_tags ){
	UnicodeString out;
	for ( int i=0; i < us.length(); ++i ){
	  if ( us[i] == ZWJ ){
	    out += " ";
	  }
	  else {
	    out += us[i];
	  }
	}
	us = out;
      }
      if ( !us.isEmpty() ){
	ofstream os( outname );
	os << us << endl;
      }
#pragma omp critical
      {
	cout << "Processed :" << docName << " into " << outname
	     << " still " << --toDo << " files to go." << endl;
      }
    }
    delete d;
  }
  return EXIT_SUCCESS;
}
