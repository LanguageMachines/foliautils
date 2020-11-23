/*
  Copyright (c) 2014 - 2020
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
#include "ticcutils/PrettyPrint.h"
#include "libfolia/folia.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/zipper.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/Unicode.h"
#include "ticcutils/CommandLine.h"
#include "foliautils/common_code.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;
using TiCC::operator<<;

bool verbose = false;

string setname = "FoLiA-page-set";
string classname = "OCR";

void appendStr( folia::FoliaElement *par, int& pos,
		const UnicodeString& val, const string& id,
		const string& file ){
  if ( !val.isEmpty() ){
    folia::KWargs args;
    args["xml:id"] = par->id() + "." + id;
    folia::String *str = new folia::String( args, par->doc() );
    par->append( str );
    str->setutext( val, pos, classname );
    pos += val.length() +1;
    args.clear();
    args["xlink:href"] = file;
    args["format"] = "text/page+xml";
    folia::Relation *h = new folia::Relation( args );
    str->append( h );
    args.clear();
    args["id"] = id;
    args["type"] = "str";
    folia::LinkReference *a = new folia::LinkReference( args );
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
    args["xml:id"] = out->id() + "." + refs[i];
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

void process_lines( folia::FoliaElement *out,
		    const vector<string>& vec,
		    const vector<string>& refs,
		    const string& file ){
  folia::KWargs args;
  //  args["xml:id"] = out->id() + "." + refs[i];
  folia::Paragraph *par = new folia::Paragraph( args, out->doc() );
  out->append( par );
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
    args["xml:id"] = out->id() + "." + refs[i];
    folia::Sentence *sent = new folia::Sentence( args, out->doc() );
    sent->settext( parTxt, classname );
    par->append( sent );
    // int pos = 0;
    // for ( size_t j=0; j< parts.size(); ++j ){
    //   string id = "word_" + TiCC::toString(j);
    //   appendStr( sent, pos, TiCC::UnicodeFromUTF8(parts[j]), id, file );
    // }
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
    args["xml:id"] = out->id() + "." + labels.at(value.first);
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

string getOrg( xmlNode *root ){
  string result;
  xmlNode* page = TiCC::xPath( root, "*:Page" );
  if ( page ){
    string ref = TiCC::getAttribute( page, "imageFilename" );
    if ( !ref.empty() ) {
      result = ref;
      return result;
    }
  }
  xmlNode* comment = TiCC::xPath( root, "*:Metadata/*:Comment" );
  if ( comment ){
    xmlNode *node = comment->children;
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

bool handle_flat_document( folia::FoliaElement *text,
			   xmlNode* document_root,
			   const string& fileName ){
  cerr << "flat document: " << fileName << endl;
  vector<string> blocks;
  vector<string> refs;
  map<string,string> specials;
  map<string,string> specialRefs;
  list<xmlNode*> regions = TiCC::FindNodes( document_root, "//*:TextRegion" );
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
    if ( type == "paragraph" || type == "heading" || type == "TOC-entry"
	 || type == "catch-word" || type == "drop-capital" ){
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
      list<xmlNode*> lines = TiCC::FindNodes( region, ".//*:TextLine" );
      if ( lines.empty() ){
	cout << "NO textlines" << endl;
	return false;
      }
      for ( const auto& line : lines ){
	list<xmlNode*> unicodes = TiCC::FindNodes( line, "./*:TextEquiv/*:Unicode" );
	string index = TiCC::getAttribute( line, "id" );
	if ( unicodes.empty() ){
#pragma omp critical
	  {
	    cerr << "missing Unicode node in " << TiCC::Name(line) << " of " << fileName << endl;
	  }
	}
	else {
	  string full_line;
	  for ( const auto& unicode : unicodes ){
	    string value = TiCC::XmlContent( unicode );
	    //	  cerr << "string: '" << value << endl;
	    full_line += value + " ";
	  }
	  blocks.push_back(full_line);
	  refs.push_back( index );
	}
      }
    }
  }
  cerr << "BLOCKS:" << endl << blocks << endl;
  cerr << "REFS:" << endl << refs << endl;
  process_lines( text, blocks, refs, TiCC::basename(fileName) );
  return true;
}

void handle_one_region( xmlNode *region,
			const string& fileName,
			vector<string>& regionStrings,
			map<string,int>& refs,
			map<string,string>& specials,
			map<string,string>& specialRefs ){
  list<xmlNode*> lines = TiCC::FindNodes( region, ".//*:TextLine" );
  if ( !lines.empty() ){
    cout << "found textlines" << endl;
  }
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
    list<xmlNode*> unicodes = TiCC::FindNodes( region, "./*:TextEquiv/*:Unicode" );
    if ( unicodes.empty() ){
#pragma omp critical
      {
	cerr << "missing Unicode node in " << TiCC::Name(region) << " of " << fileName << endl;
      }
    }
    else {
      string full_line;
      for ( const auto& unicode : unicodes ){
	string value = TiCC::XmlContent( unicode );
	//	  cerr << "string: '" << value << endl;
	full_line += value + " ";
      }
      if ( key >= 0 ){
	regionStrings[key] = full_line;
      }
      if ( type == "page-number" || type == "header"){
	specials[type] = full_line;
	specialRefs[type] = index;
      }
    }
  }
}

bool handle_ordered_document( folia::FoliaElement *textroot,
			      xmlNode* document_root,
			      list<xmlNode*>& order,
			      const string& fileName ){
  map<string,int> refs;
  vector<string> backrefs;
  vector<string> regionStrings;
  if ( !order.empty() ){
    order = TiCC::FindNodes( order.front(), ".//*:RegionRefIndexed" );
    if ( order.empty() ){
#pragma omp critical
      {
	cerr << "missing RegionRefIndexed nodes in " << fileName << endl;
      }
      return false;
    }
    backrefs.resize( order.size() );
    regionStrings.resize( order.size() );
    for ( const auto& ord : order ){
      string ref = TiCC::getAttribute( ord, "regionRef" );
      string index = TiCC::getAttribute( ord, "index" );
      int id = TiCC::stringTo<int>( index );
      refs[ref] = id;
      backrefs[id] = ref;
    }
  }
  map<string,string> specials;
  map<string,string> specialRefs;
  list<xmlNode*> regions = TiCC::FindNodes( document_root, "//*:TextRegion" );
  if ( regions.empty() ){
#pragma omp critical
    {
      cerr << "missing TextRegion nodes in " << fileName << endl;
    }
    return false;
  }

  for ( const auto& region : regions ){
    handle_one_region( region, fileName,
		       regionStrings, refs,
		       specials, specialRefs );

  }

  process( textroot, specials, specialRefs, TiCC::basename(fileName) );
  process( textroot, regionStrings, backrefs, TiCC::basename(fileName) );
  return true;
}

bool convert_pagexml( const string& fileName,
		      const string& outputDir,
		      const zipType outputType,
		      const string& prefix,
		      const string& command ){
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
  string orgFile = getOrg( root );
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
  string docid = prefix + orgFile;
  folia::Document doc( "xml:id='" + docid + "'" );
  doc.set_metadata( "page_file", stripDir( fileName ) );
  folia::processor *proc = add_provenance( doc, "FoLiA-page", command );
  folia::KWargs args;
  args["processor"] = proc->id();
  doc.declare( folia::AnnotationType::STRING, setname, args );
  args.clear();
  args["xml:id"] =  docid + ".text";
  folia::Text *text = new folia::Text( args );
  doc.append( text );

  list<xmlNode*> order = TiCC::FindNodes( root, ".//*:ReadingOrder" );
  if ( order.size() > 1 ){
#pragma omp critical
    {
      cerr << "Found more then 1 ReadingOrder node in " << fileName << endl;
      cerr << "This is not supported." << endl;
    }
    xmlFreeDoc( xdoc );
    return false;
  }
  if ( order.empty() ){
    // No reading order. So a 'flat' document
    bool ok = handle_flat_document( text, root, fileName );
    if ( !ok ){
      xmlFreeDoc( xdoc );
      return false;
    }
  }
  else {
    bool ok = handle_ordered_document( text, root, order, fileName );
    if ( !ok ){
      xmlFreeDoc( xdoc );
      return false;
    }
  }
  xmlFreeDoc( xdoc );

  string outName;
  if ( !outputDir.empty() ){
    outName = outputDir;
  }
  outName += orgFile + ".folia.xml";
  zipType type = inputType;
  if ( outputType != NORMAL ){
    type = outputType;
  }
  if ( type == BZ2 ){
    outName += ".bz2";
  }
  else if ( type == GZ ){
    outName += ".gz";
  }
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
  cerr << "\t-t <threads>\n\t--threads <threads> Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-h or --help\t this messages " << endl;
  cerr << "\t-O\t\t output directory " << endl;
  cerr << "\t--setname='set'\t the FoLiA set name for <t> nodes. "
    "(default '" << setname << "')" << endl;
  cerr << "\t--class='class'\t the FoLiA class name for <t> nodes. "
    "(default '" << classname << "')" << endl;
  cerr << "\t--prefix='pre'\t add this prefix to ALL created files. (default 'FP-') " << endl;
  cerr << "\t\t\t use 'none' for an empty prefix. (can be dangerous)" << endl;
  cerr << "\t--compress='c'\t with 'c'=b create bzip2 files (.bz2) " << endl;
  cerr << "\t\t\t\t with 'c'=g create gzip files (.gz)" << endl;
  cerr << "\t-v\t\t verbose output " << endl;
  cerr << "\t-V or --version\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVt:O:h",
			 "compress:,class:,setname:,help,version,prefix:,threads:" );
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
  string command = "FoLiA-page " + opts.toString();
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
  if ( prefix == "none" ){
    prefix.clear();
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
    if ( !convert_pagexml( fileNames[fn],
			   outputDir,
			   outputType,
			   prefix,
			   command ) )
#pragma omp critical
      {
	cerr << "failure on " << fileNames[fn] << endl;
      }
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
