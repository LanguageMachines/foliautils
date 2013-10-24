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


void create_wf_list( const map<string, unsigned int>& wc,
		     const string& filename, unsigned int total,
		     unsigned int clip ){
  ofstream os( filename.c_str() );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  map<unsigned int, set<string> > wf;
  map<string,unsigned int >::const_iterator cit = wc.begin();
  while( cit != wc.end()  ){
    if ( cit->second <= clip ){
      --total;
    }
    else {
      wf[cit->second].insert( cit->first );
    }
    ++cit;
  }
  unsigned int sum=0;
  map<unsigned int, set<string> >::const_reverse_iterator wit = wf.rbegin();
  while ( wit != wf.rend() ){
    set<string>::const_iterator sit = wit->second.begin();
    while ( sit != wit->second.end() ){
      sum += wit->first;
      os << *sit << "\t" << wit->first << "\t" << sum << "\t"
	 << 100 * double(sum)/total << endl;
      ++sit;
    }
    ++wit;
  }
#pragma omp critical
  {
    cout << "created WordFreq list '" << filename << "'" << endl;
  }
}

struct rec {
  unsigned int count;
  map<string,unsigned int> pc;
};

void create_lf_list( const map<string, unsigned int>& lc,
		     const string& filename, unsigned int total,
		     unsigned int clip ){
  ofstream os( filename.c_str() );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  map<unsigned int, set<string> > lf;
  map<string,unsigned int >::const_iterator cit = lc.begin();
  while( cit != lc.end()  ){
    if ( cit->second <= clip ){
      --total;
    }
    else {
      lf[cit->second].insert( cit->first );
    }
    ++cit;
  }

  unsigned int sum=0;
  map<unsigned int, set<string> >::const_reverse_iterator wit = lf.rbegin();
  while ( wit != lf.rend() ){
    set<string>::const_iterator sit = wit->second.begin();
    while ( sit != wit->second.end() ){
      sum += wit->first;
      os << *sit << "\t" << wit->first << "\t" << sum << "\t"
	 << 100* double(sum)/total << endl;
      ++sit;
    }
    ++wit;
  }
#pragma omp critical
  {
    cout << "created LemmaFreq list '" << filename << "'" << endl;
  }
}

void create_lpf_list( const multimap<string, rec>& lpc,
		      const string& filename, unsigned int total,
		      unsigned int clip ){
  ofstream os( filename.c_str() );
  if ( !os ){
    cerr << "failed to create outputfile '" << filename << "'" << endl;
    exit(EXIT_FAILURE);
  }
  multimap<unsigned int, pair<string,string> > lpf;
  multimap<string,rec>::const_iterator cit = lpc.begin();
  while( cit != lpc.end()  ){
    map<string,unsigned int>::const_iterator pit = cit->second.pc.begin();
    while ( pit != cit->second.pc.end() ){
      if ( pit->second <= clip ){
	--total;
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
  multimap<unsigned int, pair<string,string> >::const_reverse_iterator wit = lpf.rbegin();
  while ( wit != lpf.rend() ){
    sum += wit->first;
    os << wit->second.first << " " << wit->second.second << "\t"
       << wit->first << "\t" << sum << "\t" << 100 * double(sum)/total << endl;
    ++wit;
  }
#pragma omp critical
  {
    cout << "created LemmaPosFreq list '" << filename << "'" << endl;
  }
}

struct wlp_rec {
  string word;
  string lemma;
  string pos;
};

const string frog_cgntagset = "http://ilk.uvt.nl/folia/sets/frog-mbpos-cgn";
const string frog_mblemtagset = "http://ilk.uvt.nl/folia/sets/frog-mblem-nl";

size_t word_inventory( const Document *d, const string& docName,
		       size_t nG,
		       bool lowercase,
		       map<string,unsigned int>& wc,
		       map<string,unsigned int>& lc,
		       multimap<string, rec>& lpc ){
  size_t wordTotal = 0;
  vector<Sentence *> sents = d->sentences();
  for ( unsigned int s=0; s < sents.size(); ++s ){
    vector<Word*> words = sents[s]->words();
    if ( words.size() < nG )
      continue;
    vector<wlp_rec> data;
    for ( unsigned int i=0; i < words.size(); ++i ){
      wlp_rec rec;
      try {
	if ( lowercase ){
	  UnicodeString uword = UTF8ToUnicode( words[i]->str() );
	  uword.toLower();
	  rec.word = UnicodeToUTF8( uword );
	}
	else
	  rec.word = words[i]->str();
      }
      catch(...){
#pragma omp critical
	{
	  cerr << "missing text for word " << words[i]->id() << endl;
	}
	break;
      }
      try {
	rec.lemma = words[i]->lemma(frog_mblemtagset);
      }
      catch(...){
	try {
	  rec.lemma = words[i]->lemma();
	}
	catch(...){
	  rec.lemma = "";
	}
      }
      try {
	rec.pos = words[i]->pos(frog_cgntagset);
      }
      catch(...){
	try {
	  rec.pos = words[i]->pos();
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
	cerr << "Error: Missing words! skipped sentence " << sents[s]->id() << " in " << docName << endl;
      }
      continue;
    }

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
	  multiw += " ";
	  if ( !lem_mis )
	    multil += " ";
	  if ( !pos_mis )
	    multip += " ";
	}
      }
      ++wordTotal;
#pragma omp critical
      {
	++wc[multiw];
      }

      if ( multil.empty() ){
#pragma omp critical
	{
	  cerr << "info: some lemma's are missing in "  << sents[s]->id() << endl;
	}
      }
      else {
#pragma omp critical
	{
	  ++lc[multil];
	}

	if ( multip.empty() ){
#pragma omp critical
	  {
	    cerr << "info: some POS tags are missing in "  << sents[s]->id() << endl;
	  }
	}
	else {
#pragma omp critical
	  {
	    multimap<string, rec >::iterator it = lpc.find(multil);
	    if ( it == lpc.end() ){
	      rec tmp;
	      tmp.count = 1;
	      tmp.pc[multip]=1;
	      lpc.insert( make_pair(multil,tmp) );
	    }
	    else {
	      ++it->second.count;
	      ++it->second.pc[multip];
	    }
	  }
	}
      }
    }
  }
  return wordTotal;
}

size_t str_inventory( const Document *d, const string& docName,
		      size_t nG,
		      bool lowercase,
		      map<string,unsigned int>& wc ){
  size_t wordTotal = 0;
  vector<String*> strings = d->doc()->select<String>();
  if ( strings.size() < nG )
    return 0;
  vector<string> data;
  for ( unsigned int i=0; i < strings.size(); ++i ){
    UnicodeString us;
    try {
      us = strings[i]->text("OCR");
      if ( lowercase ){
	us.toLower();
      }
    }
    catch(...){
#pragma omp critical
      {
	cerr << "missing text for word " << strings[i]->id() << endl;
      }
      break;
    }
    string w = UnicodeToUTF8( us );
    data.push_back( w );
  }
  if ( data.size() != strings.size() ) {
#pragma omp critical
    {
      cerr << "Error: Missing words! skipped document " << docName << endl;
    }
    return 0;
  }

  for ( unsigned int i=0; i <= data.size() - nG ; ++i ){
    string multiw;
    for ( size_t j=0; j < nG; ++j ){
      multiw += data[i+j];
      if ( j < nG-1 ){
	multiw += " ";
      }
    }
    ++wordTotal;
#pragma omp critical
    {
      ++wc[multiw];
    }
  }
  return wordTotal;
}

size_t par_str_inventory( const Document *d, const string& docName,
			  size_t nG,
			  bool lowercase,
			  map<string,unsigned int>& wc ){
  size_t wordTotal = 0;
  vector<Paragraph*> pars = d->paragraphs();
  for ( unsigned int p=0; p < pars.size(); ++p ){
    vector<String*> strings = pars[p]->select<String>();
    if ( strings.size() < nG )
      continue;
    vector<string> data;
    for ( unsigned int i=0; i < strings.size(); ++i ){
      UnicodeString us;
      try {
	us = strings[i]->text("OCR");
	if ( lowercase ){
	  us.toLower();
	}
      }
      catch(...){
#pragma omp critical
	{
	  cerr << "missing text for word " << strings[i]->id() << endl;
	}
      break;
      }
      string w = UnicodeToUTF8( us );
      data.push_back( w );
    }
    if ( data.size() != strings.size() ) {
#pragma omp critical
      {
	cerr << "Error: Missing words! skipped paragraph " << pars[p]->id() << " in " << docName << endl;
      }
      continue;
    }

    for ( unsigned int i=0; i <= data.size() - nG ; ++i ){
      string multiw;
      for ( size_t j=0; j < nG; ++j ){
	multiw += data[i+j];
	if ( j < nG-1 ){
	  multiw += " ";
	}
      }
      ++wordTotal;
#pragma omp critical
      {
	++wc[multiw];
      }
    }
  }
  return wordTotal;
}

int main( int argc, char *argv[] ){
  if ( argc < 2	){
    cerr << "Usage: [-t number_of_threads]  [-n Ngram count] dir/filename " << endl;
    exit(EXIT_FAILURE);
  }
  int opt;
  int clip = 0;
  int nG = 1;
  int numThreads = 1;
  bool recursiveDirs = false;
  bool lowercase = false;
  bool donoparstr = false;
  bool doparstr = false;
  string expression;
  string outPrefix;
  while ((opt = getopt(argc, argv, "c:e:hlt:sSn:o:RV")) != -1) {
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
    case 'n':
      nG = atoi(optarg);
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
    case 's':
      doparstr = true;
      break;
    case 'S':
      donoparstr = true;
      break;
    case 'V':
      cerr << PACKAGE_STRING << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'h':
      cerr << "Usage: [options] file/dir" << endl;
      cerr << "\t-c\t clipping factor. " << endl;
      cerr << "\t\t\t\t(entries with frequency <= this factor will be ignored). " << endl;
      cerr << "\t-l\t Lowercase all words" << endl;
      cerr << "\t-n\t Ngram count " << endl;
      cerr << "\t-s\t Process <str> nodes not <w> per <p> node" << endl;
      cerr << "\t-S\t Process <str> nodes not <w> per document" << endl;
      cerr << "\t-t\t number_of_threads" << endl;
      cerr << "\t-h\t this messages " << endl;
      cerr << "\t-V\t show version " << endl;
      cerr << "\t " << argv[0] << " will produce ngram statistics for a FoLiA file, " << endl;
      cerr << "\t or a whole directoy of FoLiA files " << endl;
      cerr << "\t-e\t expr: specify the expression all files should match with." << endl;
      cerr << "\t-o\t output prefix" << endl;
      cerr << "\t-R\t search the dirs recursively. (when appropriate)" << endl;
      exit(EXIT_SUCCESS);
      break;
    default: /* '?' */
      cerr << "Usage: [-t number_of_threads]  [-n Ngram count] dir/filename " << endl;
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
  map<string,unsigned int> lc;
  multimap<string, rec> lpc;
  unsigned int wordTotal =0;

#pragma omp parallel for shared(fileNames,wordTotal,wc,lc,lpc)
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
    size_t count = 0;
    if ( doparstr ){
      count = par_str_inventory( d, docName, nG, lowercase, wc );
    }
    else if ( donoparstr ){
      count = str_inventory( d, docName, nG, lowercase, wc );
    }
    else
      count = word_inventory( d, docName, nG, lowercase, wc, lc, lpc );
    wordTotal += count;
#pragma omp critical
    {
      cout << "Processed :" << docName << " with " << count << " words."
	   << " still " << --toDo << " files to go." << endl;
    }
    delete d;
  }

  if ( toDo > 1 ){
    cout << "done processsing directory '" << name << "' in total "
	 << wordTotal << " words were found." << endl;
  }
  cout << "start calculating the results" << endl;
  string ext;
  if ( nG > 1 ){
    ext = "." + TiCC::toString( nG ) + "-gram";
  }
  ext += ".tsv";
#pragma omp parallel sections
  {
#pragma omp section
    {
      string filename;
      filename = outPrefix + ".wordfreqlist" + ext;
      create_wf_list( wc, filename, wordTotal, clip );
    }
#pragma omp section
    {
      if ( !( doparstr || donoparstr ) ){
	string filename;
	filename = outPrefix + ".lemmafreqlist" + ext;
	create_lf_list( lc, filename, wordTotal, clip );
      }
    }
#pragma omp section
    {
      if ( !( doparstr || donoparstr ) ){
	string filename;
	filename = outPrefix + ".lemmaposfreqlist" + ext;
	create_lpf_list( lpc, filename, wordTotal, clip );
      }
    }
  }
}
