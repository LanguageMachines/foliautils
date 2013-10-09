#include <cstdlib>
#include <getopt.h>
#include <string>
#include <set>
#include <iostream>
#include <fstream>

#include "ticcutils/StringOps.h"
#include "libfolia/foliautils.h"
#include "unicode/ustream.h"
#include "unicode/unistr.h"
#include "unicode/uchar.h"

#include "config.h"

using namespace	std;

enum S_Class { UNK, PUNCT, CLEAN };

ostream& operator<<( ostream& os, const S_Class& cl ){
  switch ( cl ){
  case CLEAN:
    os << "Clean";
    break;
  case UNK:
    os << "Unknown";
    break;
  case PUNCT:
    os << "Punctuated";
    break;
  default:
    os << "WTF";
  }
  return os;
}

bool fillAlpha( istream& is, set<UChar>& alphabet ){
  string line;
  while ( getline( is, line ) ){
    UnicodeString us = folia::UTF8ToUnicode( line );
    us.toLower();
    for ( int i=0; i < us.length(); ++i ){
      alphabet.insert( us[i] );
    }
    us.toUpper();
    for ( int i=0; i < us.length(); ++i ){
      alphabet.insert( us[i] );
    }
  }
  return true;
}

static UChar punctList[] = { '<', '>', '[', ']', '{', '}' };
static set<UChar> mypuncts( punctList,
			    punctList + sizeof( punctList )/sizeof( UChar ) );

bool isPunct( UChar k  ){
  if ( u_ispunct( k ) )
    return true;
  if (  u_charType( k ) == U_CURRENCY_SYMBOL )
    return true;
  if ( mypuncts.find( k ) != mypuncts.end() )
    return true;
  return false;
}

bool depunct( const UnicodeString& us, UnicodeString& result ){
  result.remove();
  if ( us.length() < 2 )
    return false;
  int i = 0;
  for ( i; i < us.length(); ++i ){
    // skip leading punctuation
    if ( !isPunct( us[i] ) )
      break;
  }
  int j = us.length()-1;
  for ( j; j >= 0; j-- ){
    // skip trailing punctuation
    if ( !isPunct( us[j] ) )
      break;
  }
  if ( i == 0 && j == us.length()-1 ){
    return false; // no leading/trailing puncts
  }
  else {
    for ( int k = i; k <= j; ++k ){
      result += us[k];
    }
    //    cerr << "depunct '" << us << "' ==> '" << result << "'" << endl;
    return true;
  }
}

S_Class classify( const UnicodeString& word,
		  set<UChar>& alphabet ){
  int is_digit = 0;
  int is_in = 0;
  int word_len = word.length();
  int is_punct = 0; //embedded
  int is_out = 0;
  if ( word_len == 1 )
    return CLEAN;

  for ( int i=0; i < word_len; ++i ){
    UChar uchar = word[i];
    if ( isPunct( uchar ) ){
      ++is_punct;
    }
    else if ( u_isdigit( uchar ) ){
      ++is_digit;
    }
    else if ( alphabet.empty() ||
	      ( alphabet.find( word[i] ) != alphabet.end() ) ){
      ++is_in;
    }
    else {
      ++is_out;
    }
  }
  if ( double(is_in + is_digit + is_punct)/word_len > 0.8 ){
    return CLEAN;
  }
  if ( double(is_digit + is_punct)/word_len > 0.8 ){
    return CLEAN;
  }
  else {
    return UNK;
  }
}

S_Class classify( const string& word, set<UChar>& alphabet,
		  string& punct ){
  S_Class result = CLEAN;
  punct.clear();
  UnicodeString us = folia::UTF8ToUnicode( word );
  UnicodeString ps;
  if ( depunct( us, ps  ) ){
    if ( ps.length() == 0 ){
      // only punctuation
      result = UNK;
    }
    else {
      result = classify( ps, alphabet );
      if ( result == CLEAN ){
	punct = folia::UnicodeToUTF8( ps );
	result = PUNCT;
      }
      else
	result = UNK;
    }
  }
  else {
    result = classify( us, alphabet );
  }
  cerr << "classify(" << word << ") ==> " << result << endl;
  return result;
}

int main( int argc, char *argv[] ){
  if ( argc < 2	){
    cerr << "Usage: [-a alphabet] [-c clip] dir/filename " << endl;
    exit(EXIT_FAILURE);
  }
  int opt;
  int clip = 10;
  string alphafile;
  while ((opt = getopt(argc, argv, "c:a:")) != -1) {
    switch (opt) {
    case 'c':
      clip = atoi(optarg);
      break;
    case 'a':
      alphafile = optarg;
      break;
    case 'V':
      cerr << "UNK" << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'h':
      cerr << "Usage: [options] freqencyfile" << endl;
      cerr << "\t-c\t clipping factor. " << endl;
      cerr << "\t\t\t\t(entries with frequency <= this factor will be ignored). " << endl;
      cerr << "\t-a\t name of the alphabet file" << endl;
      cerr << "\t-h\t this messages " << endl;
      cerr << "\t-V\t show version " << endl;
      cerr << "\t" << argv[0] << " will filter a wordfrequency list (in FoLiA-stats format) " << endl;
      cerr << "\t\tThe output will be a cleaned wordfrequency file, an unknown wordlist and a frequency list" << endl;
      cerr << "\t\tof 'clean' words with leading/trailing punctuation." << endl;
      exit(EXIT_SUCCESS);
      break;
    default: /* '?' */
      cerr << "Usage: " << argv[0] << " [options] frequencyfile " << endl;
      exit(EXIT_FAILURE);
    }
  }

  set<UChar> alphabet;
  string file_name = argv[optind];
  ifstream is( file_name.c_str() );
  if ( !is ){
    cerr << "unable to open frequency file: " << file_name << endl;
    exit(EXIT_FAILURE);
  }
  if ( !alphafile.empty() ){
    ifstream as( alphafile.c_str() );
    if ( !as ){
      cerr << "unable to open alphabet file: " << alphafile << endl;
      exit(EXIT_FAILURE);
    }
    if ( !fillAlpha( as, alphabet ) ){
      cerr << "serious problems reading alphabet file: " << alphafile << endl;
      exit(EXIT_FAILURE);
    }
  }
  string unk_file_name = file_name + ".unk";
  string clean_file_name = file_name + ".clean";
  string punct_file_name = file_name + ".punct";

  ofstream cs( clean_file_name.c_str() );
  if ( !cs ){
    cerr << "unable to open output file: " << clean_file_name << endl;
    exit(EXIT_FAILURE);
  }
  ofstream us( unk_file_name.c_str() );
  if ( !us ){
    cerr << "unable to open output file: " << unk_file_name << endl;
    exit(EXIT_FAILURE);
  }
  ofstream ps( punct_file_name.c_str() );
  if ( !ps ){
    cerr << "unable to open output file: " << unk_file_name << endl;
    exit(EXIT_FAILURE);
  }

  map<string,unsigned int> clean_words;
  map<string,unsigned int> unk_words;
  map<string,string> punct_words;
  string line;
  while ( getline( is, line ) ){
    vector<string> v;
    int n = TiCC::split_at( line, v, "\t" );
    if ( n != 4 ){
      cerr << "frequency file in wrong format!" << endl;
      exit(EXIT_FAILURE);
    }
    unsigned int freq = TiCC::stringTo<unsigned int>(v[1]);
    if ( freq > clip ){
      clean_words[v[0]] += freq;
    }
    else {
      string pun;
      S_Class cl = classify( v[0], alphabet, pun );
      switch ( cl ){
      case CLEAN:
	clean_words[v[0]] += freq;
	break;
      case UNK:
	unk_words[v[0]] += freq;
	break;
      case PUNCT:
	punct_words[v[0]] = pun;
	clean_words[pun] += freq;
	break;
      }
    }
  }
  cout << "generating output files" << endl;
  map<unsigned int, set<string> > wf;
  map<string,unsigned int >::const_iterator it = clean_words.begin();
  while( it != clean_words.end()  ){
    wf[it->second].insert( it->first );
    ++it;
  }
  map<unsigned int, set<string> >::const_reverse_iterator wit = wf.rbegin();
  while ( wit != wf.rend() ){
    set<string>::const_iterator sit = wit->second.begin();
    while ( sit != wit->second.end() ){
      cs << *sit << "\t" << wit->first << endl;
      ++sit;
    }
    ++wit;
  }
  wf.clear();
  it = unk_words.begin();
  while( it != unk_words.end()  ){
    wf[it->second].insert( it->first );
    ++it;
  }
  wit = wf.rbegin();
  while ( wit != wf.rend() ){
    set<string>::const_iterator sit = wit->second.begin();
    while ( sit != wit->second.end() ){
      us << *sit << "\t" << wit->first << endl;
      ++sit;
    }
    ++wit;
  }

  map<string,string>::const_iterator it2 = punct_words.begin();
  while ( it2 != punct_words.end() ){
    ps << it2->first << "\t" << it2->second << endl;
    ++it2;
  }
  cout << "done!" << endl;
}
