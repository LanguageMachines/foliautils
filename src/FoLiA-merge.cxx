/*
  Copyright (c) 2014 - 2023
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

#include <cstdio>
#include <cassert>
#include <string>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iostream>
#include <fstream>

#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/Unicode.h"
#include "ticcutils/Timer.h"
#include "libfolia/folia.h"
#include "foliautils/common_code.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;
using namespace	folia;
using TiCC::operator<<;

int verbose = 0;
string lem_setname = "merged-lemma-set";
string pos_setname = "merged-pos-set";

map<UnicodeString,pair<UnicodeString,UnicodeString>> fill_lexicon( const string& fn ){
  ifstream is( fn );
  map<UnicodeString,pair<UnicodeString,UnicodeString>> result;
  UnicodeString line;
  while ( TiCC::getline( is, line ) ) {
    vector<UnicodeString> parts = TiCC::split( line );
    if ( parts.size() == 3 ){
      UnicodeString word = parts[0];
      UnicodeString lemma = parts[1];
      UnicodeString pos = parts[2];
      result[word] = make_pair(lemma,pos);
    }
    else {
      cerr << "error in line " << line << endl;
    }
  }
  cout << "read " << result.size() << " lemmas" << endl;
  return result;
}

void add_lemma_pos( FoliaElement *word,
		    const map<UnicodeString,pair<UnicodeString,UnicodeString>>& lexicon ){
  UnicodeString val = word->unicode();
  if ( verbose > 0 ){
    cerr << "lookup: '" << val << "'" << endl;
  }
  auto const it = lexicon.find( val );
  if ( it != lexicon.end() ){
    UnicodeString lemma = it->second.first;
    UnicodeString pos = it->second.second;
    if ( verbose > 0 ){
      cerr << "found lemma : '" << lemma << "', pos=" << pos << endl;
    }
    KWargs args;
    args["set"] = lem_setname;
    args["class"] = TiCC::UnicodeToUTF8(lemma);
    word->addLemmaAnnotation( args );
    args.clear();
    args["set"] = pos_setname;
    args["class"] = TiCC::UnicodeToUTF8(pos);
    word->addPosAnnotation( args );
  }
}

bool merge_values( Document *doc,
		   const map<UnicodeString,pair<UnicodeString,UnicodeString>>& lexicon,
		   const string& outName,
		   const string& command ){
  processor *proc = add_provenance( *doc, "FoLiA-merge", command );
  KWargs args;
  args["processor"] = proc->id();
  doc->declare( folia::AnnotationType::LEMMA, lem_setname, args );
  doc->declare( folia::AnnotationType::POS, pos_setname, args );
  vector<FoliaElement*> wv = doc->doc()->select( Word_t );
  for( const auto& word : wv ){
    try {
      add_lemma_pos( word, lexicon );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "FoLiA error in element: " << word->id() << " of document "
	     << doc->id() << endl;
	cerr << e.what() << endl;
      }
      remove( outName.c_str() );
      return false;
    }
  }
  doc->save( outName );
  return true;
}

void usage( const string& name ){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t " << name << " will correct FoLiA files " << endl;
  cerr << "\t or a whole directory of FoLiA files " << endl;
  cerr << "\t--lemset='name'\t (default '" << lem_setname << "')" << endl;
  cerr << "\t--posset='name'\t (default '" << pos_setname << "')" << endl;
  cerr << "\t-O\t output prefix" << endl;
  cerr << "\t-t <threads>\n\t--threads <threads> Number of threads to run on. "
       << "(Default 1)" << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set" << endl;
  cerr << "\t\t\t to a reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-v increase verbosity level. Repeat for even more output." << endl;
  cerr << "\t-h or --help\t this message " << endl;
  cerr << "\t-V or --version\t show version " << endl;
}

int main( int argc, const char *argv[] ){
  TiCC::CL_Options opts( "vVl:O:t:",
			 "lemset:,posset:,lemmas:,threads:" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage(argv[0]);
    exit( EXIT_FAILURE );
  }
  string progname = opts.prog_name();
  string lemma_filename;
  string value;
  if ( opts.extract( 'h' ) || opts.extract( "help" ) ){
    usage(progname);
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  string orig_command = "FoLiA-merge " + opts.toString();
  while ( opts.extract( 'v' ) ){
    ++verbose;
  }
  opts.extract( "lemset", lem_setname );
  opts.extract( "posset", pos_setname );
  string outPrefix;
  opts.extract( 'O', outPrefix );
  if ( !( opts.extract( "l", lemma_filename ) ||
	  opts.extract( "lemmas", lemma_filename ) ) ){
    cerr << "missing '-l or --lemmas' option" << endl;
    exit( EXIT_FAILURE );
  }
  if ( !TiCC::isFile( lemma_filename ) ){
    cerr << "unable to find file '" << lemma_filename << "'" << endl;
    exit( EXIT_FAILURE );
  }
#ifdef HAVE_OPENMP
  int numThreads = 1;
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
    if ( TiCC::lowercase(value) == "max" ){
      numThreads = omp_get_max_threads() - 2;
    }
    else if ( !TiCC::stringTo(value,numThreads) ) {
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
  }
  omp_set_num_threads( numThreads );
#else
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
    exit( EXIT_FAILURE );
  }
#endif

  vector<string> filenames = opts.getMassOpts();
  if ( filenames.size() == 0 ){
    cerr << "missing input file or directory" << endl;
    exit( EXIT_FAILURE );
  }

  if ( !outPrefix.empty() ){
    if ( outPrefix[outPrefix.length()-1] != '/' )
      outPrefix += "/";
    if ( !TiCC::isDir( outPrefix ) ){
      if ( !TiCC::createPath( outPrefix ) ){
	cerr << "unable to find or create: '" << outPrefix << "'" << endl;
	exit( EXIT_FAILURE );
      }
    }
  }

  size_t toDo = filenames.size();
  if ( toDo == 0 ){
    cerr << "no input-files found" << endl;
    exit(EXIT_SUCCESS);
  }

  map<UnicodeString,pair<UnicodeString,UnicodeString>> lexicon = fill_lexicon( lemma_filename );
  cout << "verbosity = " << verbose << endl;

  if ( filenames.size() > 1  ){
    cout << "start processing of " << toDo << " files " << endl;
  }

#pragma omp parallel for shared(filenames,toDo,lexicon) schedule(dynamic,1)
  for ( size_t fn=0; fn < filenames.size(); ++fn ){
    string docName = filenames[fn];
    Document *doc = 0;
    try {
      doc = new Document( "file='"+ docName + "'" );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "failed to load document '" << docName << "'" << endl;
	cerr << "reason: " << e.what() << endl;
      }
      continue;
    }
    string outName = outPrefix;
    string::size_type pos = docName.rfind("/");
    if ( pos != string::npos ){
      docName = docName.substr( pos+1 );
    }
    pos = docName.rfind(".folia");
    if ( pos != string::npos ){
      outName += docName.substr(0,pos) + ".merged" + docName.substr(pos);
    }
    else {
      pos = docName.rfind(".");
      if ( pos != string::npos ){
	outName += docName.substr(0,pos) + ".merged" + docName.substr(pos);
      }
      else {
	outName += docName + ".merged";
      }
    }
    if ( !TiCC::createPath( outName ) ){
#pragma omp critical
      {
	cerr << "unable to create output file! " << outName << endl;
      }
      exit(EXIT_FAILURE);
    }
#pragma omp critical
    {
      cerr << "start merging in file: "
	   << doc->filename() << endl;
    }
    try {
      if ( merge_values( doc, lexicon, outName, orig_command ) ){
#pragma omp critical
	{
	  if ( toDo > 1 ){
	    cout << "Processed " << docName << " into " << outName
		 << " still " << --toDo << " files to go." << endl;
	  }
	  else {
	    cout << "Processed :" << docName << " into " << outName << endl;
	  }
	}
      }
      else {
#pragma omp critical
	{
	  cout << "FAILED merging: " << filenames[fn] << endl;
	}
      }
    }
    catch ( const exception& e ){
#pragma omp critical
      {
	cerr << docName << " failed: " << e.what() << endl;
      }
    }
    delete doc;
  }

  return EXIT_SUCCESS;
}
