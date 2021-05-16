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

#include <cstdio>
#include <cassert>
#include <string>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iostream>
#include <fstream>

#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/Unicode.h"
#include "ticcutils/Timer.h"
#include "libfolia/folia.h"
#include "foliautils/common_code.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;
using namespace	folia;
using TiCC::operator<<;

const char SEPCHAR = '_';
const string SEPARATOR = "_";

int verbose = 0;
string input_classname = "current";
string safe_inputclass = input_classname;
string output_classname = "Ticcl";
string rebase_inputclass;
string setname = "Ticcl-set";
string punct_sep = " ";
size_t ngram_size = 1;

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

class gram_r {
  friend vector<gram_r> extract_bigrams( const vector<gram_r>&  );
  friend vector<gram_r> extract_trigrams( const vector<gram_r>&  );
  friend gram_r extract_last_bigram( const vector<gram_r>&  );
  friend ostream& operator<<( ostream& os, const gram_r& );
  friend ostream& operator<<( ostream& os, const gram_r* );
public:
  gram_r( const string&, FoliaElement* );
  explicit gram_r( const string& s ): gram_r(s,0){};
  FoliaElement *word( size_t index ) const {
    return _words[index];
  }
  string orig_text() const;
  string result_text() const;
  int correct_one_trigram( const unordered_map<string,vector<word_conf> >&,
			   const unordered_set<string>&,
			   const unordered_map<string,string>&,
			   unordered_map<string,size_t>&,
			   size_t&,
			   const processor *);
  int correct_one_bigram( const unordered_map<string,vector<word_conf> >&,
			  const unordered_set<string>&,
			  const unordered_map<string,string>&,
			  unordered_map<string,size_t>&,
			  size_t&,
			  const processor * );
  void correct_one_unigram( const unordered_map<string,vector<word_conf> >&,
			    const unordered_set<string>&,
			    const unordered_map<string,string>&,
			    unordered_map<string,size_t>&,
			    size_t&,
			    const processor * );
private:
  void clear(){ _orig.clear(); _result.clear();_words.clear(); };
  bool has_folia() const { return (!_words.empty() && _words[0]); };
  string set_ed_type();
  void apply_folia_correction( size_t&, const processor * ) const;
  void set_output_text( size_t& ) const;
  string _ed_type;
  string _final_punct;
  vector<string> _orig;
  vector<string> _result;
  vector<FoliaElement*> _words;
  const vector<word_conf>* _suggestions;
};

string gram_r::orig_text() const {
  string result;
  for ( const auto& s : _orig ){
    result += s;
    if ( &s != &_orig.back() ){
      result += "_";
    }
  }
  return result;
}

string gram_r::result_text() const {
  string result;
  for ( const auto& s : _result ){
    result += s;
    if ( &s != &_result.back() ){
      result += " ";
    }
  }
  if ( !_final_punct.empty() ){
    result += punct_sep + _final_punct;
  }
  return result;
}

gram_r::gram_r( const string& val, FoliaElement *el ) :
  _suggestions(0)
{
  _orig.push_back( val );
  //  cerr << "create gram_r: " << el->xmlstring() << endl;
  _words.push_back( el );
}

size_t unicode_size( const string& value ){
  UnicodeString us = TiCC::UnicodeFromUTF8(value);
  return us.length();
}

void gram_r::set_output_text( size_t& offset ) const {
  // cerr << "SET TEXT on: " << _words[0]->xmlstring() << endl;
  // cerr << "SET TEXT: " << orig_text() << endl;
  // cerr << "SET TEXT class: " << output_classname << endl;
  _words[0]->settext( orig_text(), offset, output_classname );
  offset += unicode_size(orig_text()) + 1;
}

ostream& operator<<( ostream& os, const gram_r& rec ){
  os << rec.orig_text();
  if ( !rec._result.empty() ){
    os << " (" << rec._ed_type << ") ==> " << rec.result_text();
  }
  if ( rec._words[0] != 0 ){
    os << " " << rec._words;
  }
  return os;
}

ostream& operator<<( ostream& os, const gram_r *rec ){
  os << *rec;
  return os;
}

vector<gram_r> extract_bigrams( const vector<gram_r>& unigrams ){
  vector<gram_r> result = unigrams;
  result.pop_back();
  for ( size_t i=0; i < unigrams.size()-1; ++i ){
    result[i]._words.push_back( unigrams[i+1]._words[0] );
    result[i]._orig.push_back( unigrams[i+1].orig_text() );
  }
  return result;
}

vector<gram_r> extract_trigrams( const vector<gram_r>& unigrams ){
  vector<gram_r> result = unigrams;
  result.pop_back();
  result.pop_back();
  for ( size_t i=0; i < unigrams.size()-2; ++i ){
    result[i]._words.push_back( unigrams[i+1]._words[0] );
    result[i]._orig.push_back( unigrams[i+1].orig_text() );
    result[i]._words.push_back( unigrams[i+2]._words[0] );
    result[i]._orig.push_back( unigrams[i+2].orig_text() );
  }
  return result;
}

gram_r extract_last_bigram( const vector<gram_r>& unigrams ){
  gram_r result = unigrams[unigrams.size()-2];
  gram_r last = unigrams[unigrams.size()-1];
  result._orig.push_back( last.orig_text() );
  result._words.push_back( last._words[0] );
  return result;
}

bool fillVariants( const string& fn,
		   unordered_map<string,vector<word_conf> >& variants,
		   size_t numSugg ){
  ifstream is( fn );
  string line;
  string current_word;
  vector<word_conf> vec;
  while ( getline( is, line ) ) {
    vector<string> parts = TiCC::split_at( line, "#" );
    if ( parts.size() == 6 // normal ranking
	 || parts.size() == 7 // chained ranking
	 ){
      string word = parts[0];
      size_t seps = count(word.begin(),word.end(),SEPCHAR);
      if ( seps >= ngram_size ){
	// skip 'too long' n-gram words
	continue;
      }
      if ( current_word.empty() )
	current_word = word;

      if ( word != current_word ){
	// finish previous word
	if ( vec.size() > 0 ){
	  if ( vec.size() > numSugg ){
	    vec.resize( numSugg );
	  }
	  variants[current_word] = vec;
	  vec.clear();
	}
	current_word = word;
      }
      string trans = parts[2];
      seps = count(trans.begin(),trans.end(),SEPCHAR);
      if ( seps >= ngram_size ){
	// skip 'too long' n-gram variants
	continue;
      }
      string confS;
      if ( parts.size() == 7 ){
	confS = "1.0";
      }
      else {
	confS = parts[5];
	double d;
	if ( !TiCC::stringTo<double>( confS, d ) ){
	  confS = "1.0";
	}
      }
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
  }
  return !variants.empty();
}

bool fillUnknowns( const string& fn, unordered_set<string>& unknowns ){
  ifstream is( fn );
  string line;
  while ( getline( is, line ) ) {
    vector<string> parts = TiCC::split( line );
    if ( parts.size() == 2 ){
      UnicodeString us( parts[0].c_str() );
      if ( us.length() > 1 ){
	// '1' character words are never UNK
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

bool fillPuncts( const string& fn, unordered_map<string,string>& puncts ){
  ifstream is( fn );
  string line;
  while ( getline( is, line ) ) {
    vector<string> parts = TiCC::split( line );
    if ( parts.size() == 2 ){
      puncts[parts[0]] = parts[1];
    }
    else {
      cerr << "error reading punct value from line " << line << endl;
    }
  }
  return !puncts.empty();
}

void filter( string& word, char c ){
  for ( auto& w : word ){
    if ( w == c )
      w = '.';
  }
}

void filter( string& word ){
  filter( word, '#' );
}

bool solve_punctuation( string& word,
			const unordered_map<string,string>& puncts,
			string& final ){
  const string real_puncts = ".,;!?:";
  final.clear();
  const auto pit = puncts.find( word );
  bool result = false;
  if ( pit != puncts.end() ){
    result = true;
    string new_word = pit->second;
    //    if ( word.find(new_word) != string::npos ){
    if ( real_puncts.find(word.back()) != string::npos
	 && new_word.back() != word.back() ){
      final = word.back();
    }
    //    }
    word = new_word;
  }
  return result;
}

string gram_r::set_ed_type() {
  size_t o_s = _orig.size();
  size_t r_s = _result.size();
  _ed_type = TiCC::toString(o_s) + "-" + TiCC::toString(r_s);
  return _ed_type;
}

void gram_r::apply_folia_correction( size_t& offset,
				     const processor *proc ) const {
  if ( has_folia() ){
    bool doStrings = _words[0]->xmltag() == "str";
    string org_set = _words[0]->sett();
    string what;
    if ( _ed_type == "1-1"
	 || _ed_type == "2-2"
	 || _ed_type == "3-3" ){
      what = "edit";
    }
    else if ( _ed_type == "2-1"
	      || _ed_type == "3-1"
	      || _ed_type == "3-2" ){
      what = "merge";
    }
    else {
      what = "split";
    }
    if ( verbose > 3 ){
      cerr << what << " ngram: " << this << endl;
    }
    vector<FoliaElement*> sV;
    vector<FoliaElement*> cV;
    vector<FoliaElement*> oV;
    vector<FoliaElement*> nV;
    for( const auto& it : _words ){
      // Original elements
      oV.push_back( it );
    }
    for ( const auto& p : _result ){
      // New elements
      KWargs args;
      args["xml:id"] = _words[0]->generateId( what );
      if ( org_set != "None" ){
	args["set"] = org_set;
      }
      args["processor"] = proc->id();
      FoliaElement *el = 0;
      if ( doStrings ){
	el = new String( args, _words[0]->doc() );
      }
      else {
	el = new Word( args, _words[0]->doc() );
      }
      el->settext( p, offset, output_classname );
      offset += unicode_size(p) + 1;
      nV.push_back( el );
    }
    if ( !_final_punct.empty() ){
      // A final punct is an extra New element
      KWargs args;
      args["xml:id"] = _words[0]->generateId( "split" );
      if ( org_set != "None" ){
	args["set"] = org_set;
      }
      args["processor"] = proc->id();
      FoliaElement *el = 0;
      if ( doStrings ){
	el = new String( args, _words[0]->doc() );
      }
      else {
	el = new Word( args, _words[0]->doc() );
      }
      el->settext( _final_punct, offset, output_classname );
      offset += unicode_size(_final_punct) + 1;
      nV.push_back( el );
    }
    if ( _suggestions && _suggestions->size() > 1 ){
      // Suggestion elements
      size_t limit = _suggestions->size();
      for( size_t j=0; j < limit; ++j ){
	KWargs sargs;
	sargs["confidence"] = (*_suggestions)[j].conf;
	sargs["n"]= TiCC::toString(j+1) + "/" + TiCC::toString(limit);
	Suggestion *sug = new Suggestion( sargs );
	sV.push_back( sug );
	vector<string> parts = TiCC::split_at( (*_suggestions)[j].word,
					       SEPARATOR );
	for ( const auto& s : parts ){
	  KWargs wargs;
	  wargs["xml:id"] = _words[0]->generateId( "suggestion" );
	  if ( org_set != "None" ){
	    wargs["set"] = org_set;
	  }
	  wargs["processor"] = proc->id();
	  FoliaElement *elt;
	  if ( doStrings ){
	    elt = sug->add_child<String>( wargs );
	  }
	  else {
	    elt = sug->add_child<Word>( wargs );
	  }
	  elt->settext( s, output_classname );
	}
      }
    }
    KWargs cor_args;
    cor_args["processor"] = proc->id();
    Correction *c = _words[0]->parent()->correct( oV, cV, nV, sV, cor_args );
    if ( verbose > 3 ){
      cerr << "created: " << c->xmlstring() << endl;
    }
    else if ( verbose > 1 ){
      cerr << "created: " << c << endl;
    }
  }
}

void gram_r::correct_one_unigram( const unordered_map<string,vector<word_conf> >& variants,
				  const unordered_set<string>& unknowns,
				  const unordered_map<string,string>& puncts,
				  unordered_map<string,size_t>& counts,
				  size_t& offset,
				  const processor *proc ){
  bool did_edit = false;
  if ( verbose > 2 ){
    cout << "correct unigram " << this << endl;
  }
  string word = orig_text();
  string orig_word = word;
  string final_punct;
  bool is_punct = false;
  if ( ngram_size > 1 ){
    is_punct = solve_punctuation( word, puncts, final_punct );
    if ( is_punct ){
      if ( verbose > 2 ){
	cout << "punctuated word found, final='" << final_punct << "'" << endl;
	cout << "depuncted word   : " << word << endl;
      }
    }
  }
  const auto vit = variants.find( word );
  if ( vit != variants.end() ){
    // 1 or more edits found
    _suggestions = &vit->second;
    _result.clear();
    // edit might be seperatable!
    string edit = vit->second[0].word;
    vector<string> parts = TiCC::split_at( edit, SEPARATOR );
    for ( const auto& p : parts ){
      _result.push_back( p );
    }
    _final_punct = final_punct;
    string ed = set_ed_type( );
    ++counts[ed];
    if ( verbose > 1 ){
      cout << word << " = " << ed << " => " << result_text() << endl;
    }
    did_edit = true;
  }
  else {
    // a word with no suggested variants
    _result.clear();
    auto uit = unknowns.find( word );
    if ( uit == unknowns.end() ){
      uit = unknowns.find( orig_word );
    }
    if ( uit != unknowns.end() ){
      // ok it is a registrated garbage word
      _result.push_back( "UNK" );
      set_ed_type( );
      ++counts["UNK"];
      did_edit = true;
    }
    else if ( is_punct ){
      _result.push_back( word );
      _final_punct = final_punct;
      did_edit = true;
    }
    else {
      _result.push_back( orig_word );
    }
  }
  if ( did_edit ){
    if ( verbose ){
      cout << "correction: " << endl;
      cout << this << endl;
    }
    apply_folia_correction( offset, proc );
  }
  else {
    // NO edit just take the string
    if ( has_folia() ){
      set_output_text( offset );
    }
  }
}

string correct_unigrams( const vector<gram_r>& unigrams,
			 const unordered_map<string,vector<word_conf> >& variants,
			 const unordered_set<string>& unknowns,
			 const unordered_map<string,string>& puncts,
			 unordered_map<string,size_t>& counts,
			 const processor *proc ){
  if ( verbose > 1 ){
    cout << "correct unigrams" << endl;
  }
  string result;
  size_t offset = 0;
  for ( auto uni : unigrams ){
    uni.correct_one_unigram( variants, unknowns,
			     puncts, counts, offset, proc );
    result += uni.result_text() + " ";
  }
  if ( verbose > 2 ){
    cout << "corrected=" << result << endl;
  }
  return result;
}

int gram_r::correct_one_bigram( const unordered_map<string,vector<word_conf> >& variants,
				const unordered_set<string>& unknowns,
				const unordered_map<string,string>& puncts,
				unordered_map<string,size_t>& counts,
				size_t& offset,
				const processor *proc ){
  int extra_skip = 0;
  if ( verbose > 2 ){
    cout << "correct bigram " << this << endl;
  }
  string word = orig_text();
  filter(word);
  string orig_word = word;
  string final_punct;
  if ( ngram_size > 1 ){
    bool is_punct = solve_punctuation( word, puncts, final_punct );
    if ( is_punct ){
      if ( verbose > 2 ){
	cout << "punctuated word found, final='" << final_punct << "'" << endl;
	cout << "depuncted word   : " << word << endl;
      }
    }
  }
  const auto vit = variants.find( word );
  if ( vit != variants.end() ){
    // edits found
    _suggestions = &vit->second;
    string edit = vit->second[0].word;
    vector<string> parts = TiCC::split_at( edit, SEPARATOR ); // edit can be unseperated!
    _result.clear();
    for ( const auto& p : parts ){
      _result.push_back( p );
    }
    _final_punct = final_punct;
    string ed = set_ed_type();
    ++counts[ed];
    if ( verbose > 1 ){
      cout << orig_word << " = " << ed << " => " << result_text() << endl;
    }
    extra_skip = 1;
    if ( verbose ){
      cout << "correction: " << endl;
      cout << this << endl;
    }
    apply_folia_correction( offset, proc );
  }
  else {
    // a bigram with no suggested variants
    auto uit = unknowns.find( word );
    if ( uit == unknowns.end() ){
      uit = unknowns.find( orig_word );
    }
    if ( uit != unknowns.end() ){
      // ok it is a registrated garbage bigram
      _result.clear();
      _result.push_back( "UNK" );
      _result.push_back( "UNK" );
      ++counts["UNK UNK"];
      set_ed_type( );
      if ( verbose > 2 ){
	cout << orig_text() << " ==> " << result_text() << endl;
      }
      extra_skip = 1;
      if ( verbose ){
	cout << "correction: " << endl;
	cout << this << endl;
      }
      apply_folia_correction( offset, proc );
    }
    else {
      // just use the ORIGINAL bigram and handle the first part like unigram
      if ( verbose > 1 ){
	cout << "no correction for bigram: " << this << endl;
      }
      _orig.pop_back();
      _words.pop_back();
      _final_punct.clear();
      if ( verbose > 1 ){
	cout << "try unigram: " << this << endl;
      }
      correct_one_unigram( variants, unknowns,
			   puncts, counts, offset,
			   proc );
    }
  }
  if ( verbose > 1 ){
    cout << orig_text() << " = 2 => " << result_text() << endl;
  }
  return extra_skip;
}

string correct_bigrams( const vector<gram_r>& bigrams,
			const unordered_map<string,vector<word_conf> >& variants,
			const unordered_set<string>& unknowns,
			const unordered_map<string,string>& puncts,
			gram_r& last,
			unordered_map<string,size_t>& counts,
			const processor *proc ){
  if ( verbose > 1 ){
    cout << "correct bigrams" << endl;
  }
  string result;
  int skip = 0;
  size_t offset = 0;
  for ( auto bi : bigrams ){
    if ( verbose > 1 ){
      cout << "bigram is: '" << bi << "'" << endl;
    }
    if ( skip > 0 ){
      --skip;
      continue;
    }
    if ( verbose > 2 ){
      cout << "before correct_one_bi: bi=" << bi << endl;
    }
    skip = bi.correct_one_bigram( variants, unknowns,
				  puncts, counts, offset, proc );
    if ( verbose > 2 ){
      cout << "After correct_one_bi: cor=" << bi << endl;
    }
    result += bi.result_text() + " ";
  }
  if ( skip == 0 ){
    last.correct_one_unigram( variants, unknowns,
			      puncts, counts, offset, proc );
    if ( verbose > 2 ){
      cout << "handled last word: " << last << endl;
    }
    result += last.result_text();
  }
  return result;
}

int gram_r::correct_one_trigram( const unordered_map<string,vector<word_conf> >& variants,
				 const unordered_set<string>& unknowns,
				 const unordered_map<string,string>& puncts,
				 unordered_map<string,size_t>& counts,
				 size_t& offset,
				 const processor *proc ){
  int extra_skip = 0;
  if ( verbose > 2 ){
    cout << "correct trigram " << orig_text() << endl;
  }
  string word = orig_text();
  filter(word);
  string orig_word = word;
  string final_punct;
  if ( ngram_size > 1 ){
    bool is_punct = solve_punctuation( word, puncts, _final_punct );
    if ( is_punct ){
      if ( verbose > 2 ){
	cout << "punctuated word found, final='" << final_punct << "'" << endl;
	cout << "depuncted word   : " << word << endl;
      }
    }
  }
  const auto vit = variants.find( word );
  if ( vit != variants.end() ){
    // edits found
    string edit = vit->second[0].word;
    vector<string> parts = TiCC::split_at( edit, SEPARATOR ); // edit can can be unseperated!
    _suggestions = &vit->second;
    _result.clear();
    for ( const auto& p : parts ){
      _result.push_back( p );
    }
    string ed = set_ed_type();
    ++counts[ed];
    if ( verbose > 1 ){
      cout << word << " = " << ed << " => " << result_text() << endl;
    }
    extra_skip = 2;
    if ( verbose ){
      cout << "correction: " << endl;
      cout << this << endl;
    }
    apply_folia_correction( offset, proc );
  }
  else {
    // a word with no suggested variants
    auto uit = unknowns.find( word );
    if ( uit == unknowns.end() ){
      uit = unknowns.find( orig_word );
    }
    if ( uit != unknowns.end() ){
      // ok it is a registrated garbage trigram
      _result.clear();
      _result.push_back("UNK" );
      _result.push_back("UNK" );
      _result.push_back("UNK" );
      ++counts["UNK UNK UNK"];
      set_ed_type( );
      extra_skip = 2;
      apply_folia_correction( offset, proc );
    }
    else {
      if ( verbose > 1 ){
	cout << "no correction for trigram: " << this << endl;
      }
      // just use the ORIGINAL word so handle the first part like bigram
      _orig.clear();
      vector<string> parts = TiCC::split_at( orig_word, SEPARATOR );
      _orig.push_back( parts[0] );
      _orig.push_back( parts[1] );
      _words.pop_back();
      _final_punct.clear();
      if ( verbose > 1 ){
	cout << "try bigram: " << this << endl;
      }
      extra_skip = correct_one_bigram( variants, unknowns,
				       puncts, counts,
				       offset, proc );
    }
  }
  if ( verbose > 1 ){
    cout << orig_text() << " = 3 => " << result_text()
	 << " extra_skip=" << extra_skip << endl;
  }
  return extra_skip;
}

string correct_trigrams( const vector<gram_r>& trigrams,
			 const unordered_map<string,vector<word_conf> >& variants,
			 const unordered_set<string>& unknowns,
			 const unordered_map<string,string>& puncts,
			 const vector<gram_r>& unigrams,
			 unordered_map<string,size_t>& counts,
			 const processor *proc ){
  if ( verbose > 1 ){
    cout << "correct trigrams" << endl;
  }
  string result;
  int skip = 0;
  size_t offset = 0;
  for ( auto tri : trigrams ){
    if ( skip > 0 ){
      --skip;
      continue;
    }
    if ( verbose > 1 ){
      cout << "trigram is: '" << tri.orig_text() << "'" << endl;
    }
    skip = tri.correct_one_trigram( variants, unknowns,
				    puncts, counts, offset, proc );
    result += tri.result_text() + " ";
    if ( verbose > 2 ){
      cout << "skip=" << skip  << " intermediate:" << result << endl;
    }
  }
  if ( skip > 1 ){
    return result;
  }
  else if ( skip == 1 ){
    gram_r last = unigrams.back();
    last.correct_one_unigram( variants, unknowns,
			      puncts, counts, offset, proc );
    if ( verbose > 2 ){
      cout << "handled last word: " << last << endl;
    }
    result += last.result_text();
    return result;
  }
  else {
    gram_r last_bi = extract_last_bigram( unigrams );
    if ( verbose > 2 ){
      cout << "correct last bigram: " << last_bi << endl;
    }
    skip = last_bi.correct_one_bigram( variants, unknowns,
				       puncts, counts,
				       offset, proc );
    if ( verbose > 2 ){
      cout << "handled last bigram: " << last_bi << endl;
    }
    result += last_bi.result_text();
    if ( skip == 0 ){
      gram_r last = unigrams[unigrams.size()-1];
      if ( verbose > 2 ){
	cout << "correct last word: " << last << endl;
      }
      last.correct_one_unigram( variants, unknowns,
				puncts, counts, offset, proc );
      if ( verbose > 2 ){
	cout << "handled last unigram: " << last << endl;
      }
      result += " " + last.result_text();
    }
    return result;
  }
}

//#define HEMP_DEBUG

void add_to_result( vector<gram_r>& result,
		    const string& mw,
		    const vector<pair<hemp_status,FoliaElement*>>& inventory,
		    const size_t last ){
  vector<string> parts = TiCC::split_at( mw, "_" );
  size_t j = parts.size();
  size_t index = last+1-j;
  if ( verbose > 4 ){
    cerr << "add to result: " << parts << endl;
    cerr << "LAST=" << last << endl;
    cerr << "index=" << index << endl;
    cerr << "IN result: " << result << endl;
  }
  for ( const auto& p :parts ){
    if ( verbose > 4 ){
      cerr << "index=" << index << endl;
      cerr << "inventory[" << index << "]= " << inventory[index] << endl;
    }
    result.push_back( gram_r(p,inventory[index].second) );
    ++index;
  }
  if ( verbose > 4 ){
    cerr << "OUT result: " << result << endl;
  }
}

vector<gram_r> replace_hemps( const vector<gram_r>& unigrams,
			      vector<pair<hemp_status,FoliaElement*>> inventory,
			      const unordered_map<string,string>& puncts ){
  vector<gram_r> result;
  result.reserve(unigrams.size() );
  string mw;
  for ( size_t i=0; i < unigrams.size(); ++i ){
    if ( verbose > 4 ){
      cerr << "i=" << i << "/" << unigrams.size()-1 << " status = " << inventory[i] << " MW='" << mw << "'" << endl;
    }
    if ( inventory[i].first == NO_HEMP ){
      if ( !mw.empty() ){
	mw.pop_back(); // remove last '_'
	const auto& it = puncts.find( mw );
	if ( it != puncts.end() ){
	  result.push_back( gram_r(it->second, unigrams[i].word(0) ) );
	}
	else {
	  if ( verbose > 4 ){
	    cerr << "VOOR add to result 1: " << inventory[i] << endl;
	  }
	  add_to_result( result, mw, inventory, i-1 );
	}
	mw = "";
      }
      result.push_back( unigrams[i] );
    }
    else if ( inventory[i].first == END_PUNCT_HEMP ){
      mw += unigrams[i].orig_text();
      const auto& it = puncts.find( mw );
      if ( it != puncts.end() ){
	result.push_back( gram_r(it->second,unigrams[i].word(0) ) );
      }
      else {
	if ( verbose > 4 ){
	  cerr << "VOOR add to result 2: " << inventory[i] << endl;
	}
	add_to_result( result, mw, inventory, i );
      }
      mw = "";
    }
    else if ( inventory[i].first == START_PUNCT_HEMP ){
      if ( !mw.empty() ){
	mw.pop_back(); //  remove last '_'
	const auto& it = puncts.find( mw );
	if ( it != puncts.end() ){
	  result.push_back( gram_r(it->second,unigrams[i].word(0) ) );
	}
	else {
	  if ( verbose > 4 ){
	    cerr << "VOOR add to result 3: " << inventory[i] << endl;
	  }
	  add_to_result( result, mw, inventory, i-1 );
	}
      }
      mw = unigrams[i].orig_text() + "_";
    }
    else if ( inventory[i].first == NORMAL_HEMP ){
      mw += unigrams[i].orig_text() + "_";
    }
    if ( verbose > 4 ){
      cerr << "   result=" << result << endl;
    }
  }
  if ( !mw.empty() ){
    // leftovers
    mw.pop_back(); //  remove last '_'
    const auto& it = puncts.find( mw );
    if ( it != puncts.end() ){
      result.push_back( gram_r(it->second,unigrams.back().word(0)) );
    }
    else {
      if ( verbose > 4 ){
	cerr << "VOOR add to result 4: " << inventory[unigrams.size()-1] << endl;
      }
      add_to_result( result, mw, inventory, unigrams.size()-1 );
    }
  }
  if ( verbose > 4 ){
    cerr << " FINAL result=" << result << endl;
  }
  return result;
}

vector<gram_r> replace_hemps( const vector<gram_r>& unigrams,
			      const unordered_map<string,string>& puncts ){
  if ( verbose > 4 ){
    cout << "replace HEMPS in UNIGRAMS:\n" << unigrams << endl;
  }
  vector<UnicodeString> u_uni( unigrams.size() );
  for ( size_t i=0; i < unigrams.size(); ++i ){
    u_uni[i] = TiCC::UnicodeFromUTF8(unigrams[i].orig_text());
  }
  vector<hemp_status> hemp_inventory = create_emph_inventory( u_uni );
  if ( verbose > 4 ){
    cerr << "unigrams, size=" << unigrams.size() << endl;
    cerr << "hemp inventory, size=" << hemp_inventory.size() << endl;
    cerr << "hemp inventory: " << hemp_inventory << endl;
  }
  vector<pair<hemp_status,FoliaElement*>> inventory;
  for ( size_t i=0; i < unigrams.size(); ++i ){
    inventory.push_back(make_pair(hemp_inventory[i],unigrams[i].word(0)));
  }
  if ( verbose > 4 ){
    cerr << "PAIRED inventory " << inventory << endl;
  }
  vector<gram_r> result = replace_hemps( unigrams, inventory, puncts );
  if ( verbose > 4 ){
    cout << "replace HEMPS out UNIGRAMS:\n" << result << endl;
  }
  return result;
}

//#define TEST_HEMP

void correctNgrams( FoliaElement* par,
		    const unordered_map<string,vector<word_conf> >& variants,
		    const unordered_set<string>& unknowns,
		    const unordered_map<string,string>& puncts,
		    unordered_map<string,size_t>& counts,
		    processor *proc ){
  vector<FoliaElement*> ev;
  vector<Word*> wv = par->select<Word>();
  if ( wv.size() > 0 ){
    ev.resize(wv.size());
    copy( wv.begin(), wv.end(), ev.begin() );
  }
  else {
    vector<String*> sv = par->select<String>();
    if ( sv.size() > 0 ){
      ev.resize(sv.size());
      copy( sv.begin(), sv.end(), ev.begin() );
    }
  }

  vector<gram_r> unigrams;
#ifdef TEST_HEMP
  vector<string> grams = {"Als","N","A","P","O","L","E","O","N",")A",
			  "aan","(N","A","P","O","L","E","O","N)","EX",
			  "voor","N","A","P","O","L","E","O","toch?",
			  "tegen","P","Q.","zeker"};
  for( const auto& gr : grams ){
    unigrams.push_back( gram_r(gr) );
  }
  cout << "old_uni: " << unigrams << endl;
#else
  string inval;
  if ( ev.size() == 0 ){
    vector<TextContent *> origV = par->select<TextContent>(false);
    if ( origV.empty() ){
      // OK, no text directly
      // look deeper then
      origV = par->select<TextContent>();
      if ( origV.empty() ){
	// still nothing...
#pragma omp critical
	{
	  cerr << "no text Words or Strings in : " << par->id()
	       << " skipping" << endl;
	}
	return;
      }
    }

    for( const auto& it : origV ){
      string content = it->str(input_classname);
      filter( content, SEPCHAR ); // HACK
      inval += content + " ";
      vector<string> parts = TiCC::split( content );
      for ( const auto& p : parts ){
	unigrams.push_back( gram_r(p) );
      }
    }
  }
  else {
    for ( const auto& it : ev ){
      string content = it->str(input_classname);
      filter( content, SEPCHAR ); // HACK
      inval += content + " ";
      unigrams.push_back( gram_r( content, it ) );
    }
  }
  inval = TiCC::trim( inval );
  if ( verbose > 1 ){
#pragma omp critical
    {
      cout << "\n   correct " << ngram_size << "-grams in: '" << inval
	   << "' (" << input_classname
	   << ")" << endl;
    }
  }
#endif
  unigrams = replace_hemps( unigrams, puncts );

  vector<gram_r> bigrams;
  vector<gram_r> trigrams;
  counts["TOKENS"] += unigrams.size();
  if ( ngram_size > 1  && unigrams.size() > 1 ){
    bigrams = extract_bigrams( unigrams );
    if ( verbose > 3 ){
      cout << "BIGRAMS:\n" << bigrams << endl;
    }
  }
  if ( ngram_size > 2 && unigrams.size() > 2 ){
    trigrams = extract_trigrams( unigrams );
    if ( verbose > 5 ){
      cout << "TRIGRAMS:\n" << trigrams << endl;
    }
  }
  string corrected;
  if ( trigrams.empty() ){
    if ( bigrams.empty() ){
      corrected = correct_unigrams( unigrams, variants, unknowns,
				    puncts, counts, proc );
    }
    else {
      corrected = correct_bigrams( bigrams, variants, unknowns,
				   puncts, unigrams.back(), counts, proc );
    }
  }
  else {
    corrected = correct_trigrams( trigrams, variants, unknowns,
				  puncts, unigrams, counts, proc );
  }
  corrected = TiCC::trim( corrected );
  if ( verbose > 1 ){
#pragma omp critical
    {
      cout << "corrected " << ngram_size << "-grams uit: '"
	   << corrected << "' (" << output_classname << ")" << endl;
    }
  }
  if ( !corrected.empty() ){
    //    cerr << "set text on " << par << endl;
    //    cerr << par->str(output_classname) << endl;
    // cerr << par->data()[1]->str(output_classname) << endl;
    // cerr << par->data()[8]->str(output_classname) << endl;
    // cerr << par->data()[88]->str(output_classname) << endl;
    par->settext( corrected, output_classname );
  }
}

bool correctDoc( Document *doc,
		 const unordered_map<string,vector<word_conf> >& variants,
		 const unordered_set<string>& unknowns,
		 const unordered_map<string,string>& puncts,
		 const list<ElementType>& tag_list,
		 unordered_map<string,size_t>& counts,
		 const string& command,
		 const string& outName ){
  //
  // Code commented out, enabling 'cascading' of FoliA-correct runs
  //
//   if ( doc->declared( folia::AnnotationType::CORRECTION,
// 		      setname ) ){
// #pragma omp critical
//     {
//       cerr << "skipped " << doc->filename()
// 	   << " seems to be already processed, (with setname="
// 	   << setname
// 	   << ")" << endl;
//     }
//     return false;
//   }
  processor *proc = add_provenance( *doc, "FoLiA-correct", command );
  KWargs args;
  args["processor"] = proc->id();
  doc->declare( folia::AnnotationType::CORRECTION, setname, args );
  // doc->declare( folia::AnnotationType::STRING, setname, args );
  // doc->declare( folia::AnnotationType::TOKEN, setname, args );
  vector<FoliaElement*> ev;
  for ( const auto& et : tag_list ){
    vector<FoliaElement*> v1 = doc->doc()->select( et );
    if ( !v1.empty() ){
      ev = v1;
      break;
    }
  }
  for( const auto& par : ev ){
    try {
      correctNgrams( par, variants, unknowns, puncts, counts, proc );
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "FoLiA error in paragraph " << par->id() << " of document " << doc->id() << endl;
	cerr << e.what() << endl;
      }
      remove( outName.c_str() );
      return false;
    }
  }
  doc->save( outName );
  return true;
}

void rebase_text( Document *doc,
		  const string& inputclass,
		  const string& rebase_input ){

  vector<TextContent*> tv = doc->doc()->select<TextContent>();
  for ( const auto& t : tv ){
    if ( t->cls() == inputclass ){
      t->update_cls( rebase_input );
    }
  }
}

void usage( const string& name ){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t " << name << " will correct FoLiA files " << endl;
  cerr << "\t or a whole directory of FoLiA files " << endl;
  cerr << "\t--inputclass='classname'\t (default '" << input_classname << "')" << endl;
  cerr << "\t--outputclass='classname'.\t (default '" << output_classname << "')" << endl;
  cerr << "\t--rebase-inputclass='classname'\t (default \"\")" << endl;
  cerr << "\t\t\t rename the text with classname 'inputclass' to class 'classname'." << endl;
  cerr << "\t--setname='setname'\t (default '" << setname << "')" << endl;
  cerr << "\t--nums\t\t max number_of_suggestions. (default 10)" << endl;
  cerr << "\t\t\t suggestions are only added when there are more then 1." << endl;
  cerr << "\t--ngram=n\t analyse upto n N-grams." << endl;
  cerr << "\t--tags='tags'\t correct word/string nodes under all nodes in the list 'tags' (default='p')" << endl;
  cerr << "\t-e 'expr'\t specify the expression all files should match with." << endl;
  cerr << "\t-O\t output prefix" << endl;
  cerr << "\t--unk='uname'\t name of unknown words file, the *unk file produced by TICCL-unk" << endl;
  cerr << "\t--punct='pname'\t name of punct words file, the *punct file produced by TICCL-unk" << endl;
  cerr << "\t--rank='vname'\t name of variants file. This can be a file produced by TICCL-rank, TICCL-chain " << endl;
  cerr << "\t\t\t or TICCL-chainclean" << endl;
  cerr << "\t--clear\t\t redo ALL corrections. (default is to skip already processed files)" << endl;
  cerr << "\t-R\t\t search the dirs recursively (when appropriate)" << endl;
  cerr << "\t-t <threads>\n\t--threads <threads> Number of threads to run on. "
       << "(Default 1)" << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set" << endl;
  cerr << "\t\t\t to a reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-v increase verbosity level. Repeat for even more output." << endl;
  cerr << "\t-h or --help\t this message " << endl;
  cerr << "\t-V or --version\t show version " << endl;
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
			 "class:,inputclass:,outputclass:,rebase-inputclass:,"
			 "setname:,clear,unk:,"
			 "rank:,punct:,nums:,version,help,ngram:,string-nodes,"
			 "word-nodes,threads:,tags:" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage(argv[0]);
    exit( EXIT_FAILURE );
  }
  string progname = opts.prog_name();
  size_t numSugg = 10;
  bool recursiveDirs = false;
  bool clear = false;
  bool string_nodes = false;
  bool word_nodes = false;
  string expression;
  string variantsFileName;
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
  string orig_command = "FoLiA-correct " + opts.toString();
  while ( opts.extract( 'v' ) ){
    ++verbose;
  }
  opts.extract( "setname", setname );
  // backward compatibility
  if ( opts.extract( "class", output_classname ) ){
    cerr << "WARNING: '--class' is deprecated. Please use '--outputclass'."
	 << endl;
  }
  // prefer newer variant, if both present.
  opts.extract( "outputclass", output_classname );
  opts.extract( "inputclass", input_classname );
  opts.extract( "rebase-inputclass", rebase_inputclass );
  if ( input_classname == output_classname
       && rebase_inputclass.empty() ){
    cerr << "inputclass and outputclass are the same, and rebase_inputclass not set" << endl;
    exit( EXIT_FAILURE );
  }
  if ( rebase_inputclass == output_classname ){
    cerr << "rebase_inputclass and outputclass are the same" << endl;
    exit( EXIT_FAILURE );
  }
  if ( !rebase_inputclass.empty() ){
    safe_inputclass = input_classname;
    input_classname = rebase_inputclass;
  }
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
  if ( !opts.extract( "rank", variantsFileName ) ){
    cerr << "missing '--rank' option" << endl;
    exit( EXIT_FAILURE );
  }
  if ( !TiCC::isFile( variantsFileName ) ){
    cerr << "unable to find file '" << variantsFileName << "'" << endl;
    exit( EXIT_FAILURE );
  }
  if ( opts.extract( "nums", value ) ){
    if ( !TiCC::stringTo( value, numSugg )
	 || numSugg < 1 ){
      cerr << "unsupported value for --nums (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
#ifdef HAVE_OPENMP
  int numThreads = 1;
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
    if ( TiCC::lowercase(value) == "max" ){
      numThreads = omp_get_max_threads() - 2;
    }
    else if ( !TiCC::stringTo(value,numThreads) ) {
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
  }
  omp_set_num_threads( numThreads );
#else
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
    exit( EXIT_FAILURE );
  }
#endif
  if ( opts.extract( "ngram", value ) ){
    if ( !TiCC::stringTo( value, ngram_size )
	 || ngram_size > 3
	 || ngram_size < 1 ){
      cerr << "unsupported value for --ngram (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  string_nodes = opts.extract( "string-nodes" );
  if ( string_nodes ){
    cerr << "--string-nodes no longer needed" << endl;
  }
  word_nodes = opts.extract( "word-nodes" );
  if ( word_nodes ){
    cerr << "--word-nodes no longer needed" << endl;
  }

  list<ElementType> tag_list;
  string tagsstring;
  opts.extract( "tags", tagsstring );
  if ( !tagsstring.empty() ){
    vector<string> parts = TiCC::split_at( tagsstring, "," );
    for( const auto& t : parts ){
      ElementType et;
      try {
	et = TiCC::stringTo<ElementType>( t );
      }
      catch ( ... ){
	cerr << "in option --tags, the string '" << t
	     << "' doesn't represent a known FoLiA tag" << endl;
	exit(EXIT_FAILURE);
      }
      tag_list.push_back( et );
    }
  }
  else {
    tag_list.push_back(Paragraph_t);
  }

  vector<string> file_names = opts.getMassOpts();
  if ( file_names.size() == 0 ){
    cerr << "missing input file or directory" << endl;
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

  vector<string> fileNames;
  for ( const auto& fn : file_names ){
    vector<string> fns = TiCC::searchFilesMatch( fn, expression, recursiveDirs );
    fileNames.insert( fileNames.end(), fns.begin(), fns.end() );
  }
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }

  unordered_map<string,vector<word_conf> > variants;
  unordered_set<string> unknowns;
  unordered_map<string,string> puncts;
#ifdef TEST_HEMP
  puncts["N_A_P_O_L_E_O_N"] = "napoleon";
  puncts["N_A_P_O_L_E_O_N."] = "napoleon";
  puncts["(N_A_P_O_L_E_O_N)"] = "(napoleon)";
  puncts["P_Q."] = "PQRST";
#endif

#pragma omp parallel sections
  {
#pragma omp section
    {
#pragma omp critical
      {
	cout << "start reading variants " << endl;
      }
      if ( !fillVariants( variantsFileName, variants, numSugg ) ){
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

  if ( fileNames.size() > 1  ){
    cout << "start processing of " << toDo << " files " << endl;
  }

  map<string,size_t> total_counts;

  int fail_count = 0;

#pragma omp parallel for shared(fileNames,toDo,fail_count) schedule(dynamic,1)
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
    pos = docName.rfind(".folia");
    if ( pos != string::npos ){
      outName += docName.substr(0,pos) + ".ticcl" + docName.substr(pos);
    }
    else {
      pos = docName.rfind(".");
      if ( pos != string::npos ){
	outName += docName.substr(0,pos) + ".ticcl" + docName.substr(pos);
      }
      else {
	outName += docName + ".ticcl";
      }
    }
    if ( clear ){
      remove( outName.c_str() );
    }
    if ( TiCC::isFile( outName ) ){
#pragma omp critical
      {
	cerr << "skipping already done file: " << outName << endl;
      }
    }
    else {
      if ( !TiCC::createPath( outName ) ){
#pragma omp critical
	{
	  cerr << "unable to create output file! " << outName << endl;
	}
	exit(EXIT_FAILURE);
      }
      unordered_map<string,size_t> counts;
#pragma omp critical
      {
	cerr << "start " << ngram_size << "-gram correcting in file: "
	     << doc->filename() << endl;
      }
      if ( !rebase_inputclass.empty() ){
	rebase_text( doc, safe_inputclass, rebase_inputclass );
      }
      try {
	if ( correctDoc( doc, variants, unknowns, puncts,
			 tag_list, counts,
			 orig_command, outName ) ){
#pragma omp critical
	  {
	    if (!counts.empty() ){
	      //	    cout << "edits for: " << docName << endl;
	      for ( const auto& it : counts ){
		//	      cout << it.first << ":" << it.second << endl;
		total_counts[it.first] += it.second;
	      }
	    }
	    if ( toDo > 1 ){
	      cout << "Processed :" << docName << " into " << outName
		   << " still " << --toDo << " files to go." << endl;
	    }
	  }
	}
      }
      catch ( const exception& e ){
#pragma omp critical
	{
	  cerr << docName << " failed: " << e.what() << endl;
	  if ( ++fail_count > 10 ){
	    cerr << "more then 10 failures. Terminated" << endl;
	    exit(EXIT_FAILURE);
	  }
	}
      }
    }
    delete doc;
#pragma omp critical
    {
      cout << "finished " << fileNames[fn] << endl;
    }
  }

  if ( !total_counts.empty() ){
    cout << "edit statistics: " << endl;
    cout << "\tedit\t count" << endl;
    for ( const auto& it : total_counts ){
      cout << "\t" << it.first << "\t" << it.second << endl;
    }
  }
  return EXIT_SUCCESS;
}
