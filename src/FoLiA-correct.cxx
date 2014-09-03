#include <unistd.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>

#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/PrettyPrint.h"
#include "libfolia/document.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;

bool verbose = false;
string classname = "Ticcl";
string setname = "Ticcl-set";

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
		       const map<string,string>& puncts ){
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
      // a word with no suggested variants
      set<string>::const_iterator sit = unknowns.find( word );
      if ( sit == unknowns.end() ){
	sit = unknowns.find( orig_word );
      }
      if ( sit != unknowns.end() ){
	// ok it is a registrated garbage word
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
	// just use the ORIGINAL word
	word = orig_word;
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
		 const map<string,string>& puncts ){
  if ( doc->isDeclared( folia::AnnotationType::CORRECTION,
			setname ) ){
    return false;
  }
  doc->declare( folia::AnnotationType::CORRECTION, setname,
		"annotator='TICCL', annotatortype='auto', datetime='now()'");
  vector<Paragraph*> pv = doc->doc()->select<Paragraph>();
  for( size_t i=0; i < pv.size(); ++i ){
    try {
      correctParagraph( pv[i], variants, unknowns, puncts );
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

void usage( const string& name ){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t--setname\t FoLiA setname. (default '" << setname << "')" << endl;
  cerr << "\t--class\t classname. (default '" << classname << "')" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t--nums\t max number_of_suggestions. (default 10)" << endl;
  cerr << "\t-h\t this message " << endl;
  cerr << "\t-V\t show version " << endl;
  cerr << "\t " << name << " will correct FoLiA files " << endl;
  cerr << "\t or a whole directory of FoLiA files " << endl;
  cerr << "\t-e 'expr': specify the expression all files should match with." << endl;
  cerr << "\t-O\t output prefix" << endl;
  cerr << "\t--unk 'uname'\t name of unknown words file, the *unk file produced by TICCL-unk" << endl;
  cerr << "\t--puncts 'pname'\t name of punct words file, the *punct file produced by TICCL-unk" << endl;
  cerr << "\t--ranks 'vname'\t name of variants file, the *ranked file produced by TICCL-rank" << endl;
  cerr << "\t--clear\t redo ALL corrections. (default is to skip already processed file)" << endl;
  cerr << "\t-R\t search the dirs recursively (when appropriate)" << endl;
}

void checkFile( const string& what, const string& name, const string& ext ){
  if ( name.empty() ) {
    cerr << "missing '--" << what << "' option" << endl;
    exit( EXIT_FAILURE );
  }
  if ( name.find( ext ) == string::npos ){
    cerr << what << " file " << name << " has wrong extension!"
	 << " expected: " << ext << endl;
    exit( EXIT_FAILURE );
  }
  if ( !TiCC::isFile( name ) ){
    cerr << "unable to find file '" << name << "'" << endl;
  }
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "e:vVt:O:Rh",
			 "class:,setname:,clear,unk:,ranks:,puncts:,nums:" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage(argv[0]);
    exit( EXIT_FAILURE );
  }
  string progname = opts.prog_name();
  int numThreads = 1;
  size_t numSugg = 10;
  bool recursiveDirs = false;
  bool clear = false;
  string expression;
  string variantFileName;
  string unknownFileName;
  string punctFileName;
  string outPrefix;
  string value;
  if ( opts.extract( 'h' ) ){
    usage(progname);
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  verbose = opts.extract( 'v' );
  opts.extract( "setname", setname );
  opts.extract( "class", classname );
  clear = opts.extract( "clear" );
  opts.extract( 'e', expression );
  recursiveDirs = opts.extract( 'R' );
  opts.extract( 'O', outPrefix );
  opts.extract( "puncts", punctFileName );
  checkFile( "puncts", punctFileName, ".punct" );
  opts.extract( "unk", unknownFileName );
  checkFile( "unk", unknownFileName, ".unk" );
  opts.extract( "ranks", variantFileName );
  checkFile( "rank", variantFileName, ".rank" );
  if ( opts.extract( "nums", value ) ){
    if ( !TiCC::stringTo( value, numSugg ) ){
      cerr << "unsupported value for --nums (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  if ( opts.extract( 't', value ) ){
    if ( !TiCC::stringTo( value, numThreads ) ){
      cerr << "unsupported value for -t (" << value << ")" << endl;
      exit(EXIT_FAILURE);  }
  }
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage(progname);
    exit(EXIT_FAILURE);
  }
  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.size() == 0 ){
    cerr << "missing input file or directory" << endl;
    exit( EXIT_FAILURE );
  }
  else if ( fileNames.size() > 1 ){
    cerr << "currently only 1 file or directory is supported" << endl;
    exit( EXIT_FAILURE );
  }

  if ( !outPrefix.empty() ){
    if ( outPrefix[outPrefix.length()-1] != '/' )
      outPrefix += "/";
    if ( !TiCC::isDir( outPrefix ) ){
      if ( !TiCC::createPath( outPrefix ) ){
	cerr << "unable to find or create: '" << outPrefix << "'" << endl;
	exit( EXIT_FAILURE );
      }
    }
  }

#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 )
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
#endif

  string name = fileNames[0];
  fileNames = TiCC::searchFilesMatch( name, expression, recursiveDirs );
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
#ifdef HAVE_OPENMP
    folia::initMT();
#endif
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
    string::size_type pos = docName.rfind("/");
    if ( pos != string::npos ){
      docName = docName.substr( pos+1 );
    }
    pos = docName.rfind(".");
    if ( pos != string::npos ){
      outName += docName.substr(0,pos) + ".ticcl" + docName.substr(pos);
    }
    else {
      outName += docName + ".ticcl";
    }
    if ( clear ){
      unlink( outName.c_str() );
    }
    if ( TiCC::isFile( outName ) ){
#pragma omp critical
      cerr << "skipping already done file: " << outName << endl;
    }
    else {
      if ( !TiCC::createPath( outName ) ){
#pragma omp critical
	cerr << "unable to create output file! " << outName << endl;
	exit(EXIT_FAILURE);
      }
      if ( correctDoc( doc, variants, unknowns, puncts ) ){
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
