/*
  Copyright (c) 2014 - 2021
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
bool do_strings = true;
bool do_markup = true;
bool do_sent = false;
bool trust_tokenization = false;
const string processor_label = "FoLiA-page";

string setname = "";
string classname = "OCR";
string processor_id;

void appendStr( folia::FoliaElement *par,
		int& pos,
		const UnicodeString& val,
		const string& id,
		const string& file ){
  if ( !val.isEmpty() ){
    folia::KWargs ref_args;
    if ( do_refs ){
      folia::KWargs ref_proc_args;
      ref_proc_args["processor"] = processor_id;
      par->doc()->declare( folia::AnnotationType::RELATION, setname, ref_proc_args );
      ref_args["xlink:href"] = file;
      ref_args["format"] = "text/page+xml";
    }
    folia::KWargs p_args;
    p_args["processor"] = processor_id;
    if ( do_sent) {
        par->doc()->declare( folia::AnnotationType::SENTENCE, setname, p_args );
        p_args["xml:id"] = par->id() + "." + id;
        folia::Sentence *sent = par->add_child<folia::Sentence>( p_args );
        sent->setutext( val, pos, classname );
        if ( do_refs) {
          folia::Relation *h = sent->add_child<folia::Relation>( ref_args );
          ref_args.clear();
          ref_args["id"] = id;
          ref_args["type"] = "s";
          h->add_child<folia::LinkReference>( ref_args );
        }
    } else if (do_strings) {
        par->doc()->declare( folia::AnnotationType::STRING, setname, p_args );
        p_args["xml:id"] = par->id() + "." + id;
        folia::String *str = par->add_child<folia::String>( p_args );
        str->setutext( val, pos, classname );
        if ( do_refs) {
          folia::Relation *h = str->add_child<folia::Relation>( ref_args );
          ref_args.clear();
          ref_args["id"] = id;
          ref_args["type"] = "str";
          h->add_child<folia::LinkReference>( ref_args );
        }
    }
    pos += val.length();
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
      string cdata = folia::TextValue(node);
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

void handle_one_word( folia::FoliaElement *sent,
		      xmlNode *word,
		      const string& fileName ){
  string wid = TiCC::getAttribute( word, "id" );
  list<xmlNode*> unicodes = TiCC::FindNodes( word, "./*:TextEquiv/*:Unicode" );
  if ( unicodes.size() != 1 ){
    throw runtime_error( "expected only 1 unicode entry in Word: " + wid );
  }
  string value = TiCC::XmlContent( unicodes.front() );
  folia::KWargs p_args;
  p_args["processor"] = processor_id;
  sent->doc()->declare( folia::AnnotationType::TOKEN, setname, p_args );
  p_args["xml:id"] = sent->id() + "." + wid;
  p_args["text"] = value;
  p_args["textclass"] = classname;
  folia::Word *w = sent->add_child<folia::Word>( p_args );
  if ( do_refs ){
    folia::KWargs args;
    args["processor"] = processor_id;
    sent->doc()->declare( folia::AnnotationType::RELATION, setname, args );
    args["xlink:href"] = fileName;
    args["format"] = "text/page+xml";
    folia::Relation *h = w->add_child<folia::Relation>( args );
    args.clear();
    args["id"] = wid;
    args["type"] = "w";
    h->add_child<folia::LinkReference>( args );
  }
}

UnicodeString ltrim( const UnicodeString& in ){
  /// remove leading whitespace (including newlines and tabs)
  int begin = in.length();
  for ( int i = 0; i < in.length(); ++i ) {
    if ( !u_isspace(in[i]) ){
      begin = i;
      break;
    }
  }
  if (begin == 0) {
    return in;
  }
  else if (begin == in.length()) {
    return "";
  }
  else {
    return UnicodeString(in, begin, in.length() - begin);
  }
}

void handle_uni_lines( folia::FoliaElement *root,
		       xmlNode *parent,
		       const string& fileName ){
  static TiCC::UnicodeNormalizer UN;
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
      UnicodeString uval = TiCC::UnicodeFromUTF8(value);
      uval = UN.normalize(uval);
      uval = ltrim( uval );
      string id = "str_" + TiCC::toString(j++);
      appendStr( root, pos, uval, id, fileName );
      full_line += uval;
      if ( &unicode != &unicodes.back() ){
	full_line += " ";
	++pos;
      }
    }
  }
  full_line = ltrim( full_line );
  if ( !full_line.isEmpty() ){
    root->setutext( full_line, classname );
  }
}

UnicodeString handle_one_line( folia::FoliaElement *par,
			       int& pos,
			       xmlNode *line,
			       const string& fileName,
                               string& id //output variable
                             ){
  static TiCC::UnicodeNormalizer UN;
  UnicodeString result;
  string lid = TiCC::getAttribute( line, "id" );
  list<xmlNode*> words = TiCC::FindNodes( line, "./*:Word" );
  if ( !words.empty() ){
    // We have Words!.
    if ( trust_tokenization ){
      // trust the tokenization and create Sentences too.
      folia::KWargs args;
      args["processor"] = processor_id;
      par->doc()->declare( folia::AnnotationType::SENTENCE, setname, args );
      args.clear();
      args["xml:id"] = par->id() + "." + lid;
      id = par->id() + "."  + lid;
      folia::Sentence *sent = par->add_child<folia::Sentence>( args );
      for ( const auto& w :words ){
	handle_one_word( sent, w, fileName );
      }
      result = sent->text();
      if ( !result.isEmpty() ){
	sent->setutext( result, classname );
      }
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
	uval = ltrim( uval );
	appendStr( par, pos, uval, word_ids[unicode], fileName );
        id = par->id() + "."  + word_ids[unicode];
	result = uval;
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
	uval = ltrim( uval );
	appendStr( par, pos, uval, lid, fileName );
        id = par->id() + "."  + lid;
	result = uval;
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
  string type = TiCC::getAttribute( region, "type" );
  folia::KWargs p_args;
  p_args["processor"] = processor_id;
  root->doc()->declare( folia::AnnotationType::PARAGRAPH, setname, p_args );
  root->doc()->declare( folia::AnnotationType::LINEBREAK, setname, p_args );
  p_args["xml:id"] = root->id() + "." + ind;
  folia::FoliaElement *par = root->add_child<folia::Paragraph>( p_args );
  if ( type.empty() || type == "paragraph" ){
  }
  else if ( type == "page-number" ){
    xmlNode* unicode = TiCC::xPath( region, "*:TextEquiv/*:Unicode" );
    if ( unicode ){
      string value = TiCC::XmlContent( unicode );
      folia::KWargs args;
      args["pagenr"] = value;
      par->add_child<folia::Linebreak>( args );
      root->doc()->set_metadata( "page-number", value );
      return;
    }
    else {
      cerr << "NOT FOUND" << endl;
    }
  }
  else if ( type == "heading" || type == "header" ){
    folia::KWargs args;
    args["processor"] = processor_id;
    root->doc()->declare( folia::AnnotationType::HEAD, setname, args );
    args.clear();
    args["xml:id"] = root->id() + ".head." + ind;
    args["class"] = type;
    folia::FoliaElement *hd = par->add_child<folia::Head>( args );
    par = hd;
  }
  else {
    cerr << "ignore not implemented region TYPE: " << type << endl;
    return;
  }
  list<xmlNode*> lines = TiCC::FindNodes( region, "./*:TextLine" );
  if ( !lines.empty() ){
    folia::KWargs text_args;
    text_args["class"] = classname;
    folia::TextContent * content = NULL;
    if (do_markup) {
        content = par->add_child<folia::TextContent>( text_args);
    }
    int pos = 0;
    size_t i = 0;
    string id;
    UnicodeString par_txt;
    for ( const auto& line : lines ){
        UnicodeString line_txt = handle_one_line( par, pos,
					     line,
					     fileName, id );
        if (do_markup) {
            line_txt = ltrim(line_txt );
            if ( !line_txt.isEmpty() ){
              folia::KWargs str_args;
              if (do_strings) {
                  str_args["id"] = id; //references
              } else {
                  str_args["xml:id"] = id; //no references
              }
              str_args["text"] = TiCC::UnicodeToUTF8(line_txt);
              root->doc()->set_checktext(false); //TODO: I don't like this, but it seems we need to disable the checks (may be a bug?), otherwise we get a text validation error here (the final document validates fine)
              folia::TextMarkupString *str = new folia::TextMarkupString( str_args, root->doc());
              content->append(str);
              if (i < lines.size() - 1) {
                  content->add_child<folia::Linebreak>();
                  pos++;
              }
            }
            i++;
        } else {
            par_txt += line_txt;
            if ( &line != &lines.back() && !par_txt.isEmpty()) {
                ++pos;
                par_txt += " ";
            }
        }
    }
    if (!do_markup) {
        par_txt = ltrim(par_txt);
        if (!par_txt.isEmpty()) {
            par->setutext( par_txt, classname );
        }
    }
  }
  else {
    // No TextLine's use unicode nodes directly
    handle_uni_lines( par, region, fileName );
  }
}

list<xmlNode*> sort_regions( list<xmlNode*>& all_regions,
			     list<xmlNode*>& my_order ){
  if ( my_order.empty() ){
    return all_regions;
  }
  else {
    map<string,xmlNode*> sn;
    list<pair<string,xmlNode*>> dl;
    for ( const auto& r : all_regions ){
      string id = TiCC::getAttribute( r, "id" );
      dl.push_back( make_pair( id, r ) );
      sn[id] = r;
    }
    map<string,xmlNode*> region_refs;
    list<xmlNode*> order = TiCC::FindNodes( my_order.front(),
					    ".//*:RegionRefIndexed" );
    vector<string> id_order( order.size() );
    set<string> in_order;
    for ( const auto& ord : order ){
      string ref = TiCC::getAttribute( ord, "regionRef" );
      string index = TiCC::getAttribute( ord, "index" );
      int id = TiCC::stringTo<int>( index );
      region_refs[ref] = sn[ref];
      id_order[id] = ref;
      in_order.insert( ref );
    }
    list<xmlNode*> result;
    set<string> handled;
    for( const auto& it : dl ){
      if ( handled.find( it.first ) != handled.end() ){
	continue;
      }
      if ( in_order.find(it.first) != in_order.end() ){
	// id in reading order, handle those first
	for ( auto const& i : id_order ){
	  result.push_back( region_refs[i] );
	  handled.insert( i );
	}
	in_order.erase(it.first);
      }
      else {
	handled.insert( it.first );
	result.push_back( it.second );
      }
    }
    return result;
  }
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
  list<xmlNode*> all_regions =  TiCC::FindNodes( root, ".//*:TextRegion" );
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
  list<xmlNode*> ordered_regions = sort_regions( all_regions, order );
  //  cerr << "NEW ORDERD REGIONS: " << endl;
  // int i = 0;
  // for ( const auto& t : ordered_regions ){
  //   cerr << i++ << " " << TiCC::getAttribute( t, "id" ) << endl;
  // }

  if ( ordered_regions.empty() ){
#pragma omp critical
    {
      cerr << "no usable data in file:" << fileName << endl;
    }
    xmlFreeDoc( xdoc );
    return false;
  }
  string docid = orgFile.substr( 0, orgFile.find(".") );
  docid = prefix + docid;
  folia::Document doc( "xml:id='" + docid + "'" );
  doc.set_metadata( "page_file", stripDir( fileName ) );
  folia::processor *proc = add_provenance( doc, processor_label, command );
  processor_id = proc->id();
  folia::KWargs args;
  args["xml:id"] =  docid + ".text";
  folia::Text *text = doc.create_root<folia::Text>( args );
  for ( const auto& no : ordered_regions ){
    handle_one_region( text, no, fileName );
  }
  xmlFreeDoc( xdoc );

  string outName;
  if ( !outputDir.empty() ){
    outName = outputDir;
  }
  outName += prefix + orgFile + ".folia.xml";
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
    doc.set_checktext(true); //we disabled it earlier, set to true prior to serialisation again (not sure if it has an effect?)
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
  cerr << "\t--setname='set'\t the common FoLiA set name for all structural nodes. "
    "(default '" << setname << "')" << endl;
  cerr << "\t--class='class'\t the FoLiA class name for <t> nodes. "
    "(default '" << classname << "')" << endl;
  cerr << "\t--prefix='pre'\t add this prefix to ALL created files. (default 'FA-') " << endl;
  cerr << "\t\t\t use 'none' for an empty prefix. (can be dangerous)" << endl;
  cerr << "\t--norefs\t do not add references nodes to the original document. (default: Add References)" << endl;
  cerr << "\t--nostrings\t do not add string annotations (no str), implies --norefs" << endl;
  cerr << "\t--nomarkup\t do not add any markup to the text (no t-str)" << endl;
  cerr << "\t--sent\t treat each text line as a sentence. This is a contrived solution and not recommended." << endl;
  cerr << "\t--trusttokens\t when the Page-file contains Word items, translate them to FoLiA Word and Sentence elements" << endl;
  cerr << "\t--compress='c'\t with 'c'=b create bzip2 files (.bz2) " << endl;
  cerr << "\t\t\t with 'c'=g create gzip files (.gz)" << endl;
  cerr << "\t-v\t\t verbose output " << endl;
  cerr << "\t-V or --version\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVt:O:h",
			 "compress:,class:,setname:,help,version,prefix:,"
			 "norefs,threads:,trusttokens,sent,nostrings,nomarkup" );
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
  do_strings = !opts.extract( "nostrings" );
  do_markup = !opts.extract( "nomarkup" );
  do_sent = opts.extract( "sent" );
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
