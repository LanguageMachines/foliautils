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
#include <unordered_map>
#include <unordered_set>
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

int debug = 0;

void clean_text( FoliaElement *node,
		 const string& textclass,
		 bool current ){
  for ( size_t i=0; i < node->size(); ++i ){
    FoliaElement *p = node->index(i);
    if ( p->element_id() == TextContent_t ) {
      if ( !textclass.empty() ){
	if ( debug ){
#pragma omp critical( debugging )
	  {
	    cerr << "clean text: bekijk " << p << endl;
	    cerr << "p.text(" << p->cls() << ")" << endl;
	  }
	}
	if ( p->cls() != textclass ){
	  if ( debug ){
#pragma omp critical( debugging )
	    {
	      cerr << "remove" << p << endl;
	    }
	  }
	  node->remove(p,true);
	  --i;
	}
	else {
	  if ( current ){
	    p->set_to_current();
	  }
	  clean_text( p, textclass, current );
	}
      }
    }
    else {
      clean_text( p, textclass, current );
    }
  }
}

void clean_anno( FoliaElement *node,
		 const AnnotationType::AnnotationType anno_type,
		 const string& setname ){
  for ( size_t i=0; i < node->size(); ++i ){
    FoliaElement *p = node->index(i);
    if ( debug ){
#pragma omp critical( debugging )
      {
	cerr << "clean anno: bekijk " << p << endl;
      }
    }
    string set = p->sett();
    AnnotationType::AnnotationType at = p->annotation_type();
    if ( debug ){
#pragma omp critical( debugging )
      {
	cerr << "set = " << set << endl;
	cerr << "AT  = " << toString(at) << endl;
      }
    }
    if ( at == anno_type ){
      if ( debug ){
#pragma omp critical( debugging )
	{
	  cerr << "matched: " << toString(at) << "-annotation("
	       << set << ")" << endl;
	  cerr << "remove" << p << endl;
	}
      }
      if ( ( ( !set.empty() && setname == set )
	     || setname.empty() ) ){
	node->remove(p,true);
	--i;
      }
    }
    else {
      clean_anno( p, anno_type, setname );
    }
  }
}

void clean_doc( Document *d,
		const string& outname,
		const string& textclass,
		bool current,
		unordered_map< AnnotationType::AnnotationType,
		unordered_set<string>>& anno_setname ){
  FoliaElement *root = d->doc();
  for( const auto& it : anno_setname ){
    for ( const auto& set : it.second ){
      clean_anno( root, it.first, set );
    }
  }
  for( const auto& it : anno_setname ){
    for ( const auto& set : it.second ){
      d->un_declare( it.first, set );
    }
  }
  clean_text( root, textclass, current );
  d->save( outname );
}

void usage( const string& name ){
  cerr << "Usage: " << name << " [options] file/dir" << endl;
  cerr << "\t FoLiA-clean will produce a cleaned up version of a FoLiA file, " << endl;
  cerr << "\t or a whole directory of FoLiA files " << endl;
  cerr << "\t--textclass='name', retain only text nodes with this class. (default is to retain all)" << endl;
  cerr << "\t--current\t Make the textclass 'current'. (default is to keep 'name')" << endl;
  cerr << "\t--fixtext\t Fixup problems in structured text. " << endl;
  cerr << "\t\t Replaces on structure nodes the text class 'textclass' by what is found on deeper levels." << endl;
  cerr << "\t--cleanannoset='type\\\\setname'\t remove annotations with 'type' and 'setname'. NOTE: use a double '\\' !. The setname can be empty. This option can be repeated for different annotations." << endl;
  cerr << "\t-e\t expr: specify the expression all input files should match with. (default .folia.xml)" << endl;
  cerr << "\t--debug Set dubugging" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-V or --version \t show version " << endl;
  cerr << "\t-O\t name of the output dir." << endl;
}

int main( int argc, char *argv[] ){
  CL_Options opts( "hVvpe:t:O:", "textclass:,current,cleanannoset:,help,version,retaintok,fixtext,debug" );
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
  string expression = ".folia.xml";
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
  string class_name;
  opts.extract( "textclass", class_name );
  if ( opts.extract( "debug" ) ){
    debug = 1;
  }
  bool fixtext = opts.extract( "fixtext" );
  if ( fixtext && make_current ){
    cerr << "cannot combine --fixtext and --current" << endl;
    exit( EXIT_FAILURE );
  }
  unordered_map<AnnotationType::AnnotationType,unordered_set<string>> clean_sets;
  string line;
  while ( opts.extract( "cleanannoset", line ) ){
    string type;
    string setname;
    string::size_type bs_pos = line.find( "\\" );
    if ( bs_pos == string::npos ){
      // no '\' separator, so only a type
      type = line;
    }
    else {
      type = line.substr( 0, bs_pos );
      setname = line.substr( bs_pos+1 );
    }
    if ( type == "token" ){
      cerr << "deleting token annotation is not supported yet." << endl;
      exit( EXIT_FAILURE );
    }
    AnnotationType::AnnotationType at;
    try {
      at = stringTo<AnnotationType::AnnotationType>( type );
    }
    catch ( exception& e){
      cerr << e.what() << endl;
      exit(EXIT_FAILURE);
    }
    clean_sets[at].insert(setname);
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
      if ( fixtext ){
	d = new Document( "file='"+ docName + "',mode='fixtext'" );
      }
      else {
	d = new Document( "file='"+ docName + "'" );
      }
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "failed to load document '" << docName << "'" << endl;
	cerr << "reason: " << e.what() << endl;
      }
      continue;
    }
    string outname ;
    string::size_type pos = docName.find( expression );
    if ( pos != string::npos ){
      outname = docName.substr( 0, pos );
    }
    else {
      outname = docName;
    }
    outname = output_dir + outname + ".cleaned.folia.xml";
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
