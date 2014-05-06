/*
  $Id: FoLiA-stats.cxx 17149 2014-04-28 11:55:06Z sloot $
  $URL: https://ilk.uvt.nl/svn/sources/foliatools/trunk/src/FoLiA-stats.cxx $
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

#include <string>
#include <iostream>
#include <fstream>

#include "ticcutils/CommandLine.h"
#include "ticcutils/StringOps.h"
#include "libfolia/document.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;



void usage(){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h\t this message" << endl;
  cerr << "\t-V\t show version " << endl;
  cerr << "\t tst2folia will produce basic FoLiA files from text files " << endl;
  cerr << "\t The output will only contain <p> and <str> nodes." << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( argc, argv );
  int numThreads = 1;
  string value;
  bool mood;
  if ( opts.find( 'h', value, mood ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.find( 'V', value, mood ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
    exit(EXIT_SUCCESS);
  }
  if ( opts.find( 't', value, mood ) ){
    numThreads = TiCC::stringTo<int>( value );
  }

#ifdef HAVE_OPENMP
  if ( numThreads != 1 )
    omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 )
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
#endif

  vector<string> fileNames = opts.getMassOpts();
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }
  if ( toDo > 1 ){
    try {
      Document doc( "string='<?xml version=\"1.0\" encoding=\"UTF-8\"?><FoLiA/>'" );
    }
    catch(...){
    };
    cout << "start processing of " << toDo << " files " << endl;
  }

#pragma omp parallel for shared(fileNames )
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    string docid = fileNames[fn];
#pragma omp critical
      {
	cout << "examine: " << docid << endl;
      }
    ifstream is( docid.c_str() );
    if ( !is ){
#pragma omp critical
      {
	cerr << "failed to read " << docid << endl;
      }
      continue;
    }
    string::size_type pos = docid.rfind( "." );
    if ( pos != string::npos ){
      docid = docid.substr(0, pos );
    }
    Document *d = 0;
    try {
      d = new Document( "id='"+ docid + "'" );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "failed to create a document '" << docid << "'" << endl;
	cerr << "reason: " << e.what() << endl;
      }
      continue;
    }
    d->declare( folia::AnnotationType::STRING, "foliastr",
		"annotator='txt2folia', datetime='now()'" );
    folia::FoliaElement *text = new folia::Text( "id='" + docid + ".text'" );
    d->append( text );
    int parCount = 1;
    folia::KWargs args;
    args["id"] = docid + ".p." +  TiCC::toString(parCount);
    folia::FoliaElement *par = new folia::Paragraph( args );
    text->append( par );
    string line;
    int wrdCnt = 0;
    string parTxt;
    while ( getline( is, line ) ){
      line = TiCC::trim(line);
      if ( line.empty() ){
	if ( !parTxt.empty() ){
	  par->settext( parTxt );
	  parTxt = "";
	  folia::KWargs args;
	  args["id"] = docid + ".p." +  TiCC::toString(++parCount);
	  par = new folia::Paragraph( args );
	  text->append( par );
	}
	continue;
      }
      vector<string> words;
      TiCC::split( line, words );
      for ( size_t i=0; i < words.size(); ++i ){
	string content = words[i];
	folia::KWargs args;
	args["id"] = docid + ".str." +  TiCC::toString(++wrdCnt);
	folia::FoliaElement *str = new folia::String( args );
	if ( content.empty() ){
	  cerr << "GVD " << wrdCnt-1 << endl;
	}
	else {
	  str->settext( content );
	}
	parTxt += " " + content;
	par->append( str );
      }
    }
    if ( !parTxt.empty() ){
      par->settext( parTxt );
    }
    string outname = docid + ".folia.xml";
    d->save( outname );
#pragma omp critical
    {
      cout << "Processed :" << docid << " into " << outname
	   << " still " << --toDo << " files to go." << endl;
    }
    delete d;
  }
}
