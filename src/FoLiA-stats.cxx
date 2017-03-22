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

#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>

#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/PrettyPrint.h"
#include "libfolia/folia.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;
using namespace	TiCC;

bool verbose = false;
string classname = "current";

enum Mode { UNKNOWN,
	    T_IN_D, T_IN_P, T_IN_S,
	    S_IN_D, S_IN_P,
	    W_IN_D, W_IN_P, W_IN_S  };

Mode stringToMode( const string& ms ){
  if ( ms == "text_in_doc" ){
    return T_IN_D;
  }
  else if ( ms == "text_in_par" ){
    return T_IN_P;
  }
  else if ( ms == "text_in_sent" ){
    return T_IN_S;
  }
  else if ( ms == "string_in_doc" ){
    return S_IN_D;
  }
  else if ( ms == "string_in_par" ){
    return S_IN_P;
  }
  else if ( ms == "word_in_doc" ){
    return W_IN_D;
  }
  else if ( ms == "word_in_par" ){
    return W_IN_P;
  }
  else if ( ms == "word_in_sent" ){
    return W_IN_S;
  }
  return UNKNOWN;
}

void create_wf_list( const map<string,map<string, unsigned int>>& wc,
		     const string& filename, unsigned int totalIn,
		     unsigned int clip, int nG,
		     bool doperc ){
  unsigned int total = totalIn;
  for ( const auto& wc0 : wc ){
    string ext;
    string lang = wc0.first;
    if ( lang != "none" ){
      ext += "." + lang;
    }
    if ( nG > 1 ){
      ext += "." + toString( nG ) + "-gram";
    }
    ext += ".tsv";
    string ofilename = filename + ext;
    ofstream os( ofilename );
    if ( !os ){
      cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
      exit(EXIT_FAILURE);
    }
    map<unsigned int, set<string>> wf;
    map<string,unsigned int >::const_iterator cit = wc0.second.begin();
    while( cit != wc0.second.end()  ){
      if ( cit->second <= clip ){
	total -= cit->second;
      }
      else {
	wf[cit->second].insert( cit->first );
      }
      ++cit;
    }
    unsigned int sum=0;
    unsigned int types=0;
    map<unsigned int, set<string> >::const_reverse_iterator wit = wf.rbegin();
    while ( wit != wf.rend() ){
      set<string>::const_iterator sit = wit->second.begin();
      while ( sit != wit->second.end() ){
	sum += wit->first;
	os << *sit << "\t" << wit->first;
	if ( doperc ){
	  os << "\t" << sum << "\t" << 100 * double(sum)/total;
	}
	os << endl;
	++types;
	++sit;
      }
      ++wit;
    }
#pragma omp critical
    {
      cout << "created WordFreq list '" << ofilename << "'";
      cout << " with " << types << " unique " << nG << "-gram tokens";
      if ( clip > 0 ){
	cout << " ("<< totalIn - total << " were clipped.)";
      }
      cout << endl;
    }
  }
}

struct rec {
  unsigned int count;
  map<string,unsigned int> pc;
};

void create_lf_list( const map<string,map<string, unsigned int>>& lc,
		     const string& filename,
		     unsigned int totalIn,
		     unsigned int clip,
		     int nG,
		     bool doperc ){
  unsigned int total = totalIn;
  for ( const auto& lc0 : lc ){
    string ext;
    string lang = lc0.first;
    if ( lang != "none" ){
      ext += "." + lang;
    }
    if ( nG > 1 ){
      ext += "." + toString( nG ) + "-gram";
    }
    ext += ".tsv";
    string ofilename = filename + ext;
    ofstream os( ofilename );
    if ( !os ){
      cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
      exit(EXIT_FAILURE);
    }
    map<unsigned int, set<string> > lf;
    map<string,unsigned int >::const_iterator cit = lc0.second.begin();
    while( cit != lc0.second.end()  ){
      if ( cit->second <= clip ){
	total -= cit->second;
      }
      else {
	lf[cit->second].insert( cit->first );
      }
      ++cit;
    }

    unsigned int sum=0;
    unsigned int types=0;
    map<unsigned int, set<string> >::const_reverse_iterator wit = lf.rbegin();
    while ( wit != lf.rend() ){
      set<string>::const_iterator sit = wit->second.begin();
      while ( sit != wit->second.end() ){
	sum += wit->first;
	os << *sit << "\t" << wit->first;
	if ( doperc ){
	  os << "\t" << sum << "\t" << 100* double(sum)/total;
	}
	os << endl;
	++types;
	++sit;
      }
      ++wit;
    }
#pragma omp critical
    {
      cout << "created LemmaFreq list '" << filename << "'";
      cout << " with " << types << " unique " << nG << "-gram lemmas";
      if ( clip > 0 ){
	cout << " ("<< totalIn - total << " were clipped.)";
      }
      cout << endl;
    }
  }
}

void create_lpf_list( const map<string,multimap<string, rec>>& lpc,
		      const string& filename, unsigned int totalIn,
		      unsigned int clip,
		      int nG,
		      bool doperc ){
  unsigned int total = totalIn;
  for ( const auto& lpc0 : lpc ){
    string ext;
    string lang = lpc0.first;
    if ( lang != "none" ){
      ext += "." + lang;
    }
    if ( nG > 1 ){
      ext += "." + toString( nG ) + "-gram";
    }
    ext += ".tsv";
    string ofilename = filename + ext;
    ofstream os( ofilename );
    if ( !os ){
      cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
      exit(EXIT_FAILURE);
    }

    multimap<unsigned int, pair<string,string> > lpf;
    multimap<string,rec>::const_iterator cit = lpc0.second.begin();
    while( cit != lpc0.second.end()  ){
      map<string,unsigned int>::const_iterator pit = cit->second.pc.begin();
      while ( pit != cit->second.pc.end() ){
	if ( pit->second <= clip ){
	  total -= pit->second;
	}
	else {
	  lpf.insert( make_pair( pit->second,
				 make_pair( cit->first, pit->first ) ) );
	}
	++pit;
      }
      ++cit;
    }
    unsigned int sum =0;
    unsigned int types =0;
    multimap<unsigned int, pair<string,string> >::const_reverse_iterator wit = lpf.rbegin();
    while ( wit != lpf.rend() ){
      sum += wit->first;
      os << wit->second.first << " " << wit->second.second << "\t" << wit->first;
      if ( doperc ){
	os << "\t" << sum << "\t" << 100 * double(sum)/total;
      }
      os << endl;
      ++types;
      ++wit;
    }
#pragma omp critical
    {
      cout << "created LemmaPosFreq list '" << ofilename << "'";
      cout << " with " << types << " unique " << nG << "-gram lemmas and tags";
      if ( clip > 0 ){
	cout << " ("<< totalIn - total << " were clipped.)";
      }
      cout << endl;
    }
  }
}

struct wlp_rec {
  string word;
  string lemma;
  string pos;
};

const string frog_cgntagset = "http://ilk.uvt.nl/folia/sets/frog-mbpos-cgn";
const string frog_mblemtagset = "http://ilk.uvt.nl/folia/sets/frog-mblem-nl";

bool is_emph( const string& data ){
  return (data.size() < 2) && isalnum(data[0]);
}

void add_emph_inventory( const vector<string>& data,
			 set<string>& emph ){
  for ( unsigned int i=0; i < data.size(); ++i ){
    bool done = false;
    for ( unsigned int j=i; j < data.size() && !done; ++j ){
      if ( is_emph( data[j] ) ){
	// a candidate?
	if ( j + 1 < data.size()
	     && is_emph( data[j+1] ) ){
	  // yes a second short word
	  string mw = data[j] + "_" + data[j+1];
	  for ( unsigned int k=j+2; k < data.size(); ++k ){
	    if ( is_emph(data[k]) ){
	      mw += "_" + data[k];
	    }
	    else {
	      emph.insert(mw);
	      mw.clear();
	      i = k; // restart i loop there
	      done = true; // get out of j loop
	      break; // k loop
	    }
	  }
	}
      }
    }
  }
}

void add_emph_inventory( vector<wlp_rec>& data, set<string>& emph ){
  for ( unsigned int i=0; i < data.size(); ++i ){
    string mw;
    bool first = true;
    for ( unsigned int j=i; j < data.size(); ++j ){
      if ( data[j].word.size() < 2 ){
	if ( !first ){
	  mw += "_";
	}
	first = false;
	mw += data[j].word;
      }
      else {
	emph.insert(mw);
	mw.clear();
	first = true;
      }
    }
  }
}

size_t add_word_inventory( const vector<string>& data,
			   map<string,unsigned int>& wc,
			   size_t nG,
			   const string& sep ){
  size_t count = 0;
  if ( data.size() < nG ){
    return 0;
  }
  for ( unsigned int i=0; i <= data.size() - nG ; ++i ){
    string multiw;
    for ( size_t j=0; j < nG; ++j ){
      multiw += data[i+j];
      if ( j < nG-1 ){
	multiw += sep;
      }
    }
    ++count;
#pragma omp critical
    {
      ++wc[multiw];
    }
  }
  return count;
}

size_t doc_sent_word_inventory( const Document *d, const string& docName,
				size_t nG,
				bool lowercase,
				const string& default_language,
				const set<string>& languages,
				map<string,map<string,unsigned int>>& wc,
				map<string,map<string,unsigned int>>& lc,
				map<string,multimap<string, rec>>& lpc,
				unsigned int& lemTotal,
				unsigned int& posTotal,
				set<string>& emph,
				const string& sep ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "make a word inventory on sentences in:" << docName << endl;
    }
  }
  size_t wordTotal = 0;
  lemTotal = 0;
  posTotal = 0;
  vector<Sentence *> sents = d->sentences();
  if ( verbose ){
#pragma omp critical
    {
      cout << docName <<  ": " << sents.size() << " sentences" << endl;
    }
  }
  for ( unsigned int s=0; s < sents.size(); ++s ){
    vector<Word*> words = sents[s]->words();
    if ( verbose ){
#pragma omp critical
      {
	cout << docName <<  " sentence-" << s+1 << " : " << words.size() << " words" << endl;
      }
    }
    if ( words.size() < nG )
      continue;
    string lang = sents[s]->language(); // the language this sentence is in
    // ignore language labels on the invidual words!
    if ( languages.find( lang ) == languages.end() ){
      // lang is 'unwanted', just add to the default
      if ( default_language == "skip" ){
	continue;
      }
      lang = default_language;
    }
    vector<wlp_rec> data;
    for ( const auto& w : words ){
      wlp_rec rec;
      try {
	UnicodeString uword = w->text(classname);
	if ( lowercase ){
	  uword.toLower();
	}
	rec.word = UnicodeToUTF8( uword );
      }
      catch(...){
#pragma omp critical
	{
	  cerr << "FoLiA-stats: missing text for word " << w->id() << endl;
	}
	break;
      }
      try {
	rec.lemma = w->lemma(frog_mblemtagset);
      }
      catch(...){
	try {
	  rec.lemma = w->lemma();
	}
	catch(...){
	  rec.lemma = "";
	}
      }
      try {
	rec.pos = w->pos(frog_cgntagset);
      }
      catch(...){
	try {
	  rec.pos = w->pos();
	}
	catch (...){
	  rec.pos = "";
	}
      }
      data.push_back( rec );
    }
    if ( data.size() != words.size() ) {
#pragma omp critical
      {
	cerr << "FoLiA-stats: Error: Missing words! skipped sentence " << sents[s]->id() << " in " << docName << endl;
      }
      continue;
    }

    add_emph_inventory( data, emph );
    for ( unsigned int i=0; i <= data.size() - nG ; ++i ){
      string multiw;
      string multil;
      string multip;
      bool lem_mis = false;
      bool pos_mis = false;
      for ( size_t j=0; j < nG; ++j ){
	multiw += data[i+j].word;
	if ( data[i+j].lemma.empty() ){
	  lem_mis = true;
	}
	else if ( !lem_mis ){
	  multil += data[i+j].lemma;
	}
	if ( data[i+j].pos.empty() ){
	  pos_mis = true;
	}
	else if ( !pos_mis ){
	  multip += data[i+j].pos;
	}
	if ( j < nG-1 ){
	  multiw += sep;
	  if ( !lem_mis )
	    multil += sep;
	  if ( !pos_mis )
	    multip += sep;
	}
      }
      ++wordTotal;
      if ( !lem_mis ){
	++lemTotal;
      }
      if ( !pos_mis ){
	++posTotal;
      }
#pragma omp critical
      {
	++wc[lang][multiw];
      }

      if ( !multil.empty() ){
#pragma omp critical
	{
	  ++lc[lang][multil];
	}
      }
      if ( !multip.empty() ){
#pragma omp critical
	{
	  auto lpc0 = lpc[lang];
	  multimap<string, rec >::iterator it = lpc0.find(multil);
	  if ( it == lpc0.end() ){
	    rec tmp;
	    tmp.count = 1;
	    tmp.pc[multip]=1;
	    lpc[lang].insert( make_pair(multil,tmp) );
	  }
	  else {
	    ++it->second.count;
	    ++it->second.pc[multip];
	  }
	}
      }
    }
    if ( verbose && (lemTotal < wordTotal) ){
#pragma omp critical
      {
	cout << "info: " << wordTotal - lemTotal
	     << " lemma's are missing in "  << d->id() << endl;
      }
    }
  }
  if ( verbose && (posTotal < wordTotal) ){
#pragma omp critical
    {
      cout << "info: " << wordTotal - posTotal
	   << " POS tags are missing in "  << d->id() << endl;
    }
  }
  return wordTotal;
}

size_t doc_str_inventory( const Document *d,
			  const string& docName,
			  size_t nG,
			  bool lowercase,
			  const string& default_language,
			  const set<string>& languages,
			  map<string,map<string,unsigned int>>& wc,
			  set<string>& emph,
			  const string& sep ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "make a str inventory on:" << docName << endl;
    }
  }
  size_t wordTotal = 0;
  vector<String*> strings = d->doc()->select<String>();
  if ( verbose ){
#pragma omp critical
    {
      cout << "found " << strings.size() << " strings" << endl;
    }
  }
  if ( strings.size() < nG )
    return wordTotal;
  string lang = d->language();
  if ( languages.find( lang ) == languages.end() ){
    // lang is 'unwanted', just add to the default
    if ( default_language == "skip" ){
      return wordTotal;
    }
    lang = default_language;
  }
  vector<string> data;
  for ( const auto& s : strings ){
    UnicodeString us;
    try {
      us = s->text(classname);
      if ( lowercase ){
	us.toLower();
      }
    }
    catch(...){
#pragma omp critical
      {
	cerr << "FoLiA-stats: missing text for word " << s->id() << endl;
      }
      break;
    }
    string w = UnicodeToUTF8( us );
    data.push_back( w );
  }
  if ( data.size() != strings.size() ) {
#pragma omp critical
    {
      cerr << "FoLiA-stats: Missing words! skipped document " << docName << endl;
    }
    return 0;
  }

  add_emph_inventory( data, emph );
  wordTotal += add_word_inventory( data, wc[lang], nG, sep );
  return wordTotal;
}

size_t par_str_inventory( const Document *d, const string& docName,
			  size_t nG,
			  bool lowercase,
			  const string& default_language,
			  const set<string>& languages,
			  map<string,map<string,unsigned int>>& wc,
			  set<string>& emph,
			  const string& sep ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "make a par_str inventory on:" << docName << endl;
    }
  }
  size_t wordTotal = 0;
  vector<Paragraph*> pars = d->paragraphs();
  for ( const auto& p : pars ){
    vector<String*> strings = p->select<String>();
    if ( verbose ){
#pragma omp critical
      {
	cout << "found " << strings.size() << " strings" << endl;
      }
    }
    if ( strings.size() < nG )
      continue;

    string lang = p->language();
    if ( languages.find( lang ) == languages.end() ){
      // lang is 'unwanted', just add to the default
      if ( default_language == "skip" ){
	continue;
      }
      lang = default_language;
    }
    vector<string> data;
    for ( const auto& s : strings ){
      UnicodeString us;
      try {
	us = s->text(classname);
	if ( lowercase ){
	  us.toLower();
	}
      }
      catch(...){
#pragma omp critical
	{
	  cerr << "FoLiA-stats: missing text for word " << s->id() << endl;
	}
	  break;
      }
      string w = UnicodeToUTF8( us );
      data.push_back( w );
    }
    if ( data.size() != strings.size() ) {
#pragma omp critical
      {
	cerr << "FoLiA-stats: Missing words! skipped paragraph " << p->id() << " in " << docName << endl;
      }
      return 0;
    }

    add_emph_inventory( data, emph );
    wordTotal += add_word_inventory( data, wc[lang], nG, sep );
  }
  return wordTotal;
}

size_t par_text_inventory( const Document *d, const string& docName,
			   size_t nG,
			   bool lowercase,
			   const string& default_language,
			   const set<string>& languages,
			   map<string,map<string,unsigned int>>& wc,
			   set<string>& emph,
			   const string& sep ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "make a text_in_par inventory on:" << docName << endl;
    }
  }
  size_t wordTotal = 0;
  vector<Paragraph*> pars = d->paragraphs();
  for ( const auto& p : pars ){
    string lang = p->language(); // get the language the paragraph is in
    // totally ignore language annotation on embeded <s> and or <w> elements.
    if ( languages.find( lang ) == languages.end() ){
      // lang is 'unwanted', just add to the default
      if ( default_language == "skip" ){
	continue;
      }
      lang = default_language;
    }
    string s;
    UnicodeString us;
    try {
      us = p->text(classname);
      if ( lowercase ){
	us.toLower();
      }
      s = UnicodeToUTF8( us );
    }
    catch(...){
    }
    if ( s.empty() ){
      if ( verbose ){
#pragma omp critical
	{
	  cout << "found NO string in paragraph: " << p->id() << endl;
	}
      }
      continue;
    }
    vector<string> data;
    size_t num = TiCC::split( s, data );
    if ( verbose ){
#pragma omp critical
      {
	cout << "found string: '" << s << "'" << endl;
	if ( num <= 1 ){
	  cout << "with no substrings" << endl;
	}
	else {
	  using TiCC::operator<<;
	  cout << "with " << num << " substrings: " << data << endl;
	}
      }
    }
    add_emph_inventory( data, emph );
    wordTotal += add_word_inventory( data, wc[lang], nG, sep );
  }
  return wordTotal;
}

void usage( const string& name ){
  cerr << "Usage: " << name << " [options] file/dir" << endl;
  cerr << "\t\t FoLiA-stats will produce ngram statistics for a FoLiA file, " << endl;
  cerr << "\t\t or a whole directory of FoLiA files " << endl;
  cerr << "\t\t The output will be a 2 or 4 columned tab separated file, extension: *tsv " << endl;
  cerr << "\t\t\t (4 columns when -p is specified)" << endl;
  cerr << "\t--clip\t\t clipping factor. " << endl;
  cerr << "\t\t\t\t(entries with frequency <= this factor will be ignored). " << endl;
  cerr << "\t-p\t\t output percentages too. " << endl;
  cerr << "\t--lower\t\t Lowercase all words" << endl;
  cerr << "\t--underscore\t connect all words with underscores" << endl;
  cerr << "\t--lang\t\t Language. (default='none')." << endl;
  cerr << "\t--languages\t Lan1,Lan2,Lan3. (default='Lan1')." << endl;
  cerr << "\t\t\t Use 'skip' as Lan1 to ignore all languages not mentioned as Lan2,..." << endl;
  cerr << "\t--ngram\t\t Ngram count " << endl;
  cerr << "\t--mode='mode' Process text found like this: (default: 'word_in_sent')" << endl;
  //  cerr << "\t\t 'text_in_doc'" << endl;
  cerr << "\t\t 'text_in_par' Process text nodes per <p> node." << endl;
  //cerr << "\t\t 'text_in_sent'" << endl;
  cerr << "\t\t 'string_in_doc' Process <str> nodes per document." << endl;
  cerr << "\t\t 'string_in_par' Process <str> nodes per <p> node." << endl;
  //  cerr << "\t\t 'word_in_doc' Process <w> nodes per document." << endl;
  //  cerr << "\t\t 'word_in_par'" << endl;
  cerr << "\t\t 'word_in_sent' Process <w> nodes per <s> in the document." << endl;
  cerr << "\t-s\t\t equal to --mode=string_in_par" << endl;
  cerr << "\t-S\t\t equal to --mode=string_in_doc" << endl;
  cerr << "\t--class='name'\t When processing <str> nodes, use 'name' as the folia class for <t> nodes. (default is 'current')" << endl;
  cerr << "\t--hemp=<file>\t Create a historical emphasis file. " << endl;
  cerr << "\t\t\t (words consisting of single, space separated letters)" << endl;
  cerr << "\t-t\t\t number_of_threads" << endl;
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-v or --verbose\t very verbose output." << endl;
  cerr << "\t-V or --version\t show version " << endl;
  cerr << "\t-e\t\t expr: specify the expression all input files should match with." << endl;
  cerr << "\t-o\t\t name of the output file(s) prefix." << endl;
  cerr << "\t-R\t\t search the dirs recursively (when appropriate)." << endl;
}

int main( int argc, char *argv[] ){
  CL_Options opts( "hVvpe:t:o:RsS",
		   "class:,clip:,lang:,languages:,ngram:,lower,hemp:,underscore,help,version,mode:,verbose" );
  try {
    opts.init(argc,argv);
  }
  catch( OptionError& e ){
    cerr << "FoLiA-stats: " << e.what() << endl;
    usage(argv[0]);
    exit( EXIT_FAILURE );
  }
  string progname = opts.prog_name();
  if ( argc < 2 ){
    usage( progname );
    exit(EXIT_FAILURE);
  }
  int clip = 0;
  int nG = 1;
#ifdef HAVE_OPENMP
  int numThreads = 1;
#endif
  string expression;
  string outputPrefix;
  string value;
  set<string> languages;
  string default_language;
  if ( opts.extract('V') || opts.extract("version") ){
    cerr << "FoLiA-stats from " << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('h') || opts.extract("help") ){
    usage(progname);
    exit(EXIT_SUCCESS);
  }
  verbose = opts.extract( 'v' ) || opts.extract( "verbose" );
  bool dopercentage = opts.extract('p');
  bool lowercase = opts.extract("lower");
  string modes;
  opts.extract("mode", modes );
  Mode mode = W_IN_S;
  if ( !modes.empty() ){
    mode = stringToMode( modes );
    if ( mode == UNKNOWN ){
      cerr << "FoLiA-stats: unknown --mode " << modes << endl;
      return EXIT_FAILURE;
    }
  }
  else {
    mode = W_IN_S;
  }
  string hempName;
  opts.extract("hemp", hempName );
  bool recursiveDirs = opts.extract( 'R' );
  if ( opts.extract( 's' ) ) {
    if ( !modes.empty() ){
      cerr << "FoLiA-stats: old style -s option cannot be combined with --mode option" << endl;
      return EXIT_FAILURE;
    }
    else {
      mode = S_IN_P;
    }
  }
  if ( opts.extract( 'S' ) ){
    if ( !modes.empty() ){
      cerr << "FoLiA-stats: old style -S option cannot be combined with --mode option" << endl;
      return EXIT_FAILURE;
    }
    else {
      mode = S_IN_D;
    }
  }
  bool do_under = opts.extract( "underscore" );
  string sep = " ";
  if ( do_under ){
    sep = "_";
  }
  if ( !opts.extract( 'o', outputPrefix ) ){
    cerr << "FoLiA-stats: an output filename prefix is required. (-o option) " << endl;
    exit(EXIT_FAILURE);
  }
  if ( opts.extract("clip", value ) ){
    if ( !stringTo(value, clip ) ){
      cerr << "FoLiA-stats: illegal value for --clip (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  if ( opts.extract("ngram", value ) ){
    if ( !stringTo(value, nG ) ){
      cerr << "FoLiA-stats: illegal value for --ngram (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  if ( opts.extract('t', value ) ){
#ifdef HAVE_OPENMP
    if ( !stringTo(value, numThreads ) ){
      cerr << "FoLiA-stats: illegal value for -t (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
#else
    cerr << "FoLiA-stats: OpenMP support is missing. -t option is not supported" << endl;
    exit( EXIT_FAILURE );
#endif
  }
  if ( opts.extract("languages", value ) ){
    vector<string> parts;
    TiCC::split_at( value, parts, "," );
    if ( parts.size() < 1 ){
      cerr << "FoLiA-stats: unable to extract a default language from --languages option" << endl;
      exit( EXIT_FAILURE );
    }
    default_language = parts[0];
    for ( const auto& l : parts ){
      languages.insert(l);
    }
  }
  if ( opts.extract("lang", value ) ){
    if ( languages.size() > 0 ){
      cerr << "FoLiA-stats: --lang and --languages options conflict. Use only one!" << endl;
      exit( EXIT_FAILURE );
    }
    else {
      default_language = "skip";
      languages.insert( value );
    }
  }
  if ( languages.size() == 0 ){
    default_language = "none";
  }
  opts.extract('e', expression );
  opts.extract( "class", classname );
  if ( !opts.empty() ){
    cerr << "FoLiA-stats: unsupported options : " << opts.toString() << endl;
    usage(progname);
    exit(EXIT_FAILURE);
  }

#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
#endif

  vector<string> massOpts = opts.getMassOpts();
  if ( massOpts.empty() ){
    cerr << "FoLiA-stats: no file or dir specified!" << endl;
    exit(EXIT_FAILURE);
  }
  string name = massOpts[0];
  vector<string> fileNames = searchFilesMatch( name, expression, recursiveDirs );
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "FoLiA-stats: no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }

  string::size_type pos = outputPrefix.find( "." );
  if ( pos != string::npos && pos == outputPrefix.length()-1 ){
    // outputname ends with a .
    outputPrefix = outputPrefix.substr(0,pos);
  }
  pos = outputPrefix.find( "/" );
  if ( pos != string::npos && pos == outputPrefix.length()-1 ){
    // outputname ends with a /
    outputPrefix += "foliastats";
  }

  if ( toDo ){
    cout << "start processing of " << toDo << " files " << endl;
  }
  map<string,map<string,unsigned int>> wc;
  map<string,map<string,unsigned int>> lc;
  map<string,multimap<string, rec>> lpc;
  unsigned int wordTotal =0;
  unsigned int posTotal =0;
  unsigned int lemTotal =0;
  set<string> emph;
  int doc_counter = toDo;
#pragma omp parallel for shared(fileNames,wordTotal,posTotal,lemTotal,wc,lc,lpc,emph)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    string docName = fileNames[fn];
    Document *d = 0;
    try {
      d = new Document( "file='"+ docName + "'" );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "FoLiA-stats: failed to load document '" << docName << "'" << endl;
	cerr << "FoLiA-stats: reason: " << e.what() << endl;
      }
      continue;
    }
    unsigned int word_count = 0;
    unsigned int lem_count = 0;
    unsigned int pos_count = 0;
    switch ( mode ){
    case S_IN_P:
      word_count = par_str_inventory( d, docName, nG,
				      lowercase, default_language, languages,
				      wc, emph, sep );
      break;
    case T_IN_P:
      word_count = par_text_inventory( d, docName, nG, lowercase,
				       default_language, languages,
				       wc, emph, sep );
      break;
    case S_IN_D:
      word_count = doc_str_inventory( d, docName, nG, lowercase,
				      default_language, languages, wc,
				      emph, sep );
      break;
    case W_IN_S:
      word_count = doc_sent_word_inventory( d, docName, nG, lowercase,
					    default_language, languages,
					    wc, lc, lpc, lem_count,
					    pos_count, emph, sep );
      break;
    default:
      cerr << "FoLiA-stats: not yet implemented mode: " << modes << endl;
      exit( EXIT_FAILURE );
    }
#pragma omp critical
    {
      wordTotal += word_count;
      lemTotal += lem_count;
      posTotal += pos_count;
      cout << "Processed :" << docName << " with " << word_count << " "
	   << nG << "-grams,"
	   << " " << lem_count << " lemmas, and " << pos_count << " POS tags."
	   << " still " << --doc_counter << " files to go." << endl;
    }
    delete d;
  }

  if ( toDo ){
    cout << "done processsing directory '" << name << "'" << endl;
  }
  if ( !hempName.empty() ){
    ofstream out( hempName );
    if ( out ){
      for( auto const& it : emph ){
	out << it << endl;
      }
      cout << "historical emphasis stored in: " << hempName << endl;
    }
    else {
      cerr << "FoLiA-stats: unable to create historical emphasis file: " << hempName << endl;
    }
  }
  cout << "start calculating the results" << endl;
  cout << "in total " << wordTotal << " " << nG << "-grams were found.";
  if ( toDo > 1 ){
    cout << "in " << toDo << " FoLiA documents.";
  }
  cout << endl;

#pragma omp parallel sections
  {
#pragma omp section
    {
      string filename;
      filename = outputPrefix + ".wordfreqlist";
      create_wf_list( wc, filename, wordTotal, clip, nG, dopercentage );
    }
#pragma omp section
    {
      if ( !( mode == S_IN_P || mode == S_IN_D ) ){
	string filename;
	filename = outputPrefix + ".lemmafreqlist";
	create_lf_list( lc, filename, lemTotal, clip, nG, dopercentage );
      }
    }
#pragma omp section
    {
      if ( !( mode == S_IN_P || mode == S_IN_D ) ){
	string filename;
	filename = outputPrefix + ".lemmaposfreqlist";
	create_lpf_list( lpc, filename, posTotal, clip, nG, dopercentage );
      }
    }
  }

}
