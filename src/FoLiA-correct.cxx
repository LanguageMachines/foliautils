/*
  Copyright (c) 2014 - 2017
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

#include <unistd.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>

#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;

const string SEPARATOR = "_";
int verbose = 0;
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
  ifstream is( fn );
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
  ifstream is( fn );
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
  ifstream is( fn );
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
  for ( auto& w : word ){
    if ( w == '#' )
      w = '.';
  }
}

bool correct_one_unigram( const string& w,
			  const map<string,vector<word_conf> >& variants,
			  const set<string>& unknowns,
			  const map<string,string>& puncts,
			  string& result ){
  bool did_edit = false;
  if ( verbose > 2 ){
    cout << "correct unigram " << w << endl;
  }
  string word = w;
  string orig_word = word;
  map<string,string>::const_iterator pit = puncts.find( word );
  if ( pit != puncts.end() ){
    word = pit->second;
  }
  map<string,vector<word_conf> >::const_iterator it = variants.find( word );
  if ( it != variants.end() ){
    // 1 or more edits found
    string edit = it->second[0].word;
    vector<string> parts;
    TiCC::split_at( edit, parts, SEPARATOR ); // edit can be seperated!
    for ( const auto& p : parts ){
      result += p + " ";
    }
    did_edit = true;
  }
  else {
    // a word with no suggested variants
    set<string>::const_iterator sit = unknowns.find( word );
    if ( sit == unknowns.end() ){
      sit = unknowns.find( orig_word );
    }
    if ( sit != unknowns.end() ){
      // ok it is a registrated garbage word
      result = "UNK ";
      did_edit = true;
    }
    else {
      // just use the word
      result = word + " ";
    }
  }
  if ( verbose > 2 ){
    cout << " = 1 => " << result << endl;
  }
  return did_edit;
}

string correct_unigrams( const vector<string>& unigrams,
			 const map<string,vector<word_conf> >& variants,
			 const set<string>& unknowns,
			 const map<string,string>& puncts,
			 vector<pair<string,string>>& corrections ){
  string result;
  for ( const auto& uni : unigrams ){
    string cor;
    if ( correct_one_unigram( uni, variants, unknowns, puncts, cor ) ){
      corrections.push_back( make_pair( uni, cor ) );
    }
    result += cor;
  }
  return result;
}


int correct_one_bigram( const string& bi,
			const map<string,vector<word_conf> >& variants,
			const set<string>& unknowns,
			const map<string,string>& puncts,
			string& result ){
  int extra_skip = 0;
  result.clear();
  if ( verbose > 2 ){
    cout << "correct bigram " << bi << endl;
  }
  string word = bi;
  filter(word);
  string orig_word = word;
  map<string,string>::const_iterator pit = puncts.find( word );
  if ( pit != puncts.end() ){
    word = pit->second;
  }
  map<string,vector<word_conf> >::const_iterator it = variants.find( word );
  if ( it != variants.end() ){
    // edits found
    string edit = it->second[0].word;
    vector<string> parts;
    TiCC::split_at( edit, parts, SEPARATOR ); // edit can can be unseperated!
    for ( const auto& p : parts ){
      result += p + " ";
    }
    if ( verbose > 2 ){
      cout << " = 2 => " << result << endl;
    }
    extra_skip = 1;
  }
  else {
    // a word with no suggested variants
    set<string>::const_iterator sit = unknowns.find( word );
    if ( sit == unknowns.end() ){
      sit = unknowns.find( orig_word );
    }
    if ( sit != unknowns.end() ){
      // ok it is a registrated garbage bigram
      result = "UNK UNK ";
      if ( verbose > 2 ){
	cout << " = 2 => " << result << endl;
      }
      extra_skip = 1;
    }
    else {
      // just use the ORIGINAL word and handle the first part like unigram
      vector<string> parts;
      TiCC::split_at( orig_word, parts, SEPARATOR );
      correct_one_unigram( parts[0], variants, unknowns, puncts, result );
      if ( verbose > 2 ){
	cout << " = 2 => " << result << endl;
      }
    }
  }
  if ( verbose > 1 ){
    cout << " = 2 => " << result << endl;
  }
  return extra_skip;
}

string correct_bigrams( const vector<string>& bigrams,
			const map<string,vector<word_conf> >& variants,
			const set<string>& unknowns,
			const map<string,string>& puncts,
			const string& last ){
  string result;
  int skip = 0;
  for ( const auto& bi : bigrams ){
    if ( skip > 0 ){
      --skip;
      continue;
    }
    string cor;
    skip = correct_one_bigram( bi, variants, unknowns, puncts, cor );
    result += cor;
  }
  string corr;
  correct_one_unigram( last, variants, unknowns, puncts, corr );
  if ( verbose > 2 ){
    cout << "handled last word: " << corr << endl;
  }
  result += corr;
  return result;
}

int correct_one_trigram( const string& tri,
			  const map<string,vector<word_conf> >& variants,
			  const set<string>& unknowns,
			  const map<string,string>& puncts,
			  string& result ){
  int extra_skip = 0;
  result.clear();
  if ( verbose > 2 ){
    cout << "correct trigram " << tri << endl;
  }
  string word = tri;
  filter(word);
  string orig_word = word;
  map<string,string>::const_iterator pit = puncts.find( word );
  if ( pit != puncts.end() ){
    word = pit->second;
  }
  map<string,vector<word_conf> >::const_iterator it = variants.find( word );
  if ( it != variants.end() ){
    // edits found
    string edit = it->second[0].word;
    vector<string> parts;
    TiCC::split_at( edit, parts, SEPARATOR ); // edit can can be unseperated!
    for ( const auto& p : parts ){
      result += p + " ";
    }
    extra_skip = 2;
  }
  else {
    // a word with no suggested variants
    set<string>::const_iterator sit = unknowns.find( word );
    if ( sit == unknowns.end() ){
      sit = unknowns.find( orig_word );
    }
    if ( sit != unknowns.end() ){
      // ok it is a registrated garbage trigram
      result = "UNK UNK UNK";
      if ( verbose > 2 ){
	cout << " = 3 => " << result << endl;
      }
      extra_skip = 2;
    }
    else {
      // just use the ORIGINAL word so handle the first part like bigram
      vector<string> parts;
      TiCC::split_at( orig_word, parts, SEPARATOR );
      string corr;
      string test = parts[0] + SEPARATOR + parts[1];
      extra_skip = correct_one_bigram( test, variants, unknowns, puncts, corr );
      result += corr;
    }
  }
  if ( verbose > 2 ){
    cout << " = 3 => " << result << " skip=" << extra_skip << endl;
  }
  return extra_skip;
}

string correct_trigrams( const vector<string>& trigrams,
			 const map<string,vector<word_conf> >& variants,
			 const set<string>& unknowns,
			 const map<string,string>& puncts,
			 const vector<string>& unigrams ){
  string result;
  int skip = 0;
  for ( const auto& tri : trigrams ){
    if ( verbose > 2 ){
      cout << "skip=" << skip  << " TRI: " << tri << endl;
    }
    if ( skip > 0 ){
      --skip;
      continue;
    }
    string cor;
    skip = correct_one_trigram( tri, variants, unknowns, puncts, cor );
    result += cor;
    if ( verbose > 2 ){
      cout << "skip=" << skip  << " intermediate:" << result << endl;
    }
  }
  if ( skip > 1 ){
    return result;
  }
  else if ( skip == 1 ){
    string last = unigrams[unigrams.size()-1];
    string corr;
    correct_one_unigram( last, variants, unknowns, puncts, corr );
    if ( verbose > 2 ){
      cout << "handled last word: " << corr << endl;
    }
    result += corr;
    return result;
  }
  else {
    string last = unigrams[unigrams.size()-1];
    string last_bi = unigrams[unigrams.size()-2] + SEPARATOR + last;
    string corr;
    int skip = correct_one_bigram( last_bi, variants, unknowns, puncts, corr );
    if ( verbose > 2 ){
      cout << "handled last bigram: " << corr << endl;
    }
    result += corr;
    if ( skip == 0 ){
      correct_one_unigram( last, variants, unknowns, puncts, corr );
      if ( verbose > 2 ){
	cout << "handled last word: " << corr << endl;
      }
      result += corr;
    }
    return result;
  }
}

void correctNgrams( Paragraph* par,
		    const map<string,vector<word_conf> >& variants,
		    const set<string>& unknowns,
		    const map<string,string>& puncts,
		    int ngrams ){
  vector<TextContent *> origV = par->select<TextContent>();
  string content = origV[0]->str();
  if ( verbose > 1 ){
#pragma omp critical
    {
      cerr << "correct ngrams in: '" << content << "'" << endl;
    }
  }
  vector<string> unigrams;
  TiCC::split( content, unigrams );
  vector<string> bigrams;
  vector<string> trigrams;
  if ( ngrams > 1  && unigrams.size() > 1 ){
    for ( size_t i=0; i < unigrams.size()-1; ++i ){
      string bi;
      bi = unigrams[i] + SEPARATOR + unigrams[i+1];
      bigrams.push_back( bi );
    }
  }
  if ( ngrams > 2 && unigrams.size() > 2 ){
    for ( size_t i=0; i < unigrams.size()-2; ++i ){
      string tri;
      tri = unigrams[i] + SEPARATOR + unigrams[i+1] + SEPARATOR + unigrams[i+2];
      trigrams.push_back( tri );
    }
  }
  string corrected;
  if ( trigrams.empty() ){
    if ( bigrams.empty() ){
      vector<pair<string,string>> corrections;
      corrected = correct_unigrams( unigrams, variants, unknowns, puncts, corrections );
      if ( verbose && !corrections.empty() ){
	cout << "unigram corrections:" << endl;
	for ( const auto& p : corrections ) {
	  cout << p.first << " : " << p.second << endl;
	}
      }
    }
    else {
      corrected = correct_bigrams( bigrams, variants, unknowns, puncts, unigrams.back() );
    }
  }
  else {
    corrected = correct_trigrams( trigrams, variants, unknowns, puncts, unigrams );
  }
  corrected = TiCC::trim( corrected );
  if ( verbose > 1 ){
#pragma omp critical
    {
      cerr << " corrected ngrams: '" << corrected << "'" << endl;
    }
  }
  if ( !corrected.empty() ){
    par->settext( corrected, classname );
  }
}

void correctParagraph( Paragraph* par,
		       const map<string,vector<word_conf> >& variants,
		       const set<string>& unknowns,
		       const map<string,string>& puncts,
		       bool doStrings ){
  vector<FoliaElement*> ev;
  if ( doStrings ){
    vector<String*> sv = par->select<String>();
    ev.resize(sv.size());
    copy( sv.begin(), sv.end(), ev.begin() );
  }
  else {
    vector<Word*> sv = par->select<Word>();
    ev.resize(sv.size());
    copy( sv.begin(), sv.end(), ev.begin() );
  }
  if ( ev.size() == 0 )
    return;
  int offset = 0;
  string corrected;
  for ( const auto& s : ev ){
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
	KWargs sargs;
	sargs["confidence"] = it->second[j].conf;
	sargs["n"]= TiCC::toString(j+1) + "/" + TiCC::toString(limit);
	Suggestion *sug = new Suggestion( sargs );
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
		 const map<string,string>& puncts,
		 int ngrams,
		 bool string_nodes,
		 bool word_nodes ){
  if ( doc->isDeclared( folia::AnnotationType::CORRECTION,
			setname ) ){
    return false;
  }
  doc->declare( folia::AnnotationType::CORRECTION, setname,
		"annotator='TICCL', annotatortype='auto', datetime='now()'");
  vector<Paragraph*> pv = doc->doc()->select<Paragraph>();
  for( const auto& par : pv ){
    try {
      if ( ngrams == 1 && ( string_nodes || word_nodes ) ){
	correctParagraph( par, variants, unknowns, puncts, string_nodes );
      }
      else {
	correctNgrams( par, variants, unknowns, puncts, ngrams );
      }
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "FoLiA error in paragraph " << par->id() << " of document " << doc->id() << endl;
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
  cerr << "\t--ngram\t n analyse upto n N-grams. for n=1 see --string-nodes/--word-nodes" << endl;
  cerr << "\t--string-nodes\t Only for UNIGRAMS: descend into <str> nodes. " << endl;
  cerr << "\t--word-nodes\t Only for UNIGRAMS: descend into <w> nodes. " << endl;
  cerr << "\t-h or --help\t this message " << endl;
  cerr << "\t-V or --version\t show version " << endl;
  cerr << "\t " << name << " will correct FoLiA files " << endl;
  cerr << "\t or a whole directory of FoLiA files " << endl;
  cerr << "\t-e 'expr': specify the expression all files should match with." << endl;
  cerr << "\t-O\t output prefix" << endl;
  cerr << "\t--unk 'uname'\t name of unknown words file, the *unk file produced by TICCL-unk" << endl;
  cerr << "\t--punct 'pname'\t name of punct words file, the *punct file produced by TICCL-unk" << endl;
  cerr << "\t--rank 'vname'\t name of variants file, the *ranked file produced by TICCL-rank" << endl;
  cerr << "\t--clear\t redo ALL corrections. (default is to skip already processed file)" << endl;
  cerr << "\t-R\t search the dirs recursively (when appropriate)" << endl;
}

void checkFile( const string& what, const string& name, const string& ext ){
  if ( !TiCC::match_back( name, ext ) ){
    cerr << what << " file " << name << " has wrong extension!"
	 << " expected: " << ext << endl;
    exit( EXIT_FAILURE );
  }
  if ( !TiCC::isFile( name ) ){
    cerr << "unable to find file '" << name << "'" << endl;
  }
}

int main( int argc, const char *argv[] ){
  TiCC::CL_Options opts( "e:vVt:O:Rh",
			 "class:,setname:,clear,unk:,rank:,punct:,nums:,version,help,ngram:,string-nodes,word-nodes" );
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
  int ngram = 1;
  bool recursiveDirs = false;
  bool clear = false;
  bool string_nodes = false;
  bool word_nodes = false;
  string expression;
  string variantFileName;
  string unknownFileName;
  string punctFileName;
  string outPrefix;
  string value;
  if ( opts.extract( 'h' ) || opts.extract( "help" ) ){
    usage(progname);
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  while ( opts.extract( 'v' ) ){
    ++verbose;
  }
  opts.extract( "setname", setname );
  opts.extract( "class", classname );
  clear = opts.extract( "clear" );
  opts.extract( 'e', expression );
  recursiveDirs = opts.extract( 'R' );
  opts.extract( 'O', outPrefix );
  if ( !opts.extract( "punct", punctFileName ) ){
    cerr << "missing '--punct' option" << endl;
    exit( EXIT_FAILURE );
  }
  checkFile( "punct", punctFileName, ".punct" );
  if ( !opts.extract( "unk", unknownFileName ) ){
    cerr << "missing '--unk' option" << endl;
    exit( EXIT_FAILURE );
  }
  checkFile( "unk", unknownFileName, ".unk" );
  if ( !opts.extract( "rank", variantFileName ) ){
    cerr << "missing '--rank' option" << endl;
    exit( EXIT_FAILURE );
  }
  checkFile( "rank", variantFileName, ".ranked" );
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
  if ( opts.extract( "ngram", value ) ){
    if ( !TiCC::stringTo( value, ngram )
	 || ngram > 3
	 || ngram < 0 ){
      cerr << "unsupported value for --ngram (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  string_nodes = opts.extract( "string-nodes" );
  word_nodes = opts.extract( "word-nodes" );
  if ( string_nodes && word_nodes ){
    cerr << "--string-nodes and --word-nodes conflict" << endl;
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

  cout << "verbosity = " << verbose << endl;

  if ( doDir ){
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
      if ( correctDoc( doc, variants, unknowns, puncts, ngram, string_nodes, word_nodes ) ){
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
