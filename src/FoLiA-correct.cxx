#include <unistd.h>
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

const string frog_cgntagset = "http://ilk.uvt.nl/folia/sets/frog-mbpos-cgn";
const string frog_mblemtagset = "http://ilk.uvt.nl/folia/sets/frog-mblem-nl";

struct word_conf {
  word_conf(){};
  word_conf( const string& w, const string& c ): word(w), conf(c){};
  string word;
  string conf;
};

ostream& operator<<( ostream& os, const word_conf& wc ){
  os << wc.word << " [" << wc.conf << "]";
  return os;
}

bool fillVariants( const string& fn,
		   map<string,vector<word_conf> >& variants,
		   size_t numSugg ){
  ifstream is( fn.c_str() );
  string line;
  string current_word;
  vector<word_conf> vec;
  while ( getline( is, line ) ) {
    vector<string> parts;
    if ( TiCC::split_at( line, parts, "#" ) == 6 ){
      string word = parts[0];
      if ( current_word.empty() )
	current_word = word;

      if ( word != current_word ){
	// finish previous word
	if ( vec.size() > numSugg ){
	  vec.resize( numSugg );
	}
	variants[current_word] = vec;
	vec.clear();
	current_word = word;
      }
      string trans = parts[2];
      string confS = parts[5];
      vec.push_back( word_conf( trans, confS ) );
    }
    else {
      cerr << "error in line " << line << endl;
    }
  }
  if ( !vec.empty() ){
    if ( vec.size() > numSugg ){
      vec.resize( numSugg );
    }
    variants[current_word] = vec;
    vec.clear();
  }
  return !variants.empty();
}

bool fillUnknowns( const string& fn, set<string>& unknowns ){
  ifstream is( fn.c_str() );
  string line;
  while ( getline( is, line ) ) {
    vector<string> parts;
    if ( TiCC::split( line, parts ) == 2 ){
      UnicodeString us( parts[0].c_str() );
      if ( us.length() > 1 ){
	// '1' character words ar never UNK
	double dum;
	if ( !TiCC::stringTo( parts[0], dum ) ){
	  // 'true' numeric values are never UNK
	  unknowns.insert( parts[0] );
	}
      }
    }
    else {
      cerr << "error reading Unknown value from line " << line << endl;
    }
  }
  return !unknowns.empty();
}

bool fillPuncts( const string& fn, map<string,string>& puncts ){
  ifstream is( fn.c_str() );
  string line;
  while ( getline( is, line ) ) {
    vector<string> parts;
    if ( TiCC::split( line, parts ) == 2 ){
      puncts[parts[0]] = parts[1];
    }
    else {
      cerr << "error reading punct value from line " << line << endl;
    }
  }
  return !puncts.empty();
}

void filter( string& word ){
  for ( size_t i=0; i < word.size(); ++i ){
    if ( word[i] == '#' )
      word[i] = '.';
  }
}

void correctParagraph( Paragraph* par,
		       const map<string,vector<word_conf> >& variants,
		       const set<string>& unknowns,
		       const map<string,string>& puncts,
		       const string& classname ){
  vector<String*> sv = par->select<String>();
  if ( sv.size() == 0 )
    return;
  int offset = 0;
  string corrected;
  for ( size_t i=0; i < sv.size(); ++i ){
    String *s = sv[i];
    vector<TextContent *> origV = s->select<TextContent>();
    string word = origV[0]->str();
    filter(word);
    string orig_word = word;
    map<string,string>::const_iterator pit = puncts.find( word );
    if ( pit != puncts.end() ){
      word = pit->second;
    }
    map<string,vector<word_conf> >::const_iterator it = variants.find( word );
    if ( it != variants.end() ){
      // 1 or more edits found
      string edit = it->second[0].word;
      vector<FoliaElement*> oV;
      oV.push_back( origV[0] );
      vector<FoliaElement*> nV;
      KWargs args;
      args["class"] = classname;
      args["offset"] = TiCC::toString(offset);
      args["value"] = edit;
      TextContent *newT = new TextContent( args );
      corrected += edit + " ";
      offset = corrected.size();
      nV.push_back( newT );
      vector<FoliaElement*> sV;
      size_t limit = it->second.size();
      for( size_t j=0; j < limit; ++j ){
	Suggestion *sug = new Suggestion( "confidence='" +
					  it->second[j].conf +
					  "', n='" +
					  TiCC::toString(j+1) + "/" +
					  TiCC::toString(limit) +
					  "'" );
	sug->settext( it->second[j].word, classname );
	sV.push_back( sug );
      }
      vector<FoliaElement*> cV;
      args.clear();
      s->correct( oV, cV, nV, sV, args );
    }
    else {
      set<string>::const_iterator sit = unknowns.find( word );
      if ( sit == unknowns.end() ){
	sit = unknowns.find( orig_word );
      }
      if ( sit != unknowns.end() ){
	// a registrated garbage word
	string edit = "UNK";
	vector<FoliaElement*> oV;
	oV.push_back( origV[0] );
	vector<FoliaElement*> nV;
	KWargs args;
	args["class"] = classname;
	args["offset"] = TiCC::toString(offset);
	args["value"] = edit;
	TextContent *newT = new TextContent( args );
	corrected += edit + " ";
	offset = corrected.size();
	nV.push_back( newT );
	vector<FoliaElement*> sV;
	vector<FoliaElement*> cV;
	args.clear();
	s->correct( oV, cV, nV, sV, args );
      }
      else {
	// just use the word
	s->settext( word, offset, classname );
	corrected += word + " ";
	offset = corrected.size();
      }
    }
  }
  corrected = TiCC::trim( corrected );
  par->settext( corrected, classname );
}

bool correctDoc( Document *doc,
		 const map<string,vector<word_conf> >& variants,
		 const set<string>& unknowns,
		 const map<string,string>& puncts,
		 const string& classname ){
  if ( doc->isDeclared( folia::AnnotationType::CORRECTION,
			"Ticcl-Set" ) ){
    return false;
  }
  doc->declare( folia::AnnotationType::CORRECTION, "Ticcl-Set",
		"annotator='TICCL', annotatortype='auto', datetime='now()'");
  vector<Paragraph*> pv = doc->doc()->select<Paragraph>();
  for( size_t i=0; i < pv.size(); ++i ){
    try {
      correctParagraph( pv[i], variants, unknowns, puncts, classname );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "FoLiA error in paragraph " << pv[i]->id() << " of document " << doc->id() << endl;
	cerr << e.what() << endl;
      }
      return false;
    }
  }
  return true;
}

int main( int argc, char *argv[] ){
  if ( argc < 2	){
    cerr << "Usage: [-t number_of_threads] dir/filename " << endl;
    exit(EXIT_FAILURE);
  }
  int opt;
  int numThreads = 1;
  size_t numSugg = 10;
  bool recursiveDirs = false;
  string expression;
  string variantFileName;
  string unknownFileName;
  string punctFileName;
  string outPrefix;
  string classname = "Ticcl";
  while ((opt = getopt(argc, argv, "c:e:ht:o:RVw:p:s:u:")) != -1) {
    switch (opt) {
    case 'c':
      classname = optarg;
      break;
    case 'e':
      expression = optarg;
      break;
    case 's':
      numSugg = atoi(optarg);
      break;
    case 't':
      numThreads = atoi(optarg);
      break;
    case 'p':
      punctFileName = optarg;
      break;
    case 'u':
      unknownFileName = optarg;
      break;
    case 'o':
      outPrefix = optarg;
      break;
    case 'w':
      variantFileName = optarg;
      break;
    case 'R':
      recursiveDirs = true;
      break;
    case 'V':
      cerr << PACKAGE_STRING << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'h':
      cerr << "Usage: [options] file/dir" << endl;
      cerr << "\t-c\t classname" << endl;
      cerr << "\t-t\t number_of_threads" << endl;
      cerr << "\t-s\t max number_of_suggestions. (default 10)" << endl;
      cerr << "\t-h\t these messages " << endl;
      cerr << "\t-V\t show version " << endl;
      cerr << "\t " << argv[0] << " will correct FoLiA files, " << endl;
      cerr << "\t or a whole directory of FoLiA files " << endl;
      cerr << "\t-e 'expr': specify the expression all files should match with." << endl;
      cerr << "\t-o\t output prefix" << endl;
      cerr << "\t-u 'uname'\t name of unknown words file" << endl;
      cerr << "\t-p 'pname'\t name of punct words file" << endl;
      cerr << "\t-w 'vname'\t name of variants file" << endl;
      cerr << "\t-R\t search the dirs recursively. (when appropriate)" << endl;
      exit(EXIT_SUCCESS);
      break;
    default: /* '?' */
      cerr << "Usage: [-t number_of_threads] dir/filename " << endl;
      exit(EXIT_FAILURE);
    }
  }

  if ( !outPrefix.empty() && !TiCC::isDir( outPrefix ) ){
    cerr << "non existing output dir: '" << outPrefix << "'" << endl;
    exit( EXIT_FAILURE );
  }

#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 )
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
#endif

  if ( !argv[optind] ){
    cerr << "missing input file(s)" << endl;
    exit( EXIT_FAILURE );
  }
  string name = argv[optind];
  vector<string> fileNames = TiCC::searchFilesMatch( name, expression, recursiveDirs );
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }
  bool doDir = ( toDo > 1 );

  map<string,vector<word_conf> > variants;
  set<string> unknowns;
  map<string,string> puncts;

#pragma omp parallel sections
  {
#pragma omp section
    {
#pragma omp critical
      {
	cout << "start reading variants " << endl;
      }
      if ( !fillVariants( variantFileName, variants, numSugg ) ){
#pragma omp critical
	{
	  cerr << "no variants." << endl;
	}
	exit( EXIT_FAILURE );
      }
#pragma omp critical
      {
	cout << "read " << variants.size() << " variants " << endl;
      }
    }
#pragma omp section
    {
#pragma omp critical
      {
	cout << "start reading unknowns " << endl;
      }
      if ( !fillUnknowns( unknownFileName, unknowns ) ){
#pragma omp critical
	{
	  cerr << "no unknown words!" << endl;
	}
      }
#pragma omp critical
      {
	cout << "read " << unknowns.size() << " unknown words " << endl;
      }
    }
#pragma omp section
    {
#pragma omp critical
      {
	cout << "start reading puncts " << endl;
      }
      if ( !fillPuncts( punctFileName, puncts ) ){
#pragma omp critical
	{
	  cerr << "no punct words!" << endl;
	}
      }
#pragma omp critical
      {
	cout << "read " << puncts.size() << " punctuated words " << endl;
      }
    }
  }

  if ( doDir ){
    try {
      Document doc( "string='<?xml version=\"1.0\" encoding=\"UTF-8\"?><FoLiA/>'" );
    }
    catch(...){
    };
    cout << "start processing of " << toDo << " files " << endl;
  }

#pragma omp parallel for shared(fileNames,toDo) schedule(dynamic,1)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    string docName = fileNames[fn];
    Document *doc = 0;
    try {
      doc = new Document( "file='"+ docName + "'" );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "failed to load document '" << docName << "'" << endl;
	cerr << "reason: " << e.what() << endl;
      }
      continue;
    }
    string outName = outPrefix;
    string::size_type pos = docName.rfind(".");
    if ( pos != string::npos ){
      outName += docName.substr(0,pos) + ".corrected" + docName.substr(pos);
    }
    else {
      outName += docName + ".corrected";
    }
    if ( TiCC::isFile( outName ) ){
#pragma omp critical
      cerr << "skipping already done file: " << outName << endl;
    }
    else if ( correctDoc( doc, variants, unknowns, puncts, classname ) ){
      doc->save( outName );
#pragma omp critical
      {
	if ( toDo > 1 ){
	  cout << "Processed :" << docName << " into " << outName
	       << " still " << --toDo << " files to go." << endl;
	}
      }
    }
    else {
#pragma omp critical
      {
	cerr << "skipped " << docName << " seems to be already processed" << endl;
      }
    }
    delete doc;
  }

  if ( doDir ){
    cout << "done processsing directory '" << name << "'" << endl;
  }
  else {
    cout << "finished " << name << endl;
  }
}
