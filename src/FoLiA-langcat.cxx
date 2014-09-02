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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "libfolia/document.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "config.h"

#ifdef HAVE_TEXTCAT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "textcat.h"

#ifdef __cplusplus
}
#endif

#else
#ifdef HAVE_LIBTEXTCAT_TEXTCAT_H
#include "libtextcat/textcat.h"
#else
#ifdef HAVE_LIBEXTTEXTCAT_TEXTCAT_H
#include "libexttextcat/textcat.h"
#endif
#endif
#endif

using namespace	std;
using namespace	folia;

size_t split_at( const string& src, vector<string>& results,
		 const string& sep ){
  // split a string into substrings, using seps as seperator
  // silently skip empty entries (e.g. when two or more seperators co-incide)
  results.clear();
  string::size_type pos = 0, p;
  string res;
  while ( pos != string::npos ){
    p = src.find_first_of( sep, pos );
    if ( p == string::npos ){
      res = src.substr( pos );
      pos = p;
    }
    else {
      res = src.substr( pos, p - pos );
      pos = p + 1;
    }
    if ( !res.empty() )
      results.push_back( res );
  }
  return results.size();
}

string compress( const string& s ){
  // remove leading and trailing spaces from a string
  string result;
  if ( !s.empty() ){
    string::const_iterator b_it = s.begin();
    while ( b_it != s.end() && isspace( *b_it ) ) ++b_it;
    string::const_iterator e_it = s.end();
    --e_it;
    while ( e_it != s.begin() && isspace( *e_it ) ) --e_it;
    if ( b_it <= e_it )
      result = string( b_it, e_it+1 );
  }
  return result;
}

int to_lower( const int& i ){ return tolower(i); }

void decap( string& s ){
  transform( s.begin()+1, s.end(), s.begin()+1, to_lower );
}

void setlang( FoliaElement* e, const string& lan ){
  // append a LangAnnotation child of class 'cls'
  KWargs args;
  args["class"] = lan;
  LangAnnotation *node = new LangAnnotation( e->doc() );
  node->setAttributes( args );
  e->replace( node );
}

void addLang( TextContent *t, const string& val, bool doAll ){
  //
  // we expect something like [dutch][french]
  // or [dutch]
  // or WEIRD
  //
  vector<string> vals;
  size_t num = split_at( val, vals, "[]" );
  if ( num == 0 ){
    cerr << "O JEE: unexpected language value: '" << val << "'" << endl;
    setlang( t->parent(), val );
  }
  else {
    string val;
    for ( size_t i = 0; i < vals.size(); ++i ){
      if ( i > 0 )
	val += "|";
      val += vals[i];
      if ( !doAll )
	break;
    }
    setlang( t->parent(), val );
  }
}

class TCdata {
public:
  TCdata( const string& cf ) {
    cfName = cf;
    TC = textcat_Init( cf.c_str() );
  }
  TCdata( const TCdata& in ) {
    TC = textcat_Init( in.cfName.c_str() );
    cfName = in.cfName;
  }
  ~TCdata() { textcat_Done( TC ); };
  bool isInit() const { return TC != 0; };
  void procesFile( const string&, const string&, const string&, bool, bool, const string& );
  void *TC;
  string cfName;
};

void TCdata::procesFile( const string& outDir, const string& docName,
			 const string& default_lang,
			 bool doStrings,
			 bool doAll,
			 const string& cls ){
#pragma omp critical (logging)
  {
    cout << "process " << docName << endl;
  }
  Document *doc = 0;
  try {
    doc = new Document( "file='" + docName + "'" );
  }
  catch (const exception& e){
#pragma omp critical (logging)
    {
      cerr << "no document: " << e.what() << endl;
    }
    return;
  }
  doc->set_metadata( "language", default_lang );
  doc->declare( AnnotationType::LANG, "iso" );
  vector<Paragraph*> xp;
  vector<String*> xs;
  size_t Size;
  if ( doStrings ){
    xs = doc->doc()->select<String>();
    Size = xs.size();
#pragma omp critical (logging)
    {
      cout << "document '" << docName << "' has " << Size
	   << " strings " << endl;
    }
  }
  else {
    xp = doc->paragraphs();
    Size = xp.size();
#pragma omp critical (logging)
    {
      cout << "document '" << docName << "' has " << Size
	   << " paragraphs " << endl;
    }
  }

  string outName;
  if ( !outDir.empty() )
    outName = outDir + "/";

  string::size_type pos = docName.rfind("/");
  if ( pos != string::npos )
    outName += docName.substr( pos+1, docName.find(".xml") - pos - 1);
  else
    outName += docName.substr(0, docName.find(".xml") );
  outName += ".lc.xml";
  if ( !TiCC::createPath( outName ) ){
#pragma omp critical (logging)
    {
      cerr << "unable to open output file " << outName << endl;
      cerr << "does the outputdir exist? And is it writabe?" << endl;
    }
    exit( EXIT_FAILURE);
  }

  ofstream os( outName.c_str() );
  for ( size_t i=0; i < Size; ++i ){
    TextContent *t = 0;
    if ( doStrings )
      t = xs[i]->textcontent(cls);
    else
      t = xp[i]->textcontent(cls);
    string para = t->str();
    para = compress( para );
    if ( para.empty() ){
      // #pragma omp_critical (logging)
      // 	    {
      // 	      cerr << "WARNING: empty paragraph " << id << endl;
      // 	    }
    }
    else {
      decap( para );
      char *res = textcat_Classify( TC, para.c_str(), para.size() );
      if ( res && strlen(res) > 0 && strcmp( res, "SHORT" ) != 0 ){
	addLang( t, res, doAll );
      }
    }
  }
  os << doc << endl;
  delete doc;
}


void usage(){
  cerr << "Usage: [options] dir/filename " << endl;
  cerr << "--all\tassign ALL detected languages to the result. (default is to assign the most probable)." << endl;
  cerr << "--config=<file> use LM config from 'file'" << endl;
  cerr << "--lang=<lan> use 'lan' for unindentified text. (default 'dut')" << endl;
  cerr << "-s\texamine text in <str> nodes. (default is to use the <p> nodes)." << endl;
  cerr << "--class=<cls> use 'cls' as the FoLiA classname for text. (default 'OCR')" << endl;  cerr << "-V\tshow version info." << endl;
  cerr << "-v\tverbose" << endl;
  cerr << "-h\tthis messages." << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "svVhO:", "all,lang:,class:,config:" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
  if ( opts.extract( 'V' ) ){
    cout << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'h' ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  string outDir;
  string config = "./config/tc.txt";
  string lang = "dut";
  string cls = "OCR";
  bool verbose = opts.extract( 'v' );
  bool doAll = opts.extract( "all" );
  bool doStrings = opts.extract( 's' );
  opts.extract( "config", config );
  opts.extract( "lang", lang );
  opts.extract( "class", cls );
  opts.extract( 'O', outDir );
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  TCdata TC( config );
  if ( !TC.isInit() ){
    cerr << "unable to init from: " << config << endl;
    exit(EXIT_FAILURE);
  }

  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    cerr << "missing input file(s)" << endl;
    exit( EXIT_FAILURE );
  }
  else if ( fileNames.size() == 1 ){
    string name = fileNames[0];
    fileNames = TiCC::searchFilesExt( name, ".xml", false );
  }
  size_t toDo = fileNames.size();
  if ( toDo > 1 ){
#ifdef HAVE_OPENMP
    folia::initMT();
#endif
    cout << "start processing of " << toDo << " files " << endl;
  }
#pragma omp parallel for firstprivate(TC),shared(fileNames,toDo)
  for ( size_t fn=0; fn < toDo; ++fn ){
    string docName = fileNames[fn];
    TC.procesFile( outDir, docName, lang, doStrings, doAll, cls );
  }
}
