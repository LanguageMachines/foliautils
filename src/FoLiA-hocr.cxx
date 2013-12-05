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
#include "libxml/HTMLparser.h"
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

void getNodes( xmlNode *pnt, const string& tag, vector<xmlNode*>& res ){
  while ( pnt ){
    if ( pnt->type == XML_ELEMENT_NODE && TiCC::Name(pnt) == tag ){
      res.push_back( pnt );
    }
    getNodes( pnt->children, tag, res );
    pnt = pnt->next;
  }
}

vector<xmlNode*> getNodes( xmlNode *pnt, const string& tag ){
  vector<xmlNode*> res;
  getNodes( pnt, tag, res );
  return res;
}

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
  else if ( TiCC::match_back( file, ".xhtml.gz" ) ){
    type = GZ;
    isHtml = false;
  }
  else if ( TiCC::match_back( file, ".html.gz" ) ){
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
    cerr << "problem detecting type of file: " << file << endl;
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


void processDiv( xmlNode *div, folia::FoliaElement *out, const string& file ){
  vector<xmlNode*> pars = getNodes( div->children, "p" );
  for ( size_t i = 0; i < pars.size(); ++i ){
    string p_id = TiCC::getAttribute( pars[i], "id" );
    folia::Paragraph *par
      = new folia::Paragraph( out->doc(),
			      "id='" + out->id() + "." + p_id + "'");
    xmlNode *pnt = pars[i]->children;
    string txt;
    while ( pnt ){
      if ( pnt->type == XML_ELEMENT_NODE ){
	if ( TiCC::Name(pnt) == "span" ){
	  string cls = TiCC::getAttribute( pnt, "class" );
	  if ( cls == "ocr_line" ){
	    string l_id = TiCC::getAttribute( pnt, "id" );
	    vector<xmlNode*> words = getNodes( pnt->children, "span" );
	    for ( size_t j = 0; j < words.size(); ++j ){
	      string cls = TiCC::getAttribute( words[j], "class" );
	      if ( cls == "ocrx_word" ){
		string w_id = TiCC::getAttribute( words[j], "id" );
		string content = extractContent( words[j] );
		content = TiCC::trim( content );
		if ( !content.empty() ){
		  folia::String *str = new folia::String( out->doc(),
							  "id='" + par->id()
							  + "." + w_id + "'" );
		  par->append( str );
		  str->settext( content, txt.length(), "OCR" );
		  txt += " " + content;
		  folia::Alignment *h = new folia::Alignment( "href='" + file + "'" );
		  str->append( h );
		  folia::AlignReference *a =
		    new folia::AlignReference( "id='" + w_id + "', type='str'" );
		  h->append( a );
		}
	      }
	      else {
		cerr << "expected class='ocrx_word', got: " << cls << endl;
		return;
	      }
	    }
	  }
	  else {
	    cerr << "expected class='ocr_line', got: " << cls << endl;
	    return;
	  }
	}
      }
      pnt = pnt->next;
    }
    if ( txt.size() > 1 ){
      out->append( par );
      par->settext( txt.substr(1), "OCR" );
    }
    else
      delete par;
  }
}

void process( xmlNode *root, folia::FoliaElement *out, const string& file ){
  vector<xmlNode*> divs = getNodes( root, "div" );
  for ( size_t i=0; i < divs.size(); ++i ){
    string cls = TiCC::getAttribute( divs[i], "class" );
    if ( cls == "ocr_page" ){
      processDiv( divs[i], out, file );
    }
  }
}

string getFile( const string& title ){
  string result;
  vector<string> vec;
  TiCC::split_at( title, vec, ";" );
  for ( size_t i=0; i < vec.size(); ++i ){
    vector<string> v1;
    size_t num = TiCC::split( vec[i], v1 );
    if ( num == 2 ){
      if ( TiCC::trim( v1[0] ) == "image" )
	result = v1[1];
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
  xmlNode *root = xmlDocGetRootElement( xdoc );
  vector<xmlNode*> divs = getNodes( root, "div" );
  string title;
  for ( size_t i=0; i < divs.size(); ++i ){
    string cls = TiCC::getAttribute( divs[i], "class" );
    if ( cls == "ocr_page" ){
      if ( !title.empty() ){
	cerr << "multiple 'ocr_page' <div> nodes not supported: " << fileName << endl;
	exit(EXIT_FAILURE);
      }
      title = TiCC::getAttribute( divs[i], "title" );
    }
  }
  if ( title.empty() ){
    cerr << "No 'ocr_page' <div> node found: " << fileName << endl;
    exit(EXIT_FAILURE);
  }
  string docid = getFile( title );
  folia::Document doc( "id='" + docid + "'" );
  doc.declare( folia::AnnotationType::STRING, "OCR",
	       "annotator='folia-hocr', datetime='now()'" );
  folia::Text *text = new folia::Text( "id='" + docid + ".text'" );
  doc.append( text );
  process( root, text, docid );
  xmlFreeDoc( xdoc );

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
}

int main( int argc, char *argv[] ){
  if ( argc < 2	){
    cerr << "Usage: [-t number_of_threads] [-o outputdir] [-a altodir] dir/filename " << endl;
    exit(EXIT_FAILURE);
  }
  int opt;
  int numThreads=1;
  string outputDir;
  zipType outputType = NORMAL;
  while ((opt = getopt(argc, argv, "a:bcght:vVo:p")) != -1) {
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
      cerr << "Usage: FoLiA-hocr [options] file/dir" << endl;
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
    fileNames = TiCC::searchFilesMatch( name, "*html" );
  }
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found." << endl;
    exit(EXIT_FAILURE);
  }
  if ( toDo > 1 )
    cout << "start processing of " << toDo << " files " << endl;
  if ( numThreads >= 1 ){
    omp_set_num_threads( numThreads );
  }

#pragma omp parallel for shared(fileNames)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    convert_hocr( fileNames[fn], outputDir, outputType );
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
