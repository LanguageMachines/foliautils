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
#include "ticcutils/zipper.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/Unicode.h"
#include "ticcutils/CommandLine.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;

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
		const UnicodeString& val, const string& id,
		const string& file ){
  if ( !val.isEmpty() ){
    folia::KWargs args;
    args["id"] = par->id() + "." + id;
    folia::String *str = new folia::String( args, par->doc() );
    par->append( str );
    str->setutext( val, pos, classname );
    pos += val.length() +1;
    args.clear();
    args["href"] = file;
    folia::Alignment *h = new folia::Alignment( args );
    str->append( h );
    args.clear();
    args["id"] = id;
    args["type"] = "str";
    folia::AlignReference *a = new folia::AlignReference( args );
    h->append( a );
  }
}

void process( folia::FoliaElement *out,
	      const vector<string>& vec,
	      const vector<string>& refs,
	      const string& file ){
  for ( size_t i=0; i < vec.size(); ++i ){
    vector<string> parts = TiCC::split( vec[i] );
    string parTxt;
    for ( auto const& p : parts ){
      parTxt += p;
      if ( &p != &parts.back() ){
	parTxt += " ";
      }
    }
    folia::KWargs args;
    args["id"] = out->id() + "." + refs[i];
    folia::Paragraph *par = new folia::Paragraph( args, out->doc() );
    par->settext( parTxt, classname );
    out->append( par );
    int pos = 0;
    for ( size_t j=0; j< parts.size(); ++j ){
      string id = "word_" + TiCC::toString(j);
      appendStr( par, pos, TiCC::UnicodeFromUTF8(parts[j]), id, file );
    }
  }
}

void process( folia::FoliaElement *out,
	      const map<string,string>& values,
	      const map<string,string>& labels,
	      const string& file ){
  for ( const auto& value : values ){
    string line = value.second;
    vector<string> parts = TiCC::split( line );
    string parTxt;
    for ( const auto& p : parts ){
      parTxt += p;
      if ( &p != &parts.back() ){
	parTxt += " ";
      }
    }
    folia::KWargs args;
    args["id"] = out->id() + "." + labels.at(value.first);
    folia::Paragraph *par = new folia::Paragraph( args, out->doc() );
    par->settext( parTxt, classname );
    out->append( par );
    int pos = 0;
    for ( size_t j=0; j< parts.size(); ++j ){
      string id = "word_" + TiCC::toString(j);
      appendStr( par, pos, TiCC::UnicodeFromUTF8(parts[j]), id, file );
    }
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
		      const zipType outputType,
		      const string& prefix ){
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
  if ( order.empty() ){
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
  if ( order.empty() ){
#pragma omp critical
    {
      cerr << "missing RegionRefIndexed nodes in " << fileName << endl;
    }
    return false;
  }
  map<string,int> refs;
  vector<string> backrefs( order.size() );
  for ( const auto& ord : order ){
    string ref = TiCC::getAttribute( ord, "regionRef" );
    string index = TiCC::getAttribute( ord, "index" );
    int id = TiCC::stringTo<int>( index );
    refs[ref] = id;
    backrefs[id] = ref;
  }

  vector<string> regionStrings( refs.size() );
  map<string,string> specials;
  map<string,string> specialRefs;
  list<xmlNode*> regions = TiCC::FindNodes( root, "//*:TextRegion" );
  if ( regions.empty() ){
#pragma omp critical
    {
      cerr << "missing TextRegion nodes in " << fileName << endl;
    }
    return false;
  }
  for ( const auto& region : regions ){
    string index = TiCC::getAttribute( region, "id" );
    string type = TiCC::getAttribute( region, "type" );
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
      xmlNode *unicode = TiCC::xPath( region, ".//*:Unicode" );
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
  }
  xmlFreeDoc( xdoc );

  string docid = prefix + orgFile;
  folia::Document doc( "id='" + docid + "'" );
  doc.declare( folia::AnnotationType::STRING, setname,
	       "annotator='folia-page', datetime='now()'" );
  doc.set_metadata( "page_file", stripDir( fileName ) );
  folia::KWargs args;
  args["id"] =  docid + ".text";
  folia::Text *text = new folia::Text( args );
  doc.append( text );
  process( text, specials, specialRefs, docid );
  process( text, regionStrings, backrefs, docid );

  string outName;
  if ( !outputDir.empty() ){
    outName = outputDir;
  }
  outName += orgFile + ".folia.xml";
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
  cerr << "\t-t\t\t number_of_threads" << endl;
  cerr << "\t-h or --help\t this messages " << endl;
  cerr << "\t-O\t\t output directory " << endl;
  cerr << "\t--setname='set'\t the FoLiA set name for <t> nodes. "
    "(default '" << setname << "')" << endl;
  cerr << "\t--class='class'\t the FoLiA class name for <t> nodes. "
    "(default '" << classname << "')" << endl;
  cerr << "\t--prefix='pre'\t add this prefix to ALL created files. (default 'FP-') " << endl;
  cerr << "\t--compress='c'\t with 'c'=b create bzip2 files (.bz2) " << endl;
  cerr << "\t\t\t\t with 'c'=g create gzip files (.gz)" << endl;
  cerr << "\t-v\t\t verbose output " << endl;
  cerr << "\t-V or --version\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVt:O:h",
			 "compress:,class:,setname:,help,version,prefix:" );
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
  if ( opts.extract( 'h' ) || opts.extract( "help" ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
    cerr << opts.prog_name() << " [" << PACKAGE_STRING << "]"<< endl;
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
#ifdef HAVE_OPENMP
    numThreads = TiCC::stringTo<int>( value );
#else
    cerr << "OpenMP support is missing. -t options not supported!" << endl;
    exit( EXIT_FAILURE );
#endif
  }
  verbose = opts.extract( 'v' );
  opts.extract( 'O', outputDir );
  opts.extract( "setname", setname );
  opts.extract( "class", classname );
  string prefix = "FP-";
  opts.extract( "prefix", prefix );
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
    outputDir += "/";
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
    cout << "start processing of " << toDo << " files " << endl;
  }

#ifdef HAVE_OPENMP
  if ( numThreads >= 1 ){
    omp_set_num_threads( numThreads );
  }
#endif

#pragma omp parallel for shared(fileNames)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    if ( !convert_pagexml( fileNames[fn], outputDir, outputType, prefix ) )
#pragma omp critical
      {
	cerr << "failure on " << fileNames[fn] << endl;
      }
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
