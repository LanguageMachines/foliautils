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

#include <cassert>
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
  cerr << "\t--remove-end-hyphens:yes|no (default = 'yes') " << endl;
  cerr << "\t\t\t if 'yes', hyphens (-) att the end of lines are converted to"
    " <t-hbr> nodes.\n\t\t\t And ignored in general." << endl;
  cerr << "\t--setname The FoLiA setname of the created nodes. "
    "(Default '" << setname << "')" << endl;
  cerr << "\t--class The classname of the <t> nodes that are created. (Default '"
       << classname << "')"<< endl;
}

void add_paragraph( folia::FoliaElement *par,
		    const vector<FoliaElement*>& par_stack ){
  folia::KWargs text_args;
  text_args["class"] = classname;
  FoliaElement *txt = par->add_child<folia::TextContent>( text_args );
  for ( const auto& it : par_stack ){
    // we don't want a terminating <br/> at the end of a paragraph.
    // 2 newlines ar already implicit for a paragraph
    if ( &it == &par_stack.back()
	 && it->isSubClass( Linebreak_t ) ){
      break;
    }
    txt->append(it );
  }
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "hVt:O:",
			 "class:,setname:,remove-end-hyphens:,"
			 "help,version,threads:" );
  try {
    opts.init( argc, argv );
  }
  catch ( exception&e ){
    cerr << e.what() << endl;
    exit(EXIT_FAILURE);
  }
  string outputDir;
  string value;
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
#ifdef HAVE_OPENMP
  int numThreads = 1;
#endif
  string command = "FoLiA-txt " + opts.toString();
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
#ifdef HAVE_OPENMP
    if ( TiCC::lowercase(value) == "max" ){
      numThreads = omp_get_max_threads() - 2;
    }
    else if ( !TiCC::stringTo(value,numThreads) ) {
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
#else
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
    exit( EXIT_FAILURE );
#endif
  }
#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
#endif
  opts.extract( 'O', outputDir );
  if ( !outputDir.empty() && outputDir[outputDir.length()-1] != '/' ){
    outputDir += "/";
  }
  opts.extract( "class", classname );
  opts.extract( "setname", setname );
  bool keep_hyphens = false;
  string h_val;
  if ( opts.extract( "remove-end-hyphens", h_val ) ){
    keep_hyphens = !TiCC::stringTo<bool>( h_val );
  }
  vector<string> file_names = opts.getMassOpts();
  size_t to_do = file_names.size();
  if ( to_do == 0 ){
    cerr << "no matching files found" << endl;
    usage();
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
    if ( !outputDir.empty() ){
      if ( !TiCC::isDir(outputDir) ){
	if ( !TiCC::createPath( outputDir ) ){
	  cerr << "outputdir '" << outputDir
	       << "' doesn't exist and can't be created" << endl;
	  exit(EXIT_FAILURE);
	}
      }
    }
    cout << "start processing of " << to_do << " files" << endl;
  }
  size_t failed_docs = 0;
#ifdef HAVE_OPENMP
  bool shown = false;
#endif
#pragma omp parallel for shared(file_names) schedule(dynamic)
  for ( size_t fn=0; fn < file_names.size(); ++fn ){
#ifdef HAVE_OPENMP
#pragma omp critical
    {
      if ( !shown && omp_get_thread_num() == 1 ){
	cerr << "running on " << omp_get_num_threads() << " threads" << endl;
	shown = true;
      }
    }
#endif
    string fileName = file_names[fn];
    ifstream is( fileName );
    if ( !is ){
#pragma omp critical
      {
	cerr << "failed to read " << fileName << endl;
      }
      continue;
    }
#pragma omp critical
    {
      cout << "Starting " << fileName << endl;
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
    string processor_id = proc->id();
    KWargs p_args;
    p_args["processor"] = processor_id;
    d->declare( folia::AnnotationType::STRING, setname, p_args );
    d->declare( folia::AnnotationType::PARAGRAPH, setname, p_args );
    d->declare( folia::AnnotationType::LINEBREAK, setname, p_args );
    p_args.clear();
    p_args["xml:id"] = docid + ".text";
    folia::Text *text = d->create_root<folia::Text>( p_args );
    int parCount = 0;
    int wrdCnt = 0;
    folia::FoliaElement *par = 0;
    string parId;
    vector<FoliaElement*> par_stack; // temp store for textfragments which will
    // make up the paragraph text. may include formatting like <t-hbr/>
    UnicodeString line;
    while ( TiCC::getline( is, line ) ){
      line.trim();
      if ( line.length() == 1
	   && line[line.length()-1] == ZWNJ ){
	line = pop_back( line );
      }
      if ( line.isEmpty() ){
	// end a paragraph
	if ( par && !par_stack.empty() ){
	  // do we have some fragments?
	  add_paragraph( par, par_stack );
	  par_stack.clear();
	  par = 0;
	}
	continue;
      }
      vector<UnicodeString> words = TiCC::split( line );
      for ( const auto& w : words ){
	if ( par == 0 ){
	  // start a new Paragraph, only when at least 1 entry.
	  folia::KWargs par_args;
	  par_args["processor"] = processor_id;
	  parId = docid + ".p." +  TiCC::toString(++parCount);
	  par_args["xml:id"] = parId;
	  par = text->add_child<folia::Paragraph>( par_args );
	  wrdCnt = 0;
	}
	UnicodeString str_content = w; // the value to create a String node
	str_content.trim();
	if ( !is_norm_empty(str_content) ){
	  UnicodeString par_content = str_content; // the value we will use for
	  // the paragraph text
	  UnicodeString hyph; // hyphen symbol
	  if ( keep_hyphens ){
	    // only soft hyphens are removed
	    par_content = extract_soft_hyphen( par_content, hyph );
	  }
	  else {
	    par_content = extract_final_hyphen( par_content, hyph );
	  }
	  // now we can add the <String>
	  folia::KWargs str_args;
	  str_args["xml:id"] = parId + ".str." +  TiCC::toString(++wrdCnt);
	  folia::FoliaElement *str = par->add_child<folia::String>( str_args );
	  str->setutext( str_content, classname );
	  if ( hyph.isEmpty() && &w != &words.back() ){
	    par_content += " "; // no hyphen, so add a space separator except
	    // for last word
	  }
	  XmlText *e = new folia::XmlText(); // create partial text
	  e->setuvalue( par_content );
	  par_stack.push_back( e ); // add the XmlText to te stack
	  if ( !hyph.isEmpty() ){
	    // add an extra HyphBreak to the stack
	    FoliaElement *hb = new folia::Hyphbreak();
	    XmlText *hb_txt = hb->add_child<folia::XmlText>(); // create partial text
	    hb_txt->setuvalue( hyph );
	    par_stack.push_back( hb );
	  }
	  else if ( &w == &words.back() ){
	    folia::KWargs line_args;
	    par_stack.push_back( new folia::Linebreak(line_args) );
	  }
	}
      }
    }
    if ( par && !par_stack.empty() ){
      // leftovers
      add_paragraph( par, par_stack );
      par = 0;
      par_stack.clear();
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
    string outname = outputDir + nameNoExt + ".folia.xml";
#pragma omp critical
    {
      d->save( outname );
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
