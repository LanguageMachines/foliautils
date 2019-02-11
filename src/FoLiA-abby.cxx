/*
  Copyright (c) 2014 - 2019
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

string setname = "FoLiA-abby-set";
string classname = "OCR";

string get_line( xmlNode *line ){
  UnicodeString result;
  list<xmlNode*> variants = TiCC::FindNodes( line, "*:wordRecVariants" );
  if ( !variants.empty() ){
    if ( verbose ){
#pragma omp critical
      {
	cout << "\t\t\tfound " << variants.size() << " wordRecVariants nodes" << endl;
      }
    }
    for ( const auto& var : variants ){
      list<xmlNode*> recs = TiCC::FindNodes( var, "*:wordRecVariant" );
      if ( recs.empty() ){
	// hapens sometimes, just skip..
      }
      else {
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "\t\t\t\tfound " << recs.size() << " wordRecVariant nodes" << endl;
	  }
	}
	list<xmlNode*> text = TiCC::FindNodes( recs.front(), "*:variantText" );
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "\t\t\t\tfound " << text.size() << " text nodes" << endl;
	  }
	}
	UnicodeString bla = TiCC::UnicodeFromUTF8(TiCC::XmlContent(text.front()));
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "\t\t\t\t\traw text: '" << bla << "'" << endl;
	  }
	}
	UnicodeString tmp;
	for ( int i=0; i < bla.length(); ++i ){
	  UChar c = bla[i];
	  switch ( c ){
	  case ' ':
	    // fallthrough
	  case '\t':
	    // fallthrough
	  case '\n':
	    // fallthrough
	  case '\r':
	    break;
	  default:
	    tmp += c;
	  }
	}
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "\t\t\t\t\tintermediate text: '" << tmp << "'" << endl;
	  }
	}
	if ( tmp.endsWith( "¬" ) ){
	  tmp.remove(tmp.length()-1);
	}
	else if ( tmp.endsWith( "-" ) ){
	  tmp.remove(tmp.length()-1);
	}
	else if ( tmp.endsWith( "\n" ) || tmp.endsWith( "\r" ) ){
	  tmp.remove(tmp.length()-1);
	  if ( !tmp.endsWith( " " ) ){
	    tmp += " ";
	  }
	}
	else if ( !tmp.endsWith( " " ) ){
	  tmp += " ";
	}
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "\t\t\t\t\tfinal text: '" << tmp << "'" << endl;
	  }
	}
	result += tmp;
      }
    }
  }
  else {
    list<xmlNode*> chars = TiCC::FindNodes( line, "*:charParams" );
    if ( verbose ){
#pragma omp critical
      {
	cout << "\t\t\t\tfound " << chars.size() << " chars" << endl;
      }
    }
    for ( const auto& ch : chars ){
      result += TiCC::UnicodeFromUTF8(TiCC::XmlContent(ch));
    }
    if ( result.endsWith( "¬" ) ){
      result.remove(result.length()-1);
    }
    else if ( result.endsWith( "-" ) ){
      result.remove(result.length()-1);
    }
    else if ( result.endsWith( "\n" ) ){
      result.remove(result.length()-1);
      if ( !result.endsWith( " " ) ){
	result += " ";
      }
    }
    else if ( !result.endsWith( " " ) ){
      result += " ";
    }
  }
  if ( verbose ){
#pragma omp critical
    {
      cout << "Word text = '" << result << "'" << endl;
    }
  }
  return TiCC::UnicodeToUTF8(result);
}

void process_line( xmlNode *block, map<string,vector<string>>& parts ){
  list<xmlNode*> formats = TiCC::FindNodes( block, "*:formatting" );
  if ( verbose ){
#pragma omp critical
    {
      cout << "\t\t\tfound " << formats.size() << " formatting nodes" << endl;
    }
  }

  for ( const auto& form : formats ){
    string small = TiCC::getAttribute( form, "smallcaps" );
    string result = get_line( form );
    if ( !small.empty() ){
      parts["lemma"].push_back(result);
    }
    else {
      parts["entry"].push_back(result);
    }
  }
}

bool process_par( folia::FoliaElement *root,
		  xmlNode *par ){
  list<xmlNode*> lines = TiCC::FindNodes( par, "*:line" );
  if ( verbose ){
#pragma omp critical
    {
      cout << "\t\tfound " << lines.size() << " lines" << endl;
    }
  }
  map<string,vector<string>> parts;
  for ( const auto& line : lines ){
    process_line( line, parts );
  }

  string head;
  string lemma;
  string entry;
  for ( const auto& it : parts ){
    string result;
    for ( const auto& s : it.second ){
      result += s;
    }
    result = TiCC::trim(result);
    if ( it.first == "lemma" ){
      lemma += result;
    }
    else {
      entry += result;
    }
  }
  lemma = TiCC::trim(lemma);
  entry = TiCC::trim(entry);
  bool didit = false;
  if ( !lemma.empty() ){
    folia::KWargs args;
    args["_id"] = root->id() + ".lemma";
    folia::Part *part = new folia::Part( args );
    root->append( part );
    part->settext( lemma, classname );
    didit = true;
  }
  if ( !entry.empty() ){
    folia::KWargs args;
    args["_id"] = root->id() + ".entry";
    folia::Part *part = new folia::Part( args );
    root->append( part );
    part->settext( entry, classname );
    didit = true;
  }
  return didit;
}

bool process_page( folia::FoliaElement *root,
		   xmlNode *block ){
  list<xmlNode*> paragraphs = TiCC::FindNodes( block, ".//*:par" );
  if ( verbose ){
#pragma omp critical
    {
      cout << "\tfound " << paragraphs.size() << " paragraphs" << endl;
    }
  }
  int i = 0;
  bool didit = false;
  for ( const auto& par_node : paragraphs ){
    folia::KWargs args;
    args["_id"] = root->id() + ".p" + TiCC::toString(++i);
    folia::Paragraph *paragraph = new folia::Paragraph( args );
    if ( process_par( paragraph, par_node ) ){
      root->append( paragraph );
      didit = true;
    }
    else {
      --i;
      delete paragraph;
    }
  }
  return didit;
}

bool convert_abbyxml( const string& fileName,
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
  list<xmlNode*> pages = TiCC::FindNodes( root, ".//*:page" );
  if ( pages.empty() ){
#pragma omp critical
    {
      cerr << "Problem finding pages node in " << fileName << endl;
    }
    return false;
  }
  string orgFile = TiCC::basename( fileName );
  string docid = prefix + orgFile;
  folia::Document doc( "_id='" + docid + "'" );
  // doc.declare( folia::AnnotationType::STRING, setname,
  // 	       "annotator='folia-abby', datetime='now()'" );
  doc.set_metadata( "abby_file", orgFile );
  folia::KWargs args;
  args["_id"] =  docid + ".text";
  folia::Text *text = new folia::Text( args );
  doc.append( text );
  int i = 0;
  for ( const auto& page : pages ){
    folia::KWargs args;
    args["_id"] = text->id() + ".div" + TiCC::toString(++i);
    folia::Division *div = new folia::Division( args );
    text->append( div );
    process_page( div, page );
  }

  string outName;
  if ( !outputDir.empty() ){
    outName = outputDir + "/";
  }
  string::size_type pos = orgFile.find(".xml");
  orgFile.erase( pos );
  outName += orgFile + ".folia.xml";
  zipType type = inputType;
  if ( outputType != NORMAL )
    type = outputType;
  if ( type == BZ2 )
    outName += ".bz2";
  else if ( type == GZ )
    outName += ".gz";

  doc.save( outName );
  if ( verbose ){
#pragma omp critical
    {
      cout << "created " << outName << endl;
    }
  }
  return true;
}

void usage(){
  cerr << "Usage: FoLiA-abby [options] file/dir" << endl;
  cerr << "\t-t\t\t number_of_threads" << endl;
  cerr << "\t-h or --help\t this messages " << endl;
  cerr << "\t-O\t\t output directory " << endl;
  cerr << "\t--setname='set'\t the FoLiA set name for <t> nodes. "
    "(default '" << setname << "')" << endl;
  cerr << "\t--class='class'\t the FoLiA class name for <t> nodes. "
    "(default '" << classname << "')" << endl;
  cerr << "\t--prefix='pre'\t add this prefix to ALL created files. (default 'FA-') " << endl;
  cerr << "\t\t\t use 'none' for an empty prefix. (can be dangerous)" << endl;
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
  string prefix = "FA-";
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
    string name = outputDir + "/";
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
    fileNames = TiCC::searchFilesMatch( name, ".xml($|.gz$|.bz2$)", false );
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
    if ( !convert_abbyxml( fileNames[fn], outputDir, outputType, prefix ) )
#pragma omp critical
      {
	cerr << "failure on " << fileNames[fn] << endl;
      }
    else
#pragma omp critical
      {
	cout << "\tconverted " << fileNames[fn] << endl;
      }
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
