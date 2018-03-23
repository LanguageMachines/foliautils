/*
  Copyright (c) 2014 - 2018
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
#include <iostream>
#include <fstream>

#include "ticcutils/CommandLine.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"

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
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-V or --version\t show version " << endl;
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
  TiCC::CL_Options opts( "hVt:O:", "class:,setname:,help,version" );
  try {
    opts.init( argc, argv );
  }
  catch ( exception&e ){
    cerr << e.what() << endl;
    exit(EXIT_FAILURE);
  }
  string outputDir;
  int numThreads = 1;
  string value;
  if ( opts.empty() ){
    usage();
    exit(EXIT_FAILURE);
  }
  if ( opts.extract( 'h' ) ||
       opts.extract( "help" ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) ||
       opts.extract( "version" ) ){
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
  if ( !outputDir.empty() ){
    if ( !TiCC::isDir(outputDir) ){
      if ( !TiCC::createPath( outputDir ) ){
	cerr << "outputdir '" << outputDir
	     << "' doesn't exist and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  vector<string> file_names = opts.getMassOpts();
  size_t to_do = file_names.size();
  if ( to_do == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }
  if ( to_do == 1 ){
    if ( TiCC::isDir( file_names[0] ) ){
      file_names = TiCC::searchFiles( file_names[0] );
      to_do = file_names.size();
      if ( to_do == 0 ){
	cerr << "no files found in inputdir" << endl;
	exit(EXIT_SUCCESS);
      }
    }
  }
  if ( to_do > 1 ){
    cout << "start processing of " << to_do << " files " << endl;
  }

#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 )
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
#endif

  size_t failed_docs = 0;
#pragma omp parallel for shared(file_names) schedule(dynamic)
  for ( size_t fn=0; fn < file_names.size(); ++fn ){
    string fileName = file_names[fn];
    ifstream is( fileName );
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
	++failed_docs;
	--to_do;
      }
      continue;
    }
    d->declare( folia::AnnotationType::STRING, setname,
		"annotator='FoLiA-txt', datetime='now()'" );
    folia::Text *text = new folia::Text( folia::getArgs("id='" + docid + ".text'"), d );
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
      vector<string> words = TiCC::split( line );
      for ( const auto& w : words ){
	if ( par == 0 ){
	  folia::KWargs args;
	  parId = docid + ".p." +  TiCC::toString(++parCount);
	  args["id"] = parId;
	  par = new folia::Paragraph( args, d );
	  wrdCnt = 0;
	}
	string content = w;
	content = TiCC::trim( content);
	if ( !content.empty() ){
	  folia::KWargs args;
	  args["id"] = parId + ".str." +  TiCC::toString(++wrdCnt);
	  folia::FoliaElement *str = new folia::String( args, d );
	  str->settext( content, classname );
	  parTxt += " " + content;
	  par->append( str );
	}
      }
    }
    if ( !par ){
#pragma omp critical
      {
	cerr << "nu useful data found in document:'" << docid << "'" << endl;
	cerr << "skipped!" << endl;
	++failed_docs;
	--to_do;
      }
      continue;
    }
    parTxt = TiCC::trim( parTxt );
    if ( !parTxt.empty() ){
      par->settext( parTxt, classname );
      text->append( par );
    }
    string outname = outputDir + nameNoExt + ".folia.xml";
    d->save( outname );
#pragma omp critical
    {
      cout << "Processed: " << fileName << " into " << outname
	   << " still " << --to_do << " files to go." << endl;
    }
    delete d;
  }
  if ( failed_docs == to_do ){
    cerr << "No documents could be handled successfully!" << endl;
    return EXIT_SUCCESS;
  }
  else {
    return EXIT_SUCCESS;
  }
}
