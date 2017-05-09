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


void usage( const string& name ){
  cerr << "Usage: [options] file/dir" << endl;
  cerr << "Description: Simple word-by-word translator on the basis of a dictionary (or multiple)" << endl;
  cerr << "Options:" << endl;
  cerr << "\t-d\t dictionary_file (format: $source\\t$target\\n)" << endl;
  //cerr << "\t-p\t lexicon_file (format: $word\n); monolingual lexicon of words that are preserved as-is" << endl;
  //cerr << "\t-r or --rules\t rules_file" << endl;
  cerr << "\t--inputclass\t class (default: current)" << endl;
  cerr << "\t--outputclass\t class (default: translated)" << endl;
  cerr << "\t-e 'expr': specify the pattern matching expression all files should match with (default: *.folia.xml)" << endl;

  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h or --help\t this message " << endl;
  cerr << "\t-V or --version\t show version " << endl;
  cerr << "\t-R\t search the dirs recursively (when appropriate)" << endl;
  cerr << "\t-O\t output prefix" << endl;
}

typedef unordered_map<string,string> t_dictionary;

bool translateDoc( Document *doc, t_dictionary & dictionary, const string & inputclass, const string & outputclass ) {
    bool changed = false;
    vector<Word*> words = doc->doc()->select<Word>();
    for (const auto& word : words) {
        const string source = UnicodeToUTF8(word->text(inputclass));
        string target = source;
        if (dictionary.find(source) != dictionary.end()) {
            if (outputclass != inputclass) {
                //TODO: check if outputclass is not already present
                target = dictionary[source];
                changed = true;
            } else {
                //TODO (also remove check when implemented)
            }
        }

        //add text content
        KWargs args;
        args["class"] = outputclass;
        args["value"] = target;
        TextContent * translatedtext = new TextContent( args );
        word->append(translatedtext);
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
        vector<string> parts;
        if (TiCC::split_at(line, parts, "\t") == 2) {
            added++;
            dictionary[parts[0]] = parts[1];
        } else {
            cerr << "WARNING: loadDictionary: error in line " << linenum << ": " << line << endl;
        }
    }
    return added;
}


int main( int argc, const char *argv[] ) {
      TiCC::CL_Options opts( "d:e:vVt:O:Rh",
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

      if ( opts.extract( 'h' ) || opts.extract( "help" ) ){
        usage(progname);
        exit(EXIT_SUCCESS);
      }
      if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
        cerr << PACKAGE_STRING << endl;
        exit(EXIT_SUCCESS);
      }
      recursiveDirs = opts.extract( 'R' );
      opts.extract( 'O', outPrefix );

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
          cerr << "Loading dictionary..." << endl;
          loadDictionary(dictionaryfile, dictionary);
      } else {
          cerr << "No dictionary specified" << endl;
          exit( EXIT_FAILURE );
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
          string outName = outPrefix;
          string::size_type pos = docName.rfind("/");
          if ( pos != string::npos ){
              docName = docName.substr( pos+1 );
          }
          pos = docName.rfind(".folia");
          if ( pos != string::npos ){
              outName += docName.substr(0,pos) + ".translated" + docName.substr(pos);
          } else {
              pos = docName.rfind(".");
              if ( pos != string::npos ){
                  outName += docName.substr(0,pos) + ".translated" + docName.substr(pos);
              } else {
                  outName += docName + ".translated";
              }
          }
          if ( TiCC::isFile( outName ) ){
#pragma omp critical
                cerr << "skipping already done file: " << outName << endl;
          } else {
                if ( !TiCC::createPath( outName ) ){
#pragma omp critical
                  cerr << "unable to create output file! " << outName << endl;
                  exit(EXIT_FAILURE);
                }

                translateDoc(doc, dictionary, inputclass, outputclass);
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
     } else {
         cout << "finished " << name << endl;
     }
}
