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

#include <unistd.h> // getopt, unlink
#include <sys/stat.h>
#include <sys/types.h>
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
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;

bool verbose = false;
bool predict = false;

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
    cerr << "problem detecting type of file: " << file << endl;
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

const string setname = "OCR-GT";

void appendStr( folia::FoliaElement *par, int& pos,
		const string& val, const string& id,
		const string& file ){
  if ( !val.empty() ){
    folia::String *str = new folia::String( par->doc(),
					    "id='" + par->id()
					    + "." + id + "'" );
    par->append( str );
    str->settext( val, pos, setname );
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
    par->settext( parTxt, setname );
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
    par->settext( parTxt, setname );
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

  string outName = outputDir;
  outName += docid + ".folia.xml";
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

int main( int argc, char *argv[] ){
  if ( argc < 2	){
    cerr << "Usage: [-t number_of_threads] [-o outputdir] dir/filename " << endl;
    exit(EXIT_FAILURE);
  }
  int opt;
  int numThreads=1;
  string outputDir;
  zipType outputType = NORMAL;
  while ((opt = getopt(argc, argv, "bcght:vVo:p")) != -1) {
    switch (opt) {
    case 'b':
      outputType = BZ2;
      break;
    case 'g':
      outputType = GZ;
      break;
    case 't':
      numThreads = atoi(optarg);
      break;
    case 'v':
      verbose = true;
      break;
    case 'p':
      predict = true;
      break;
    case 'V':
      cerr << PACKAGE_STRING << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'h':
      cerr << "Usage: FoLiA-page [options] file/dir" << endl;
      cerr << "\t-t\t number_of_threads" << endl;
      cerr << "\t-h\t this messages " << endl;
      cerr << "\t-o\t output directory " << endl;
      cerr << "\t-b\t create bzip2 files (.bz2)" << endl;
      cerr << "\t-g\t create gzip files (.gz)" << endl;
      cerr << "\t-v\t verbose output " << endl;
      cerr << "\t-V\t show version " << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'o':
      outputDir = string(optarg) + "/";
      break;
    default: /* '?' */
      cerr << "Usage: alto [-t number_of_threads] [-o output_dir] dir/filename " << endl;
      exit(EXIT_FAILURE);
    }
  }
  vector<string> fileNames;
  string dirName;
  if ( !outputDir.empty() ){
    string name = outputDir;
    if ( !TiCC::isDir(name) ){
      int res = mkdir( name.c_str(), S_IRWXU|S_IRWXG );
      if ( res < 0 ){
	cerr << "outputdir '" << name
	     << "' doesn't existing and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  if ( !argv[optind] ){
    exit(EXIT_FAILURE);
  }
  string name = argv[optind];
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
      fileNames.push_back( name );
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
