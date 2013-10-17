#include <getopt.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>

#include "ticcutils/FileUtils.h"
#include "libfolia/document.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;


void create_idf_list( const map<string, unsigned int>& wc,
		      const string& filename, int clip ){
  ofstream os( filename.c_str() );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  map<string,unsigned int >::const_iterator cit = wc.begin();
  while( cit != wc.end()  ){
    if ( cit->second > clip ){
      os << cit->first << "\t" << cit->second << endl;
    }
    ++cit;
  }
#pragma omp critical
  {
    cout << "created IDF list '" << filename << "'" << endl;
  }
}

const string frog_cgntagset = "http://ilk.uvt.nl/folia/sets/frog-mbpos-cgn";
const string frog_mblemtagset = "http://ilk.uvt.nl/folia/sets/frog-mblem-nl";

size_t inventory( const Document *doc, const string& docName,
		  bool lowercase,
		  map<string,unsigned int>& wc ){
  vector<Word*> words = doc->words();
  set<string> ws;
  for ( unsigned int i=0; i < words.size(); ++i ){
    string word;
    try {
      if ( lowercase ){
	UnicodeString uword = UTF8ToUnicode( words[i]->str() );
	uword.toLower();
	word = UnicodeToUTF8( uword );
      }
      else
	word = words[i]->str();
    }
    catch(...){
#pragma omp critical
      {
	cerr << "missing text for word " << words[i]->id() << endl;
      }
      break;
    }
    ws.insert( word );
  }

  set<string>::const_iterator it = ws.begin();
  while ( it != ws.end() ){
#pragma omp critical
    {
      ++wc[*it];
    }
    ++it;
  }
  return ws.size();
}

int main( int argc, char *argv[] ){
  if ( argc < 2	){
    cerr << "Usage: [-t number_of_threads] dir/filename " << endl;
    exit(EXIT_FAILURE);
  }
  int opt;
  int clip = 0;
  int numThreads = 1;
  bool recursiveDirs = false;
  bool lowercase = false;
  string expression;
  string outPrefix;
  while ((opt = getopt(argc, argv, "c:e:hlt:o:RV")) != -1) {
    switch (opt) {
    case 'c':
      clip = atoi(optarg);
      break;
    case 'l':
      lowercase = true;
      break;
    case 'e':
      expression = optarg;
      break;
    case 't':
      numThreads = atoi(optarg);
      break;
    case 'o':
      outPrefix = optarg;
      break;
    case 'R':
      recursiveDirs = true;
      break;
    case 'V':
      cerr << "IDF" << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'h':
      cerr << "Usage: [options] file/dir" << endl;
      cerr << "\t-c\t clipping factor. " << endl;
      cerr << "\t\t\t\t(entries with frequency <= this factor will be ignored). " << endl;
      cerr << "\t-l\t Lowercase all words" << endl;
      cerr << "\t-t\t number_of_threads" << endl;
      cerr << "\t-h\t this messages " << endl;
      cerr << "\t-V\t show version " << endl;
      cerr << "\t " << argv[0] << " will produce IDF statistics for a directoy of FoLiA files " << endl;
      cerr << "\t-e\t expr: specify the expression all files should match with." << endl;
      cerr << "\t-o\t output prefix" << endl;
      cerr << "\t-R\t search the dirs recursively. (when appropriate)" << endl;
      exit(EXIT_SUCCESS);
      break;
    default: /* '?' */
      cerr << "Usage: [-t number_of_threads]  dir " << endl;
      exit(EXIT_FAILURE);
    }
  }
#ifdef HAVE_OPENMP
  if ( numThreads != 1 )
    omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 )
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
#endif

  string name = argv[optind];
  vector<string> fileNames = TiCC::searchFilesMatch( name, expression, recursiveDirs );
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }
  if ( outPrefix.empty() ){
    string::size_type pos = fileNames[0].find( "/" );
    if ( pos != string::npos )
      outPrefix = fileNames[0].substr(0,pos);
    else
      outPrefix = "current";
  }

  if ( toDo > 1 )
    cout << "start processing of " << toDo << " files " << endl;

  map<string,unsigned int> wc;
  unsigned int wordTotal =0;

#pragma omp parallel for shared(fileNames,wordTotal,wc )
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    string docName = fileNames[fn];
    Document *d = 0;
    try {
      d = new Document( "file='"+ docName + "'" );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "failed to load document '" << docName << "'" << endl;
	cerr << "reason: " << e.what() << endl;
      }
      continue;
    }
    size_t count = inventory( d, docName, lowercase, wc );
    wordTotal += count;
#pragma omp critical
    {
      cout << "Processed :" << docName << " with " << count << " unique words."
	   << " still " << --toDo << " files to go." << endl;
    }
    delete d;
  }

  if ( toDo > 1 ){
    cout << "done processsing directory '" << name << "' in total "
	 << wordTotal << " unique words were found." << endl;
  }
  cout << "start calculating the results" << endl;
  string filename = outPrefix + ".idf.tsv";
  create_idf_list( wc, filename, clip );
  cout << "done: " << endl;
}