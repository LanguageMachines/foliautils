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

xmlNode *getNode( xmlNode *pnt, const string& tag ){
  while ( pnt ){
    if ( pnt->type == XML_ELEMENT_NODE && TiCC::Name(pnt) == tag ){
      return pnt;
    }
    else {
      xmlNode *res  = getNode( pnt->children, tag );
      if ( res )
	return res;
    }
    pnt = pnt->next;
  }
  return 0;
}

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
    cerr << "HTML files not supported yet" << endl;
    exit( EXIT_FAILURE );
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

string strip_ext( const string& name ){
  string::size_type pos = name.rfind( "." );
  if ( pos != string::npos ){
    return name.substr(0,pos);
  }
  cerr << "problem removing extension (.gz or .bz2)" << endl;
  exit(EXIT_FAILURE);
}

void handleSpan( xmlNode *span, folia::FoliaElement *par ){
  string cls = TiCC::getAttribute( span, "class" );
  if ( cls != "ocr_line" ){
    cerr << "unhandle span class: " << cls << endl;
  }
  else {
    string content  = TiCC::XmlContent(span);
    if ( content.empty() ){
      return;
    }
    folia::Sentence *s = new folia::Sentence( par->doc(),
					      "generate_id='" + par->id() + "'");
    par->append( s );
    folia::KWargs args;
    args["value"] = TiCC::XmlContent(span);
    folia::TextContent *t = new folia::TextContent( args );
    s->append( t );
  }
}

void handlePar( xmlNode *par, folia::FoliaElement *out ){
  xmlNode *pnt = par->children;
  string text;
  while ( pnt ){
    if ( pnt->type == XML_ELEMENT_NODE ){
      if ( TiCC::Name(pnt) == "span" ){
	string content = TiCC::XmlContent(pnt);
	text += content + " ";
      }
      else if ( TiCC::Name(pnt) == "br" ){
	// ignore
      }
      else {
	cerr << "unhandled tag " << TiCC::Name(pnt) << endl;
      }
    }
    pnt = pnt->next;
  }
  if ( !text.empty() ){
    folia::KWargs args;
    args["value"] = text;
    folia::TextContent *t = new folia::TextContent( args );
    out->append( t );
  }
}

string filterMeuck( const string& f ){
  string result = f;
  for( size_t i=0; i < result.length(); ++i ){
    if ( result[i] == ':' )
      result[i] = '.';
    else if ( result[i] == '/' )
      result[i] = '.';
  }
  return result;
}

string extractName( const string& title, const string& fallback ){
  vector<string> vec;
  string result;
  TiCC::split_at( title, vec, ";" );
  for ( size_t i=0; i < vec.size(); ++i ){
    vector<string> parts;
    TiCC::split_at( vec[i], parts, " " );
    if ( parts.size() > 1 ){
      if ( parts[0] == "image" ){
	result = TiCC::trim(parts[1]);
	break;
      }
      else if ( parts[0] == "file" ){
	result = TiCC::trim(parts[1]);
	break;
      }
    }
  }
  if ( result.empty() )
    result = fallback;
  else if ( !isalpha( result[0] ) )
    result = "file-" + result;
  return filterMeuck( result );
}

void processDiv( xmlNode *div, folia::FoliaElement *out ){
  string title = TiCC::getAttribute( div, "title" );
  string nodeId;
  if ( !title.empty() ){
    nodeId = extractName( title, out->id() );
  }
  folia::Division *division =
    new folia::Division( out->doc(), "id='" + nodeId + "'");
  out->append( division );
  xmlNode *pnt = div->children;
  folia::Paragraph *par = 0;
  int parcount = 0;
  string text;
  while ( pnt ){
    if ( pnt->type == XML_ELEMENT_NODE ){
      if ( TiCC::Name(pnt) == "p" ){
	parcount++;
	if ( !text.empty() ){
	  folia::KWargs args;
	  args["value"] = text;
	  folia::TextContent *t = new folia::TextContent( args );
	  par->append( t );
	}
	par = 0;
	text.clear();
	folia::Paragraph *tmp
	  = new folia::Paragraph( division->doc(),
				  "id='" + division->id() + ".p." +
				  TiCC::toString(parcount) + "'");
	cerr << "PAR created paragraph: " << tmp->id() << endl;
	division->append( tmp );
	handlePar( pnt, tmp );
      }
      else if ( TiCC::Name(pnt) == "span" ){
	string content = TiCC::XmlContent(pnt);
	text += content + " ";
	if ( par == 0 ){
	  parcount++;
	  par = new folia::Paragraph( division->doc(),
				      "id='" + division->id() + ".p." +
				      TiCC::toString(parcount) + "'");
	  cerr << "SPAN created paragraph: " << par->id() << endl;
	  division->append( par );
	}

      }
      else if ( TiCC::Name(pnt) == "br" ){
	// ignore
      }
      else {
	cerr << "unhandled tag " << TiCC::Name(pnt) << endl;
      }
    }
    pnt = pnt->next;
  }
  if ( !text.empty() ){
    folia::KWargs args;
    args["value"] = text;
    folia::TextContent *t = new folia::TextContent( args );
    par->append( t );
  }
}

void process( xmlDoc* in, folia::FoliaElement *out ){
  xmlNode *root = xmlDocGetRootElement( in );
  vector<xmlNode*> divs = getNodes( root, "div" );
  for ( size_t i=0; i < divs.size(); ++i ){
    string cls = TiCC::getAttribute( divs[i], "class" );
    if ( cls != "ocr_page" ){
      cerr << "unhandled <div> with class='" << cls << "'" << endl;
    }
    else {
      processDiv( divs[i], out );
    }
  }
}

void convert_hocr( const string& fileName,
		   const string& outputDir,
		   const zipType outputType ){
  zipType inputType;
  xmlDoc *xdoc = getXml( fileName, inputType );
  string docid = fileName;
  if ( inputType != NORMAL )
    docid = strip_ext( docid );
  folia::Document doc( "id='" + docid + "'" );
  doc.declare( folia::AnnotationType::STRING, "hocr",
	       "annotator='folia-hocr', datetime='now()'" );
  folia::Text *text = new folia::Text( "id='" + docid + ".text'" );
  doc.append( text );
  process( xdoc, text );
  string outName = outputDir;
  outName += docid + ".folia.xml";
  zipType type = inputType;
  if ( outputType != NORMAL )
    type = outputType;
  if ( type == BZ2 )
    outName += ".bz2";
  else if ( type == GZ )
    outName += ".gz";
  doc.save( outName );
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
    fileNames = TiCC::searchFilesMatch( name, "*.xhtml" );
  }
  size_t toDo = fileNames.size();
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
