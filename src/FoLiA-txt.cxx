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

#include <string>
#include <iostream>
#include <fstream>

#include "ticcutils/CommandLine.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/FileUtils.h"
#include "libfolia/document.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;

string setname = "FoLiA-txt-set";
string classname = "FoLiA-txt";

void usage(){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t FoLiA-txt will produce FoLiA files from text files " << endl;
  cerr << "\t The output will only contain <p> and <str> nodes." << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h\t this message" << endl;
  cerr << "\t-V\t show version " << endl;
  cerr << "\t-O\t output directory " << endl;
  cerr << "\t--setname The FoLiA setname of the <str> nodes. "
    "(Default '" << setname << "')" << endl;
  cerr << "\t--class The classname of the <str> nodes. (Default '"
       << classname << "')"<< endl;
}

string filterMeuck( const string& s ){
  string result;
  for( size_t i=0; i < s.size(); ++i ){
    int val = int(s[i]);
    //    cerr << s[i] << "-" << val << endl;
    if ( val == 31 || val == 12 ){
      result += ' ';
    }
    else
      result += s[i];
  }
  return result;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "hVt:O:", "class:,setname:" );
  opts.init( argc, argv );
  string outputDir;
  int numThreads = 1;
  string value;
  if ( opts.extract( 'h' ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 't', value ) ){
    numThreads = TiCC::stringTo<int>( value );
  }
  opts.extract( 'O', outputDir );
  if ( !outputDir.empty() && outputDir[outputDir.length()-1] != '/' )
    outputDir += "/";
  opts.extract( "class", classname );
  opts.extract( "setname", setname );
  if ( !opts.empty() ){
    usage();
    exit(EXIT_FAILURE);
  }
  if ( !outputDir.empty() ){
    if ( !TiCC::isDir(outputDir) ){
      if ( !TiCC::createPath( outputDir ) ){
	cerr << "outputdir '" << outputDir
	     << "' doesn't exist and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  vector<string> fileNames = opts.getMassOpts();
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }
  if ( toDo > 1 ){
#ifdef HAVE_OPENMP
    folia::initMT();
#endif
    cout << "start processing of " << toDo << " files " << endl;
  }

#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 )
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
#endif

#pragma omp parallel for shared(fileNames )
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    string fileName = fileNames[fn];
    ifstream is( fileName.c_str() );
    if ( !is ){
#pragma omp critical
      {
	cerr << "failed to read " << fileName << endl;
      }
      continue;
    }
    string nameNoExt = fileName;
    string::size_type pos = fileName.rfind( "." );
    if ( pos != string::npos ){
      nameNoExt = fileName.substr(0, pos );
    }
    string docid = nameNoExt;
    pos = docid.rfind( "/" );
    if ( pos != string::npos ){
      docid = docid.substr( pos+1 );
    }
    if ( !outputDir.empty() ){
      nameNoExt = docid;
    }
    Document *d = 0;
    try {
      d = new Document( "id='"+ docid + "'" );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "failed to create a document with id:'" << docid << "'" << endl;
	cerr << "reason: " << e.what() << endl;
      }
      continue;
    }
    d->declare( folia::AnnotationType::STRING, setname,
		"annotator='FoLiA-txt', datetime='now()'" );
    folia::Text *text = new folia::Text( d, "id='" + docid + ".text'" );
    d->addText( text );
    int parCount = 0;
    int wrdCnt = 0;
    folia::FoliaElement *par = 0;
    string parTxt;
    string parId;
    string line;
    while ( getline( is, line ) ){
      line = TiCC::trim(line);
      if ( line.empty() ){
	TiCC::trim( parTxt );
	if ( par && !parTxt.empty() ){
	  par->settext( parTxt, classname );
	  text->append( par );
	  parTxt = "";
	}
	par = 0;
	continue;
      }
      vector<string> words;
      TiCC::split( line, words );
      for ( size_t i=0; i < words.size(); ++i ){
	if ( par == 0 ){
	  folia::KWargs args;
	  parId = docid + ".p." +  TiCC::toString(++parCount);
	  args["id"] = parId;
	  par = new folia::Paragraph( d, args );
	  wrdCnt = 0;
	}
	string content = words[i];
	content = filterMeuck( content );
	content = TiCC::trim( content);
	if ( !content.empty() ){
	  folia::KWargs args;
	  args["id"] = parId + ".str." +  TiCC::toString(++wrdCnt);
	  folia::FoliaElement *str = new folia::String( d, args );
	  str->settext( content, classname );
	  parTxt += " " + content;
	  par->append( str );
	}
      }
    }
    parTxt = TiCC::trim( parTxt );
    if ( !parTxt.empty() ){
      par->settext( parTxt, classname );
      text->append( par );
    }
    string outname = outputDir + nameNoExt + ".folia.xml";
    cerr << "OUTNAME " << outname << endl;
    d->save( outname );
#pragma omp critical
    {
      cout << "Processed: " << fileName << " into " << outname
	   << " still " << --toDo << " files to go." << endl;
    }
    delete d;
  }
}
