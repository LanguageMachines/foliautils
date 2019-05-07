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

#include <string>
#include "libfolia/folia.h"
#include "libxml/HTMLparser.h"
#include "foliautils/common_code.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/zipper.h"
#include "ticcutils/Unicode.h"
#include "config.h"

using namespace std;
using namespace icu;

#ifdef OLD
xmlDoc *getXml( const string& file, zipType& type ){
  type = UNKNOWN;
  if ( type == UNKNOWN ){
    cerr << "problem detecting type of file: " << file << endl;
    return 0;
  }
  if ( type == NORMAL ){
    return xmlReadFile( file.c_str(), 0, XML_PARSE_NOBLANKS|XML_PARSE_HUGE );
  }
  string buffer;
  if ( type == GZ ){
    buffer = TiCC::gzReadFile( file );
  }
  else if ( type == BZ2 ){
    buffer = TiCC::bz2ReadFile( file );
  }
  return xmlReadMemory( buffer.c_str(), buffer.length(),
			0, 0, XML_PARSE_NOBLANKS|XML_PARSE_HUGE );
}
#else
xmlDoc *getXml( const string& file, zipType& type ){
  type = UNKNOWN;
  bool isHtml = false;
  if ( TiCC::match_back( file, ".xml" ) ){
    type = NORMAL;
  }
  else if ( TiCC::match_back( file, ".xml.gz" ) ){
    type = GZ;
  }
  else if ( TiCC::match_back( file, ".xml.bz2" ) ){
    type = BZ2;
  }
  else if ( TiCC::match_back( file, ".xhtml" ) ){
    type = NORMAL;
    isHtml = false;
  }
  else if ( TiCC::match_back( file, ".html" ) ){
    type = NORMAL;
    isHtml = true;
  }
  else if ( TiCC::match_back( file, ".hocr" ) ){
    type = NORMAL;
    isHtml = true;
  }
  else if ( TiCC::match_back( file, ".xhtml.gz" ) ){
    type = GZ;
    isHtml = false;
  }
  else if ( TiCC::match_back( file, ".html.gz" ) ){
    type = GZ;
    isHtml = true;
  }
  else if ( TiCC::match_back( file, ".hocr.gz" ) ){
    type = GZ;
    isHtml = true;
  }
  else if ( TiCC::match_back( file, ".xhtml.bz2" ) ){
    type = BZ2;
    isHtml = false;
  }
  else if ( TiCC::match_back( file, ".html.bz2" ) ){
    type = BZ2;
    isHtml = true;
  }
  else {
    return 0;
  }
  if ( isHtml ){
    if ( type == NORMAL ){
      return htmlReadFile( file.c_str(), 0, XML_PARSE_NOBLANKS );
    }
    string buffer;
    if ( type == GZ ){
      buffer = TiCC::gzReadFile( file );
    }
    else if ( type == BZ2 ){
      buffer = TiCC::bz2ReadFile( file );
    }
    return htmlReadMemory( buffer.c_str(), buffer.length(),
			   0, 0, XML_PARSE_NOBLANKS );
  }
  else {
    if ( type == NORMAL ){
      return xmlReadFile( file.c_str(), 0, XML_PARSE_NOBLANKS );
    }
    string buffer;
    if ( type == GZ ){
      buffer = TiCC::gzReadFile( file );
    }
    else if ( type == BZ2 ){
      buffer = TiCC::bz2ReadFile( file );
    }
    return xmlReadMemory( buffer.c_str(), buffer.length(),
			  0, 0, XML_PARSE_NOBLANKS );
  }
}
#endif

bool isalnum( UChar uc ){
  int8_t charT =  u_charType( uc );
  return ( charT == U_LOWERCASE_LETTER ||
	   charT == U_UPPERCASE_LETTER ||
	   charT == U_DECIMAL_DIGIT_NUMBER );
}

bool isalpha( UChar uc ){
  return u_isalpha( uc );
}

bool ispunct( UChar uc ){
  return u_ispunct( uc );
}

hemp_status is_emph_part( const UnicodeString& data ){
  hemp_status result = NO_HEMP;
  if (data.length() < 2 ){
    if ( isalnum(data[0]) ){
      result = NORMAL_HEMP;
    }
    //    cerr << "test: '" << data << "' ==> " << (result?"OK":"nee dus") << endl;
  }
  else if (data.length() < 3){
    UnicodeString low = data;
    low.toLower();
    if ( low == "ij" ){
      result = NORMAL_HEMP;
    }
    else if ( isalpha(data[0]) && ispunct(data[1]) ){
      result = END_PUNCT_HEMP;
    }
    else if ( isalpha(data[1]) && ispunct(data[0]) ){
      result = START_PUNCT_HEMP;
    }
    //    cerr << "test: '" << data << "' ==> " << (result?"OK":"nee dus") << endl;
  }
  return result;
}

vector<hemp_status> create_emph_inventory( const vector<UnicodeString>& data ){
  vector<hemp_status> inventory(data.size(),NO_HEMP);
  hemp_status prev = NO_HEMP;
  int length = 0;
  for ( unsigned int i=0; i < data.size(); ++i ){
    hemp_status status = is_emph_part( data[i] );
    // cerr << "i=" << i << " INV=" << inventory << " ADD=" << status << endl;
    if ( status == NO_HEMP ){
      // no hemp. ends previous, if any
      if ( length == 1 ){
	// no loose hemps;
	inventory[i-1] = NO_HEMP;
      }
      length = 0;
      inventory[i] = status;
      prev = status;
    }
    else if ( status == START_PUNCT_HEMP ){
      if ( prev == START_PUNCT_HEMP ){
	// clear previous start
	inventory[i-1] = NO_HEMP;
	length = 0;
      }
      else if ( length == 1 ){
	// short before, clear
	inventory[i-1] = NO_HEMP;
	length = 0;
      }
      // normal hemp part
      ++length;
      inventory[i] = status;
      prev = status;
    }
    else if ( status == NORMAL_HEMP ){
      // end_punct
      if ( prev == END_PUNCT_HEMP ){
	status = NO_HEMP;
      }
      inventory[i] = status;
      ++length;
      prev = status;
    }
    else if ( status == END_PUNCT_HEMP ){
      // an end punct
      if ( length == 0 ){
	// no hemp yet, forget this one
	status = NO_HEMP;
      }
      else {
	// ends current hemp
	length = 0;
      }
      inventory[i] = status;
    }
    if ( length == 1 && i == data.size()-1 ){
      // so we seem to end with a singe emph_candidate. reject it
      inventory[i] = NO_HEMP;
    }
  }
  return inventory;
}

folia::processor *add_provenance( folia::Document& doc,
				  const string& label,
				  const string& command ) {
  folia::processor *proc = doc.get_processor( label );
  folia::KWargs args;
  args["name"] = label;
  args["id"] = label + ".1";
  args["version"] = PACKAGE_VERSION;
  args["command"] = command;
  args["begindatetime"] = "now()";
  args["generator"] = "yes";
  proc = doc.add_processor( args );
  proc->get_system_defaults();
  return proc;
}
