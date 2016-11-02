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

string classname = "current";
bool doHeads = false;

vector<Head*> getHead( const FoliaElement  *el ) {
  static set<ElementType> excludeSet;
  if ( excludeSet.empty() ){
    excludeSet.insert( Quote_t );
  }
  return el->select<Head>( excludeSet, false );
}

void handle_sentences( vector<Sentence *>& sents, ostream& os ){
  for ( const auto& s : sents ){
    UnicodeString us;
    try {
      us = s->deeptext(classname);
    }
    catch(...){
#pragma omp critical
      {
	cerr << "missing text for sentence " << s->id() << endl;
      }
      break;
    }
    string line = UnicodeToUTF8( us );
    os << line << endl;
  }
  os << endl;
}

void handle_deeper( FoliaElement *, ostream&, bool );

void handle_heads( vector<Head*>& heads, ostream& os ){
  for ( const auto& h : heads ){
    if ( h->hastext(classname) ){
      UnicodeString us = h->stricttext(classname);
      string line = UnicodeToUTF8( us );
      os << line << endl;
    }
    else {
      handle_deeper( h, os, true );
    }
  }
  os << endl;
}

void handle_pars( vector<Paragraph *>& pars, ostream& os, bool doHeads ){
  for ( const auto& p : pars ){
    if ( doHeads ){
      vector<Head *> heads = getHead( p );
      handle_heads( heads, os );
    }
    if ( p->hastext(classname) ){
      UnicodeString us = p->stricttext(classname);
      string line = UnicodeToUTF8( us );
      os << line << endl;
    }
    else {
      vector<Sentence *> sents = p->select<Sentence>( false );
      handle_sentences( sents, os );
    }
  }
}

void handle_deeper( FoliaElement *el, ostream& os, bool doHeads ){
  vector<Paragraph *> pars = el->select<Paragraph>( false );
  if ( pars.empty() ){
    vector<Sentence *> sents = el->select<Sentence>( false );
    handle_sentences( sents, os );
  }
  else {
    handle_pars( pars, os, doHeads );
  }
}

void handle_divs( vector<Division *>& divs, ostream& os, bool doHeads ){
  for ( unsigned int p=0; p < divs.size(); ++p ){
    if ( doHeads ){
      vector<Head *> heads = getHead( divs[p] );
      handle_heads( heads, os );
    }
    handle_deeper( divs[p], os, doHeads );
  }
}

void handle_txts( vector<Text *>& txts, ostream& os, bool doHeads ){
  for ( const auto& txt : txts ){
    if ( doHeads ){
      vector<Head *> heads = getHead( txt );
      handle_heads( heads, os );
    }
    vector<Division *> divs = txt->select<Division>( false );
    if ( divs.empty() ){
      handle_deeper( txt, os, doHeads );
    }
    else {
      handle_divs( divs, os, doHeads );
    }
  }
}

void text_out( const Document *d,
	       ostream& os,
	       bool doHeads ){
  FoliaElement *doc = d->doc();
  if ( doHeads ){
    vector<Head *> heads = getHead( doc );
    handle_heads( heads, os );
  }
  vector<Text *> txts = doc->select<Text>( false );
  if ( txts.empty() ){
    vector<Division *> divs = doc->select<Division>( false );
    if ( divs.empty() ){
      handle_deeper( doc, os, doHeads );
    }
    else {
      handle_divs( divs, os, doHeads );
    }
  }
  else {
    handle_txts( txts, os, doHeads );
  }
}

void usage( const string& name ){
  cerr << "Usage: " << name << " [options] file/dir" << endl;
  cerr << "\t FoLiA-2text will produce a text from a FoLiA file, " << endl;
  cerr << "\t or a whole directory of FoLiA files " << endl;
  cerr << "\t--class='name', use 'name' as the folia class for <t> nodes. (default is 'current')" << endl;
  cerr << "\t--heads\t Incude text from Head nodes too. (default don't)" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-V or --version \t show version " << endl;
  cerr << "\t-e\t expr: specify the expression all input files should match with." << endl;
  cerr << "\t-o\t name of the output file(s) prefix." << endl;
}

int main( int argc, char *argv[] ){
  CL_Options opts( "hVvpe:t:o:", "class:,help,version,heads" );
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
  string expression;
  string outputPrefix;
  string value;
  if ( opts.extract('V') || opts.extract("version") ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('h') || opts.extract("help") ){
    usage(progname);
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'o', outputPrefix ) ){
    if ( outputPrefix.empty() ){
      cerr << "an output filename prefix is required. (-o option) " << endl;
      exit(EXIT_FAILURE);
    }
  }
  bool doHeads = opts.extract( "heads" );
  if ( opts.extract('t', value ) ){
    if ( !stringTo(value, numThreads ) ){
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  opts.extract('e', expression );
  opts.extract( "class", classname );

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
    string outname = outputPrefix + docName + ".txt";
    if ( !TiCC::createPath( outname ) ){
#pragma omp critical
      {
	cerr << "Output to '" << outname << "' is impossible" << endl;
      }
    }
    else {
      ofstream os( outname );
      text_out( d, os, doHeads );
#pragma omp critical
      {
	cout << "Processed :" << docName << " into " << outname
	     << " still " << --toDo << " files to go." << endl;
      }
    }
    delete d;
  }
}
