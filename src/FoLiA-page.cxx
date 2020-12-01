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
bool do_refs = true;
bool trust_tokenization = false;

string setname = "FoLiA-page-set";
string classname = "OCR";

folia::processor *page_processor = 0;

void appendStr( folia::FoliaElement *par,
		int& pos,
		const UnicodeString& val,
		const string& id,
		const string& file ){
  if ( !val.isEmpty() ){
    folia::KWargs args;
    args["xml:id"] = par->id() + "." + id;
    folia::String *str = new folia::String( args, par->doc() );
    par->append( str );
    str->setutext( val, pos, classname );
    pos += val.length();
    args.clear();
    if ( do_refs ){
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

string handle_one_word( folia::FoliaElement *sent,
			xmlNode *word,
			const string& fileName ){
  string result;
  string wid = TiCC::getAttribute( word, "id" );
  //  cerr << "handle word " << wid << endl;
  list<xmlNode*> unicodes = TiCC::FindNodes( word, "./*:TextEquiv/*:Unicode" );
  if ( unicodes.size() != 1 ){
    throw runtime_error( "expected only 1 unicode entry in Word: " + wid );
  }
  result = TiCC::XmlContent( unicodes.front() );
  folia::KWargs args;
  args["processor"] = page_processor->id();
  sent->doc()->declare( folia::AnnotationType::TOKEN, setname, args );
  args.clear();
  args["xml:id"] = sent->id() + "." + wid;
  args["text"] = result;
  args["textclass"] = classname;
  folia::Word *w = new folia::Word( args, sent->doc() );
  sent->append( w );
  if ( do_refs ){
    args.clear();
    args["xlink:href"] = fileName;
    args["format"] = "text/page+xml";
    folia::Relation *h = new folia::Relation( args );
    w->append( h );
    args.clear();
    args["id"] = wid;
    args["type"] = "w";
    folia::LinkReference *a = new folia::LinkReference( args );
    h->append( a );
  }
  return result;
}

void handle_uni_lines( folia::FoliaElement *root,
		       xmlNode *parent,
		       const string& fileName ){
  list<xmlNode*> unicodes = TiCC::FindNodes( parent, "./*:TextEquiv/*:Unicode" );
  if ( unicodes.empty() ){
#pragma omp critical
    {
      cerr << "missing Unicode node in " << TiCC::Name(parent) << " of " << fileName << endl;
    }
    return;
  }
  UnicodeString full_line;
  int pos = 0;
  int j = 0;
  for ( const auto& unicode : unicodes ){
    string value = TiCC::XmlContent( unicode );
    if ( !value.empty() ){
      //      cerr << "string: '" << value << endl;
      UnicodeString uval = TiCC::UnicodeFromUTF8(value);
      string id = "str_" + TiCC::toString(j++);
      appendStr( root, pos, uval, id, fileName );
      full_line += uval;
      if ( &unicode != &unicodes.back() ){
	full_line += " ";
	++pos;
      }
    }
  }
  root->setutext( full_line, classname );
}

string handle_one_line( folia::FoliaElement *par,
			int& pos,
			xmlNode *line,
			const string& fileName ){
  static TiCC::UnicodeNormalizer UN;
  string result;
  string lid = TiCC::getAttribute( line, "id" );
  //  cerr << "handle line " << lid << endl;
  list<xmlNode*> words = TiCC::FindNodes( line, "./*:Word" );
  if ( !words.empty() ){
    // We have Words!.
    if ( trust_tokenization ){
      // trust the tokenization and create Sentences too.
      folia::KWargs args;
      args["processor"] = page_processor->id();
      par->doc()->declare( folia::AnnotationType::SENTENCE, setname, args );
      args.clear();
      args["xml:id"] = par->id() + "." + lid;
      folia::Sentence *sent = new folia::Sentence( args, par->doc() );
      par->append( sent );
      for ( const auto& w :words ){
	handle_one_word( sent, w, fileName );
      }
      result = sent->str();
      sent->settext( result, classname );
    }
    else {
      // we add the text as strings, enabling external tokenizations
      map<xmlNode*,string> word_ids;
      list<xmlNode*> unicodes;
      for ( const auto& w : words ){
	list<xmlNode*> tmp = TiCC::FindNodes( w, "./*:TextEquiv/*:Unicode" );
	string wid = TiCC::getAttribute( w, "id" );
	for ( const auto& it : tmp ){
	  string value = TiCC::XmlContent( it );
	  if ( !value.empty() ){
	    unicodes.push_back( it );
	    word_ids[it] = wid;
	    break;  // We assume only 1 non-empty Unicode string
	  }
	}
      }
      if ( unicodes.empty() ){
#pragma omp critical
	{
	  cerr << "missing Unicode node in " << TiCC::Name(line) << " of " << fileName << endl;
	}
	return "";
      }
      for ( const auto& unicode : unicodes ){
	string value = TiCC::XmlContent( unicode );
	UnicodeString uval = TiCC::UnicodeFromUTF8(value);
	uval = UN.normalize(uval);
	appendStr( par, pos, uval, word_ids[unicode], fileName );
	result = value;
	break; // We assume only 1 non-empty Unicode string
      }
    }
  }
  else {
    // lines without words.
    list<xmlNode*> unicodes = TiCC::FindNodes( line, "./*:TextEquiv/*:Unicode" );
    if ( unicodes.empty() ){
#pragma omp critical
      {
	cerr << "missing Unicode node in " << TiCC::Name(line) << " of " << fileName << endl;
      }
      return "";
    }
    for ( const auto& unicode : unicodes ){
      string value = TiCC::XmlContent( unicode );
      if ( !value.empty() ){
	UnicodeString uval = TiCC::UnicodeFromUTF8(value);
	uval = UN.normalize(uval);
	appendStr( par, pos, uval, lid, fileName );
	result = value;
	break; // We assume only 1 non-empty Unicode string
      }
    }
  }
  return result;
}

void handle_one_region( folia::FoliaElement *root,
			xmlNode *region,
			const string& fileName ){
  string ind = TiCC::getAttribute( region, "id" );
  //  cerr << "handle region " << ind << endl;
  folia::KWargs args;
  args["xml:id"] = root->id() + "." + ind;
  folia::Paragraph *par = new folia::Paragraph( args, root->doc() );
  root->append( par );
  list<xmlNode*> lines = TiCC::FindNodes( region, "./*:TextLine" );
  if ( !lines.empty() ){
    string par_txt;
    int pos = 0;
    for ( const auto& line : lines ){
      string value  = handle_one_line( par, pos,
				       line,
				       fileName );
      par_txt += value;
      if ( &line != &lines.back() ){
	++pos;
	par_txt += " ";
      }
    }
    par->settext( par_txt, classname );
  }
  else {
    // No TextLine's use unicode nodes directly
    //    cerr << "only unicode" << endl;
    handle_uni_lines( par, region, fileName );
  }
}

vector<xmlNode*> extract_regions( xmlNode *root ){
  vector<xmlNode*> result;
  list<xmlNode*> regions = TiCC::FindNodes( root, ".//*:TextRegion" );
  if ( regions.empty() ){
    cerr << "NO textRegion nodes found in flat document" << endl;
    return result;
  }
  for ( const auto& r : regions ){
    string ref = TiCC::getAttribute( r, "id" );
    result.push_back( r );
  }
  return result;
}

vector<xmlNode *> extract_regions( xmlNode *root,
				   vector<xmlNode*>& specials ){
  vector<xmlNode*> result;
  specials.clear();
  list<xmlNode*> order = TiCC::FindNodes( root, ".//*:RegionRefIndexed" );
  if ( order.empty() ){
#pragma omp critical
    {
      cerr << "missing RegionRefIndexed nodes." << endl;
    }
    return result;
  }
  list<xmlNode*> regions = TiCC::FindNodes( root->parent, ".//*:TextRegion" );
  map<string,int> region_refs;
  for ( const auto& ord : order ){
    string ref = TiCC::getAttribute( ord, "regionRef" );
    string index = TiCC::getAttribute( ord, "index" );
    int id = TiCC::stringTo<int>( index );
    region_refs[ref] = id;
  }
  result.resize( region_refs.size() );
  for ( const auto& region : regions ){
    string ref = TiCC::getAttribute( region, "id" );
    if ( region_refs.find(ref) != region_refs.end() ){
      result[region_refs[ref]] = region;
    }
    else {
      specials.push_back( region );
    }
  }
  return result;
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
  vector<xmlNode*> new_order;
  vector<xmlNode*> specials;
  if ( order.empty() ){
    // No reading order. So a 'flat' document
    new_order = extract_regions( root );
  }
  else {
    // A reading order
    new_order = extract_regions( order.front(), specials );
  }
  if ( new_order.empty() && specials.empty() ){
    cerr << "no usable data in file:" << fileName << endl;
    xmlFreeDoc( xdoc );
    return false;
  }
  string docid = prefix + orgFile;
  folia::Document doc( "xml:id='" + docid + "'" );
  doc.set_metadata( "page_file", stripDir( fileName ) );
  page_processor = add_provenance( doc, "FoLiA-page", command );
  folia::KWargs args;
  args["processor"] = page_processor->id();
  doc.declare( folia::AnnotationType::STRING, setname, args );
  args.clear();
  args["xml:id"] =  docid + ".text";
  folia::Text *text = new folia::Text( args );
  doc.append( text );
  for ( const auto& no : new_order ){
    handle_one_region( text, no, fileName );
  }
  for ( const auto& no : specials ){
    handle_one_region( text, no, fileName );
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
#pragma omp critical
    {
      cout << "converted: " << fileName << " into: "  << outName << endl;
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
  cerr << "\t--prefix='pre'\t add this prefix to ALL created files. (default: 'FP-') " << endl;
  cerr << "\t--norefs\t do not add references nodes to the original document. (default: Add References)" << endl;
  cerr << "\t--trusttokens\t when the Page-file contains Word items, translate them to FoLiA Word and Sentence elements" << endl;
  cerr << "\t\t\t use 'none' for an empty prefix. (can be dangerous)" << endl;
  cerr << "\t--compress='c'\t with 'c'=b create bzip2 files (.bz2) " << endl;
  cerr << "\t\t\t\t with 'c'=g create gzip files (.gz)" << endl;
  cerr << "\t-v\t\t verbose output " << endl;
  cerr << "\t-V or --version\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVt:O:h",
			 "compress:,class:,setname:,help,version,prefix:,"
			 "norefs,threads:,trusttokens" );
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
  do_refs = !opts.extract( "norefs" );
  trust_tokenization = opts.extract( "trusttokens" );
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
