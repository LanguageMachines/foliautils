/*
  Copyright (c) 2014 - 2019
  CLST  - Radboud University

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

/*
 * Do simple dictionary based substitutions on the text, either as a new text layer or replacing the provided one
 */

#include <unistd.h>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include "ticcutils/FileUtils.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/Unicode.h"
#include "libfolia/folia.h"
#include "foliautils/common_code.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;
using namespace	folia;

const string INT_LEMMAIDSET = "https://raw.githubusercontent.com/proycon/folia/master/setdefinitions/int_lemmaid_withcompounds.foliaset.ttl";
const string INT_LEMMATEXTSET = "https://raw.githubusercontent.com/proycon/folia/master/setdefinitions/int_lemmatext_withcompounds.foliaset.ttl";
const string INT_METRICSET = "https://raw.githubusercontent.com/proycon/folia/master/setdefinitions/nederlab-metrics.foliaset.ttl";

const UnicodeString NBSP = TiCC::UnicodeFromUTF8(" ");//THIS IS NOT A NORMAL SPACE BUT A narrow no-break space (0x202F), this is a fairly ugly patch that will be propagated to the end-result because Frog can't deal with spaces in tokens at this stage

void usage( const string& name ){
  cerr << "Usage: " << name << " [options] file/dir" << endl;
  cerr << "Description: Simple word-by-word translator on the basis of a dictionary (or multiple)" << endl;
  cerr << "Options:" << endl;
  cerr << "\t-d\t dictionary_file (format: $source\\t$target\\n)" << endl;
  cerr << "\t-p\t lexicon_file (format: $word\\n); monolingual lexicon of words that are preserved as-is" << endl;
  cerr << "\t-H\t dictionary_file (format: INT historical lexicon dump)" << endl;
  cerr << "\t-l\t language - constraint to language" << endl;
  cerr << "\t-r or --rules\t rules_file (format: $pattern\\s$replacement\\n)" << endl;
  cerr << "\t--inputclass\t class (default: current)" << endl;
  cerr << "\t--outputclass\t class (default: translated)" << endl;
  cerr << "\t-e 'expr': specify the pattern matching expression all files should match with (default: *.folia.xml)" << endl;

  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h or --help\t this message " << endl;
  cerr << "\t-V or --version\t show version " << endl;
  cerr << "\t-R\t search the dirs recursively (when appropriate)" << endl;
  cerr << "\t-O\t output prefix" << endl;
}

namespace std
{
  // needed to make unordered_[set|map] work
  template<>
  class hash<UnicodeString> {
  public:
    size_t operator()(const UnicodeString &s) const
    {
      return (size_t) s.hashCode();
    }
  };
}


typedef unordered_map<UnicodeString,UnicodeString> t_dictionary;
typedef unordered_set<UnicodeString> t_lexicon;
typedef vector<pair<UnicodeString,UnicodeString>> t_rules;
typedef unordered_map<UnicodeString,unordered_map<UnicodeString,int>> t_histdictionary; //dictionary from historical lexicon,    form => lemma => freq
typedef unordered_map<UnicodeString,unordered_map<UnicodeString,int>> t_lemmamap; //lemma => src:lemma_id => freq

UnicodeString applyRules( const UnicodeString& orig_source, const t_rules& rules) {
  UnicodeString source = orig_source;
  UnicodeString target = source;
  for ( const auto& iter : rules ) {
    UnicodeString pattern = iter.first;
    UnicodeString replacement = iter.second;
    // cerr << "source =" << source << endl;
    // cerr << "pattern=" << pattern << endl;
    // cerr << "replace=" << replacement << endl;
    UErrorCode u_stat = U_ZERO_ERROR;
    RegexMatcher *matcher = new RegexMatcher(pattern, source, 0, u_stat);
    if ( U_FAILURE(u_stat) ){
      throw runtime_error( "failed to create a regexp matcher with '" + TiCC::UnicodeToUTF8(pattern) + "'" );
    }
    target = matcher->replaceAll(replacement, u_stat);
    //	cerr << "target =" << target << endl;
    if ( U_FAILURE(u_stat) ){
      throw runtime_error( "failed to execute regexp s/" + TiCC::UnicodeToUTF8(pattern) + "/" + TiCC::UnicodeToUTF8(replacement) + "/g" );
    }
    source = target; //reset source for next pattern
    delete matcher;
  }
  return target;
}

UnicodeString lemmatiser( Word *word,
			  const UnicodeString& target,
			  const t_lemmamap &lemmamap) {
  UnicodeString target_flat = target;
  target_flat.toLower();
  if (lemmamap.empty()) return target_flat;
  const auto& lemmamap_iter = lemmamap.find(target_flat);
  if (lemmamap_iter != lemmamap.end()) {
    //resolve ambiguity by majority vote: just select the most frequent lemma->id pair (lexicon contains multiple occurrences)
    int max = 0;
    UnicodeString lemma_id;
    for ( const auto& iter2 : lemmamap_iter->second ){
      if (iter2.second >= max) {
	max = iter2.second;
	lemma_id = iter2.first;
      }
    }
    {
      KWargs args;
      args["class"] = TiCC::UnicodeToUTF8(lemma_id);
      args["set"] = INT_LEMMAIDSET;
      LemmaAnnotation *lemma = new LemmaAnnotation( args, word->doc() );
      word->append(lemma);
    }
    {
      KWargs args;
      UnicodeString lemmatextclass = target;
      lemmatextclass = lemmatextclass.findAndReplace(" ", "_"); //use underscores instead of spaces for multiword lemmas (does not affect ⊕!)
      args["class"] = TiCC::UnicodeToUTF8(lemmatextclass);
      args["set"] = INT_LEMMATEXTSET;
      LemmaAnnotation *lemma = new LemmaAnnotation( args, word->doc() );
      word->append(lemma);
    }
  }

  //return a version of target that could be suited for modernisation
  target_flat = target_flat.findAndReplace(TiCC::UnicodeFromUTF8("⊕"), NBSP);
  //we can't deal with pipes for multiple options, just choose the first one:
  int pipeindex;
  const UnicodeString emptystr = "";
  do {
    pipeindex = target_flat.indexOf("|");
    if (pipeindex != -1) {
      const int end = target_flat.indexOf(NBSP, pipeindex);
      if (end != -1) {
	target_flat = target_flat.remove(pipeindex, end-pipeindex); //delete 2nd option
      } else {
	target_flat = target_flat.remove(pipeindex, target_flat.length()-pipeindex); //delete 2nd option
      }
    }
  } while (pipeindex != -1);
  return target_flat; //return variant more or less suitable for modernisation (whitespaces will be handled later still)
}

const string getlanguage(FoliaElement * e) {
  if ( e->has_annotation<folia::LangAnnotation>() ){
    return e->annotation<folia::LangAnnotation>()->cls();
  } else if (e->parent() != NULL) {
    return getlanguage(e->parent());
  } else {
    return "";
  }
}


bool translateDoc( Document *doc,
		   const t_dictionary& dictionary,
		   const string& inputclass,
		   const string& outputclass,
		   const t_lexicon& preserve_lexicon,
		   const t_rules& rules,
                   const t_histdictionary& histdictionary,
                   const t_lemmamap& lemmamap,
                   const string& constrainlanguage) {
  bool changed = false;
  vector<Word*> words = doc->doc()->select<Word>();
  for (const auto& word : words) {
    const UnicodeString source = word->text(inputclass);
    cerr << "Processing word " << word->id() << ": " << TiCC::UnicodeToUTF8(source);
    if (!constrainlanguage.empty()) {
        if (getlanguage(word) != constrainlanguage) {
            cerr << "(language constraint not satisfied)" << endl;
            continue;
        }
    }
    UnicodeString source_flat = source;
    source_flat = source_flat.toLower();
    const bool recase = (source_flat != source);
    string modernisationsource = "none";
    UnicodeString target = source_flat;
    //check if word is in dictionary
    const auto& entry = dictionary.find(source_flat);
    const auto& histentry = histdictionary.find(source_flat);
    if ( entry != dictionary.end()) {
      if (outputclass != inputclass) {
	//TODO: check if outputclass is not already present
	target = entry->second;
	modernisationsource = "lexicon";
	changed = true;
        lemmatiser(word, target, lemmamap);
      }
      else {
	//TODO (also remove check when implemented)
      }
    } else if ((!preserve_lexicon.empty()) && (preserve_lexicon.find(source_flat) != preserve_lexicon.end())) {
      //word is in preservation lexicon
        modernisationsource = "preservationlexicon";
        lemmatiser(word, source_flat, lemmamap);
    } else {
      //word is NOT in preservation lexicon
      if (histentry != histdictionary.end()) {
	//word is in INT historical lexicon
	if (outputclass != inputclass) {
	  //find the most frequent lemma for this word form (resolves ambiguity harshly)
	  int max = 0;
	  for ( const auto& iter2 : histentry->second ){
	    if (iter2.second >= max) {
	      max = iter2.second;
	      target = iter2.first;
	    }
	  }
	  modernisationsource = "inthistlexicon";
	  changed = true;
	  target = lemmatiser(word, target, lemmamap);
	}
      } else if (!rules.empty()) {
	//apply rules:
	target = applyRules(source_flat, rules);
	changed = (target != source_flat);
	if (changed) modernisationsource = "rules";
        lemmatiser(word, target, lemmamap);
      }
    }

    //Frog can't deal with spaces in the target, replace those with a narrow non-breaking space
    //this is a bit of an ugly hack that will propagate to the final FoLiA
    if (changed) target = target.findAndReplace(" ", NBSP);
    cerr << " -> " << TiCC::UnicodeToUTF8(target) << " [" << modernisationsource << "]" << endl;


    if (recase) {
      //recase
      bool allcaps = true;
      bool initialcap = false;
      for (int i = 0; i < source.length(); i++) {
	bool islower = (source[i] == u_tolower(source[i]) );
	if ((i == 0) && (!islower)) {
	  initialcap = true;
	}
	if (islower) {
	  allcaps = false;
	}
      }
      if (allcaps || initialcap) {
	UnicodeString target_u = target;
	if (allcaps) {
	  target_u = target_u.toUpper();
	}
	else if (initialcap) {
	  target_u = target_u.replace(0,1, target_u.tempSubString(0,1).toUpper());
	}
	target = target_u;
      }
    }

    KWargs args;
    args["class"] = outputclass;
    args["value"] = TiCC::UnicodeToUTF8(target);
    TextContent * translatedtext = new TextContent( args );
    word->append(translatedtext);
    args["class"] = "modernisationsource";
    args["value"] =  modernisationsource;
    Metric *metric = new Metric( args, doc );
    word->append(metric);
  }
  return changed;
}

int loadDictionary(const string & filename, t_dictionary & dictionary) {
  ifstream is(filename);
  string line;
  int added = 0;
  int linenum = 0;
  while (getline(is, line)) {
    linenum++;
    if ((!line.empty()) && (line[0] != '#')) {
      vector<string> parts = TiCC::split_at( line, "\t" );
      if ( parts.size() == 2) {
	added++;
	dictionary[TiCC::UnicodeFromUTF8(parts[0])] = TiCC::UnicodeFromUTF8(parts[1]);
      }
      else {
	cerr << "WARNING: loadDictionary: error in line " << linenum << ": " << line << endl;
      }
    }
  }
  return added;
}

int loadHistoricalLexicon(const string & filename, t_histdictionary & dictionary, t_lemmamap & lemmamap) {
  //Load INT historical lexicon dump
  ifstream is(filename);
  string line;
  int added = 0;
  int linenum = 0;
  while (getline(is, line)) {
    linenum++;
    if ((!line.empty()) && (line[0] != '#')) {
      vector<string> parts = TiCC::split_at( line, "\t" );
      if ( parts.size() == 9) {
        if (parts[0] != "multiple") { //ignore many=>one
	  added++;
	  const UnicodeString lemma = TiCC::UnicodeFromUTF8(parts[4]).toLower();
	  dictionary[TiCC::UnicodeFromUTF8(parts[6])][lemma]++;
	  const UnicodeString lemma_id = TiCC::UnicodeFromUTF8(parts[1]) + TiCC::UnicodeFromUTF8(":") + TiCC::UnicodeFromUTF8(parts[3]); //e.g: WNT:M078848  or clitics like MNW:57244⊕40508
	  lemmamap[lemma][lemma_id]++;
        }
      } else {
	cerr << "WARNING: loadHistoricalLexicon: error in line " << linenum << ": " << line << endl;
      }
    }
  }
  return added;
}

int loadLexicon(const string & filename, t_lexicon & lexicon) {
  ifstream is(filename);
  string line;
  int added = 0;
  int linenum = 0;
  while (getline(is, line)) {
    linenum++;
    if ((!line.empty()) && (line[0] != '#')) {
      vector<string> parts = TiCC::split_at( line, "\t" );
      added++;
      lexicon.insert(TiCC::UnicodeFromUTF8(parts[0]));
    }
  }
  return added;
}

int loadRules( const string& filename,
	       t_rules& rules) {
  ifstream is(filename);
  string line;
  int added = 0;
  int linenum = 0;
  while (getline(is, line)) {
    linenum++;
    if ((!line.empty()) && (line[0] != '#')) {
      vector<string> parts = TiCC::split_at( line, " " );
      if ( parts.size() == 5) {
	// example expected line format: 222 0.996 aen => aan
	added++;
	rules.push_back(make_pair(TiCC::UnicodeFromUTF8(parts[2]),
				  TiCC::UnicodeFromUTF8(parts[4])));
      }
      else if ( parts.size() == 2 ) {
	// simplified format: aen aan
	added++;
	rules.push_back(make_pair(TiCC::UnicodeFromUTF8(parts[0]),
				  TiCC::UnicodeFromUTF8(parts[1])));
      }
      else {
	cerr << "WARNING: loadRules: error in line " << linenum << ": " << line << endl;
      }
    }
  }
  return added;
}

int main( int argc, const char *argv[] ) {
  TiCC::CL_Options opts( "d:e:p:r:vVt:O:RhH:",
			 "inputclass:,outputclass:,version,help" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage(argv[0]);
    exit( EXIT_FAILURE );
  }
  string progname = opts.prog_name();
  string inputclass = "current";
  string outputclass = "translated";
  string expression = "*.folia.xml";
  int numThreads = 1;
  bool recursiveDirs = false;
  string outPrefix;
  string value;
  t_dictionary dictionary;
  t_histdictionary histdictionary; //wordform => lemma   from INT historical lexicon
  t_lemmamap lemmamap; // lemma => lemma_id    from INT historical lexicon
  t_rules rules;
  t_lexicon preserve_lexicon;

  if ( opts.extract( 'h' ) || opts.extract( "help" ) ){
    usage(progname);
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  string command = "FoLiA-wordtranslate " + opts.toString();
  recursiveDirs = opts.extract( 'R' );
  opts.extract( 'O', outPrefix );
  if ( opts.extract( 't', value ) ){
    numThreads = TiCC::stringTo<int>( value );
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
    if ( outPrefix[outPrefix.length()-1] != '/' ) outPrefix += "/";
    if ( !TiCC::isDir( outPrefix ) ){
      if ( !TiCC::createPath( outPrefix ) ){
	cerr << "unable to find or create: '" << outPrefix << "'" << endl;
	exit( EXIT_FAILURE );
      }
    }
  }
  opts.extract( "inputclass", inputclass );
  opts.extract( "outputclass", outputclass );
  opts.extract( "expr", expression );

  if (inputclass == outputclass) {
    cerr << "Inputclass and outputclass are the same, this is not implemented yet..." << endl;
    exit( EXIT_FAILURE );
  }

  string dictionaryfile;
  if ( opts.extract( 'd', dictionaryfile) ){
    cerr << "Loading dictionary... ";
    int cnt = loadDictionary(dictionaryfile, dictionary);
    cerr << cnt << " entries" << endl;
  }
  else {
    cerr << "No dictionary specified" << endl;
    exit( EXIT_FAILURE );
  }

  string constrainlanguage = "";
  if ( opts.extract( 'l', constrainlanguage) ){
    cerr << "Constraining language to " << constrainlanguage << endl;
  }


  string inthistlexiconfile;
  if ( opts.extract( 'H', inthistlexiconfile) ){
    cerr << "Loading INT historical lexicon... ";
    int cnt = loadHistoricalLexicon(inthistlexiconfile, histdictionary, lemmamap);
    cerr << cnt << " entries" << endl;
  }

  string preservelexiconfile;
  if ( opts.extract( 'p', preservelexiconfile)) {
    cerr << "Loading preserve lexicon... ";
    int cnt = loadLexicon(preservelexiconfile, preserve_lexicon);
    cerr << cnt << " entries" << endl;
  }

  string rulefile;
  if ( opts.extract( 'r', rulefile) ){
    cerr << "Loading rules... ";
    int cnt = loadRules(rulefile, rules);
    cerr << cnt << " entries" << endl;
  }

#ifdef HAVE_OPENMP
  omp_set_num_threads( numThreads );
#else
  if ( numThreads != 1 ) {
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
    exit( EXIT_FAILURE );
  }
#endif

  string name = fileNames[0];
  fileNames = TiCC::searchFilesMatch( name, expression, recursiveDirs );
  size_t filecount = fileNames.size();
  if ( filecount == 0 ){
    cerr << "no matching files found" << endl;
    exit(EXIT_SUCCESS);
  }
  bool doDir = ( filecount > 1 );

#pragma omp parallel for shared(fileNames,filecount) schedule(dynamic,1)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    string docName = fileNames[fn];
    Document *doc = 0;
    try {
      doc = new Document( "file='"+ docName + "',mode='nochecktext'" ); //TODO: remove nochecktext?
    }
    catch ( exception& e ){
#pragma omp critical
      {
	cerr << "failed to load document '" << docName << "'" << endl;
	cerr << "reason: " << e.what() << endl;
      }
      continue;
    }
    processor *proc = add_provenance( *doc, "FoLiA-wordtranslate", command );
    KWargs args;
    args["processor"] = proc->id();
    doc->declare( folia::AnnotationType::METRIC,
		  INT_METRICSET,
		  args );
    if (!lemmamap.empty()) {
        doc->declare( folia::AnnotationType::LEMMA,
		      INT_LEMMAIDSET,
		      args );
        doc->declare( folia::AnnotationType::LEMMA,
		      INT_LEMMATEXTSET,
		      args );
    }
    string outName = outPrefix;
    string::size_type pos = docName.rfind("/");
    if ( pos != string::npos ){
      docName = docName.substr( pos+1 );
    }
    pos = docName.rfind(".folia");
    if ( pos != string::npos ){
      outName += docName.substr(0,pos) + ".translated" + docName.substr(pos);
    }
    else {
      pos = docName.rfind(".");
      if ( pos != string::npos ){
	outName += docName.substr(0,pos) + ".translated" + docName.substr(pos);
      }
      else {
	outName += docName + ".translated";
      }
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

      translateDoc(doc, dictionary, inputclass, outputclass, preserve_lexicon, rules, histdictionary, lemmamap, constrainlanguage);
      doc->save(outName);
    }
#pragma omp critical
    {
      if ( filecount > 1 ){
	cout << "Processed :" << docName << " into " << outName << " still " << --filecount << " files to go." << endl;
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
  return EXIT_SUCCESS;
}
