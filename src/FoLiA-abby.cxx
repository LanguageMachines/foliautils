/*
  Copyright (c) 2014 - 2024
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

#include <cassert>
#include <string>
#include <list>
#include <map>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "ticcutils/StringOps.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/zipper.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/Unicode.h"
#include "ticcutils/CommandLine.h"
#include "libfolia/folia.h"
#include "foliautils/common_code.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;
using TiCC::operator<<;

bool verbose = false;

string setname = "FoLiA-abby-set";
string classname = "OCR";
const string processor_name= "FoLiA-abby";
string processor_id;
bool keep_hyphens = false;
bool add_breaks = false;
bool add_metrics = false;

enum font_style { REGULAR=0,
		  ITALIC=1,
		  BOLD=2,
		  SMALLCAPS=4,
		  SUPERSCRIPT=8,
		  SUBSCRIPT=16,
		  UNDERLINE=32,
		  STRIKEOUT=64
};

inline font_style operator~( font_style f ){
  return (font_style)( ~(int)f );
}

inline font_style operator&( font_style f1, font_style f2 ){
  return (font_style)((int)f1&(int)f2);
}

inline font_style& operator&=( font_style& f1, font_style f2 ){
  f1 = (f1 & f2);
  return f1;
}

inline font_style operator|( font_style f1, font_style f2 ){
  return (font_style) ((int)f1|(int)f2);
}

inline font_style& operator|=( font_style& f1, font_style f2 ){
  f1 = (f1 | f2);
  return f1;
}

font_style stringToMode( const string& s ){
  if ( s.empty() ){
    return REGULAR;
  }
  else if ( s == "italic" ){
    return ITALIC;
  }
  else if ( s == "bold" ) {
    return BOLD;
  }
  else if ( s == "smallcaps" ) {
    return SMALLCAPS;
  }
  else if ( s == "superscript" ) {
    return SUPERSCRIPT;
  }
  else if ( s == "subscript" ) {
    return SUBSCRIPT;
  }
  else if ( s == "underline" ) {
    return UNDERLINE;
  }
  else if ( s == "strikeout" ) {
    return STRIKEOUT;
  }
  else {
    cerr << "FoLiA-abby: unsupported Font-Style " << s << " (ignored)" << endl;
    return REGULAR;
  }
}

string toString( font_style fs ){
  if ( fs == REGULAR ){
    return "";
  }
  string result;
  if ( fs & ITALIC ){
    result += "italic|";
  }
  if ( fs & BOLD ){
    result += "bold|";
  }
  if ( fs & SMALLCAPS ){
    result += "smallcaps|";
  }
  if ( fs & SUPERSCRIPT ){
    result += "superscript|";
  }
  if ( fs & SUBSCRIPT ){
    result += "subscript|";
  }
  if ( fs & UNDERLINE ){
    result += "underline|";
  }
  if ( fs & STRIKEOUT ){
    result += "strikeout|";
  }
  result.pop_back();
  return result;
}

ostream& operator<<( ostream& os, const font_style& fs ){
  os << toString( fs );
  return os;
}
struct formatting_info {
  formatting_info():
    _fst(REGULAR)
  {};
  formatting_info( const string& lang,
		   const string& ff,
		   const string& fs,
		   const font_style fst ):
    _lang(lang),
    _ff(ff),
    _fs(fs),
    _fst(fst)
  {};
  string _lang;
  string _ff;
  string _fs;
  string _id;
  font_style _fst;
};

ostream& operator<<( ostream& os, const formatting_info& fi ){
  os << fi._ff << " (" << fi._fs << ") " << fi._fst << " "
     << fi._id << " " << fi._lang;
  return os;
}

UnicodeString get_text( const xmlNode *node ){
  string result;
  const xmlNode *pnt = node->children;
  while ( pnt ){
    if ( pnt->type == XML_TEXT_NODE ){
      xmlChar *tmp = xmlNodeGetContent( pnt );
      if ( tmp ){
	result = string( folia::to_char(tmp) );
	xmlFree( tmp );
      }
      break;
    }
  }
  return TiCC::UnicodeFromUTF8( result );
}

UnicodeString get_line( xmlNode *line ){
  UnicodeString result;
  list<xmlNode*> variants = TiCC::FindNodes( line, "*:wordRecVariants" );
  if ( !variants.empty() ){
    if ( verbose ){
#pragma omp critical
      {
	cout << "\t\t\tfound " << variants.size() << " wordRecVariants nodes" << endl;
      }
    }
    for ( const auto& var : variants ){
      list<xmlNode*> recs = TiCC::FindNodes( var, "*:wordRecVariant" );
      if ( recs.empty() ){
	// hapens sometimes, just skip..
      }
      else {
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "\t\t\t\tfound " << recs.size() << " wordRecVariant nodes" << endl;
	  }
	}
	list<xmlNode*> text = TiCC::FindNodes( recs.front(), "*:variantText" );
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "\t\t\t\tfound " << text.size() << " text nodes" << endl;
	  }
	}
	const xmlNode *front = text.front();
	UnicodeString tmp = get_text( front );
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "\t\t\t\t\traw text: '" << tmp << "'" << endl;
	  }
	}
	result += tmp + " "; // separate by spaces
      }
    }
    if ( !result.isEmpty() ){
      // remove last space of the line
      result = pop_back( result );
    }
  }
  else {
    list<xmlNode*> chars = TiCC::FindNodes( line, "*:charParams" );
    if ( verbose ){
#pragma omp critical
      {
	cout << "\t\t\t\tfound " << chars.size() << " chars" << endl;
      }
    }
    for ( const auto *ch : chars ){
      result += UnicodeValue(ch);
    }
  }
  if ( verbose ){
#pragma omp critical
    {
      cout << "Word text = '" << result << "'" << endl;
    }
  }
  return result;
}

void update_formatting_info( formatting_info& line_font,
			     xmlNode *node,
			     const map<string,formatting_info>& font_styles ){
  try {
    string style = TiCC::getAttribute( node, "style" );
    if ( !style.empty() ){
      line_font = font_styles.at( style );
      line_font._id = style;
    }
  }
  catch ( const out_of_range& ){
    // continue
  }
  string lang = TiCC::getAttribute( node, "lang" );
  if ( !lang.empty() ){
    line_font._lang = lang;
  }
  string fs = TiCC::getAttribute( node, "fs" );
  if ( !fs.empty() ){
    line_font._fs = fs;
  }
  string ff = TiCC::getAttribute( node, "ff" );
  if ( !ff.empty() ){
    line_font._ff = ff;
  }
  string value = TiCC::getAttribute( node, "bold" );
  if ( !value.empty() ){
    if ( value == "1" ){
      line_font._fst |= BOLD;
    }
    else {
      line_font._fst &= ~BOLD;
    }
  }
  value = TiCC::getAttribute( node, "italic" );
  if ( !value.empty() ){
    if ( value == "1" ){
      line_font._fst |= ITALIC;
    }
    else {
      line_font._fst &= ~ITALIC;
    }
  }
  value = TiCC::getAttribute( node, "smallcaps" );
  if ( !value.empty() ){
    if ( value == "1" ){
      line_font._fst |= SMALLCAPS;
    }
    else {
      line_font._fst &= ~SMALLCAPS;
    }
  }
  value = TiCC::getAttribute( node, "superscript" );
  if ( !value.empty() ){
    if ( value == "1" ){
      line_font._fst |= SUPERSCRIPT;
    }
    else {
      line_font._fst &= ~SUPERSCRIPT;
    }
  }
  value = TiCC::getAttribute( node, "subscript" );
  if ( !value.empty() ){
    if ( value == "1" ){
      line_font._fst |= SUBSCRIPT;
    }
    else {
      line_font._fst &= ~SUBSCRIPT;
    }
  }
  value = TiCC::getAttribute( node, "strikeout" );
  if ( !value.empty() ){
    if ( value == "1" ){
      line_font._fst |= STRIKEOUT;
    }
    else {
      line_font._fst &= ~STRIKEOUT;
    }
  }
  value = TiCC::getAttribute( node, "underline" );
  if ( !value.empty() ){
    if ( value == "1" ){
      line_font._fst |= UNDERLINE;
    }
    else {
      line_font._fst &= ~UNDERLINE;
    }
  }
}

struct line_info {
  line_info():
    _line(0)
  {};
  UnicodeString _value;
  formatting_info _fi;
  xmlNode *_line;
  UnicodeString _hyph;
  UnicodeString _spaces;
};

void process_line( xmlNode *block,
		   vector<line_info>& line_parts,
		   const formatting_info& default_format,
		   const map<string,formatting_info>& font_styles ){
  list<xmlNode*> formats = TiCC::FindNodes( block, "*:formatting" );
  if ( verbose ){
#pragma omp critical
    {
      cout << "\t\t\tfound " << formats.size() << " formatting nodes" << endl;
    }
  }
  for ( const auto& form : formats ){
    formatting_info line_format = default_format;
    update_formatting_info( line_format, form, font_styles );
    UnicodeString uresult = get_line( form );
    UnicodeString hyph;
    uresult = extract_final_hyphen( uresult, hyph );
    if ( !uresult.isEmpty() || !hyph.isEmpty() ){
      line_info li;
      li._value = uresult;
      li._line = block;
      li._fi = line_format;
      li._hyph = hyph;
      UnicodeString tmp = uresult;
      tmp.trim();
      if ( tmp.isEmpty() ){
	li._spaces = uresult;
      }
      line_parts.push_back( li );
    }
  }
}

void append_styles( folia::TextMarkupStyle* markup,
		    const font_style& fs ){
  if ( fs & BOLD ){
    folia::KWargs args;
    args["subset"] = "font_typeface";
    args["class"] = "bold";
    markup->add_child<folia::Feature>( args );
  }
  if ( fs & ITALIC ){
    folia::KWargs args;
    args["subset"] = "font_typeface";
    args["class"] = "italic";
    markup->add_child<folia::Feature>( args );
  }
  if ( fs & SMALLCAPS ){
    folia::KWargs args;
    args["subset"] = "font_typeface";
    args["class"] = "smallcaps";
    markup->add_child<folia::Feature>( args );
  }
  if ( fs & SUPERSCRIPT ){
    folia::KWargs args;
    args["subset"] = "font_typeface";
    args["class"] = "superscript";
    markup->add_child<folia::Feature>( args );
    markup->settag("token");
  }
  if ( fs & SUBSCRIPT ){
    folia::KWargs args;
    args["subset"] = "font_typeface";
    args["class"] = "subscript";
    markup->add_child<folia::Feature>( args );
    markup->settag("token");
  }
  if ( fs & UNDERLINE ){
    folia::KWargs args;
    args["subset"] = "font_typeface";
    args["class"] = "underline";
    markup->add_child<folia::Feature>( args );
  }
  if ( fs & STRIKEOUT ){
    folia::KWargs args;
    args["subset"] = "font_typeface";
    args["class"] = "strikeout";
    markup->add_child<folia::Feature>( args );
  }
}

folia::TextMarkupStyle* make_style_content( const formatting_info& info,
					    folia::Document *doc ){
  font_style style = info._fst;
  folia::TextMarkupStyle *content = new folia::TextMarkupStyle( doc );
  if ( !info._lang.empty() ){
    // add language as a t-lang
    folia::KWargs args;
    args["class"] = info._lang;
    content->add_child<folia::TextMarkupLanguage>( args );
  }
  if ( style != REGULAR ){
    append_styles( content, style );
  }
  if ( !info._ff.empty() ){
    folia::KWargs args;
    args["subset"] = "font_family";
    args["class"] = info._ff;
    content->add_child<folia::Feature>( args );
  }
  if ( !info._fs.empty() ){
    folia::KWargs args;
    args["subset"] = "font_size";
    args["class"] = info._fs;
    content->add_child<folia::Feature>( args );
  }
  if ( !info._id.empty() ){
    folia::KWargs args;
    args["subset"] = "font_style";
    args["class"] = info._id;
    content->add_child<folia::Feature>( args );
  }
  return content;
}

void add_hspace( folia::FoliaElement *content,
		 const UnicodeString& value ){
  //! insert a <t-hspace> node to a FoliaElement
  /*!
    \param content the node to connect to
   */
  folia::KWargs args;
  args["class"] = "space";
  folia::FoliaElement *hs = content->add_child<folia::TextMarkupHSpace>( args );
  if ( !value.isEmpty() ){
    folia::XmlText *te = hs->add_child<folia::XmlText>();
    te->setuvalue(value );
  }
}

void add_value( folia::FoliaElement *content,
		const UnicodeString& value ){
  //! add a string value to the content
  /*!
    \param content the Folia to extend
    \param value the Unicode string to add
    this fuction will replace leading and trailing spaces by <t-hspace> nodes
  */
  if ( !value.isEmpty() ){
    UnicodeString start_spaces;
    for ( int i=0;i < value.length(); ++i ){
      if ( u_isspace(value[i] ) ){
	start_spaces += value[i];
      }
      else {
	break;
      }
    }
    UnicodeString end_spaces;
    for ( int i=value.length()-1; i>0; --i ){
      if ( u_isspace(value[i] ) ){
	end_spaces = value[i] + end_spaces;
      }
      else {
	break;
      }
    }
    bool begin_space = !start_spaces.isEmpty();
    bool end_space = !end_spaces.isEmpty();
    UnicodeString out = value;
    out.trim();
    if ( begin_space ){
      // represent ALL leading spaces as 1 TextMarkupHSpace
      add_hspace( content, start_spaces );
    }
    content->add_child<folia::XmlText>( TiCC::UnicodeToUTF8(out) );
    if ( end_space ){
      // represent ALL trailing spaces as 1 TextMarkupHSpace
      add_hspace( content, end_spaces );
    }
  }
}

void append_metric( folia::Paragraph *root,
		    const string& att,
		    const string& val ){
  folia::KWargs args;
  args["value"] = val;
  args["class"] = att;
  root->add_child<folia::Metric>( args );
}

bool process_paragraph( folia::Paragraph *paragraph,
			xmlNode *par,
			const map<string,formatting_info>& font_styles ){
  formatting_info par_font;
  try {
    string par_style = TiCC::getAttribute( par, "style" );
    if ( !par_style.empty() ){
      par_font = font_styles.at(par_style);
    }
  }
  catch ( const out_of_range& ){
    // continue
  }
  string item = TiCC::getAttribute( par, "isListItem" );
  bool is_item = ( item == "1" );
  list<xmlNode*> lines = TiCC::FindNodes( par, "*:line" );
  if ( verbose ){
#pragma omp critical
    {
      cout << "\t\tfound " << lines.size() << " lines" << endl;
    }
  }
  if ( lines.empty() ){
    return false;
  }

  if ( add_metrics ){
    // add the metrics for the first charParam of the first line
    // and the last charParam of the last line
    // First the charParams of the first line
    list<xmlNode *> chrs = TiCC::FindNodes( lines.front(), "*/*:charParams" );
    // get the attributes of the first charParam
    map<string,string> atts = TiCC::getAttributes( chrs.front() );
    append_metric( paragraph, "first_char_top", atts["t"] );
    append_metric( paragraph, "first_char_left", atts["l"] );
    append_metric( paragraph, "first_char_right", atts["r"] );
    append_metric( paragraph, "first_char_bottom", atts["b"] );
    // Then the charParams of the last line
    chrs = TiCC::FindNodes( lines.back(), "*/*:charParams" );
    // get the attributes of the last charParam
    atts = TiCC::getAttributes( chrs.back() );
    append_metric( paragraph, "last_char_top", atts["t"] );
    append_metric( paragraph, "last_char_left", atts["l"] );
    append_metric( paragraph, "last_char_right", atts["r"] );
    append_metric( paragraph, "last_char_bottom", atts["b"] );
  }
  //  cerr << "start process lines: " << endl;
  folia::KWargs text_args;
  text_args["class"] = classname;
  folia::FoliaElement *root
    = paragraph->add_child<folia::TextContent>( text_args);
  bool previous_hyphen = false;
  for ( const auto& line : lines ){
    vector<line_info> line_parts;
    process_line( line, line_parts, par_font, font_styles );
    bool no_break = false;
    //    cerr << "\tstart process parts: " << endl;
    folia::TextMarkupString *container = 0;
    for ( const auto& it : line_parts ){
      // cerr << "\tNEXT part " << endl;
      // cerr << "previous_hyphen=" << previous_hyphen << endl;
      folia::TextMarkupStyle *content = 0;
      if ( !container ){
	// start with a fresh <t-style>.
	string val;
	if ( !add_breaks && !previous_hyphen ) {
	  // start on a new line too
	  val = "\n";
	}
	root->add_child<folia::XmlText>( val );
	folia::KWargs args;
	args["generate_id"] = paragraph->id();
	container = root->add_child<folia::TextMarkupString>( args );
	content = make_style_content( it._fi, root->doc() );
	container->append( content );
	//	cerr << "\t First styled container: " << container << endl;
      }
      else {
	// end previous t-str and start a new one
	content = make_style_content( it._fi, root->doc() );
	container->append( content );
	//	cerr << "\t Next styled container: " << container << endl;
	no_break = false;
      }
      UnicodeString value = it._value;
      //      cerr << "VALUE= '" << value << "'" << endl;
      previous_hyphen = false;
      if ( !it._hyph.isEmpty() ){
	// check if we have a hyphenation
	if ( is_item ){
	  // list items are special. KEEP the hyphen!
	  value = "- " + value;
	  add_value( content, value );
	}
	else {
	  // a 'true' hyphen: add the value + <t-hbr/>
	  //	cerr << "HYPH= '" << it._hyph << "'" << endl;
	  add_value( content, value );
	  folia::Hyphbreak *hb = content->add_child<folia::Hyphbreak>();
	  if ( it._hyph == SOFT_HYPHEN
	       || ( it._hyph == "-"
		    && &it == &line_parts.back() ) ){
	    folia::XmlText *e = hb->add_child<folia::XmlText>();
	    e->setuvalue( it._hyph );
	    previous_hyphen = true;
	  }
	  //	cerr << "content now: " << content << endl;
	  no_break = true;
	}
      }
      else if ( !it._spaces.isEmpty() ){
	add_hspace( content, it._spaces );
      }
      else {
	add_value( content, value );
	//	cerr << "added content now: " << content << endl;
      }
      if ( &it == &line_parts.back() ){
	// the remains
	if ( !no_break && add_breaks ){
	  content->add_child<folia::Linebreak>();
	  //	  cerr << "content now: " << content << endl;
	}
      }
      //      cerr << "textcontent now: " << root << endl;
    }
  }
  return true;
}

bool process_page( folia::FoliaElement *root,
		   xmlNode *block,
		   const map<string,formatting_info>& font_styles ){
  list<xmlNode*> paragraphs = TiCC::FindNodes( block, ".//*:par" );
  if ( verbose ){
#pragma omp critical
    {
      cout << "\tfound " << paragraphs.size() << " paragraphs" << endl;
    }
  }
  bool didit = false;
  for ( const auto& par_node : paragraphs ){
    folia::KWargs args;
    args["generate_id"] = root->id();
    folia::Paragraph *paragraph = root->add_child<folia::Paragraph>( args );
    string style = TiCC::getAttribute( par_node, "style" );
    if ( !style.empty() ){
      args.clear();
      args["subset"] = "par_style";
      args["class"] = style;
      paragraph->add_child<folia::Feature>( args );
    }
    string align = TiCC::getAttribute( par_node, "align" );
    if ( !align.empty() ){
      args.clear();
      args["subset"] = "par_align";
      args["class"] = align;
      paragraph->add_child<folia::Feature>( args );
    }
    if ( process_paragraph( paragraph, par_node, font_styles ) ){
      didit = true;
    }
    else {
      destroy( paragraph );
    }
  }
  return didit;
}

map<string,formatting_info> extract_formatting_info( xmlNode *root ){
  map<string,formatting_info> result;
  list<xmlNode*> par_styles =
    TiCC::FindNodes( root, ".//*:paragraphStyles/*:paragraphStyle" );
  map<string,string> main_font_styles;
  for ( const auto& ps : par_styles ){
    string pid = TiCC::getAttribute( ps, "id" );
    string mf_id = TiCC::getAttribute( ps, "mainFontStyleId" );
    main_font_styles[pid] = mf_id;
    list<xmlNode*> font_styles =
      TiCC::FindNodes( ps, ".//*:fontStyle" );
    for ( const auto& fst : font_styles ){
      string lang = TiCC::getAttribute( fst, "lang" );
      string id = TiCC::getAttribute( fst, "id" );
      string ff = TiCC::getAttribute( fst, "ff" );
      string fs = TiCC::getAttribute( fst, "fs" );
      string it = TiCC::getAttribute( fst, "italic" );
      string bl = TiCC::getAttribute( fst, "bold" );
      font_style f_s = REGULAR;
      if ( it == "1" ){
	f_s = ITALIC;
      }
      else if ( bl == "1" ){
	f_s = BOLD;
      }
      formatting_info fi( lang, ff, fs, f_s );
      result.insert( make_pair(id,fi) );
    }
  }
  for ( const auto& mf : main_font_styles ){
    auto it = result.find(mf.second);
    if ( it != result.end() ){
      auto const val = it->second;
      result.insert( make_pair(mf.first,val) );
    }
    else {
      cerr << "surprise for " << mf.first << " ==> "<< mf.second << endl;
    }
  }
  return result;
}

bool convert_abbyxml( const string& fileName,
		      const string& outputDir,
		      const zipType outputType,
		      const string& prefix,
		      const string& command ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "start handling " << fileName << endl;
    }
  }
  zipType inputType;
  xmlDoc *xdoc = getXml( fileName, inputType );
  if ( !xdoc ){
#pragma omp critical
    {
      cerr << "problem detecting type of file: " << fileName << endl;
      cerr << "it MUST have extension .xml, .xml.bz2 or .xml.gz" << endl;
    }
    return false;
  }
  xmlNode *root = xmlDocGetRootElement( xdoc );
  list<xmlNode*> pages = TiCC::FindNodes( root, ".//*:page" );
  if ( pages.empty() ){
#pragma omp critical
    {
      cerr << "Problem finding pages node in " << fileName << endl;
    }
    return false;
  }
  else if ( verbose ){
#pragma omp critical
    {
      cout << "found " << pages.size() << " page nodes" << endl;
    }
  }
  map<string,formatting_info> font_styles = extract_formatting_info( root );
  string orgFile = TiCC::basename( fileName );
  string docid = orgFile.substr( 0, orgFile.find(".") );
  docid = prefix + docid;
  folia::Document doc( "xml:id='" + docid + "'" );
  folia::KWargs args;
  args["xml:id"] = docid + ".text";
  folia::Text *text = doc.setTextRoot( args );
  args.extract("xml:id");
  folia::processor *proc = add_provenance( doc, processor_name, command );
  processor_id = proc->id();
  args["processor"] = processor_id;
  doc.declare( folia::AnnotationType::PARAGRAPH, setname, args );
  doc.declare( folia::AnnotationType::DIVISION, setname, args );
  doc.declare( folia::AnnotationType::STRING, setname, args );
  doc.declare( folia::AnnotationType::STYLE, setname, args );
  doc.declare( folia::AnnotationType::HYPHENATION, setname, args );
  doc.declare( folia::AnnotationType::HSPACE, setname, args );
  doc.declare( folia::AnnotationType::LANG, setname, args );
  if ( add_metrics ){
    doc.declare( folia::AnnotationType::METRIC, setname, args );
  }
  if ( add_breaks ){
    doc.declare( folia::AnnotationType::LINEBREAK, setname, args );
  }
  xmlNode *docinfo = TiCC::xPath( root, "//*:paragraphStyles" );
  if ( docinfo ){
    doc.set_foreign_metadata( docinfo );
  }
  doc.set_metadata( "abby_file", orgFile );
  for ( const auto& page : pages ){
    args["generate_id"] = text->id();
    folia::Division *div = text->add_child<folia::Division>( args );
    process_page( div, page, font_styles );
  }

  string outName;
  if ( !outputDir.empty() ){
    outName = outputDir + "/";
  }
  string::size_type pos2 = orgFile.find( ".xml" );
  orgFile = orgFile.erase( pos2 );
  outName += prefix+ orgFile + ".folia.xml";
  zipType type = inputType;
  if ( outputType != NORMAL ){
    type = outputType;
  }
  if ( type == BZ2 ){
    outName += ".bz2";
  }
  else if ( type == GZ ){
    outName += ".gz";
  }

  doc.save( outName );
#pragma omp critical
  {
    cout << "\tconverted: " << fileName << " into: " << outName << endl;
  }
  return true;
}

void usage(){
  cerr << "Usage: FoLiA-abby [options] file/dir" << endl;
  cerr << "\t-t <threads>\n\t--threads <threads> Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-h or --help\t this messages " << endl;
  cerr << "\t-O\t\t output directory " << endl;
  cerr << "\t-S or --setname='set'\t the FoLiA set name for <t> nodes. "
    "(default '" << setname << "')" << endl;
  cerr << "\t-C or --class='class'\t the FoLiA class name for <t> nodes. "
    "(default '" << classname << "')" << endl;
  cerr << "\t--addbreaks\t optionally add <br/> nodes for every newline in the input" << endl;
  cerr << "\t--addmetrics\t optionally add Metric information about first and last characters in each <par> from the input" << endl;
  cerr << "\t--prefix='pre'\t add this prefix to the xml:id of ALL nodes in the created files. (default 'FA-') " << endl;
  cerr << "\t\t\t use 'none' for an empty prefix. (can be dangerous)" << endl;
  cerr << "\t--compress='c'\t with 'c'=b create bzip2 files (.bz2) " << endl;
  cerr << "\t\t\t with 'c'=g create gzip files (.gz)" << endl;
  cerr << "\t-v\t\t verbose output " << endl;
  cerr << "\t-V or --version\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts( "vVt:O:hC:S:",
			 "compress:,class:,setname:,help,version,prefix:,"
			 "addbreaks,addmetrics,keephyphens,threads:" );
  try {
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
#ifdef HAVE_OPENMP
  int numThreads=1;
#endif
  string outputDir;
  zipType outputType = NORMAL;
  if ( opts.extract( 'h' ) || opts.extract( "help" ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract( 'V' ) || opts.extract( "version" ) ){
    cerr << opts.prog_name() << " [" << PACKAGE_STRING << "]"<< endl;
    exit(EXIT_SUCCESS);
  }
  string orig_command = "FoLiA-abby " + opts.toString();
  string value;
  if ( opts.extract( "compress", value ) ){
    if ( value == "b" ){
      outputType = BZ2;
    }
    else if ( value == "g" ) {
      outputType = GZ;
    }
    else {
      cerr << "unknown compression: use 'b' or 'g'" << endl;
      exit( EXIT_FAILURE );
    }
  }
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
#ifdef HAVE_OPENMP
    if ( TiCC::lowercase(value) == "max" ){
      numThreads = omp_get_max_threads() - 2;
    }
    else if ( !TiCC::stringTo(value,numThreads) ) {
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
    else {
      numThreads = TiCC::stringTo<int>( value );
    }
#else
    cerr << "OpenMP support is missing. -t options not supported!" << endl;
    exit( EXIT_FAILURE );
#endif
  }
  verbose = opts.extract( 'v' );
  keep_hyphens = opts.extract( "keephyphens" );
  if ( keep_hyphens ){
    cerr << "WARNING: --keep-hyphens no longer necessesary. Ignored" << endl;
  }
  add_breaks = opts.extract( "addbreaks" );
  add_metrics = opts.extract( "addmetrics" );
  opts.extract( 'O', outputDir );
  if ( opts.extract( "setname", value )
       || opts.extract( 'S', value ) ){
    setname = value;
  }
  if ( opts.extract( "classname", value )
       || opts.extract( 'C', value ) ){
    classname = value;
  }
  string prefix = "FA-";
  opts.extract( "prefix", prefix );
  if ( prefix == "none" ){
    prefix.clear();
  }
  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    cerr << "missing input file(s)" << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  else if ( fileNames.size() > 1 ){
    cerr << "currently only 1 file or directory is supported" << endl;
    exit( EXIT_FAILURE );
  }

  if ( !outputDir.empty() ){
    string name = outputDir + "/";
    if ( !TiCC::isDir(name) ){
      if ( !TiCC::createPath( name ) ){
	cerr << "outputdir '" << name
	     << "' doesn't exist and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  string name = fileNames[0];
  if ( !( TiCC::isFile(name) || TiCC::isDir(name) ) ){
    cerr << "parameter '" << name << "' doesn't seem to be a file or directory"
	 << endl;
    exit(EXIT_FAILURE);
  }
  else {
    // we assume name is a directory
    if ( name.back() == '/' ){
      name.pop_back();
    }
    fileNames = TiCC::searchFilesMatch( name, ".xml($|.gz$|.bz2$)", false );
  }
  size_t toDo = fileNames.size();
  if ( toDo == 0 ){
    cerr << "no matching files found." << endl;
    exit(EXIT_FAILURE);
  }
  if ( toDo > 1 ){
    cout << "start processing of " << toDo << " files " << endl;
  }

#ifdef HAVE_OPENMP
  if ( numThreads >= 1 ){
    omp_set_num_threads( numThreads );
  }
#endif

#pragma omp parallel for shared(fileNames)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    if ( !convert_abbyxml( fileNames[fn],
			   outputDir,
			   outputType,
			   prefix,
			   orig_command ) )
#pragma omp critical
      {
	cerr << "failure on " << fileNames[fn] << endl;
      }
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
