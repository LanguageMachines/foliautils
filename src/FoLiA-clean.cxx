/*
  Copyright (c) 2014 - 2017
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
#include <map>
#include <vector>
#include <iostream>
#include <fstream>

#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;
using namespace	TiCC;

void clean_folia( FoliaElement *node,
		  const string& text,
		  bool current,
		  const set<string>& setnames ){
  for ( size_t i=0; i < node->size(); ++i ){
    FoliaElement *p = node->index(i);
    if ( p->element_id() == TextContent_t ) {
      cerr << "clean text: bekijk " << p << endl;
      cerr << "p.text(" << p->cls() << ")" << endl;
      if ( p->cls() != text ){
	cerr << "remove" << p << endl;
	node->remove(p,true);
	--i;
      }
      else {
	clean_folia( p, text, current, setnames );
      }
    }
    else {
      cerr << "clean anno: bekijk " << p << endl;
      string set = p->sett();
      if ( !set.empty() && setnames.find(set) != setnames.end() ){
	cerr << "matched : " << set << endl;
	cerr << "remove" << p << endl;
	node->remove(p,true);
	--i;
      }
      else {
	clean_folia( p, text, current, setnames );
      }
    }
  }
}

void clean_doc( Document *d,
		const string& outname,
		const string& text,
		bool current,
		const set<string>& setnames ){
  FoliaElement *root = d->doc();
  clean_folia( root, text, current, setnames );
  d->save( outname );
}

void usage( const string& name ){
  cerr << "Usage: " << name << " [options] file/dir" << endl;
  cerr << "\t FoLiA-clean will produce a cleaned up version of a FoLiA file, " << endl;
  cerr << "\t or a whole directory of FoLiA files " << endl;
  cerr << "\t--textclass='name', retain only text nodes with this class. (required option)" << endl;
  //  cerr << "\t--current\t Make the textclass 'current'. (default is to keep 'name')" << endl;
  cerr << "\t--cleanset='setname'\t remove annotations with this 'setname'. This option can be repeated for different annotations." << endl;
  cerr << "\t-e\t expr: specify the expression all input files should match with. (default .xml)" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-V or --version \t show version " << endl;
  cerr << "\t-O\t name of the output dir." << endl;
}

int main( int argc, char *argv[] ){
  CL_Options opts( "hVvpe:t:O:", "textclass:,current,cleanset:,help,version,retaintok" );
  try {
    opts.init(argc,argv);
  }
  catch( OptionError& e ){
    cerr << e.what() << endl;
    usage(argv[0]);
    exit( EXIT_FAILURE );
  }
  string progname = opts.prog_name();
  if ( opts.empty() ){
    usage( progname );
    exit(EXIT_FAILURE);
  }
  int numThreads = 1;
  string value;
  if ( opts.extract('V') || opts.extract("version") ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('h') || opts.extract("help") ){
    usage(progname);
    exit(EXIT_SUCCESS);
  }
  string expression = ".xml";
  opts.extract( 'e', expression );
  string output_dir;
  opts.extract( 'O', output_dir );
  bool make_current = opts.extract( "current" );
  if ( opts.extract('t', value ) ){
    if ( !stringTo(value, numThreads ) ){
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  string class_name = "current";
  if ( !opts.extract( "textclass", class_name ) ){
    cerr << "missing value for --textclass" << endl;
    exit(EXIT_FAILURE);
  }
  set<string> clean_sets;
  string set;
  while ( opts.extract( "cleanset", set ) ){
    clean_sets.insert( set );
  }
#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 ){
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
  }
#endif

  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    cerr << "no file or dir specified!" << endl;
    exit(EXIT_FAILURE);
  }

  if ( fileNames.size() == 1 && TiCC::isDir( fileNames[0] ) ){
    fileNames = searchFilesMatch( fileNames[0], expression );
  }
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }

  if ( !output_dir.empty() ){
    string::size_type pos = output_dir.find( "/" );
    if ( pos != string::npos && pos == output_dir.length()-1 ){
      // outputname ends with a /
    }
    else {
      output_dir += "/";
    }
    if ( !TiCC::createPath( output_dir ) ){
      cerr << "Output to '" << output_dir << "' is impossible" << endl;
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
    string outname = output_dir + docName + ".cleaned.xml";
    if ( !TiCC::createPath( outname ) ){
#pragma omp critical
      {
	cerr << "Output to '" << outname << "' is impossible" << endl;
      }
    }
    else {
      clean_doc( d, outname, class_name, make_current, clean_sets );
#pragma omp critical
      {
	cout << "Processed :" << docName << " into " << outname
	     << " still " << --toDo << " files to go." << endl;
      }
    }
    delete d;
  }
}
