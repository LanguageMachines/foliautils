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
#include "foliautils/common_code.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif
#include "ucto/my_textcat.h"

using namespace	std;
using namespace	folia;

const string ISO_SET = "http://raw.github.com/proycon/folia/master/setdefinitions/iso639_3.foliaset";

bool verbose = false;

void setlang( FoliaElement* e, const string& lan ){
  // append a LangAnnotation child of class 'lan'
  vector<LangAnnotation*> lav;
  try {
    lav = e->annotations<LangAnnotation>(ISO_SET);
  }
  catch (...){
  }
  if ( lav.empty() ){
    KWargs args;
    args["class"] = lan;
    args["set"] = ISO_SET;
    LangAnnotation *node = new LangAnnotation( args, e->doc() );
    e->append( node );
  }
  else {
    bool present = false;
    for ( const auto it : lav ){
      if ( it->cls() == lan ){
	present = true;
	break;
      }
    }
    if ( !present ){
      KWargs args;
      args["class"] = lan;
      args["set"] = ISO_SET;
      LangAnnotation *node = new LangAnnotation( args, e->doc() );
      Alternative *a = new Alternative( );
      a->append( node );
      e->append( a );
    }
  }
}

void addLang( const TextContent *t,
	      const vector<string>& lv,
	      bool doAll ){
  //
  // we expect something like [dutch][french]
  //
  string val;
  for ( const auto& l : lv ){
    setlang( t->parent(), l );
    if ( !doAll )
      break;
  }
}

vector<FoliaElement*> gather_nodes( Document *doc, const string& docName,
				    const set<string>& tags ){
  vector<FoliaElement*> result;
  for ( const auto& tag : tags ){
    ElementType et;
    try {
      et = TiCC::stringTo<ElementType>( tag );
    }
    catch ( ... ){
#pragma omp critical (logging)
      {
	cerr << "the string '" << tag
	     << "' doesn't represent a known FoLiA tag" << endl;
	exit(EXIT_FAILURE);
      }
    }
    vector<FoliaElement*> v = doc->doc()->select( et );
#pragma omp critical (logging)
    {
      cout << "document '" << docName << "' has " << v.size() << " "
	   << tag << " nodes " << endl;
    }
    result.insert( result.end(), v.begin(), v.end() );
  }
  return result;
}


void procesFile( const TextCat& tc,
		 const string& outDir, const string& docName,
		 const string& default_lang,
		 const set<string>& tags,
		 bool doAll,
		 const string& cls,
		 const string& command ){
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
  processor *proc = add_provenance( *doc, "FoLiA-langcat", command );
  if ( !doc->declared(  AnnotationType::LANG, ISO_SET ) ){
    KWargs args;
    args["processor"] = proc->id();
    doc->declare( AnnotationType::LANG, ISO_SET, args );
  }
  string outName;
  if ( !outDir.empty() ){
    outName = outDir + "/";
  }
  string::size_type pos = docName.rfind("/");
  if ( pos != string::npos ){
    outName += docName.substr( pos+1);
  }
  else {
    outName += docName;
  }
  string::size_type xml_pos = outName.find(".folia.xml");
  if ( xml_pos == string::npos ){
    xml_pos = outName.find(".xml");
  }
  outName.insert( xml_pos, ".lang" );
  if ( !TiCC::createPath( outName ) ){
#pragma omp critical (logging)
    {
      cerr << "unable to open output file " << outName << endl;
      cerr << "does the outputdir exist? And is it writabe?" << endl;
    }
    exit( EXIT_FAILURE);
  }
  vector<FoliaElement*> nodes = gather_nodes( doc, docName, tags );
  for ( const auto& node : nodes ){
    const TextContent *t = 0;
    string id = "NO ID";
    try {
      id = node->id();
      t = node->text_content(cls);
    }
    catch (...){
    }
    if ( t ){
      string para = t->str(cls);
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
  doc->save(outName);
  delete doc;
}


void usage(){
  cerr << "Usage: [options] dir/filename " << endl;
  cerr << "\t add language information to a file or files in FoLiA XML format" << endl;
  cerr << "\t  The files must have extension '.folia.xml' or '.xml'" << endl;
  cerr << "\t  or their .gz or .bz2 variants" << endl;
  cerr << "--config=<file>\t use LM config from 'file'" << endl;
  cerr << "--lang=<lan>\t use 'lan' for unindentified text. (default 'nld')" << endl;
  cerr << "--tags=t1,t2,..\t examine text in all <t1>, <t2> ...  nodes. (default is to use the <p> nodes)." << endl;
  cerr << "-s\t\t (obsolete) shorthand for --tags='str'" << endl;
  cerr << "--all\t\t assign ALL detected languages to the result. (default is to assign the most probable)." << endl;
  cerr << "--class=<cls>\t use 'cls' as the FoLiA classname for searching text. "
       << endl;
  cerr << "\t\t\t (default 'OCR')" << endl;
  cerr << "\t-t <threads>\n\t--threads <threads> Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "-O path\t\t output path" << endl;
  cerr << "-V or --version\t show version info." << endl;
  cerr << "-v\t\t verbose" << endl;
  cerr << "-h or --help\t this messages." << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "svVhO:t:", "all,lang:,class:,config:,help,version,tags:,threads:" );
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
  string value;
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
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
    cerr << "FoLiA-langcat: OpenMP support is missing. -t option is not supported" << endl;
    exit( EXIT_FAILURE );
#endif
  }
  bool doAll = opts.extract( "all" );
  bool doStrings = opts.extract( 's' );
  opts.extract( "config", config );
  opts.extract( "lang", lang );
  opts.extract( "class", cls );
  opts.extract( 'O', outDir );
  set<string> tags;
  string tagsstring;
  opts.extract( "tags", tagsstring );
  if ( !tagsstring.empty() ){
    vector<string> parts = TiCC::split_at( tagsstring, "," );
    for( const auto& t : parts ){
      tags.insert( t );
    }
  }
  if ( doStrings ){
    if ( !tags.empty() ){
      cerr << "--tags and -s conflict." << endl;
      exit(EXIT_FAILURE);
    }
    else {
      tags.insert( "str" );
    }
  }
  if ( tags.empty() ){
    tags.insert( "p" );
  }
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  string command = "FoLiA-langcat " + opts.toString();
  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    cerr << "missing input file(s)" << endl;
    exit( EXIT_FAILURE );
  }
  else if ( fileNames.size() == 1 ){
    string name = fileNames[0];
    try {
      fileNames = TiCC::searchFilesMatch( name, "*.xml*", false );
    }
    catch ( ... ){
      cerr << "no matching xml file found: '" << name << "'" << endl;
      exit( EXIT_FAILURE );
    }
  }
  TextCat TC( config );
  if ( !TC.isInit() ){
    cerr << "unable to init from: " << config << endl;
    exit(EXIT_FAILURE);
  }

  size_t toDo = fileNames.size();
  if ( toDo > 1 ){
    cout << "start processing of " << toDo << " files " << endl;
  }
#pragma omp parallel for firstprivate(TC) shared(fileNames,toDo) schedule(dynamic)
  for ( size_t fn=0; fn < toDo; ++fn ){
    string docName = fileNames[fn];
    procesFile( TC, outDir, docName, lang, tags, doAll, cls, command );
  }
  return EXIT_SUCCESS;
}
