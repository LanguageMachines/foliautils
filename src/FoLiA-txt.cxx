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

#include <string>
#include <iostream>
#include <fstream>

#include "ticcutils/CommandLine.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"
#include "foliautils/common_code.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;

string setname = "FoLiA-txt-set";
string classname = "FoLiA-txt";
string processor_id;

void usage(){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t FoLiA-txt will produce FoLiA files from text files " << endl;
  cerr << "\t The output will only contain <p> and <str> nodes." << endl;
  cerr << "\t-t <threads>\n\t--threads <threads> Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-V or --version\t show version " << endl;
  cerr << "\t-O\t output directory " << endl;
  cerr << "\t--setname The FoLiA setname of the <str> nodes. "
    "(Default '" << setname << "')" << endl;
  cerr << "\t--class The classname of the <str> nodes. (Default '"
       << classname << "')"<< endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "hVt:O:", "class:,setname:,help,version,threads:" );
  try {
    opts.init( argc, argv );
  }
  catch ( exception&e ){
    cerr << e.what() << endl;
    exit(EXIT_FAILURE);
  }
  string outputDir;
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
  string command = "FoLiA-txt " + opts.toString();
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
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
    exit( EXIT_FAILURE );
#endif
  }

  opts.extract( 'O', outputDir );
  if ( !outputDir.empty() && outputDir[outputDir.length()-1] != '/' ){
    outputDir += "/";
  }
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
    if ( !isNCName( docid ) ){
      docid = "doc-" + docid;
      if ( !isNCName( docid ) ){
	throw ( "unable to generate a Document ID from the filename: '"
		+ fileName + "'" );
      }
    }
    Document *d = 0;
    try {
      d = new Document( "xml:id='"+ docid + "'" );
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
    processor *proc = add_provenance( *d, "FoLiA-txt", command );
    processor_id = proc->id();
    KWargs args;
    args["processor"] = processor_id;
    d->declare( folia::AnnotationType::STRING, setname, args );
    args.clear();
    args["xml:id"] = docid + ".text";
    folia::Text *text = d->create_root<folia::Text>( args );
    int parCount = 0;
    int wrdCnt = 0;
    folia::FoliaElement *par = 0;
    string parTxt;
    string parId;
    string line;
    while ( getline( is, line ) ){
      line = TiCC::trim(line);
      if ( line.empty() ){
	parTxt = TiCC::trim( parTxt );
	if ( par && !is_norm_empty(parTxt) ){
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
	  folia::KWargs p_args;
	  p_args["processor"] = processor_id;
	  d->declare( folia::AnnotationType::PARAGRAPH, setname, p_args );
	  p_args.clear();
	  parId = docid + ".p." +  TiCC::toString(++parCount);
	  p_args["xml:id"] = parId;
	  par = new folia::Paragraph( p_args, d );
	  wrdCnt = 0;
	}
	string content = w;
	content = TiCC::trim( content);
	if ( !is_norm_empty(content) ){
	  folia::KWargs str_args;
	  str_args["xml:id"] = parId + ".str." +  TiCC::toString(++wrdCnt);
	  folia::FoliaElement *str = par->add_child<folia::String>( str_args );
	  str->settext( content, classname );
	  parTxt += " " + content;
	}
      }
    }
    if ( parCount == 0 ){
#pragma omp critical
      {
	cerr << "no useful data found in document:'" << docid << "'" << endl;
	cerr << "skipped!" << endl;
	++failed_docs;
	--to_do;
      }
      continue;
    }
    parTxt = TiCC::trim( parTxt );
    if ( !is_norm_empty(parTxt) ){
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
  if ( failed_docs > 0 && failed_docs == to_do ){
    cerr << "No documents could be handled successfully!" << endl;
    return EXIT_SUCCESS;
  }
  else {
    return EXIT_SUCCESS;
  }
}
