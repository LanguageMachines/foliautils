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

#include <cmath>
#include <string>
#include <map>
#include <vector>
#include <exception>
#include <iostream>
#include <iomanip>
#include <fstream>

#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/Unicode.h"
#include "libfolia/folia.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;
using namespace	folia;

bool verbose = false;
string classname = "current";

enum Mode { UNKNOWN,
	    S_IN_D,
	    L_P };

Mode stringToMode( const string& ms ){
  if ( ms == "string_in_doc" ){
    return S_IN_D;
  }
  else if ( ms == "lemma_pos" ){
    return L_P;
  }
  return UNKNOWN;
}

void create_agg_list( const map<string,vector<map<UnicodeString, unsigned int>>>& wcv,
		      const string& filename,
		      unsigned int clip, int min_ng, int max_ng ){
  for ( int ng=min_ng; ng <= max_ng; ++ng ){
    unsigned int clipped = 0;
    string ext;
    if ( ng > 1 ){
      ext += "." + TiCC::toString( ng ) + "-gram";
    }
    ext += ".tsv";
    string ofilename = filename + ext;
    if ( !TiCC::createPath( ofilename ) ){
      cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
      exit(EXIT_FAILURE);
    }
    ofstream os( ofilename );
    vector<string> srt;
    os << "## " << ng << "-gram\t\ttotal" ;
    for ( const auto& wc0 : wcv ){
      os << "\t" << wc0.first;
      srt.push_back( wc0.first );
    }
    os << endl;
    map<UnicodeString,map<string,unsigned int>> totals;
    for ( const auto& wc0 : wcv ){
      string lang = wc0.first;
      auto cit = wc0.second[ng].begin();
      while( cit != wc0.second[ng].end()  ){
	if ( cit->second <= clip ){
	  ++clipped;
	}
	else {
	  totals[cit->first].insert( make_pair(lang, cit->second ) );
	}
	++cit;
      }
    }
    map<string,unsigned int> lang_tot;
    unsigned int grand_total = 0;
    for ( const auto& it : totals ){
      os << it.first;
      if ( it.first.length() < 8 ){
	os << "\t\t\t";
      }
      else if ( it.first.length() < 16 ){
	os << "\t\t";
      }
      else {
	os << "\t";
      }
      os << it.second.size();
      for ( const auto& l : srt ){
	auto const& f = it.second.find(l);
	if ( f == it.second.end() ){
	  os << "\t" << 0;
	}
	else {
	  os << "\t" << f->second;
	  lang_tot[l] += f->second;
	  grand_total += f->second;
	}
      }
      os << endl;
    }
    os << "## " << ng << "-gram\t\ttotal" ;
    for ( const auto& wc0 : wcv ){
      os << "\t" << wc0.first;
    }
    os << endl;
    os << "##totals:\t\t" << grand_total;
    for ( const auto& l : srt  ){
      os << "\t" << lang_tot[l];
    }
    os << endl;
    os << "##percentages:\t\t100";
    for ( const auto& l : srt  ){
      os << "\t" << std::fixed << std::setprecision(1) << (double)lang_tot[l]/grand_total*100;
    }
    os << endl;
#pragma omp critical
    {
      cout << "created aggregate WordFreq list '" << ofilename << "'";
      if ( clipped > 0 ){
	cout << " ("<< clipped << " were clipped.)";
      }
      cout << endl;
    }
  }
}

void create_wf_list( const map<string,vector<map<UnicodeString, unsigned int>>>& wcv,
		     const string& filename,
		     unsigned int clip, int min_ng, int max_ng,
		     map<string,vector<unsigned int>>& totals_per_n,
		     bool doperc ){
  for ( const auto& wc0 : wcv ){
    string lext;
    string lang = wc0.first;
    if ( lang != "none" ){
      lext += "." + lang;
    }
    for ( int ng=min_ng; ng <= max_ng; ++ng ){
      unsigned int total_n = totals_per_n[lang][ng];
      if ( total_n > 0 ){
	string ext = lext;
	if ( ng > 1 ){
	  ext += "." + TiCC::toString( ng ) + "-gram";
	}
	ext += ".tsv";
	string ofilename = filename + ext;
	if ( !TiCC::createPath( ofilename ) ){
	  cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
	  exit(EXIT_FAILURE);
	}
	ofstream os( ofilename );
	map<unsigned int, set<UnicodeString>> wf;
	auto cit = wc0.second[ng].begin();
	while( cit != wc0.second[ng].end()  ){
	  if ( cit->second <= clip ){
	    total_n -= cit->second;
	  }
	  else {
	    wf[cit->second].insert( cit->first );
	  }
	  ++cit;
	}
	unsigned int sum=0;
	unsigned int types=0;
	map<unsigned int, set<UnicodeString> >::const_reverse_iterator wit = wf.rbegin();
	while ( wit != wf.rend() ){
	  for ( const auto sit : wit->second ){
	    sum += wit->first;
	    os << sit << "\t" << wit->first;
	    if ( doperc ){
	      os << "\t" << sum << "\t" << 100 * double(sum)/total_n;
	    }
	    os << endl;
	    ++types;
	  }
	  ++wit;
	}
#pragma omp critical
	{
	  cout << "created WordFreq list '" << ofilename << "'";
	  cout << " for " << ng << "-grams. Stored " << sum << " tokens and "
	       << types << " types, TTR= " << (double)types/sum
	       << ", the angle is " << atan((double)types/sum)*180/M_PI
	       << " degrees";
	  if ( clip > 0 ){
	    cout << " ("<< totals_per_n[lang][ng] - total_n << " were clipped.)";
	  }
	  cout << endl;
	}
      }
      else {
#pragma omp critical
	{
	  cout << "NOT created a WordFreq list for " << ng
	       << "-grams. None were found." << endl;
	}
      }
    }
  }
}

void create_collected_wf_list( const map<string,vector<map<UnicodeString, unsigned int>>>& wcv,
			       const string& filename,
			       unsigned int clip, int min_ng, int max_ng,
			       map<string,vector<unsigned int>>& totals_per_n,
			       bool doperc,
			       const string& lang ){
  string ext;
  if ( lang != "none" ){
    ext += "." + lang;
  }
  ext += "." + TiCC::toString( min_ng ) + "to"
    + TiCC::toString( max_ng ) + ".tsv";
  string ofilename = filename + ext;
  if ( !TiCC::createPath( ofilename ) ){
    cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  ofstream os( ofilename );
  map<unsigned int, set<UnicodeString>> wf;
  size_t grand_total = 0;
  size_t grand_total_clipped = 0;
  for ( const auto& wc0 : wcv ){
    for ( int ng=min_ng; ng <= max_ng; ++ng ){
      grand_total += totals_per_n[lang][ng];
      unsigned int total_n = totals_per_n[lang][ng];
      if ( total_n > 0 ){
	auto cit = wc0.second[ng].begin();
	while( cit != wc0.second[ng].end()  ){
	  if ( cit->second <= clip ){
	    total_n -= cit->second;
	  }
	  else {
	    wf[cit->second].insert( cit->first );
	  }
	  ++cit;
	}
	grand_total_clipped += total_n;
      }
    }
  }
  unsigned int sum=0;
  unsigned int types=0;
  map<unsigned int, set<UnicodeString> >::const_reverse_iterator wit = wf.rbegin();
  while ( wit != wf.rend() ){
    for ( const auto sit : wit->second ){
      sum += wit->first;
      os << sit << "\t" << wit->first;
      if ( doperc ){
	os << "\t" << sum << "\t" << 100 * double(sum)/grand_total_clipped;
      }
      os << endl;
      ++types;
    }
    ++wit;
  }
#pragma omp critical
  {
    cout << "created collected WordFreq list '" << ofilename << "'";
    cout << ". Stored " << sum << " tokens and "
	 << types << " types, TTR= " << (double)types/sum
	 << ", the angle is " << atan((double)types/sum)*180/M_PI
	 << " degrees";
    if ( clip > 0 ){
      cout << " ("<< grand_total - grand_total_clipped << " were clipped.)";
    }
    cout << endl;
  }
}

struct rec {
  unsigned int count;
  map<string,unsigned int> pc;
};

void create_lf_list( const map<string,vector<map<UnicodeString, unsigned int>>>& lcv,
		     const string& filename,
		     unsigned int clip,
		     int min_ng,
		     int max_ng,
		     map<string,vector<unsigned int>>& totals_per_n,
		     bool doperc ){
  for ( const auto& lc0 : lcv ){
    string lext;
    string lang = lc0.first;
    if ( lang != "none" ){
      lext += "." + lang;
    }
    for ( int ng=min_ng; ng <= max_ng; ++ng ){
      unsigned int total_n = totals_per_n[lang][ng];
      if ( total_n > 0 ){
	string ext = lext;
	if ( ng > 1 ){
	  ext += "." + TiCC::toString( ng ) + "-gram";
	}
	ext += ".tsv";
	string ofilename = filename + ext;
	if ( !TiCC::createPath( ofilename ) ){
	  cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
	  exit(EXIT_FAILURE);
	}
	ofstream os( ofilename );
	map<unsigned int, set<UnicodeString> > lf;
	for ( const auto& cit : lc0.second[ng] ){
	  if ( cit.second <= clip ){
	    total_n -= cit.second;
	  }
	  else {
	    lf[cit.second].insert( cit.first );
	  }
	}

	unsigned int sum=0;
	unsigned int types=0;
	map<unsigned int, set<UnicodeString> >::const_reverse_iterator wit = lf.rbegin();
	while ( wit != lf.rend() ){
	  for ( const auto& sit : wit->second ){
	    sum += wit->first;
	    os << sit << "\t" << wit->first;
	    if ( doperc ){
	      os << "\t" << sum << "\t" << 100* double(sum)/total_n;
	    }
	    os << endl;
	    ++types;
	  }
	  ++wit;
	}
#pragma omp critical
	{
	  cout << "created LemmaFreq list '" << ofilename << "'";
	  cout << " for " << ng << "-gram lemmas. Stored " << sum
	       << " tokens and " << types << " types. TTR= " << (double)types/sum
	       << ", the angle is " << atan((double)types/sum)*180/M_PI
	       << " degrees";
	  if ( clip > 0 ){
	    cout << " ("<< totals_per_n[lang][ng] - total_n << " lemmas were clipped.)";
	  }
	  cout << endl;
	}
      }
      else {
#pragma omp critical
	{
	  cout << "NOT created a LemmaFreq list for " << ng
	       << "-grams. None were found." << endl;
	}
      }
    }
  }
}

void create_collected_lf_list( const map<string,vector<map<UnicodeString, unsigned int>>>& lcv,
			       const string& filename,
			       unsigned int clip,
			       int min_ng,
			       int max_ng,
			       map<string,vector<unsigned int>>& totals_per_n,
			       bool doperc,
			       const string& lang ){
  string ext;
  if ( lang != "none" ){
    ext += "." + lang;
  }
  ext += "." + TiCC::toString( min_ng ) + "to"
    + TiCC::toString( max_ng ) + ".tsv";
  string ofilename = filename + ext;
  if ( !TiCC::createPath( ofilename ) ){
    cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  ofstream os( ofilename );
  map<unsigned int, set<UnicodeString>> lf;
  size_t grand_total = 0;
  size_t grand_total_clipped = 0;
  for ( const auto& lc0 : lcv ){
    for ( int ng=min_ng; ng <= max_ng; ++ng ){
      grand_total += totals_per_n[lang][ng];
      unsigned int total_n = totals_per_n[lang][ng];
      if ( total_n > 0 ){
	for ( const auto& cit : lc0.second[ng] ){
	  if ( cit.second <= clip ){
	    total_n -= cit.second;
	  }
	  else {
	    lf[cit.second].insert( cit.first );
	  }
	}
	grand_total_clipped += total_n;
      }
    }
  }
  unsigned int sum=0;
  unsigned int types=0;
  map<unsigned int, set<UnicodeString> >::const_reverse_iterator wit = lf.rbegin();
  while ( wit != lf.rend() ){
    for ( const auto& sit : wit->second ){
      sum += wit->first;
      os << sit << "\t" << wit->first;
      if ( doperc ){
	os << "\t" << sum << "\t" << 100* double(sum)/grand_total_clipped;
      }
      os << endl;
      ++types;
    }
    ++wit;
  }
#pragma omp critical
  {
    cout << "created LemmaFreq list '" << ofilename << "'";
    cout << ". Stored " << sum
	 << " tokens and " << types << " types. TTR= " << (double)types/sum
	 << ", the angle is " << atan((double)types/sum)*180/M_PI
	 << " degrees";
    if ( clip > 0 ){
      cout << " ("<< grand_total - grand_total_clipped << " lemmas were clipped.)";
    }
    cout << endl;
  }
}

void create_lpf_list( const map<string,vector<multimap<UnicodeString, rec>>>& lpcv,
		      const string& filename,
		      unsigned int clip,
		      int min_ng,
		      int max_ng,
		      map<string,vector<unsigned int>>& totals_per_n,
		      bool doperc ){
  for ( const auto& lpc0 : lpcv ){
    string lext;
    string lang = lpc0.first;
    if ( lang != "none" ){
      lext += "." + lang;
    }
    for( int ng=min_ng; ng <= max_ng; ++ng ){
      unsigned int total_n = totals_per_n[lang][ng];
      if ( total_n > 0 ){
	string ext = lext;
	if ( ng > 1 ){
	  ext += "." + TiCC::toString( ng ) + "-gram";
	}
	ext += ".tsv";
	string ofilename = filename + ext;
	if ( !TiCC::createPath( ofilename ) ){
	  cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
	  exit(EXIT_FAILURE);
	}
	ofstream os( ofilename );
	multimap<unsigned int, pair<UnicodeString,string> > lpf;
	for ( const auto& cit : lpc0.second[ng] ){
	  for ( const auto& pit : cit.second.pc ){
	    if ( pit.second <= clip ){
	      total_n -= pit.second;
	    }
	    else {
	      lpf.insert( make_pair( pit.second,
				     make_pair( cit.first, pit.first ) ) );
	    }
	  }
	}
	unsigned int sum =0;
	unsigned int types =0;
	multimap<unsigned int, pair<UnicodeString,string> >::const_reverse_iterator wit = lpf.rbegin();
	while ( wit != lpf.rend() ){
	  sum += wit->first;
	  os << wit->second.first << " " << wit->second.second << "\t" << wit->first;
	  if ( doperc ){
	    os << "\t" << sum << "\t" << 100 * double(sum)/total_n;
	  }
	  os << endl;
	  ++types;
	  ++wit;
	}
#pragma omp critical
	{
	  cout << "created LemmaPosFreq list '" << ofilename << "'";
	  cout << " for " << ng << "-gram Lemma-Pos pairs. Stored " << sum
	       << " tokens and " << types << " types. TTR= " << (double)types/sum
	       << ", the angle is " << atan((double)types/sum)*180/M_PI
	       << " degrees";
	  if ( clip > 0 ){
	    cout << " ("<< totals_per_n[lang][ng] - total_n << " were clipped.)";
	  }
	  cout << endl;
	}
      }
      else {
#pragma omp critical
	{
	  cout << "NOT created a LemmaPosFreq list for " << ng
	       << "-grams. None were found." << endl;
	}
      }
    }
  }
}

void create_collected_lpf_list( const map<string,vector<multimap<UnicodeString, rec>>>& lpcv,
				const string& filename,
				unsigned int clip,
				int min_ng,
				int max_ng,
				map<string,vector<unsigned int>>& totals_per_n,
				bool doperc,
				const string& lang ){
  string ext;
  if ( lang != "none" ){
    ext += "." + lang;
  }
  ext += "." + TiCC::toString( min_ng ) + "to"
    + TiCC::toString( max_ng ) + ".tsv";
  string ofilename = filename + ext;
  if ( !TiCC::createPath( ofilename ) ){
    cerr << "FoLiA-stats: failed to create outputfile '" << ofilename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  ofstream os( ofilename );
  multimap<unsigned int, pair<UnicodeString,string> > lpf;
  size_t grand_total = 0;
  size_t grand_total_clipped = 0;
  for ( const auto& lpc0 : lpcv ){
    for( int ng=min_ng; ng <= max_ng; ++ng ){
      grand_total += totals_per_n[lang][ng];
      unsigned int total_n = totals_per_n[lang][ng];
      if ( total_n > 0 ){
	for ( const auto& cit : lpc0.second[ng] ){
	  for ( const auto& pit : cit.second.pc ){
	    if ( pit.second <= clip ){
	      total_n -= pit.second;
	    }
	    else {
	      lpf.insert( make_pair( pit.second,
				     make_pair( cit.first, pit.first ) ) );
	    }
	  }
	}
	grand_total_clipped += total_n;
      }
    }
  }
  unsigned int sum =0;
  unsigned int types =0;
  multimap<unsigned int, pair<UnicodeString,string> >::const_reverse_iterator wit = lpf.rbegin();
  while ( wit != lpf.rend() ){
    sum += wit->first;
    os << wit->second.first << " " << wit->second.second << "\t" << wit->first;
    if ( doperc ){
      os << "\t" << sum << "\t" << 100 * double(sum)/grand_total_clipped;
    }
    os << endl;
    ++types;
    ++wit;
  }
#pragma omp critical
  {
    cout << "created LemmaPosFreq list '" << ofilename << "'";
    cout << ". Stored " << sum
	 << " tokens and " << types << " types. TTR= " << (double)types/sum
	 << ", the angle is " << atan((double)types/sum)*180/M_PI
	 << " degrees";
    if ( clip > 0 ){
      cout << " ("<< grand_total - grand_total_clipped << " were clipped.)";
    }
    cout << endl;
  }
}

struct wlp_rec {
  UnicodeString word;
  UnicodeString lemma;
  string pos;
};

const string frog_cgntagset = "http://ilk.uvt.nl/folia/sets/frog-mbpos-cgn";
const string frog_mblemtagset = "http://ilk.uvt.nl/folia/sets/frog-mblem-nl";

inline bool isalnum( int8_t charT ){
  return ( charT == U_LOWERCASE_LETTER ||
	   charT == U_UPPERCASE_LETTER ||
	   charT == U_DECIMAL_DIGIT_NUMBER );
}

inline bool isalnum( UChar uc ){
  int8_t charT =  u_charType( uc );
  return isalnum( charT );
}

bool is_emph( const UnicodeString& data ){
  return (data.length() < 2) && isalnum(data[0]);
}

void add_emph_inventory( const vector<UnicodeString>& data,
			 set<UnicodeString>& emph ){
  for ( unsigned int i=0; i < data.size(); ++i ){
    bool done = false;
    for ( unsigned int j=i; j < data.size() && !done; ++j ){
      if ( is_emph( data[j] ) ){
	// a candidate?
	if ( j + 1 < data.size()
	     && is_emph( data[j+1] ) ){
	  // yes a second short word
	  UnicodeString mw = data[j] + "_" + data[j+1];
	  for ( unsigned int k=j+2; k < data.size(); ++k ){
	    if ( is_emph(data[k]) ){
	      mw += "_" + data[k];
	    }
	    else {
	      emph.insert(mw);
	      mw.remove();
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

void add_emph_inventory( vector<wlp_rec>& data, set<UnicodeString>& emph ){
  for ( unsigned int i=0; i < data.size(); ++i ){
    UnicodeString mw;
    bool first = true;
    for ( unsigned int j=i; j < data.size(); ++j ){
      if ( data[j].word.length() < 2 ){
	if ( !first ){
	  mw += "_";
	}
	first = false;
	mw += data[j].word;
      }
      else {
	emph.insert(mw);
	mw.remove();
	first = true;
      }
    }
  }
}

size_t add_word_inventory( const vector<UnicodeString>& data,
			   vector<map<UnicodeString,unsigned int>>& wc,
			   int min_ng,
			   int max_ng,
			   vector<unsigned int>& totals_per_n,
			   const UnicodeString& sep ){
#pragma omp critical
  {
    wc.resize(max_ng+1);
    totals_per_n.resize(max_ng+1);
  }
  size_t count = 0;
  for( int ng=min_ng; ng <= max_ng; ++ng ){
    for ( int i=0; i <= int(data.size()) - ng ; ++i ){
      UnicodeString multiw;
      for ( int j=0; j < ng; ++j ){
	multiw += data[i+j];
	if ( j < ng-1 ){
	  multiw += sep;
	}
      }
      ++count;
      ++totals_per_n[ng];
#pragma omp critical
      {
	++wc[ng][multiw];
      }
    }
  }
  return count;
}

size_t doc_sent_word_inventory( const Document *d, const string& docName,
				unsigned int min_ng,
				unsigned int max_ng,
				map<string,vector<unsigned int>>& w_totals_per_n,
				map<string,vector<unsigned int>>& l_totals_per_n,
				map<string,vector<unsigned int>>& p_totals_per_n,
				unsigned int& lem_count,
				unsigned int& pos_count,
				bool lowercase,
				const string& default_language,
				const set<string>& languages,
				map<string,vector<map<UnicodeString,unsigned int>>>& wcv,
				map<string,vector<map<UnicodeString,unsigned int>>>& lcv,
				map<string,vector<multimap<UnicodeString, rec>>>& lpcv,
				set<UnicodeString>& emph,
				const UnicodeString& sep,
				bool detokenize ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "make a word inventory on sentences in:" << docName << endl;
    }
  }
  unsigned int grand_total = 0;
  unsigned int mis_lem = 0;
  unsigned int mis_pos = 0;
  string sep8 = TiCC::UnicodeToUTF8(sep);
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
    string lang = sents[s]->language(); // the language this sentence is in
    // ignore language labels on the invidual words!
    if ( default_language != "all" ){
      if ( languages.find( lang ) == languages.end() ){
	// lang is 'unwanted', just add to the default
	if ( default_language == "skip" ){
	  continue;
	}
	lang = default_language;
      }
    }
#pragma omp critical
    {
      wcv[lang].resize(max_ng+1);
      lcv[lang].resize(max_ng+1);
      lpcv[lang].resize(max_ng+1);
      w_totals_per_n[lang].resize(max_ng+1);
      l_totals_per_n[lang].resize(max_ng+1);
      p_totals_per_n[lang].resize(max_ng+1);
    }

    vector<wlp_rec> data;
    for ( const auto& w : words ){
      wlp_rec rec;
      try {
	UnicodeString uword = w->text(classname,detokenize==false);
	if ( lowercase ){
	  uword.toLower();
	}
	rec.word = uword;
      }
      catch(...){
#pragma omp critical
	{
	  cerr << "FoLiA-stats: missing text for word " << w->id() << endl;
	}
	break;
      }
      try {
	rec.lemma = TiCC::UnicodeFromUTF8(w->lemma(frog_mblemtagset));
      }
      catch(...){
	try {
	  rec.lemma = TiCC::UnicodeFromUTF8(w->lemma());
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
    for ( unsigned int ng = min_ng; ng <= max_ng; ++ng ){
      if ( ng > data.size() ){
	break;
      }
      for ( unsigned int i=0; i <= data.size() - ng ; ++i ){
	UnicodeString multiw;
	UnicodeString multil;
	string multip;
	bool lem_mis = false;
	bool pos_mis = false;
	for ( unsigned int j=0; j < ng; ++j ){
	  multiw += data[i+j].word;
	  if ( data[i+j].lemma.isEmpty() ){
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
	  if ( j < ng-1 ){
	    multiw += sep;
	    if ( !lem_mis )
	      multil += sep;
	    if ( !pos_mis )
	      multip += sep8;
	  }
	}
	++grand_total;
	++w_totals_per_n[lang][ng];
	if ( lem_mis ){
	  ++mis_lem;
	}
	else {
	  ++lem_count;
	  ++l_totals_per_n[lang][ng];
	}
	if ( pos_mis ){
	  ++mis_pos;
	}
	else {
	  ++pos_count;
	  ++p_totals_per_n[lang][ng];
	}
#pragma omp critical
	{
	  ++wcv[lang][ng][multiw];
	}

	if ( !multil.isEmpty() ){
#pragma omp critical
	  {
	    ++lcv[lang][ng][multil];
	  }
	}
	if ( !multip.empty() ){
#pragma omp critical
	  {
	    auto& lpc0 = lpcv[lang][ng];
	    multimap<UnicodeString, rec >::iterator it = lpc0.find(multil);
	    if ( it == lpc0.end() ){
	      rec tmp;
	      tmp.count = 1;
	      tmp.pc[multip]=1;
	      lpcv[lang][ng].insert( make_pair(multil,tmp) );
	    }
	    else {
	      ++it->second.count;
	      ++it->second.pc[multip];
	    }
	  }
	}
      }
    }
    if ( verbose && mis_lem ){
#pragma omp critical
      {
	cout << "info: " << mis_lem
	     << " lemma's are missing in "  << d->id() << endl;
      }
    }
  }
  if ( verbose && mis_pos ){
#pragma omp critical
    {
      cout << "info: " << mis_pos
	   << " POS tags are missing in "  << d->id() << endl;
    }
  }
  return grand_total;
}

size_t doc_str_inventory( const Document *d,
			  const string& docName,
			  int min_ng,
			  int max_ng,
			  map<string,vector<unsigned int>>& totals_per_n,
			  bool lowercase,
			  const string& default_language,
			  const set<string>& languages,
			  map<string,vector<map<UnicodeString,unsigned int>>>& wcv,
			  set<UnicodeString>& emph,
			  const UnicodeString& sep,
			  bool detokenize ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "make a str inventory on:" << docName << endl;
    }
  }
  size_t grand_total = 0;
  vector<String*> strings = d->doc()->select<String>();
  if ( verbose ){
#pragma omp critical
    {
      cout << "found " << strings.size() << " strings" << endl;
    }
  }
  string lang = d->language();
  if ( default_language != "all" ){
    if ( languages.find( lang ) == languages.end() ){
      // lang is 'unwanted', just add to the default
      if ( default_language == "skip" ){
	return grand_total;
      }
      lang = default_language;
    }
  }
  vector<UnicodeString> data;
  for ( const auto& s : strings ){
    UnicodeString us;
    try {
      us = s->text(classname,detokenize==false);
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
    data.push_back( us );
  }
  if ( data.size() != strings.size() ) {
#pragma omp critical
    {
      cerr << "FoLiA-stats: Missing words! skipped document " << docName << endl;
    }
    return 0;
  }

  add_emph_inventory( data, emph );
  grand_total += add_word_inventory( data, wcv[lang], min_ng, max_ng, totals_per_n[lang], sep );
  return grand_total;
}

size_t par_str_inventory( const Document *d, const string& docName,
			  int min_ng,
			  int max_ng,
			  map<string,vector<unsigned int>>& totals_per_n,
			  bool lowercase,
			  const string& default_language,
			  const set<string>& languages,
			  map<string,vector<map<UnicodeString,unsigned int>>>& wcv,
			  set<UnicodeString>& emph,
			  const UnicodeString& sep,
			  bool detokenize ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "make a par_str inventory on:" << docName << endl;
    }
  }
  size_t grand_total = 0;
  vector<Paragraph*> pars = d->paragraphs();
  for ( const auto& p : pars ){
    vector<String*> strings = p->select<String>();
    if ( verbose ){
#pragma omp critical
      {
	cout << "found " << strings.size() << " strings" << endl;
      }
    }
    string lang = p->language();
    if ( default_language != "all" ){
      if ( languages.find( lang ) == languages.end() ){
	// lang is 'unwanted', just add to the default
	if ( default_language == "skip" ){
	  continue;
	}
	lang = default_language;
      }
    }
    vector<UnicodeString> data;
    for ( const auto& s : strings ){
      UnicodeString us;
      try {
	us = s->text(classname,detokenize==false);
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
      data.push_back( us );
    }
    if ( data.size() != strings.size() ) {
#pragma omp critical
      {
	cerr << "FoLiA-stats: Missing words! skipped paragraph " << p->id() << " in " << docName << endl;
      }
      return 0;
    }

    add_emph_inventory( data, emph );
    grand_total += add_word_inventory( data, wcv[lang], min_ng, max_ng, totals_per_n[lang], sep );
  }
  return grand_total;
}

vector<FoliaElement*> gather_nodes( const Document *doc, const string& docName,
				    const set<string>& tags ){
  vector<FoliaElement*> result;
  for ( const auto& tag : tags ){
    ElementType et;
    try {
      et = stringToET( tag );
    }
    catch ( ... ){
#pragma omp critical (logging)
      {
	cerr << "the string '" << tag
	     << "' doesn't represent a known FoLiA tag" << endl;
	exit(EXIT_FAILURE);
      }
    }
    vector<FoliaElement*> v = doc->doc()->select( et, true );
#pragma omp critical (logging)
    {
      cout << "document '" << docName << "' has " << v.size() << " "
	   << tag << " nodes " << endl;
    }
    result.insert( result.end(), v.begin(), v.end() );
  }
  return result;
}


size_t text_inventory( const Document *d, const string& docName,
		       int min_ng,
		       int max_ng,
		       map<string,vector<unsigned int>>& totals_per_n,
		       bool lowercase,
		       const string& default_language,
		       const set<string>& languages,
		       const set<string>& tags,
		       map<string,vector<map<UnicodeString,unsigned int>>>& wcv,
		       set<UnicodeString>& emph,
		       const UnicodeString& sep,
		       bool detokenize ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "make a text inventory on:" << docName << endl;
    }
  }
  size_t grand_total = 0;
  vector<FoliaElement *> nodes = gather_nodes( d, docName, tags );
  for ( const auto& node : nodes ){
    string lang = node->language(); // get the language the node is in
    if ( default_language != "all" ){
      if ( languages.find( lang ) == languages.end() ){
	// lang is 'unwanted', just add to the default
	if ( default_language == "skip" ){
	  continue;
	}
	lang = default_language;
      }
    }
    UnicodeString us;
    try {
      us = node->text(classname,detokenize==false);
      if ( lowercase ){
	us.toLower();
      }
    }
    catch(...){
    }
    if ( us.isEmpty() ){
      if ( verbose ){
#pragma omp critical
	{
	  cout << "found NO string in node: " << node->id() << endl;
	}
      }
      continue;
    }
    vector<UnicodeString> data = TiCC::split( us );
    if ( verbose ){
#pragma omp critical
      {
	cout << "found string: '" << us << "'" << endl;
	if ( data.size() <= 1 ){
	  cout << "with no substrings" << endl;
	}
	else {
	  using TiCC::operator<<;
	  cout << "with " << data.size() << " substrings: " << data << endl;
	}
      }
    }
    add_emph_inventory( data, emph );
    grand_total += add_word_inventory( data, wcv[lang], min_ng, max_ng, totals_per_n[lang], sep );
  }
  return grand_total;
}

void usage( const string& name ){
  cerr << "Usage: " << name << " [options] file/dir" << endl << endl;
  cerr << "FoLiA-stats will produce ngram statistics for a FoLiA file, " << endl;
  cerr << "or a whole directory of FoLiA files " << endl;
  cerr << "The output will be a 2 or 4 columned tab separated file, extension: *tsv " << endl;
  cerr << "\t (4 columns when -p is specified)" << endl;
  cerr << "\t--clip='factor'\t\t clipping factor. " << endl;
  cerr << "\t\t(entries with frequency <= 'factor' will be ignored). " << endl;
  cerr << "\t-p\t output percentages too. " << endl;
  cerr << "\t--lower\t Lowercase all words" << endl;
  cerr << "\t--separator='sep' \tconnect all n-grams with 'sep' (default is a space)" << endl;
  cerr << "\t--underscore\t Obsolete. Equals to --separator='_'" << endl;
  cerr << "\t--languages\t Lan1,Lan2,Lan3. (default='Lan1')." << endl;
  cerr << "\t\t languages that are not assigned to Lan1,Lan2,... are counted as Lan1" << endl;
  cerr << "\t\t If Lan1='skip' all languages not mentioned as Lan2,... are ignored." << endl;
  cerr << "\t\t If Lan1='all' all detected languages are counted." << endl;
  cerr << "\t--lang='lan' (default='none')." << endl;
  cerr << "\t\t This is a shorthand for --languages=skip,lan." << endl;
  cerr << "\t\t The value 'none' (the default) means: accept all languages." << endl;
  cerr << "\t\t Meaning: only count words from 'lan' ignoring all other languages." << endl;
  cerr << "\t--ngram='count'\t\t construct n-grams of length 'count'" << endl;
  cerr << "\t--max-ngram='max'\t construct ALL n-grams upto a length of 'max'" << endl;
  cerr << "\t\t If --ngram='min' is specified too, ALL n-grams from 'min' upto 'max' are created" << endl;
  cerr << "\t--mode='mode' Special actions:" << endl;
  cerr << "\t\t 'string_in_doc' Collect ALL <str> nodes from the document and handle them as one long Sentence." << endl;
  cerr << "\t\t 'lemma_pos' When processsing nodes, also collect lemma and POS tag information. THIS implies --tags=s" << endl;
  cerr << "\t--tags='tags' collect text from all nodes in the list 'tags'" << endl;
  cerr << "\t-s\t equal to --tags=p" << endl;
  cerr << "\t-S\t equal to --mode=string_in_doc" << endl;
  cerr << "\t--class='name'\t When processing <str> nodes, use 'name' as the folia class for <t> nodes." << endl;
  cerr << "\t\t (default is 'current')" << endl;
  cerr << "\t --collect\t collect all n-gram values in one file." << endl;
  cerr << "\t--hemp=<file>\t Create a historical emphasis file. " << endl;
  cerr << "\t\t (words consisting of single, space separated letters)" << endl;
  cerr << "\t--detokenize when processing FoLiA with ucto tokenizer info, UNDO that tokenization. (default is to keep it)" << endl;
  cerr << "\t-t <threads>\n\t--threads <threads> Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-h or --help\t this message" << endl;
  cerr << "\t-v or --verbose\t very verbose output." << endl;
  cerr << "\t-V or --version\t show version " << endl;
  cerr << "\t-e\t expr: specify the expression all input files should match with." << endl;
  cerr << "\t-o\t name of the output file(s) prefix." << endl;
  cerr << "\t-R\t search the dirs recursively (when appropriate)." << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "hVvpe:t:o:RsS",
			 "class:,clip:,lang:,languages:,ngram:,max-ngram:,"
			 "lower,hemp:,underscore,separator:,help,version,"
			 "mode:,verbose,collect,aggregate,tags:,threads:,"
			 "detokenize" );
  try {
    opts.init(argc,argv);
  }
  catch( TiCC::OptionError& e ){
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
  int max_NG = 1;
  int min_NG = 0;
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
  bool lowercase = opts.extract("lower");
  bool dopercentage = opts.extract('p');
  bool aggregate = opts.extract("aggregate");
  if ( dopercentage && aggregate ){
    cerr << "FoLiA-stats: --aggregate and -p conflict." << endl;
    return EXIT_FAILURE;
  }
  bool detokenize = opts.extract( "detokenize" );
  set<string> tags;
  string tagsstring;
  opts.extract( "tags", tagsstring );
  if ( !tagsstring.empty() ){
    vector<string> parts = TiCC::split_at( tagsstring, "," );
    for( const auto& t : parts ){
      tags.insert( t );
    }
  }
  string modes;
  opts.extract("mode", modes );
  Mode mode = UNKNOWN;
  if ( !modes.empty() ){
    mode = stringToMode( modes );
    if ( mode == UNKNOWN ){
      if ( modes == "text_in_par" ){
	tags.insert("p");
      }
      else {
	cerr << "FoLiA-stats: unknown --mode " << modes << endl;
	return EXIT_FAILURE;
      }
    }
    else if ( !tags.empty() ){
      cerr << "FoLiA-stats: --mode cannot be combined with --tags option" << endl;
      return EXIT_FAILURE;
    }
  }
  string hempName;
  opts.extract("hemp", hempName );
  bool recursiveDirs = opts.extract( 'R' );
  if ( opts.extract( 's' ) ) {
    if ( !modes.empty() || !tags.empty() ){
      cerr << "FoLiA-stats: old style -s option cannot be combined with --mode or --tags option" << endl;
      return EXIT_FAILURE;
    }
    else {
      tags.insert( "p" );
    }
  }
  if ( opts.extract( 'S' ) ){
    if ( !modes.empty() || !tags.empty() ){
      cerr << "FoLiA-stats: old style -S option cannot be combined with --mode or --tags option" << endl;
      return EXIT_FAILURE;
    }
    else {
      mode = S_IN_D;
    }
  }
  UnicodeString sep = " ";
  if( opts.extract( "separator", value ) ){
    sep = TiCC::UnicodeFromUTF8(value);
  }
  if ( opts.extract( "underscore" ) ){
    if ( sep != " " ){
      cerr << "--separator and --underscore conflict!" << endl;
      exit(EXIT_FAILURE);
    }
    sep = "_";
  }
  if ( !opts.extract( 'o', outputPrefix ) ){
    cerr << "FoLiA-stats: an output filename prefix is required. (-o option) " << endl;
    exit(EXIT_FAILURE);
  }
  if ( opts.extract("clip", value ) ){
    if ( !TiCC::stringTo(value, clip ) ){
  cerr << "FoLiA-stats: illegal value for --clip (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  if ( opts.extract("ngram", value ) ){
    if ( !TiCC::stringTo(value, min_NG ) ){
      cerr << "FoLiA-stats: illegal value for --ngram (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  if ( opts.extract("max-ngram", value ) ){
    if ( !TiCC::stringTo(value, max_NG ) ){
      cerr << "FoLiA-stats: illegal value for --max-ngram (" << value << ")" << endl;
      exit(EXIT_FAILURE);
    }
  }
  if ( min_NG == 0 ){
    min_NG = 1;
  }
  else if ( max_NG < min_NG ){
    max_NG = min_NG;
  }
  value = "1";
  if ( !opts.extract( 't', value ) ){
    opts.extract( "threads", value );
  }
#ifdef HAVE_OPENMP
  int numThreads=1;
  if ( TiCC::lowercase(value) == "max" ){
    numThreads = omp_get_max_threads() - 2;
  }
  else {
    if ( !TiCC::stringTo(value,numThreads) ) {
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
  }
  omp_set_num_threads( numThreads );
#else
  if ( value != "1" ){
    cerr << "unable to set number of threads!.\nNo OpenMP support available!"
	 <<endl;
    exit(EXIT_FAILURE);
  }
#endif
  if ( opts.extract("languages", value ) ){
    vector<string> parts = TiCC::split_at( value, "," );
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
    if ( !languages.empty() ){
      cerr << "FoLiA-stats: --lang and --languages options conflict. Use only one!" << endl;
      exit( EXIT_FAILURE );
    }
    else if ( value != "none" ){
      default_language = "skip"; // skip except when the 'lang' value is found
      languages.insert( value );
    }
  }
  bool collect = opts.extract("collect");
  if ( languages.empty() ){
    default_language = "none"; // don't care, any language will do
  }
  else if ( collect ){
    cerr << "--collect cannot be combined with --languages" << endl;
    exit( EXIT_FAILURE );
  }
  opts.extract('e', expression );
  opts.extract( "class", classname );
  if ( !opts.empty() ){
    cerr << "FoLiA-stats: unsupported options : " << opts.toString() << endl;
    usage(progname);
    exit(EXIT_FAILURE);
  }

  vector<string> massOpts = opts.getMassOpts();
  if ( massOpts.empty() ){
    cerr << "FoLiA-stats: no file or dir specified!" << endl;
    exit(EXIT_FAILURE);
  }
  string dir_name;
  vector<string> fileNames;
  if ( massOpts.size() > 1 ){
    fileNames = massOpts;
  }
  else {
    dir_name = massOpts[0];
    fileNames = TiCC::searchFilesMatch( dir_name, expression, recursiveDirs );
  }
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
  map<string,vector<map<UnicodeString,unsigned int>>> wcv; // word-freq list per language
  map<string,vector<map<UnicodeString,unsigned int>>> lcv; // lemma-freq list per language
  map<string,vector<multimap<UnicodeString, rec>>> lpcv; // lemma-pos freq list per language
  unsigned int wordTotal =0;
  map<string,vector<unsigned int>> wordTotals;  // totals per language
  map<string,vector<unsigned int>> lemmaTotals; // totals per language
  map<string,vector<unsigned int>> posTotals;   // totals per language
  set<UnicodeString> emph;
  int doc_counter = toDo;
  unsigned int fail_docs = 0;
#pragma omp parallel for shared(fileNames,wordTotal,wordTotals,posTotals,lemmaTotals,wcv,lcv,lpcv,emph,doc_counter,fail_docs) schedule(dynamic)
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
	--doc_counter;
	++fail_docs;
      }
      continue;
    }
    unsigned int word_count = 0;
    unsigned int lem_count = 0;
    unsigned int pos_count = 0;
    switch ( mode ){
    case L_P:
      word_count = doc_sent_word_inventory( d, docName, min_NG, max_NG,
					    wordTotals, lemmaTotals, posTotals,
					    lem_count, pos_count,
					    lowercase,
					    default_language, languages,
					    wcv, lcv, lpcv,
					    emph, sep, detokenize );
      break;
    case S_IN_D:
      word_count = doc_str_inventory( d, docName, min_NG, max_NG,
				      wordTotals, lowercase,
				      default_language, languages,
				      wcv, emph, sep, detokenize );
      break;
    default:
      if ( !tags.empty() ){
	word_count = text_inventory( d, docName, min_NG, max_NG,
				     wordTotals, lowercase,
				     default_language, languages, tags,
				     wcv, emph, sep, detokenize );
      }
      else {
	cerr << "FoLiA-stats: not yet implemented mode: " << modes << endl;
	exit( EXIT_FAILURE );
      }
    }
#pragma omp critical
    {
      wordTotal += word_count;
      cout << "Processed :" << docName << " with " << word_count << " "
	   << "n-grams,"
	   << " " << lem_count << " lemmas, and " << pos_count << " POS tags."
	   << " still " << --doc_counter << " files to go." << endl;
    }
    delete d;
  }

  if ( toDo ){
    cout << "done processsing directory '" << dir_name << "'" << endl;
  }
  if ( fail_docs == toDo ){
    cerr << "no documents were successfully handled!" << endl;
    return EXIT_FAILURE;
  }
  if ( !hempName.empty() ){
    if (!TiCC::createPath( hempName ) ){
      cerr << "FoLiA-stats: unable to create historical emphasis file: " << hempName << endl;
    }
    ofstream out( hempName );
    for( auto const& it : emph ){
      out << it << endl;
    }
    cout << "historical emphasis stored in: " << hempName << endl;
  }
  cout << "start calculating the results" << endl;
  cout << "in total " << wordTotal << " " << "n-grams were found.";
  if ( toDo > 1 ){
    cout << "in " << toDo << " FoLiA documents.";
  }
  cout << endl;
  if ( aggregate ){
    string filename;
    filename = outputPrefix + ".agg.freqlist";
    create_agg_list( wcv, filename, clip, min_NG, max_NG );
  }
  else {
#pragma omp parallel sections
    {
#pragma omp section
      {
	string filename;
	filename = outputPrefix + ".wordfreqlist";
	if ( collect ){
	  create_collected_wf_list( wcv, filename, clip, min_NG, max_NG,
				    wordTotals, dopercentage,
				    default_language );
	}
	else {
	  create_wf_list( wcv, filename, clip, min_NG, max_NG,
			  wordTotals, dopercentage );
	}
      }
#pragma omp section
      {
	if ( mode == L_P ){
	  string filename;
	  filename = outputPrefix + ".lemmafreqlist";
	  if ( collect ){
	    create_collected_lf_list( lcv, filename, clip, min_NG, max_NG,
				      lemmaTotals, dopercentage,
				      default_language );
	  }
	  else {
	    create_lf_list( lcv, filename, clip, min_NG, max_NG,
			    lemmaTotals, dopercentage );
	  }
	}
      }
#pragma omp section
      {
	if ( mode == L_P ){
	  string filename;
	  filename = outputPrefix + ".lemmaposfreqlist";
	  if ( collect ){
	    create_collected_lpf_list( lpcv, filename, clip, min_NG, max_NG,
				       posTotals, dopercentage,
				       default_language );
	  }
	  else {
	    create_lpf_list( lpcv, filename, clip, min_NG, max_NG,
			     posTotals, dopercentage );
	  }
	}
      }
    }
  }
  return EXIT_SUCCESS;
}
