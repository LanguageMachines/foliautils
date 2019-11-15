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

#include <cstdio>
#include <string>
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
string output_classname = "Ticcl";
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
      string confS = parts[5]; // WILL FAIL for chained rank files
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

string test_final_punct( const string& word, const string& dep ){
  const string real_puncts = ".,;!?";
  string result;
  if ( real_puncts.find(word.back()) != string::npos
       && dep.back() != word.back() ){
    result = word.back();
  }
  return result;
}

bool correct_one_unigram( const string& w,
			  const unordered_map<string,vector<word_conf> >& variants,
			  const unordered_set<string>& unknowns,
			  const unordered_map<string,string>& puncts,
			  string& result,
			  unordered_map<string,size_t>& counts ){
  bool did_edit = false;
  if ( verbose > 2 ){
    cout << "correct unigram " << w << endl;
  }
  string word = w;
  string orig_word = word;
  string final_punct;
  const auto pit = puncts.find( word );
  if ( pit != puncts.end() ){
    final_punct = test_final_punct( word, pit->second );
    word = pit->second;
  }
  const auto vit = variants.find( word );
  if ( vit != variants.end() ){
    // 1 or more edits found
    string edit = vit->second[0].word;
    vector<string> parts = TiCC::split_at( edit, SEPARATOR );
    // edit might be seperatable!
    for ( const auto& p : parts ){
      result += p + " ";
    }
    size_t ed_size = parts.size();
    if ( !final_punct.empty() ){
      ++ed_size;
      result += final_punct + " ";
    }
    string ed;
    switch ( ed_size ){
    case 1:
      ed ="11";
      break;
    case 2:
      ed = "12";
      break;
    case 3:
      ed = "13";
      break;
    case 4:
      ed = "14";
      break;
    case 5:
      ed = "15";
      break;
    default:
      break;
      // just ignore for now
    }
    ++counts[ed];
    if ( verbose > 1 ){
      cout << word << " = " << ed << " => " << result << endl;
    }
    did_edit = true;
  }
  else {
    // a word with no suggested variants
    auto uit = unknowns.find( word );
    if ( uit == unknowns.end() ){
      uit = unknowns.find( orig_word );
    }
    if ( uit != unknowns.end() ){
      // ok it is a registrated garbage word
      result = "UNK ";
      ++counts[result];
      did_edit = true;
    }
    else {
      // just use the word
      result = word + " ";
      if ( !final_punct.empty() ){
	result += final_punct + " ";
	did_edit = true;
      }
    }
  }
  return did_edit;
}

string correct_unigrams( const vector<string>& unigrams,
			 const unordered_map<string,vector<word_conf> >& variants,
			 const unordered_set<string>& unknowns,
			 const unordered_map<string,string>& puncts,
			 vector<pair<string,string>>& corrections,
			 unordered_map<string,size_t>& counts ){
  if ( verbose > 1 ){
    cout << "correct unigrams" << endl;
  }
  string result;
  for ( const auto& uni : unigrams ){
    string cor;
    if ( correct_one_unigram( uni, variants, unknowns,
			      puncts, cor, counts ) ){
      corrections.push_back( make_pair( uni, cor ) );
    }
    result += cor;
  }
  return result;
}


int correct_one_bigram( const string& bi,
			const unordered_map<string,vector<word_conf> >& variants,
			const unordered_set<string>& unknowns,
			const unordered_map<string,string>& puncts,
			string& result,
			unordered_map<string,size_t>& counts ){
  int extra_skip = 0;
  result.clear();
  if ( verbose > 2 ){
    cout << "correct bigram " << bi << endl;
  }
  string word = bi;
  filter(word);
  string orig_word = word;
  string final_punct;
  const auto pit = puncts.find( word );
  if ( pit != puncts.end() ){
    final_punct = test_final_punct( word, pit->second );
    word = pit->second;
  }
  const auto vit = variants.find( word );
  if ( vit != variants.end() ){
    // edits found
    string edit = vit->second[0].word;
    vector<string> parts = TiCC::split_at( edit, SEPARATOR ); // edit can can be unseperated!
    for ( const auto& p : parts ){
      result += p + " ";
    }
    size_t ed_size = parts.size();
    if ( !final_punct.empty() ){
      ++ed_size;
      result += final_punct + " ";
    }
    string ed;
    switch ( ed_size ){
    case 1:
      ed ="21";
      break;
    case 2:
      ed = "22";
      break;
    case 3:
      ed = "23";
      break;
    case 4:
      ed = "24";
      break;
    case 5:
      ed = "25";
      break;
    default:
      break;
      // just ignore for now
    }
    ++counts[ed];
    if ( verbose > 1 ){
      cout << word << " = " << ed << " => " << result << endl;
    }
    extra_skip = 1;
  }
  else {
    // a word with no suggested variants
    auto uit = unknowns.find( word );
    if ( uit == unknowns.end() ){
      uit = unknowns.find( orig_word );
    }
    if ( uit != unknowns.end() ){
      // ok it is a registrated garbage bigram
      result = "UNK UNK ";
      ++counts[result];
      if ( verbose > 2 ){
	cout << " = 2 => " << result << endl;
      }
      extra_skip = 1;
    }
    else {
      // just use the ORIGINAL word and handle the first part like unigram
      vector<string> parts = TiCC::split_at( orig_word, SEPARATOR );
      correct_one_unigram( parts[0], variants, unknowns,
			   puncts, result, counts );
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
			const unordered_map<string,vector<word_conf> >& variants,
			const unordered_set<string>& unknowns,
			const unordered_map<string,string>& puncts,
			const string& last,
			unordered_map<string,size_t>& counts ){
  if ( verbose > 1 ){
    cout << "correct bigrams" << endl;
  }
  string result;
  int skip = 0;
  for ( const auto& bi : bigrams ){
    if ( skip > 0 ){
      --skip;
      continue;
    }
    string cor;
    skip = correct_one_bigram( bi, variants, unknowns,
			       puncts, cor, counts );
    result += cor;
  }
  string corr;
  correct_one_unigram( last, variants, unknowns,
		       puncts, corr, counts );
  if ( verbose > 2 ){
    cout << "handled last word: " << corr << endl;
  }
  result += corr;
  return result;
}

int correct_one_trigram( const string& tri,
			 const unordered_map<string,vector<word_conf> >& variants,
			 const unordered_set<string>& unknowns,
			 const unordered_map<string,string>& puncts,
			 string& result,
			 unordered_map<string,size_t>& counts ){
  int extra_skip = 0;
  result.clear();
  if ( verbose > 2 ){
    cout << "correct trigram " << tri << endl;
  }
  string word = tri;
  filter(word);
  string orig_word = word;
  string final_punct;
  const auto pit = puncts.find( word );
  if ( pit != puncts.end() ){
    final_punct = test_final_punct( word, pit->second );
    word = pit->second;
  }
  const auto vit = variants.find( word );
  if ( vit != variants.end() ){
    // edits found
    string edit = vit->second[0].word;
    vector<string> parts = TiCC::split_at( edit, SEPARATOR ); // edit can can be unseperated!
    for ( const auto& p : parts ){
      result += p + " ";
    }
    size_t ed_size = parts.size();
    if ( !final_punct.empty() ){
      ++ed_size;
      result += final_punct + " ";
    }
    string ed;
    switch ( ed_size ){
    case 1:
      ed ="31";
      break;
    case 2:
      ed = "32";
      break;
    case 3:
      ed = "33";
      break;
    case 4:
      ed = "34";
      break;
    case 5:
      ed = "35";
      break;
    default:
      break;
      // just ignore for now
    }
    ++counts[ed];
    if ( verbose > 1 ){
      cout << word << " = " << ed << " => " << result << endl;
    }
    extra_skip = 2;
  }
  else {
    // a word with no suggested variants
    auto uit = unknowns.find( word );
    if ( uit == unknowns.end() ){
      uit = unknowns.find( orig_word );
    }
    if ( uit != unknowns.end() ){
      // ok it is a registrated garbage trigram
      result = "UNK UNK UNK";
      ++counts[result];
      if ( verbose > 2 ){
	cout << " = 3 => " << result << endl;
      }
      extra_skip = 2;
    }
    else {
      // just use the ORIGINAL word so handle the first part like bigram
      vector<string> parts = TiCC::split_at( orig_word, SEPARATOR );
      string corr;
      string test = parts[0] + SEPARATOR + parts[1];
      extra_skip = correct_one_bigram( test, variants, unknowns,
				       puncts, corr, counts );
      result += corr;
    }
  }
  return extra_skip;
}

string correct_trigrams( const vector<string>& trigrams,
			 const unordered_map<string,vector<word_conf> >& variants,
			 const unordered_set<string>& unknowns,
			 const unordered_map<string,string>& puncts,
			 const vector<string>& unigrams,
			 unordered_map<string,size_t>& counts ){
  if ( verbose > 1 ){
    cout << "correct trigrams" << endl;
  }
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
    skip = correct_one_trigram( tri, variants, unknowns,
				puncts, cor, counts );
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
    correct_one_unigram( last, variants, unknowns,
			 puncts, corr, counts );
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
    if ( verbose > 2 ){
      cout << "correct last bigram: " << last_bi << endl;
    }
    int skip = correct_one_bigram( last_bi, variants, unknowns,
				   puncts, corr, counts );
    if ( verbose > 2 ){
      cout << "handled last bigram: " << corr << endl;
    }
    result += corr;
    if ( skip == 0 ){
      if ( verbose > 2 ){
	cout << "correct last word: " << last << endl;
      }
      string uni_corr;
      correct_one_unigram( last, variants, unknowns,
			   puncts, uni_corr, counts );
      if ( verbose > 2 ){
	cout << "handled last word: " << uni_corr << endl;
      }
      result += uni_corr;
    }
    return result;
  }
}

//#define HEMP_DEBUG

vector<string> replace_hemps( const vector<string>& unigrams,
			      vector<hemp_status> inventory,
			      const unordered_map<string,string>& puncts ){
  vector<string> result;
  result.reserve(unigrams.size() );
  string mw;
  for ( size_t i=0; i < unigrams.size(); ++i ){
    //    cerr << "i=" << i << "/" << unigrams.size()-1 << " status = " << inventory[i] << " MW='" << mw << "'" << endl;
    if ( inventory[i] == NO_HEMP ){
      if ( !mw.empty() ){
	mw.pop_back(); // remove last '_'
	const auto& it = puncts.find( mw );
	if ( it != puncts.end() ){
	  result.push_back( it->second );
	}
	else {
	  vector<string> parts = TiCC::split_at( mw, "_" );
	  for ( const auto& p :parts ){
	    result.push_back( p );
	  }
	}
	mw = "";
      }
      result.push_back(unigrams[i]);
    }
    else if ( inventory[i] == END_PUNCT_HEMP ){
      mw += unigrams[i];
      const auto& it = puncts.find( mw );
      if ( it != puncts.end() ){
	result.push_back( it->second );
      }
      else {
	vector<string> parts = TiCC::split_at( mw, "_" );
	for ( const auto& p :parts ){
	  result.push_back( p );
	}
      }
      mw = "";
    }
    else if ( inventory[i] == START_PUNCT_HEMP ){
      if ( !mw.empty() ){
	mw.pop_back(); //  remove last '_'
	const auto& it = puncts.find( mw );
	if ( it != puncts.end() ){
	  result.push_back( it->second );
	}
	else {
	  vector<string> parts = TiCC::split_at( mw, "_" );
	  for ( const auto& p :parts ){
	    result.push_back( p );
	  }
	}
	mw = "";
      }
      mw = unigrams[i] + "_";
    }
    else if ( inventory[i] == NORMAL_HEMP ){
      mw += unigrams[i] + "_";
    }
    //    cerr << "   result=" << result << endl;
  }
  if ( !mw.empty() ){
    // leftovers
    mw.pop_back(); //  remove last '_'
    const auto& it = puncts.find( mw );
    if ( it != puncts.end() ){
      result.push_back( it->second );
    }
    else {
      vector<string> parts = TiCC::split_at( mw, "_" );
      for ( const auto& p :parts ){
       	result.push_back( p );
      }
    }
  }
  return result;
}

vector<string> replace_hemps( const vector<string>& unigrams,
			      const unordered_map<string,string>& puncts ){
  vector<UnicodeString> u_uni( unigrams.size() );
  for ( size_t i=0; i < unigrams.size(); ++i ){
    u_uni[i] = TiCC::UnicodeFromUTF8(unigrams[i]);
  }
  vector<hemp_status> inventory = create_emph_inventory( u_uni );
  vector<string> result = replace_hemps( unigrams, inventory, puncts );
  return result;
}

//#define TEST_HEMP

void correctNgrams( Paragraph* par,
		    const unordered_map<string,vector<word_conf> >& variants,
		    const unordered_set<string>& unknowns,
		    const unordered_map<string,string>& puncts,
		    int ngrams,
		    unordered_map<string,size_t>& counts ){
  vector<TextContent *> origV = par->select<TextContent>(false);
  if ( origV.empty() ){
    // OK, no text directly
    // look deeper then
    origV = par->select<TextContent>();
    if ( origV.empty() ){
      if ( verbose > 1 ){
#pragma omp critical
	{
	  cerr << "no text text in : " << par->id() << " skipping" << endl;
	}
      }
      return;
    }
  }
  string partext;
  for ( const auto& org : origV ){
    string content = org->str(input_classname);
    if ( verbose > 1 ){
#pragma omp critical
      {
	cerr << "correct ngrams in: '" << content << "' (" << input_classname
	     << ")" << endl;
      }
    }
    filter( content, SEPCHAR ); // HACK
    vector<string> unigrams = TiCC::split( content );
#ifdef TEST_HEMP
    unigrams = {"Als","N","A","P","O","L","E","O","N",")A",
		"aan","(N","A","P","O","L","E","O","N)","EX",
		"voor","N","A","P","O","L","E","O","toch?",
		"tegen","P","Q.","zeker"};
    cerr << "old_uni: " << unigrams << endl;
#endif
    unigrams = replace_hemps( unigrams, puncts );
#ifdef TEST_HEMP
    cerr << "new_uni: " << unigrams << endl;
    exit(1);
#endif
    vector<string> bigrams;
    vector<string> trigrams;
    counts["TOKENS"] += unigrams.size();
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
	corrected = correct_unigrams( unigrams, variants, unknowns,
				      puncts, corrections, counts );
	if ( verbose && !corrections.empty() ){
	  cout << "unigram corrections:" << endl;
	  for ( const auto& p : corrections ) {
	    cout << p.first << " : " << p.second << endl;
	  }
	}
      }
      else {
	corrected = correct_bigrams( bigrams, variants, unknowns,
				     puncts, unigrams.back(), counts );
      }
    }
    else {
      corrected = correct_trigrams( trigrams, variants, unknowns,
				    puncts, unigrams, counts );
    }
    corrected = TiCC::trim( corrected );
    if ( verbose > 1 ){
#pragma omp critical
      {
	cerr << " corrected ngrams: '" << corrected << "'" << endl;
      }
    }
    if ( !corrected.empty() ){
      org->parent()->settext( corrected, output_classname );
      if ( partext.empty() ){
	partext = corrected;
      }
      else {
	partext += " " + corrected;
      }
    }
  }
  // if ( !partext.empty() ){
  //   par->settext( partext, output_classname );
  // }
}

void correctParagraph( Paragraph* par,
		       const unordered_map<string,vector<word_conf> >& variants,
		       const unordered_set<string>& unknowns,
		       const unordered_map<string,string>& puncts,
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
    if ( origV.empty() ){
      if ( verbose > 1 ){
#pragma omp critical
	{
	  cerr << "no text text in : " << s->id() << " skipping" << endl;
	}
      }
      continue;
    }
    string word = origV[0]->str(input_classname);
    filter(word);
    string orig_word = word;
    string final_punct;
    const auto pit = puncts.find( word );
    if ( pit != puncts.end() ){
      final_punct = test_final_punct( word, pit->second );
      word = pit->second;
    }
    const auto vit = variants.find( word );
    if ( vit != variants.end() ){
      // 1 or more edits found
      string edit = vit->second[0].word;
      vector<FoliaElement*> oV;
      oV.push_back( origV[0] );
      vector<FoliaElement*> nV;
      KWargs args;
      args["class"] = output_classname;
      args["offset"] = TiCC::toString(offset);
      args["value"] = edit;
      TextContent *newT = new TextContent( args );
      corrected += edit + " ";
      if ( !final_punct.empty() ){
	corrected += final_punct + " ";
      }
      offset = corrected.size();
      nV.push_back( newT );
      vector<FoliaElement*> sV;
      size_t limit = vit->second.size();
      for( size_t j=0; j < limit; ++j ){
	KWargs sargs;
	sargs["confidence"] = vit->second[j].conf;
	sargs["n"]= TiCC::toString(j+1) + "/" + TiCC::toString(limit);
	Suggestion *sug = new Suggestion( sargs );
	sug->settext( vit->second[j].word, output_classname );
	sV.push_back( sug );
      }
      vector<FoliaElement*> cV;
      args.clear();
      s->correct( oV, cV, nV, sV, args );
    }
    else {
      // a word with no suggested variants
      auto uit = unknowns.find( word );
      if ( uit == unknowns.end() ){
	uit = unknowns.find( orig_word );
      }
      if ( uit != unknowns.end() ){
	// ok it is a registrated garbage word
	string edit = "UNK";
	vector<FoliaElement*> oV;
	oV.push_back( origV[0] );
	vector<FoliaElement*> nV;
	KWargs args;
	args["class"] = output_classname;
	args["offset"] = TiCC::toString(offset);
	args["value"] = edit;
	TextContent *newT = new TextContent( args );
	corrected += edit + " ";
	if ( !final_punct.empty() ){
	  corrected += final_punct + " ";
	}
	offset = corrected.size();
	nV.push_back( newT );
	vector<FoliaElement*> sV;
	vector<FoliaElement*> cV;
	args.clear();
	s->correct( oV, cV, nV, sV, args );
      }
      else {
	// just use the ORIGINAL word
	//	word = orig_word;
	string my_word = word;
	if ( !final_punct.empty() ){
	  my_word += " " + final_punct;
	}
	s->settext( my_word, offset, output_classname );
	corrected += word + " ";
	if ( !final_punct.empty() ){
	  corrected += final_punct + " ";
	}
	offset = corrected.size();
      }
    }
  }
  corrected = TiCC::trim( corrected );
  if ( !corrected.empty() ){
    par->settext( corrected, output_classname );
  }
}

bool correctDoc( Document *doc,
		 const unordered_map<string,vector<word_conf> >& variants,
		 const unordered_set<string>& unknowns,
		 const unordered_map<string,string>& puncts,
		 int ngrams,
		 bool string_nodes,
		 bool word_nodes,
		 unordered_map<string,size_t>& counts,
		 const string& command,
		 const string& outName ){
  if ( doc->declared( folia::AnnotationType::CORRECTION,
		      setname ) ){
#pragma omp critical
    {
      cerr << "skipped " << doc->filename()
	   << " seems to be already processed, (with setname="
	   << setname
	   << ")" << endl;
    }
    return false;
  }
  processor *proc = add_provenance( *doc, "FoLiA-correct", command );
  KWargs args;
  args["processor"] = proc->id();
  doc->declare( folia::AnnotationType::CORRECTION, setname, args );
  vector<Paragraph*> pv = doc->doc()->select<Paragraph>();
  for( const auto& par : pv ){
    try {
      if ( ngrams == 1 && ( string_nodes || word_nodes ) ){
	correctParagraph( par, variants, unknowns, puncts, string_nodes );
      }
      else {
	correctNgrams( par, variants, unknowns, puncts, ngrams, counts );
      }
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

void usage( const string& name ){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "\t--setname\t FoLiA setname. (default '" << setname << "')" << endl;
  cerr << "\t--inputclass\t classname. (default '" << input_classname << "')" << endl;
  cerr << "\t--outputclass\t classname. (default '" << output_classname << "')" << endl;
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
  cerr << "\t--rank 'vname'\t name of variants file. This can be a file produced by TICCL-rank, TICCL-chain or TICCL-chainclean" << endl;
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
			 "class:,inputclass:,outputclass:,setname:,clear,unk:,rank:,punct:,nums:,version,help,ngram:,string-nodes,word-nodes" );
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
  opts.extract( "class", output_classname );
  // prefer newer variant, if both present.
  opts.extract( "outputclass", output_classname );
  opts.extract( "inputclass", input_classname );
  if ( input_classname == output_classname ){
    cerr << "inputclass and outputclass are the same" << endl;
    exit( EXIT_FAILURE );
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

  if ( doDir ){
    cout << "start processing of " << toDo << " files " << endl;
  }

  map<string,size_t> total_counts;

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
	cerr << "start correcting file: " << doc->filename() << endl;
      }
      if ( correctDoc( doc, variants, unknowns, puncts,
		       ngram, string_nodes, word_nodes, counts,
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
    delete doc;
  }

  if ( doDir ){
    cout << "done processsing directory '" << name << "'" << endl;
  }
  else {
    cout << "finished " << name << endl;
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
