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

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "libfolia/document.h"
#include "config.h"

#ifdef HAVE_TEXTCAT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "textcat.h"

#ifdef __cplusplus
}
#endif

#else
#ifdef HAVE_LIBTEXTCAT_TEXTCAT_H
#include "libtextcat/textcat.h"
#else
#ifdef HAVE_LIBEXTTEXTCAT_TEXTCAT_H
#include "libexttextcat/textcat.h"
#endif
#endif
#endif

using namespace	std;
using namespace	folia;

size_t split_at( const string& src, vector<string>& results,
		 const string& sep ){
  // split a string into substrings, using seps as seperator
  // silently skip empty entries (e.g. when two or more seperators co-incide)
  results.clear();
  string::size_type pos = 0, p;
  string res;
  while ( pos != string::npos ){
    p = src.find_first_of( sep, pos );
    if ( p == string::npos ){
      res = src.substr( pos );
      pos = p;
    }
    else {
      res = src.substr( pos, p - pos );
      pos = p + 1;
    }
    if ( !res.empty() )
      results.push_back( res );
  }
  return results.size();
}

string compress( const string& s ){
  // remove leading and trailing spaces from a string
  string result;
  if ( !s.empty() ){
    string::const_iterator b_it = s.begin();
    while ( b_it != s.end() && isspace( *b_it ) ) ++b_it;
    string::const_iterator e_it = s.end();
    --e_it;
    while ( e_it != s.begin() && isspace( *e_it ) ) --e_it;
    if ( b_it <= e_it )
      result = string( b_it, e_it+1 );
  }
  return result;
}

int to_lower( const int& i ){ return tolower(i); }

void decap( string& s ){
  transform( s.begin()+1, s.end(), s.begin()+1, to_lower );
}

void setlang( FoliaElement* e, const string& lan ){
  // append a LangAnnotation child of class 'cls'
  KWargs args;
  args["class"] = lan;
  LangAnnotation *node = new LangAnnotation( e->doc() );
  node->setAttributes( args );
  e->replace( node );
}

void addLang( TextContent *t, const string& val, bool doAll ){
  //
  // we expect something like [dutch][french]
  // or [dutch]
  // or WEIRD
  //
  vector<string> vals;
  size_t num = split_at( val, vals, "[]" );
  if ( num == 0 ){
    cerr << "O JEE: unexpected language value: " << val << endl;
    setlang( t->parent(), val );
  }
  else {
    string val;
    for ( size_t i = 0; i < vals.size(); ++i ){
      if ( i > 0 )
	val += "|";
      val += vals[i];
      if ( !doAll )
	break;
    }
    setlang( t->parent(), val );
  }
}

class TCdata {
public:
  TCdata( const string& cf ) {
    cfName = cf;
    TC = textcat_Init( cf.c_str() );
  }
  TCdata( const TCdata& in ) {
    TC = textcat_Init( in.cfName.c_str() );
    cfName = in.cfName;
  }
  ~TCdata() { textcat_Done( TC ); };
  bool isInit() const { return TC != 0; };
  void procesFile( const string&, const string&, bool, bool );
  void *TC;
  string cfName;
};

void TCdata::procesFile( const string& outDir, const string& docName,
			 bool doStrings,
			 bool doAll ){
#pragma omp critical
  {
    cout << "process " << docName << endl;
  }
  Document doc( "file='" + docName + "'" );
  doc.declare( AnnotationType::LANG, "iso" );
  vector<Paragraph*> xp;
  vector<String*> xs;
  size_t Size;
  if ( doStrings ){
    xs = doc.doc()->select<String>();
    Size = xs.size();
#pragma omp critical
    {
      cout << "document '" << docName << "' has " << Size
	   << " strings " << endl;
    }
  }
  else {
    xp = doc.paragraphs();
    Size = xp.size();
#pragma omp critical
    {
      cout << "document '" << docName << "' has " << Size
	   << " paragraphs " << endl;
    }
  }
  cerr << "OUTDIR" << outDir << endl;
  string outName;
  if ( !outDir.empty() )
    outName = outDir + "/";
  outName += docName.substr(0, docName.find(".xml") );
  outName += ".lc.xml";
  cerr << "OUTNAME" << outName << endl;
  //attempt to open the outfile
  ofstream os1( outName.c_str() );
  if ( !os1.good() ){
    // it fails
    // attempt to create the path
    vector<string> parts;
    int num = split_at( outName, parts, "/" );
    if ( num > 1 ){
      string path;
      if ( outName[0] == '/' )
	path += "/";
      for ( size_t i=0; i < parts.size()-1; ++i ){
	path += parts[i] + "/";
	//	cerr << "mkdir path = " << path << endl;
	int status = mkdir( path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH );
	if ( status != 0 ){
#pragma omp critical
	  {
	    cerr << "unable to create directory: " << path << endl;
	  }
	  exit(EXIT_FAILURE);
	}
      }
    }
    // now retry
    ofstream os2( outName.c_str() );
    if ( !os2 ){
      // still fails, we are lost
#pragma omp critical
      {
	cerr << "unable to open output file " << outName << endl;
	cerr << "does the outputdir exist? And is it writabe?" << endl;
      }
      exit( EXIT_FAILURE);
    }
  }
  os1.close();
  ofstream os( outName.c_str() );
  if ( !os ){
    // this can never fail but ok.
#pragma omp critical
    {
      cerr << "unable to open output file " << outName << endl;
      cerr << "does the outputdir exist? And is it writabe?" << endl;
    }
    exit( EXIT_FAILURE );
  }
  for ( size_t i=0; i < Size; ++i ){
    TextContent *t = 0;
    if ( doStrings )
      t = xs[i]->textcontent("OCR");
    else
      t = xp[i]->textcontent("OCR");
    string para = t->str();
    para = compress( para );
    if ( para.empty() ){
      // #pragma omp_critical
      // 	    {
      // 	      cerr << "WARNING: empty paragraph " << id << endl;
      // 	    }
    }
    else {
      decap( para );
      char *res = textcat_Classify( TC, para.c_str(), para.size() );
      if ( res && strcmp( res, "SHORT" ) != 0 ){
	addLang( t, res, doAll );
      }
    }
  }
  os << doc << endl;
}

bool gatherNames( const string& dirName, vector<string>& fileNames ){
  DIR *dir = opendir( dirName.c_str() );
  if ( !dir ){
    cerr << "unable to open dir:" << dirName << endl;
    return false;
  }
  struct dirent *entry = readdir( dir );
  while ( entry ){
    string tmp = entry->d_name;
    cerr << "BEKIJK " << tmp << endl;
    if ( tmp[0] != '.' ){
      struct stat st_buf;
      string fullName  = dirName + "/" + tmp;
      int status = stat( fullName.c_str(), &st_buf );
      if ( status != 0 ){
	cerr << "cannot 'stat' file: " << fullName << endl;
	return false;
      }
      if ( S_ISDIR (st_buf.st_mode) ){
	if ( !gatherNames( fullName, fileNames ) )
	  return false;
      }
      else {
	string::size_type pos = fullName.find( ".xml" );
	if ( pos != string::npos && fullName.substr( pos ).length() == 4 ){
	  fileNames.push_back( fullName );
	}
      }
    }
    entry = readdir( dir );
  }
  closedir( dir );
  return true;
}

int main( int argc, char *argv[] ){
  if ( argc < 2	){
    cerr << "missing arg " << endl;
    exit(EXIT_FAILURE);
  }
  bool doAll =false;
  int opt;
  string outDir;
  string config = "./config/tc.txt";
  bool doStrings = false;
  while ((opt = getopt(argc, argv, "ac:ho:sV")) != -1) {
    switch (opt) {
    case 'a':
      doAll = true;
      break;
    case 'c':
      config = optarg;
      break;
    case 'o':
      outDir = optarg;
      break;
    case 's':
      doStrings = true;
      break;
    case 'V':
      cout << PACKAGE_STRING << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'h':
      cerr << "Usage: [-c config] [-a] [-V] [-s] [-o outputdir] dir/filename " << endl;
      cerr << "-a\tassign ALL detected languages to the result. (default is to assing the most probable)." << endl;
      cerr << "-c <file> use LM config from 'file'" << endl;
      cerr << "-s\texamine text in <str> nodes. (default is to use the <p> nodes)." << endl;
      cerr << "-V\tshow version info." << endl;
      exit(EXIT_SUCCESS);
      break;
    default: /* '?' */
      cerr << "Usage: [-c config] [-a] [-V] [-s] [-o outputdir] dir/filename " << endl;
      exit(EXIT_FAILURE);
    }
  }
  TCdata TC( config );
  if ( !TC.isInit() ){
    cerr << "unable to init from: " << config << endl;
    exit(EXIT_FAILURE);
  }

  vector<string> fileNames;
  string dirName;
  string name = argv[optind];
  struct stat st_buf;
  int status = stat( name.c_str(), &st_buf );
  if ( status != 0 ){
    cerr << "parameter '" << name << "' doesn't seem to be a file or directory"
	 << endl;
    exit(EXIT_FAILURE);
  }
  if ( S_ISREG (st_buf.st_mode) ){
    cerr << "name " << name << " is a file" << endl;
    fileNames.push_back( name );
    string::size_type pos = name.rfind( "/" );
    if ( pos != string::npos )
      dirName = name.substr(0,pos);
  }
  else if ( S_ISDIR (st_buf.st_mode) ){
    cerr << "name " << name << " is a dir" << endl;
    string::size_type pos = name.rfind( "/" );
    if ( pos != string::npos )
      name.erase(pos);
    dirName = name;
    if ( !gatherNames( dirName, fileNames ) ){
      exit( EXIT_FAILURE );
    }
  }
  size_t toDo = fileNames.size();
  if ( toDo > 1 )
    cout << "start processing of " << toDo << " files " << endl;
#pragma omp parallel for firstprivate(TC),shared(fileNames)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    string docName = fileNames[fn];
    TC.procesFile( outDir, docName, doStrings, doAll );
  }
}
