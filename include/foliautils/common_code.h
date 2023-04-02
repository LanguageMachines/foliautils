/* ex: set tabstop=8 expandtab: */
/*
  Copyright (c) 2006 - 2023
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
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  For questions and suggestions, see:
      https://github.com/LanguageMachines/frog/issues
  or send mail to:
      lamasoftware (at ) science.ru.nl

*/

#ifndef COMMON_CODE_H
#define COMMON_CODE_H

#include "ticcutils/XMLtools.h"
#include "ticcutils/Unicode.h"
#include <string>

extern const int XML_PARSER_OPTIONS;

enum zipType { NORMAL, GZ, BZ2, UNKNOWN };

xmlDoc *getXml( const std::string& , zipType& );

bool isalnum( UChar uc );
bool isalpha( UChar uc );
bool ispunct( UChar uc );

enum hemp_status {NO_HEMP,START_PUNCT_HEMP, NORMAL_HEMP, END_PUNCT_HEMP };

std::string toString( const hemp_status& );

inline std::ostream& operator<<( std::ostream& os,
				 const hemp_status& h){
  os << toString( h );
  return os;
}

hemp_status is_emph_part( const icu::UnicodeString& );
std::vector<hemp_status> create_emph_inventory( const std::vector<icu::UnicodeString>& );

folia::processor *add_provenance( folia::Document&,
				  const std::string&,
				  const std::string& );

UnicodeString& pop_back( UnicodeString& );

#endif // COMMON_CODE_H
