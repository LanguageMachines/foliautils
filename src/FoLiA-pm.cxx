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

#include <unistd.h> // getopt, unlink
#include <string>
#include <list>
#include <map>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/zipper.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;

bool verbose = false;

void convert_to_folia( const string& file,
		       const string& outDir ){
  bool succes = true;
#pragma omp critical
  {
    cout << "converting: " << file << endl;
  }
  xmlDoc *xmldoc = xmlReadFile( file.c_str(),
				0,
				XML_PARSE_NOBLANKS|XML_PARSE_HUGE );
  if ( xmldoc ){
    string docid = "test";
    folia::Document doc( "id='" + docid + "'" );
    xmlNode *root = xmlDocGetRootElement( xmldoc );
    xmlNode *metadata = TiCC::xPath( root, "//meta" );
    if ( metadata ){
      doc.set_foreign_metadata( metadata );
      // xmlNode *p = metadata;
      // while ( p ){
      // 	cerr << "Node: " << TiCC::Name( p ) << endl;
      // 	std::map<std::string,std::string> ns = TiCC::getDefinedNS( p );
      // 	using TiCC::operator<<;
      // 	cerr << ns << endl;
      // 	p = p->next;
      // }
    }
    else {
#pragma omp critical
      {
	cerr << "no metadata" << endl;
      }
      succes = false;
    }
    folia::Text *text = new folia::Text( folia::getArgs( "id='" + docid + ".text'"  ));
    doc.append( text );
    xmlNode *p = metadata->next;
    while ( p ){
      if ( TiCC::Name( p ) == "proceedings" ){
	string id = TiCC::getAttribute( p, "id" );
	folia::Division *div = new folia::Division( folia::getArgs( "id='" + id + "'") );
	text->append( div );
	xmlNode *topic = TiCC::xPath( p, "*:topic" );
	xmlNode *p = topic;
	while ( p ){
	  string id = TiCC::getAttribute( p, "id" );
	  folia::Division *div1 = new folia::Division( folia::getArgs( "id='" + id + "'") );
	  div->append( div1 );
	  p = p->next;
	}
      }
      p = p->next;
    }
    string outname = outDir+file+".folia";
#pragma omp critical
    {
      cerr << "save " << outname << endl;
    }

    doc.save( outname );
    xmlFreeDoc( xmldoc );
  }
  else {
#pragma omp critical
    {
      cerr << "XML failed: " << file << endl;
    }
    succes = false;
  }
  if ( succes ){
#pragma omp critical
    {
      cout << "resolved " << file << endl;
    }
  }
  else {
#pragma omp critical
    {
      cout << "FAILED: " << file << endl;
    }
  }
}


void usage(){
  cerr << "Usage: FoLiA-pm [options] file/dir" << endl;
  cerr << "\t convert Political Mashup XML files to FoLiA" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h\t this messages " << endl;
  cerr << "\t-O\t output directory " << endl;
  cerr << "\t-v\t verbose output " << endl;
  cerr << "\t-V\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts;
  try {
    opts.set_short_options( "vVt:O:h" );
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
  int numThreads=1;
  string outputDir;
  if ( opts.extract('h' ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('V' ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  verbose = opts.extract( 'v' );
  string value;
  if ( opts.extract( 't', value ) ){
    numThreads = TiCC::stringTo<int>( value );
  }
  opts.extract( 'O', outputDir );
  if ( !outputDir.empty() && outputDir[outputDir.length()-1] != '/' )
    outputDir += "/";
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    cerr << "missing input file(s)" << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  string dirName;
  if ( !outputDir.empty() ){
    if ( !TiCC::isDir(outputDir) ){
      if ( !TiCC::createPath( outputDir ) ){
	cerr << "outputdir '" << outputDir
	     << "' doesn't exist and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  if ( fileNames.size() == 1 ){
    string name = fileNames[0];
    if ( !( TiCC::isFile(name) || TiCC::isDir(name) ) ){
      cerr << "'" << name << "' doesn't seem to be a file or directory"
	   << endl;
      exit(EXIT_FAILURE);
    }
    if ( TiCC::isFile(name) ){
      string::size_type pos = name.rfind( "/" );
      if ( pos != string::npos )
	dirName = name.substr(0,pos);
    }
    else {
      fileNames = TiCC::searchFilesMatch( name, "*.xml" );
    }
  }
  else {
    // sanity check
    vector<string>::iterator it = fileNames.begin();
    while ( it != fileNames.end() ){
      if ( it->find( ".xml" ) == string::npos ){
	if ( verbose ){
	  cerr << "skipping file: " << *it << endl;
	}
	it = fileNames.erase(it);
      }
      else
	++it;
    }
  }
  size_t toDo = fileNames.size();
  cout << "start processing of " << toDo << " files " << endl;
  if ( numThreads >= 1 ){
#ifdef HAVE_OPENMP
    omp_set_num_threads( numThreads );
#else
    numThreads = 1;
#endif
  }

#pragma omp parallel for shared(fileNames)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    convert_to_folia( fileNames[fn], outputDir );
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
