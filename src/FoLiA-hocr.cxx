/*
  $Id$
  $URL$
  Copyright (c) 1998 - 2015
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
#include <list>
#include <map>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "libfolia/document.h"
#include "libxml/HTMLparser.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/zipper.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;

bool verbose = false;
string setname = "FoLiA-hocr-set";
string classname = "OCR";

enum zipType { NORMAL, GZ, BZ2, UNKNOWN };

xmlDoc *getXml( const string& file, zipType& type ){
  type = UNKNOWN;
  bool isHtml;
  if ( TiCC::match_back( file, ".xhtml" ) ){
    type = NORMAL;
    isHtml = false;
  }
  else if ( TiCC::match_back( file, ".html" ) ){
    type = NORMAL;
    isHtml = true;
  }
  else if ( TiCC::match_back( file, ".hocr" ) ){
    type = NORMAL;
    isHtml = true;
  }
  else if ( TiCC::match_back( file, ".xhtml.gz" ) ){
    type = GZ;
    isHtml = false;
  }
  else if ( TiCC::match_back( file, ".html.gz" ) ){
    type = GZ;
    isHtml = true;
  }
  else if ( TiCC::match_back( file, ".hocr.gz" ) ){
    type = GZ;
    isHtml = true;
  }
  else if ( TiCC::match_back( file, ".xhtml.bz2" ) ){
    type = BZ2;
    isHtml = false;
  }
  else if ( TiCC::match_back( file, ".html.bz2" ) ){
    type = BZ2;
    isHtml = true;
  }
  else {
    return 0;
  }
  if ( isHtml ){
    if ( type == NORMAL ){
      return htmlReadFile( file.c_str(), 0, XML_PARSE_NOBLANKS );
    }
    string buffer;
    if ( type == GZ ){
      buffer = TiCC::gzReadFile( file );
    }
    else if ( type == BZ2 ){
      buffer = TiCC::bz2ReadFile( file );
    }
    return htmlReadMemory( buffer.c_str(), buffer.length(),
			   0, 0, XML_PARSE_NOBLANKS );
  }
  else {
    if ( type == NORMAL ){
      return xmlReadFile( file.c_str(), 0, XML_PARSE_NOBLANKS );
    }
    string buffer;
    if ( type == GZ ){
      buffer = TiCC::gzReadFile( file );
    }
    else if ( type == BZ2 ){
      buffer = TiCC::bz2ReadFile( file );
    }
    return xmlReadMemory( buffer.c_str(), buffer.length(),
			  0, 0, XML_PARSE_NOBLANKS );
  }
}

string extractContent( xmlNode* pnt ) {
  string result;
  if ( pnt ){
    result = TiCC::XmlContent(pnt);
    if ( result == "" )
      return extractContent( pnt->children );
  }
  return result;
}


void processParagraphs( xmlNode *div, folia::FoliaElement *out, const string& file ){
  list<xmlNode*> pars = TiCC::FindNodes( div, "//p" );
  list<xmlNode*>::const_iterator pit = pars.begin();
  while ( pit != pars.end() ){
    string p_id = TiCC::getAttribute( *pit, "id" );
    folia::Paragraph *par
      = new folia::Paragraph( out->doc(),
			      "id='" + out->id() + "." + p_id + "'");
    list<xmlNode*> lines = TiCC::FindNodes( *pit, ".//span[@class='ocr_line']" );
    if ( lines.size() == 0 ){
#pragma omp critical
      {
	cerr << "found no OCR_LINE nodes in " << file << endl;
      }
      return;
    }
    list<xmlNode*>::const_iterator lit = lines.begin();
    string txt;
    while ( lit != lines.end() ){
      list<xmlNode*> words = TiCC::FindNodes( *lit, ".//span[@class='ocrx_word']" );
      if ( words.size() == 0 ){
	// no ocrx_words. Lets see...
	words = TiCC::FindNodes( *lit, ".//span[@class='ocr_word']" );
	if ( words.size() == 0 ){
#pragma omp critical
	  {
	    cerr << "found no OCRX_WORD or OCR_WORD nodes in " << file << endl;
	  }
	  return;
	}
      }
      list<xmlNode*>::const_iterator it = words.begin();
      while ( it != words.end() ){
	string w_id = TiCC::getAttribute( *it, "id" );
	string content = extractContent( *it );
	content = TiCC::trim( content );
	if ( !content.empty() ){
	  folia::String *str = new folia::String( out->doc(),
						  "id='" + par->id()
						  + "." + w_id + "'" );
	  par->append( str );
	  str->settext( content, txt.length(), classname );
	  txt += " " + content;
	  folia::Alignment *h = new folia::Alignment( "href='" + file + "'" );
	  str->append( h );
	  folia::AlignReference *a =
	    new folia::AlignReference( "id='" + w_id + "', type='str'" );
	  h->append( a );
	}
	++it;
      }
      ++lit;
    }
    if ( txt.size() > 1 ){
      out->append( par );
      par->settext( txt.substr(1), classname );
    }
    else
      delete par;
    ++pit;
  }
}

string getDocId( const string& title ){
  string result;
  vector<string> vec;
  TiCC::split_at( title, vec, ";" );
  for ( size_t i=0; i < vec.size(); ++i ){
    vector<string> v1;
    size_t num = TiCC::split( vec[i], v1 );
    if ( num == 2 ){
      if ( TiCC::trim( v1[0] ) == "image" ){
	result = v1[1];
	string::size_type pos = result.rfind( "/" );
	if ( pos != string::npos ){
	  result = result.substr( pos+1 );
	}
      }
    }
  }
  result = TiCC::trim( result, " \t\"" );
  return result;
}

void convert_hocr( const string& fileName,
		   const string& outputDir,
		   const zipType outputType ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "start handling " << fileName << endl;
    }
  }
  zipType inputType;
  xmlDoc *xdoc = getXml( fileName, inputType );
  if ( !xdoc ){
#pragma omp critical
    {
      cerr << "problem detecting type of file: " << fileName << endl;
      cerr << "it MUST have extension .hocr, .html or .xhtml (or .bz2 or .gz variants)" << endl;
    }
    return;
  }
  xmlNode *root = xmlDocGetRootElement( xdoc );
  list<xmlNode*> divs = TiCC::FindNodes( root, "//div[@class='ocr_page']" );
  if ( divs.size() == 0 ) {
#pragma omp critical
    {
      cerr << "no OCR_PAGE node found in " << fileName << endl;
    }
    exit(EXIT_FAILURE);
  }
  if ( divs.size() > 1 ) {
#pragma omp critical
    {
      cerr << "multiple OCR_PAGE nodes found. Not supported. in " << fileName << endl;
    }
    exit(EXIT_FAILURE);
  }
  string title = TiCC::getAttribute( *divs.begin(), "title" );
  if ( title.empty() ){
#pragma omp critical
    {
      cerr << "No 'title' attribute found in ocr_page: " << fileName << endl;
    }
    exit(EXIT_FAILURE);
  }
  string docid = getDocId( title );
  folia::Document doc( "id='" + docid + "'" );
  doc.declare( folia::AnnotationType::STRING, setname,
	       "annotator='folia-hocr', datetime='now()'" );
  folia::Text *text = new folia::Text( "id='" + docid + ".text'" );
  doc.append( text );
  processParagraphs( root, text, docid );
  xmlFreeDoc( xdoc );

  string outName = outputDir + "/" + docid + ".folia.xml";
  zipType type = inputType;
  if ( outputType != NORMAL )
    type = outputType;
  if ( type == BZ2 )
    outName += ".bz2";
  else if ( type == GZ )
    outName += ".gz";
  vector<folia::Paragraph*> pv = doc.paragraphs();
  if ( pv.size() == 0 ||
       ( pv.size() == 1 && pv[0]->size() == 0 ) ){
    // no paragraphs, or just 1 without data
#pragma omp critical
    {
      cerr << "skipped empty result : " << outName << endl;
    }
  }
  else {
    doc.save( outName );
    if ( verbose ){
#pragma omp critical
      {
	cout << "created " << outName << endl;
      }
    }
  }
}

void usage(){
  cerr << "Usage: FoLiA-hocr [options] file/dir" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h\t this message " << endl;
  cerr << "\t-O\t output directory " << endl;
  cerr << "\t--compress='c'\t with 'c'=b create bzip2 files (.bz2) " << endl;
  cerr << "\t\t\t with 'c'=g create gzip files (.gz)" << endl;
  cerr << "\t--setname='set'\t the FoLiA set name for <t> nodes. "
    "(default '" << setname << "')" << endl;
  cerr << "\t--class='class'\t the FoLiA class name for <t> nodes. "
    "(default '" << classname << "')" << endl;
  cerr << "\t-v\t verbose output " << endl;
  cerr << "\t-V\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVt:O:h", "compress:,class:,setname:" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
#ifdef HAVE_OPENMP
  int numThreads=1;
#endif

  string outputDir;
  zipType outputType = NORMAL;
  string value;
  if ( opts.extract( 'h' ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  verbose = opts.extract( 'v', value );
  if ( opts.extract( "compress", value ) ){
    if ( value == "b" )
      outputType = BZ2;
    else if ( value == "g" )
      outputType = GZ;
    else {
      cerr << "unknown compression: use 'b' or 'g'" << endl;
      exit( EXIT_FAILURE );
    }
  }
  if ( opts.extract( 't', value ) ){
#ifdef HAVE_OPENMP
    numThreads = TiCC::stringTo<int>( value );
#else
    cerr << "You don't have OpenMP support, so '-t' option is impossible" << endl;
    exit(EXIT_FAILURE);
#endif
  }
  opts.extract( 'O', outputDir );
  opts.extract( "setname", setname );
  opts.extract( "class", classname );
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    cerr << "missing input file(s)" << endl;
    exit(EXIT_FAILURE);
  }
  else if ( fileNames.size() > 1 ){
    cerr << "currently only 1 file or directory is supported" << endl;
    exit( EXIT_FAILURE );
  }

  if ( !outputDir.empty() ){
    string name = outputDir;
    if ( !TiCC::isDir(name) ){
      if ( !TiCC::createPath( name ) ){
	cerr << "outputdir '" << name
	     << "' doesn't exist and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  string name = fileNames[0];
  if ( !( TiCC::isFile(name) || TiCC::isDir(name) ) ){
    cerr << "parameter '" << name << "' doesn't seem to be a file or directory"
	 << endl;
    exit(EXIT_FAILURE);
  }
  string dirName;
  if ( TiCC::isFile(name) ){
    if ( TiCC::match_back( name, ".tar" ) ){
      cerr << "TAR files are not supported yet." << endl;
      exit(EXIT_FAILURE);
    }
    else {
      string::size_type pos = name.rfind( "/" );
      if ( pos != string::npos )
	dirName = name.substr(0,pos);
    }
  }
  else {
    fileNames = TiCC::searchFilesMatch( name, "*hocr" );
    if ( fileNames.empty() )
      fileNames = TiCC::searchFilesMatch( name, "*html" );
  }
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found." << endl;
    exit(EXIT_FAILURE);
  }
  if ( toDo > 1 ){
#ifdef HAVE_OPENMP
    folia::initMT();
#endif
    cout << "start processing of " << toDo << " files " << endl;
  }

#ifdef HAVE_OPENMP
  if ( numThreads >= 1 ){
    omp_set_num_threads( numThreads );
  }
#endif

#pragma omp parallel for shared(fileNames)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    convert_hocr( fileNames[fn], outputDir, outputType );
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
