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
#include "foliautils/common_code.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/zipper.h"

using namespace std;

xmlDoc *getXml( const string& file, zipType& type ){
  type = UNKNOWN;
  if ( TiCC::match_back( file, ".xml" ) ){
    type = NORMAL;
  }
  else if ( TiCC::match_back( file, ".xml.gz" ) ){
    type = GZ;
  }
  else if ( TiCC::match_back( file, ".xml.bz2" ) ){
    type = BZ2;
  }
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
