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
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "config.h"
#include "ucto/textcat.h"

using namespace	std;
using namespace	folia;

const string ISO_SET = "http://raw.github.com/proycon/folia/master/setdefinitions/iso639_3.foliaset";

bool verbose = false;

void setlang( FoliaElement* e, const string& lan ){
  // append a LangAnnotation child of class 'lan'
  KWargs args;
  args["class"] = lan;
  args["set"] = ISO_SET;
  LangAnnotation *node = new LangAnnotation( e->doc() );
  node->setAttributes( args );
  e->replace( node );
}

void addLang( TextContent *t,
	      const vector<string>& lv,
	      bool doAll ){
  //
  // we expect something like [dutch][french]
  //
  string val;
  for ( const auto& l : lv ){
    val += l;
    if ( !doAll )
      break;
    if ( &l != &lv.back() ){
      val += "|";
    }
  }
  if ( !val.empty() ){
    setlang( t->parent(), val );
  }
}

void procesFile( const TextCat& tc,
		 const string& outDir, const string& docName,
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
  doc->declare( AnnotationType::LANG, ISO_SET );
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

  ofstream os( outName );
  for ( size_t i=0; i < Size; ++i ){
    TextContent *t = 0;
    string id = "NO ID";
    try {
      if ( doStrings ){
	id = xs[i]->id();
	t = xs[i]->textcontent(cls);
      }
      else {
	id = xp[i]->id();
	t = xp[i]->textcontent(cls);
      }
    }
    catch (...){
    }
    if ( t ){
      string para = t->str();
      para = TiCC::trim( para );
      if ( para.empty() ){
	if ( verbose )
#pragma omp critical (logging)
	  {
	    cerr << "WARNING: no textcontent " << id << endl;
	  }
      }
      else {
	TiCC::to_lower( para );
	vector<string> lv = tc.get_languages( para );
	addLang( t, lv, doAll );
      }
    }
    else if ( verbose ){
#pragma omp critical (logging)
      {
	cerr << "WARNING: no textcontent " << id << endl;
      }
    }
  }
  os << doc << endl;
  delete doc;
}


void usage(){
  cerr << "Usage: [options] dir/filename " << endl;
  cerr << "--config=<file>\t use LM config from 'file'" << endl;
  cerr << "--lang=<lan>\t use 'lan' for unindentified text. (default 'nld')" << endl;
  cerr << "-s\t\t examine text in <str> nodes. (default is to use the <p> nodes)." << endl;
  cerr << "--all\t\t assign ALL detected languages to the result. (default is to assign the most probable)." << endl;
  cerr << "--class=<cls>\t use 'cls' as the FoLiA classname for searching text. "
       << endl;
  cerr << "\t\t\t (default 'OCR')" << endl;
  cerr << "-O path\t\t output path" << endl;
  cerr << "-V or --version\t show version info." << endl;
  cerr << "-v\t\t verbose" << endl;
  cerr << "-h or --help\t this messages." << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "svVhO:", "all,lang:,class:,config:,help,version" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
  if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
    cout << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'h' ) || opts.extract( "help") ){
    usage();
    exit(EXIT_SUCCESS);
  }
  string outDir;
  string config = "./config/tc.txt";
  string lang = "nld";
  string cls = "OCR";
  verbose = opts.extract( 'v' );
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
  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    cerr << "missing input file(s)" << endl;
    exit( EXIT_FAILURE );
  }
  else if ( fileNames.size() == 1 ){
    string name = fileNames[0];
    fileNames = TiCC::searchFilesExt( name, ".xml", false );
  }
  TextCat TC( config );
  if ( !TC.isInit() ){
    cerr << "unable to init from: " << config << endl;
    exit(EXIT_FAILURE);
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
    procesFile( TC, outDir, docName, lang, doStrings, doAll, cls );
  }
}
