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

enum zipType { NORMAL, GZ, BZ2, UNKNOWN };

xmlDoc *getXml( const string& file, zipType& type ){
  type = UNKNOWN;
  if ( TiCC::match_back( file, ".xml" ) ){
    type = NORMAL;
  }
  else if ( TiCC::match_back( file, ".xml.gz" ) ){
    type = GZ;
  }
  else if ( TiCC::match_back( file, ".xml.bz2" ) ){
    type = BZ2;
  }
  else {
    return 0;
  }
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

string setname = "FoLiA-page-set";
string classname = "OCR";

void appendStr( folia::FoliaElement *par, int& pos,
		const string& val, const string& id,
		const string& file ){
  if ( !val.empty() ){
    folia::String *str = new folia::String( par->doc(),
					    "id='" + par->id()
					    + "." + id + "'" );
    par->append( str );
    str->settext( val, pos, classname );
    pos += val.length();
    folia::Alignment *h = new folia::Alignment( "href='" + file + "'" );
    str->append( h );
    folia::AlignReference *a =
      new folia::AlignReference( "id='" + id + "', type='str'" );
    h->append( a );
  }
}

void process( folia::FoliaElement *out,
	      const vector<string>& vec,
	      const vector<string>& refs,
	      const string& file ){
  for ( size_t i=0; i < vec.size(); ++i ){
    vector<string> parts;
    TiCC::split( vec[i], parts );
    string parTxt;
    for ( size_t j=0; j< parts.size(); ++j ){
      parTxt += parts[j];
      if ( j != parts.size()-1 )
	parTxt += " ";
    }
    folia::Paragraph *par
      = new folia::Paragraph( out->doc(),
			      "id='" + out->id() + "." + refs[i] + "'");
    par->settext( parTxt, classname );
    out->append( par );
    int pos = 0;
    for ( size_t j=0; j< parts.size(); ++j ){
      string id = "word_" + TiCC::toString(j);
      appendStr( par, pos, parts[j], id, file );
    }
  }
}

void process( folia::FoliaElement *out,
	      const map<string,string>& values,
	      const map<string,string>& labels,
	      const string& file ){
  map<string,string>::const_iterator it = values.begin();
  while ( it != values.end() ){
    string line = it->second;
    vector<string> parts;
    TiCC::split( line, parts );
    string parTxt;
    for ( size_t j=0; j< parts.size(); ++j ){
      parTxt += parts[j];
      if ( j != parts.size()-1 )
	parTxt += " ";
    }
    folia::Paragraph *par
      = new folia::Paragraph( out->doc(),
			      "id='" + out->id() + "." + labels.at(it->first) + "'");
    par->settext( parTxt, classname );
    out->append( par );
    int pos = 0;
    for ( size_t j=0; j< parts.size(); ++j ){
      string id = "word_" + TiCC::toString(j);
      appendStr( par, pos, parts[j], id, file );
    }
    ++it;
  }
}

string getOrg( xmlNode *node ){
  string result;
  if ( node->type == XML_CDATA_SECTION_NODE ){
    string cdata = (char*)node->content;
    string::size_type pos = cdata.find("Original.Path");
    if ( pos != string::npos ){
      string::size_type epos = cdata.find( "/meta", pos );
      string longName = cdata.substr( pos+15, epos - pos - 16 );
      pos = longName.rfind( "/" );
      result = longName.substr( pos+1 );
    }
  }
  return result;
}

string stripDir( const string& name ){
  string::size_type pos = name.rfind( "/" );
  if ( pos == string::npos ){
    return name;
  }
  else {
    return name.substr( pos+1 );
  }
}

bool convert_pagexml( const string& fileName,
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
      cerr << "it MUST have extension .xml, .xml.bz2 or .xml.gz" << endl;
    }
    return false;
  }
  xmlNode *root = xmlDocGetRootElement( xdoc );
  xmlNode* comment = TiCC::xPath( root, "*:Metadata/*:Comment" );
  string orgFile;
  if ( comment ){
    orgFile = getOrg( comment->children );
  }
  if ( orgFile.empty() ) {
#pragma omp critical
    {
      cerr << "unable to retrieve an original filename from " << fileName << endl;
    }
    return false;
  }
  if ( verbose ){
#pragma omp critical
    {
      cout << "original file: " << orgFile << endl;
    }
  }
  list<xmlNode*> order = TiCC::FindNodes( root, ".//*:ReadingOrder" );
  if ( order.size() ==  0 ){
#pragma omp critical
    {
      cerr << "Problem finding ReadingOrder node in " << fileName << endl;
    }
    return false;
  }
  if ( order.size() > 1 ){
#pragma omp critical
    {
      cerr << "Found more then 1 ReadingOrder node in " << fileName << endl;
      cerr << "This is not supported." << endl;
    }
    return false;
  }
  list<xmlNode*>::const_iterator it = order.begin();
  order = TiCC::FindNodes( order.front(), ".//*:RegionRefIndexed" );
  if ( order.size() == 0 ){
#pragma omp critical
    {
      cerr << "missing RegionRefIndexed nodes in " << fileName << endl;
    }
    return false;
  }
  string title;
  map<string,int> refs;
  vector<string> backrefs( order.size() );
  it = order.begin();
  while ( it != order.end() ){
    string ref = TiCC::getAttribute( *it, "regionRef" );
    string index = TiCC::getAttribute( *it, "index" );
    int id = TiCC::stringTo<int>( index );
    refs[ref] = id;
    backrefs[id] = ref;
    ++it;
  }

  vector<string> regionStrings( refs.size() );
  map<string,string> specials;
  map<string,string> specialRefs;
  list<xmlNode*> regions = TiCC::FindNodes( root, "//*:TextRegion" );
  if ( regions.size() == 0 ){
#pragma omp critical
    {
      cerr << "missing TextRegion nodes in " << fileName << endl;
    }
    return false;
  }
  it = regions.begin();
  while ( it != regions.end() ){
    string index = TiCC::getAttribute( *it, "id" );
    string type = TiCC::getAttribute( *it, "type" );
    int key = -1;
    if ( type == "paragraph" || type == "heading" || type == "TOC-entry"
	 || type == "catch-word" || type == "drop-capital" ){
      map<string,int>::const_iterator mit = refs.find(index);
      if ( mit == refs.end() ){
#pragma omp critical
	{
	  cerr << "ignoring paragraph index=" << index
	       << ", not found in ReadingOrder of " << fileName << endl;
	}
      }
      else {
	key = mit->second;
      }
    }
    else if ( type == "page-number" || type == "header" ){
      //
    }
    else  if ( type == "signature-mark" ){
      if ( verbose ) {
#pragma omp critical
	{
	  cerr << "ignoring " << type << " in " << fileName << endl;
	}
      }
      type.clear();
    }
    else {
#pragma omp critical
      {
	  cerr << "ignoring unsupported type=" << type << " in " << fileName << endl;
      }
      type.clear();
    }
    if ( !type.empty() ){
      xmlNode *unicode = TiCC::xPath( *it, ".//*:Unicode" );
      if ( !unicode ){
#pragma omp critical
	{
	  cerr << "missing Unicode node in " << TiCC::Name(*it) << " of " << fileName << endl;
	}
      }
      else {
	string value = TiCC::XmlContent( unicode );
	if ( key >= 0 ){
	  regionStrings[key] = value;
	}
	else if ( type == "page-number" || type == "header"){
	  specials[type] = value;
	  specialRefs[type] = index;
	}
      }
    }
    ++it;
  }
  xmlFreeDoc( xdoc );
  // for ( size_t i=0; i < regionStrings.size(); ++i ){
  //    cerr << "[" << i << "]-" << regionStrings[i] << endl;
  // }

  string docid = orgFile;
  folia::Document doc( "id='" + docid + "'" );
  doc.declare( folia::AnnotationType::STRING, setname,
	       "annotator='folia-page', datetime='now()'" );
  doc.set_metadata( "page_file", stripDir( fileName ) );
  folia::Text *text = new folia::Text( "id='" + docid + ".text'" );
  doc.append( text );
  process( text, specials, specialRefs, docid );
  process( text, regionStrings, backrefs, docid );

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
  return true;
}

void usage(){
  cerr << "Usage: FoLiA-page [options] file/dir" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h\t this messages " << endl;
  cerr << "\t-O\t output directory " << endl;
  cerr << "\t--setname='set'\t the FoLiA set name for <t> nodes. "
    "(default '" << setname << "')" << endl;
  cerr << "\t--class='class'\t the FoLiA class name for <t> nodes. "
    "(default '" << classname << "')" << endl;
  cerr << "\t--compress='c'\t with 'c'=b create bzip2 files (.bz2) " << endl;
  cerr << "\t\t\t with 'c'=g create gzip files (.gz)" << endl;
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
  int numThreads=1;
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
    numThreads = TiCC::stringTo<int>( value );
  }
  verbose = opts.extract( 'v' );
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

  string dirName;
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
    fileNames = TiCC::searchFilesMatch( name, ".xml", false );
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

  if ( numThreads >= 1 ){
    omp_set_num_threads( numThreads );
  }

#pragma omp parallel for shared(fileNames)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    if ( !convert_pagexml( fileNames[fn], outputDir, outputType ) )
#pragma omp critical
      {
	cerr << "failure on " << fileNames[fn] << endl;
      }
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
