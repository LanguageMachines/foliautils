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

#include <cstdio> // remove()
#include <string>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iostream>
#include <fstream>
#include "libfolia/folia.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "ticcutils/Unicode.h"
#include "ticcutils/zipper.h"
#include "foliautils/common_code.h"

#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	icu;

bool verbose = false;
bool clearCachedFiles = false;
string setname = "FoLia-alto-set";
string classname = "OCR";
string prefix = "FA-";
string processor_id;

class docCache {
public:
  ~docCache() { clear(); };
  void clear();
  void fill( const string&, const list<xmlNode*>& );
  void add( const string&, const string& );
  xmlDoc *find( const string& ) const;
  unordered_map<string, xmlDoc*> cache;
};

void docCache::clear(){
  for ( const auto& it : cache ){
    xmlFreeDoc( it.second );
  }
}

void docCache::add( const string& dir, const string& f ){
  if ( cache.find( f ) == cache.end() ){
    string file = dir + f;
    ifstream is( file );
    if ( is ){
      if ( verbose ){
#pragma omp critical
	{
	  cout << "open file " << file << endl;
	}
      }
      xmlDoc *xmldoc = xmlReadFile( file.c_str(), 0, XML_PARSER_OPTIONS );
      if ( xmldoc ){
	cache[f] = xmldoc;
      }
      else {
#pragma omp critical
	{
	  cerr << "Unable to read the ALTO XML from " << file << endl;
	}
      }
    }
    else {
#pragma omp critical
      {
	cerr << "unable to find ALTO XML file " << file << endl;
      }
    }
  }
  else if ( verbose ){
#pragma omp critical
    {
      cout << "found " << f << " in cache." << endl;
    }
  }
}

void docCache::fill( const string& alto_cache, const list<xmlNode*>& blocks ){
  for ( const auto& it : blocks ){
    string alt = TiCC::getAttribute( it, "alto" );
    if ( !alt.empty() ){
      add( alto_cache, alt );
    }
  }
}

xmlDoc *docCache::find( const string& f ) const {
  const auto it = cache.find( f );
  if ( it == cache.end() ){
    return 0;
  }
  else {
    return it->second;
  }
}

xmlNode *findPart2Level( const xmlNode *start ){
  if ( start && start->next ){
    xmlNode *pnt = start->next->children;
    while ( pnt ){
      if ( pnt->type == XML_ELEMENT_NODE &&
	   TiCC::Name(pnt) == "String" ){
	break;
      }
      pnt = pnt->next;
    }
    if ( pnt ){
      // found a node "String"
      string sub = TiCC::getAttribute( pnt, "SUBS_TYPE" );
      if ( sub == "HypPart2" )
	return pnt;
    }
  }
  return 0;
}

xmlNode *findPart2Block( const xmlNode *start ){
  if ( start && start->next ){
    xmlNode *pnt = start->next->children;
    while ( pnt ){
      if ( pnt->type == XML_ELEMENT_NODE &&
	   TiCC::Name(pnt) == "TextLine" ){
	break;
      }
      pnt = pnt->next;
    }
    if ( pnt ){
      // found a node "TextLine"
      pnt = pnt->children;
      while ( pnt ){
	if ( pnt->type == XML_ELEMENT_NODE &&
	     TiCC::Name(pnt) == "String" ){
	  break;
	}
	pnt = pnt->next;
      }
      if ( pnt ){
	// found a node "String"
	string sub = TiCC::getAttribute( pnt, "SUBS_TYPE" );
	if ( sub == "HypPart2" ){
	  return pnt;
	}
      }
    }
  }
  return 0;
}

void addStr( folia::Paragraph *par, UnicodeString& txt,
	     const xmlNode *pnt, const string& altoFile,
	     bool do_strings,
	     int cnt = 1 ){
  folia::KWargs atts = folia::getAttributes( pnt );
  string kid = atts["ID"];
  string content = atts["CONTENT"];
  if ( content.empty() )
    return;
  vector<string> parts = TiCC::split( content );
  if ( parts.size() == 1 ){
    // OK that's what we hoped for
    folia::KWargs args;
    string arg = par->id() + ".";
    if ( !kid.empty() ){
      arg += kid;
    }
    else {
      arg += "String_" + TiCC::toString(cnt);
    }
    args["xml:id"] = arg;
    folia::FoliaElement *s;
    if ( do_strings ){
      s = par->add_child<folia::String>( args );
    }
    else {
      s = par->add_child<folia::Word>( args );
    }
    UnicodeString uc = TiCC::UnicodeFromUTF8( content );
    s->setutext( uc, txt.length(), classname );
    txt += " " + uc;
    if ( !altoFile.empty() ){
      // direct mode has no altofile
      args.clear();
      args["processor"] = processor_id;
      par->doc()->declare( folia::AnnotationType::RELATION,
			   setname, args );
      args.clear();
      args["xlink:href"] = altoFile;
      folia::Relation *h = s->add_child<folia::Relation>( args );
      args.clear();
      args["id"] = kid;
      args["type"] = "str";
      h->add_child<folia::LinkReference>( args );
    }
  }
  else {
    for ( size_t i=0; i < parts.size(); ++i ){
      folia::KWargs args;
      string arg = par->id() + ".";
      if ( !kid.empty() ){
	arg += kid + "_" + TiCC::toString(i);
      }
      else {
	arg += "String_" + TiCC::toString(i);
      }
      args["xml:id"] = arg;

      folia::FoliaElement *s;
      if ( do_strings ){
	s = par->add_child<folia::String>( args );
      }
      else {
	s = par->add_child<folia::Word>( args );
      }
      UnicodeString uc = TiCC::UnicodeFromUTF8( parts[i] );
      s->setutext( uc, txt.length(), classname );
      txt += " " + uc;
      if ( !altoFile.empty() ){
	args.clear();
	args["processor"] = processor_id;
	par->doc()->declare( folia::AnnotationType::RELATION,
			     setname, args );
	args.clear();
	args["xlink:href"] = altoFile;
	folia::Relation *h = s->add_child<folia::Relation>( args );
	args.clear();
	args["id"] = kid;
	args["type"] = "str";
	h->add_child<folia::LinkReference>( args );
      }
    }
  }
}

void createFile( folia::FoliaElement *text,
		 xmlDoc *alt_doc,
		 const string& altoFile,
		 const list<xmlNode*>& textblocks,
		 bool do_strings ){
  xmlNode *root = xmlDocGetRootElement( alt_doc );
  xmlNode *keepPart1 = 0;
  set<string> ids;
  for ( const auto& block : textblocks ){
    string id = TiCC::getAttribute( block, "ID" );
    if ( ids.find(id) != ids.end() ){
      if ( verbose ){
#pragma omp critical
	{
	  cout << "skip duplicate ID " << id << endl;
	}
      }
      continue;
    }
    else {
      ids.insert(id);
    }
    folia::KWargs p_args;
    p_args["processor"] = processor_id;
    text->doc()->declare( folia::AnnotationType::PARAGRAPH, setname, p_args );
    string arg = text->id() + ".p.";
    if ( !altoFile.empty() ){
      arg += altoFile + ".";
    }
    arg += id;
    p_args["xml:id"] = arg;
    folia::Paragraph *p = text->add_child<folia::Paragraph>( p_args );
    UnicodeString ocr_text;
    list<xmlNode*> v =
      TiCC::FindNodes( root,
			"//*[local-name()='TextBlock' and @ID='"
			+ id + "']" );
    if ( v.size() == 1 ){
      xmlNode *node = v.front();
      list<xmlNode*> lv = TiCC::FindNodes( node, "*[local-name()='TextLine']" );
      int cnt = 0;
      for ( const auto *line : lv ){
	xmlNode *pnt = line->children;
	while ( pnt ){
	  if ( pnt->type == XML_ELEMENT_NODE ){
	    if ( TiCC::Name(pnt) == "String" ){
	      string sub_t = TiCC::getAttribute( pnt, "SUBS_TYPE" );
	      if ( sub_t == "HypPart2" ){
		if ( keepPart1 == 0 ){
		  addStr( p, ocr_text, pnt, altoFile, do_strings, ++cnt );
		}
		else {
		  folia::KWargs atts = folia::getAttributes( keepPart1 );
		  string kid = atts["ID"];
		  string sub_c = atts["SUBS_CONTENT"];
		  UnicodeString subc = TiCC::UnicodeFromUTF8( sub_c );
		  folia::KWargs sub_args;
		  string id_arg = p->id() + ".";
		  if ( !kid.empty() ){
		    id_arg += kid;
		  }
		  else {
		    id_arg += "String_" + TiCC::toString(++cnt);
		  }
		  sub_args["xml:id"] = id_arg;
		  sub_args["class"] = classname;
		  folia::FoliaElement *s;
		  if ( do_strings ){
		    s = p->add_child<folia::String>( sub_args );
		  }
		  else {
		    s = p->add_child<folia::Word>( sub_args );
		  }
		  s->setutext( subc,
			       ocr_text.length(),
			       classname );
		  ocr_text += " " + subc;
		  if ( !altoFile.empty() ){
		    folia::KWargs rel_args;
		    rel_args["processor"] = processor_id;
		    text->doc()->declare( folia::AnnotationType::RELATION,
		    			  setname, rel_args );
		    rel_args.clear();
		    rel_args["xlink:href"] = altoFile;
		    folia::Relation *h = s->add_child<folia::Relation>( rel_args );
		    rel_args.clear();
		    rel_args["id"] = kid;
		    rel_args["type"] = "str";
		    h->add_child<folia::LinkReference>( rel_args );
		    rel_args["id"] = TiCC::getAttribute( pnt, "ID" );
		    h->add_child<folia::LinkReference>( rel_args );
		  }
		  keepPart1 = 0;
		}
		pnt = pnt->next;
		continue;
	      }
	      else if ( sub_t == "HypPart1" ){
		// see if there is a Part2 in next line at this level.
		const xmlNode *part2 = findPart2Level( line );
		if ( part2 ){
		  keepPart1 = pnt;
		  break; //continue with next TextLine
		}
		else {
		  // no second part on this level. seek in the next TextBlock
		  part2 = findPart2Block( node );
		  if ( !part2 ){
		    // Ok. Just ignore this and take the CONTENT
		    addStr( p, ocr_text, pnt, altoFile, do_strings );
		  }
		  else {
		    keepPart1 = pnt;
		    break; //continue with next TextLine, but this should
		    // be empty, so in fact we go to the next block
		  }
		}
	      }
	      else {
		addStr( p, ocr_text, pnt, altoFile, do_strings, ++cnt );
	      }
	    }
	  }
	  pnt = pnt->next;
	}
      }
    }
    else if ( v.empty() ){
      // probably an CB node...
      if ( id.find("CB") != string::npos ){
	// YES
      }
      else
	cerr << "found nothing, what is this? " << id << endl;
    }
    else {
      cerr << "Confusing! " << endl;
    }
    if ( !ocr_text.isEmpty() )
      p->setutext( ocr_text.tempSubString(1), classname );
  }
}

void processBlocks( folia::FoliaElement *text,
		    const list<xmlNode*>& blocks,
		    const docCache& cache,
		    bool do_strings ){
  for ( const auto& it : blocks ){
    string alt = TiCC::getAttribute( it, "alto" );
    xmlDoc *alt_doc = cache.find( alt );
    if ( alt_doc ){
      list<xmlNode*> texts = TiCC::FindNodes( it, "dcx:TextBlock" );
      createFile( text, alt_doc, alt, texts, do_strings );
    }
    else {
#pragma omp critical
      {
	cerr << "didn't find doc " << alt << endl;
      }
    }
  }
}

string replaceColon( const string& f, char r ){
  string result = f;
  for( size_t i=0; i < result.length(); ++i ){
    if ( result[i] == ':' )
      result[i] = r;
  }
  return result;
}

void processArticle( const string& f,
		     const string& subject,
		     const list<xmlNode*>& parts,
		     const docCache& cache,
		     const string& outDir,
		     const zipType inputType,
		     const zipType outputType,
		     const string& command,
		     bool do_strings ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "start handling " << f << " (" << subject << ")" << endl;
    }
  }
  string docid = replaceColon(f,'.');
  docid = prefix + docid;
  string outName = outDir;
  if ( subject == "artikel" ){
    outName += "artikel/";
  }
  else {
    outName += "overige/";
  }
  outName += docid + ".folia.xml";
  folia::Document doc( "xml:id='" + docid + "'" );
  folia::processor *proc = add_provenance( doc, "FoLiA-alto", command );
  processor_id = proc->id();
  folia::KWargs args;
  args["processor"] = processor_id;
  if ( do_strings ){
    doc.declare( folia::AnnotationType::STRING, setname, args );
  }
  else {
    doc.declare( folia::AnnotationType::TOKEN, setname, args );
  }
  doc.set_metadata( "genre", subject );
  args.clear();
  args["xml:id"] = docid + ".text";
  folia::Text *text = doc.create_root<folia::Text>( args );
  for ( const auto& it : parts ){
    list<xmlNode*> blocks = TiCC::FindNodes( it, "dcx:blocks" );
    if ( blocks.empty() ){
#pragma omp critical
      {
	cerr << "found no blocks" << endl;
      }
    }
    processBlocks( text, blocks, cache, do_strings );
  }

  zipType type = inputType;
  if ( outputType != NORMAL )
    type = outputType;
  if ( type == BZ2 ){
    outName += ".bz2";
  }
  else if ( type == GZ ){
    outName += ".gz";
  }
  vector<folia::Paragraph*> pv = doc.paragraphs();
  if ( pv.size() == 0 ||
       ( pv.size() == 1 && pv[0]->size() == 0 ) ){
    // no paragraphs, or just 1 without data
#pragma omp critical
    {
      cerr << "skipped empty result : " << outName << endl;
    }
  }
  else {
    doc.save( outName );
    if ( verbose ){
#pragma omp critical
      {
	cout << "created: " << outName << endl;
      }
    }
  }
}

void processZone( const string& id,
		  const string& subject,
		  xmlNode *zone,
		  const docCache& cache,
		  const string& outDir,
		  const zipType inputType,
		  const zipType outputType,
		  const string& command,
		  bool do_strings ){
  list<xmlNode *> parts =  TiCC::FindNodes( zone, "dcx:article-part" );
  if ( !parts.empty() ){
    processArticle( id,
		    subject,
		    parts,
		    cache,
		    outDir,
		    inputType,
		    outputType,
		    command,
		    do_strings );
  }
}

string generateName( const string& ref, string& id ){
  string res;
  string::size_type pos = ref.find( "urn=" );
  res = ref.substr(pos+4);
  id = res;
  pos = res.find( ":mpeg21" );
  res.replace(pos, string::npos, res.substr(pos+7) );
  res = replaceColon(res,'_');
  res += ".xml";
  return res;
}

bool download( const string& alto_cache,
	       const list<xmlNode*>& resources,
	       unordered_map<string,string>& urns,
	       unordered_map<string,string>& downloaded ) {
  urns.clear();
  downloaded.clear();
  int d_cnt = 0;
  int c_cnt = 0;
  if ( !TiCC::isDir(alto_cache) ){
    if ( !TiCC::createPath( alto_cache ) ){
#pragma omp critical
      {
	cerr << "alto dir '" << alto_cache
	     << "' doesn't exist and can't be created" << endl;
      }
      return false;
    }
  }
  unordered_map<string,string> ref_file_map;
  for ( const auto& res : resources ){
    string ref = TiCC::getAttribute( res, "ref" );
    if ( ref.find( ":alto" ) != string::npos ){
      string id;
      string fn = generateName( ref, id );
      urns[id] = ref;
      if ( !fn.empty() ){
	fn = alto_cache + fn;
	ifstream is( fn );
	if ( !is ){
	  // not yet downloaded
	  ref_file_map[ref] = fn;
	  downloaded[id] = fn;
	}
	else {
	  ++c_cnt;
	  downloaded[id] = fn;
	  if ( verbose ){
#pragma omp critical
	    {
	      cout << "file " << fn << " already exists" << endl;
	    }
	  }
	}
      }
    }
  }

  int retry = 3;
  while ( retry-- > 0 ){
    auto it = ref_file_map.begin();
    while ( it != ref_file_map.end() ){
      string cmd = "wget " + it->first + " -q -O " + it->second;
      int res = system( cmd.c_str() );
      if ( res ){
	if ( retry == 0 ){
#pragma omp critical
	  {
	    cerr << "repeatedly failed to execute: '" << cmd << "'" << endl;
	  }
	  downloaded[it->second] = "FAIL";
	}
	++it;
      }
      else {
	++d_cnt;
	ref_file_map.erase( it++ );
      }
    }
  }
  if ( !ref_file_map.empty() ){
#pragma omp critical
    {
      cerr << "unable to retrieve some alto files:" << endl;
    }
#pragma omp critical
    {
      for ( const auto& it : ref_file_map ){
	cerr << "reference:" << it.first
	     << " filename: " << it.second << endl;
      }
    }
    if ( (d_cnt + c_cnt) == 0 ){
#pragma omp critical
      {
	cerr << "NO alto files retrieved, no use to continue." << endl;
      }
      return false;
    }

  }
  if ( verbose ){
#pragma omp critical
    {
      cout << "retrieved " << d_cnt + c_cnt << " alto files. ("
	   << c_cnt << " from cache)" << endl;
    }
  }
  return true;
}

bool download( const string& alto_cache,
	       const list<xmlNode*>& resources,
	       unordered_set<string>& downloaded ){
  downloaded.clear();
  int d_cnt = 0;
  int c_cnt = 0;
  if ( !TiCC::isDir(alto_cache) ){
    if ( !TiCC::createPath( alto_cache ) ){
#pragma omp critical
      {
	cerr << "alto dir '" << alto_cache
	     << "' doesn't exist and can't be created" << endl;
      }
      return false;
    }
  }
  unordered_map<string,string> ref_file_map;
  for ( const auto res :resources ){
    string ref = TiCC::getAttribute( res, "ref" );
    string fn = TiCC::getAttribute( res, "filename" );
    if ( ref.find( ":alto" ) != string::npos ){
      if ( !fn.empty() ){
	fn = alto_cache + fn;
	ifstream is( fn );
	if ( !is ){
	  // not yet downloaded
	  ref_file_map[ref] = fn;
	}
	else {
	  ++c_cnt;
	  downloaded.insert( fn );
	  if ( verbose ){
#pragma omp critical
	    {
	      cout << "file " << fn << " already exists" << endl;
	    }
	  }
	}
      }
    }
  }

  int retry = 3;
  while ( retry-- > 0 ){
    auto it = ref_file_map.begin();
    while ( it != ref_file_map.end() ){
      string cmd = "wget " + it->first + " -q -O " + it->second;
      int res = system( cmd.c_str() );
      if ( res ){
	if ( retry == 0 ){
#pragma omp critical
	  {
	    cerr << "repeatedly failed to execute: '" << cmd << "'" << endl;
	  }
	}
	++it;
      }
      else {
	downloaded.insert( it->second );
	++d_cnt;
	ref_file_map.erase( it++ );
      }
    }
  }
  if ( !ref_file_map.empty() ){
#pragma omp critical
    {
      cerr << "unable to retrieve some alto files:" << endl;
    }
#pragma omp critical
    {
      for ( const auto& it : ref_file_map ){
	cerr << "reference:" << it.first
	     << " filename: " << it.second << endl;
      }
    }
    if ( (d_cnt + c_cnt) == 0 ){
#pragma omp critical
      {
	cerr << "NO alto files retrieved, no use to continue." << endl;
      }
      return false;
    }

  }
  if ( verbose ){
#pragma omp critical
    {
      cout << "retrieved " << d_cnt + c_cnt << " alto files. ("
	   << c_cnt << " from cache)" << endl;
    }
  }
  return true;
}

bool clear_alto_files( const string& dirName ){
  string cmd = "exec rm -r " + dirName + "*alto.xml > /dev/null 2>&1";
  int res = system( cmd.c_str() );
  if ( res ){
    cerr << "failed to remove alto files in " << dirName << endl;
    cerr << "already empty?" << endl;
    return false;
  }
  cout << "cleared the cache " << dirName << endl;
  return true;
}

void clear_files( const unordered_set<string>& files ){
  for( const auto& it : files ){
    int res = remove( it.c_str() );
    if ( res ){
#pragma omp critical
      {
	cerr << "problems removing file " << it << endl;
      }
    }
    else if ( verbose ){
#pragma omp critical
      {
	cout << "cleared file " << it << endl;
      }
    }
  }
}

void solveArtAlto( const string& alto_cache,
		   const string& file,
		   const string& outDir,
		   zipType outputType,
		   const string& command,
		   bool do_strings ){
  bool succes = true;
#pragma omp critical
  {
    cout << "resolving KRANT " << file << endl;
  }
  zipType inputType;
  xmlDoc *xmldoc = getXml( file, inputType );
  if ( xmldoc ){
    xmlNode *root = xmlDocGetRootElement( xmldoc );
    xmlNode *metadata = TiCC::xPath( root, "//*:metadata" );
    if ( metadata ){
      xmlNode *didl = TiCC::xPath( metadata, "//*[local-name()='DIDL']" );
      if ( didl ){
	list<xmlNode*> resources = TiCC::FindNodes( didl, "//didl:Component/didl:Resource[@mimeType='text/xml']" );
	if ( resources.empty() ){
#pragma omp critical
	  {
	    cout << "Unable to find usable text/xml resources in the DIDL: "
		 << file << endl;
	    succes = false;
	  }
	}
	else {
	  unordered_set<string> downloaded_files;
	  if ( download( alto_cache, resources, downloaded_files ) ){
	    if ( downloaded_files.empty() ){
#pragma omp critical
	      {
		cerr << "unable to find downloadable files " <<  file << endl;
	      }
	      succes = false;
	    }
	    else {
	      cout << "Downloaded files " << endl;
	      list<xmlNode*> blocks = TiCC::FindNodes( didl, "//dcx:blocks" );
	      docCache cache; // each thread it own cache...
	      cache.fill( alto_cache, blocks );
	      list<xmlNode*> items = TiCC::FindNodes( didl, "didl:Item/didl:Item/didl:Item" );
	      if ( items.empty() ){
#pragma omp critical
		{
		  cout << "Unable to find usable Items in the DIDL: "
		       << file << endl;
		  succes = false;
		}
	      }
	      else {
		unordered_set<string> article_names;
		for ( const auto& it : items ){
		  string art_id = TiCC::getAttribute( it, "article_id" );
		  if ( art_id.empty() ){
#pragma omp critical
		    {
		      cerr << "no article ID in " << TiCC::getAttribute( it, "identifier" )  << endl;
		    }
		    succes = false;
		  }
		  else {
		    article_names.insert( art_id );
		  }
		}
		for ( const auto& art : article_names ){
		  string subject;
		  list<xmlNode*> meta = TiCC::FindNodes( didl, "//didl:Item/didl:Component[@dc:identifier='" + art + ":metadata']" );
		  if ( meta.size() == 1 ){
		    list<xmlNode*> subs = TiCC::FindNodes( meta.front(), "didl:Resource//srw_dc:dcx/dc:subject" );
		    if ( subs.size() == 1 ){
		      subject = TiCC::XmlContent(subs.front());
		    }
		    else {
#pragma omp critical
		      {
			cerr << "problems with dc:subject in " << TiCC::getAttribute( subs.front(), "identifier" ) << endl;
		      }
		      succes = false;
		    }
		  }
		  else {
#pragma omp critical
		    {
		      cerr << "problems with metadata in " << art << endl;
		    }
		    succes = false;
		  }

		  list<xmlNode*> comps = TiCC::FindNodes( didl, "//didl:Item/didl:Component[@dc:identifier='" + art + ":zoning']" );
		  if ( comps.size() == 1 ){
		    list<xmlNode*> zones = TiCC::FindNodes( comps.front(), "didl:Resource/dcx:zoning" );
		    if ( zones.size() == 1 ){
		      processZone( art,
				   subject,
				   zones.front(),
				   cache,
				   outDir,
				   inputType,
				   outputType,
				   command,
				   do_strings );
		    }
		    else {
#pragma omp critical
		      {
			cerr << "problems with zones in " << TiCC::getAttribute( comps.front(), "identifier" ) << endl;
		      }
		      succes = false;
		    }
		  }
		  else {
#pragma omp critical
		    {
		      cerr << "problems with Components in " << art << endl;
		    }
		    succes = false;
		  }
		}
	      }
	      if ( clearCachedFiles )
		clear_files( downloaded_files );
	    }
	  }
	}
      }
      else {
#pragma omp critical
	{
	  cerr << "no didl" << endl;
	}
	succes = false;
      }
    }
    else {
#pragma omp critical
      {
	cerr << "no metadata" << endl;
      }
      succes = false;
    }
    xmlFreeDoc( xmldoc );
  }
  else {
#pragma omp critical
    {
      cerr << "XML failed: " << file << endl;
    }
    succes = false;
  }
  if ( succes ){
#pragma omp critical
    {
      cout << "resolved " << file << endl;
    }
  }
  else {
#pragma omp critical
    {
      cout << "FAILED: " << file << endl;
    }
  }
}

void solveBook( const string& altoFile,
		const string& book_id,
		const string& urn,
		const string& outDir,
		zipType outputType,
		const string& command,
		bool do_strings ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "start handling " << altoFile << endl;
    }
  }
  zipType inputType;
  xmlDoc *xmldoc = getXml( altoFile, inputType );
  if ( xmldoc ){
    string docid = replaceColon( book_id, '.' );
    docid = prefix + docid;
    string outName = outDir + docid + ".folia.xml";
    folia::Document doc( "xml:id='" + docid + "'" );
    folia::processor *proc = add_provenance( doc, "FoLiA-alto", command );
    processor_id = proc->id();
    folia::KWargs proc_args;
    proc_args["processor"] = processor_id;
    if ( do_strings ){
      doc.declare( folia::AnnotationType::STRING, setname, proc_args );
    }
    else {
      doc.declare( folia::AnnotationType::TOKEN, setname, proc_args );
    }
    folia::KWargs text_args;
    text_args["xml:id"] = docid + ".text";
    folia::Text *text = doc.create_root<folia::Text>( text_args );
    xmlNode *root = xmlDocGetRootElement( xmldoc );
    list<xmlNode*> textblocks = TiCC::FindNodes( root, "//*:TextBlock" );
    if ( textblocks.empty() ){
#pragma omp critical
      {
	cerr << "found no textblocks in " << altoFile << endl;
      }
      xmlFreeDoc( xmldoc );
      return;
    }
    xmlNode *keepPart1 = 0;
    set<string> ids;
    for ( auto const& block : textblocks ){
      string id = TiCC::getAttribute( block, "ID" );
      if ( ids.find(id) != ids.end() ){
	if ( verbose ){
#pragma omp critical
	  {
	    cout << "skip duplicate ID " << id << endl;
	  }
	}
	continue;
      }
      else {
	ids.insert(id);
      }
      folia::KWargs p_args;
      p_args["processor"] = processor_id;
      doc.declare( folia::AnnotationType::PARAGRAPH, setname, p_args );
      p_args["xml:id"] = text->id() + ".p." + id;
      folia::Paragraph *p = text->add_child<folia::Paragraph>( p_args );
      UnicodeString ocr_text;
      list<xmlNode*> v =
	TiCC::FindNodes( root,
			 "//*[local-name()='TextBlock' and @ID='"
			 + id + "']" );
      if ( v.size() == 1 ){
	xmlNode *node = v.front();
	list<xmlNode*> lv = TiCC::FindNodes( node, "*[local-name()='TextLine']" );
	for ( const auto *line : lv ){
	  xmlNode *pnt = line->children;
	  while ( pnt ){
	    if ( pnt->type == XML_ELEMENT_NODE ){
	      if ( TiCC::Name(pnt) == "String" ){
		string sub_t = TiCC::getAttribute( pnt, "SUBS_TYPE" );
		if ( sub_t == "HypPart2" ){
		  if ( keepPart1 == 0 ){
		    addStr( p, ocr_text, pnt, urn, do_strings );
		  }
		  else {
		    folia::KWargs atts = folia::getAttributes( keepPart1 );
		    string kid = atts["ID"];
		    string sub_c = atts["SUBS_CONTENT"];
		    UnicodeString subc = TiCC::UnicodeFromUTF8( sub_c );
		    folia::KWargs args;
		    args["xml:id"] = p->id() + "." + kid;
		    args["class"] = classname;
		    folia::FoliaElement *s;
		    if ( do_strings ){
		      s = p->add_child<folia::String>( args );
		    }
		    else {
		      s = p->add_child<folia::Word>( args );
		    }
		    s->setutext( subc,
				 ocr_text.length(),
				 classname );
		    ocr_text += " " + subc;
		    args.clear();
		    args["processor"] = processor_id;
		    text->doc()->declare( folia::AnnotationType::RELATION,
		    			  setname, args );
		    args.clear();
		    args["xlink:href"] = urn;
		    folia::Relation *h = s->add_child<folia::Relation>( args );
		    args.clear();
		    args["id"] = kid;
		    args["type"] = "str";
		    h->add_child<folia::LinkReference>( args );
		    args["id"] = TiCC::getAttribute( pnt, "ID" );
		    h->add_child<folia::LinkReference>( args );
		    keepPart1 = 0;
		  }
		  pnt = pnt->next;
		  continue;
		}
		else if ( sub_t == "HypPart1" ){
		  // see if there is a Part2 in next line at this level.
		  const xmlNode *part2 = findPart2Level( line );
		  if ( part2 ){
		    keepPart1 = pnt;
		    break; //continue with next TextLine
		  }
		  else {
		    // no second part on this level. seek in the next TextBlock
		    part2 = findPart2Block( node );
		    if ( !part2 ){
		      // Ok. Just ignore this and take the CONTENT
		      addStr( p, ocr_text, pnt, urn, do_strings );
		    }
		    else {
		      keepPart1 = pnt;
		      break; //continue with next TextLine, but this should
		      // be empty, so in fact we go to the next block
		    }
		  }
		}
		else {
		  addStr( p, ocr_text, pnt, urn, do_strings );
		}
	      }
	    }
	    pnt = pnt->next;
	  }
	}
      }
      else if ( v.empty() ){
	// probably an CB node...
	if ( id.find("CB") != string::npos ){
	  // YES
	}
	else {
	  cerr << "found nothing, what is this? " << id << endl;
	}
      }
      else {
	cerr << "Confusing! " << endl;
      }
      if ( !ocr_text.isEmpty() )
	p->setutext( ocr_text.tempSubString(1), classname );
    }
    zipType type = inputType;
    if ( outputType != NORMAL )
      type = outputType;
    if ( type == BZ2 ){
      outName += ".bz2";
    }
    else if ( type == GZ ){
      outName += ".gz";
    }
    vector<folia::Paragraph*> pv = doc.paragraphs();
    if ( pv.size() == 0 ||
	 ( pv.size() == 1 && pv[0]->size() == 0 ) ){
      // no paragraphs, or just 1 without data
#pragma omp critical
      {
	cerr << "skipped empty result : " << outName << endl;
      }
    }
    else {
      doc.save( outName );
      if ( verbose ){
#pragma omp critical
	{
	  cout << "created: " << outName << endl;
	}
      }
    }
    xmlFreeDoc( xmldoc );
  }
  else {
    cerr << "unable to read " << altoFile << endl;
  }
}

void solveBookAlto( const string& alto_cache,
		    const string& file,
		    const string& outDir,
		    zipType outputType,
		    const string& command,
		    bool do_strings ){
  bool succes = true;
#pragma omp critical
  {
    cout << "resolving BOEK " << file << endl;
  }
  zipType inputType;
  xmlDoc *xmldoc = getXml( file, inputType );
  if ( xmldoc ){
    xmlNode *root = xmlDocGetRootElement( xmldoc );
    xmlNode *metadata = TiCC::xPath( root, "//*:metadata" );
    if ( metadata ){
      xmlNode *didl = TiCC::xPath( metadata, "//*[local-name()='DIDL']" );
      if ( didl ){
	list<xmlNode*> resources = TiCC::FindNodes( didl, "//didl:Component/didl:Resource[@mimeType='text/xml']" );
	if ( resources.empty() ){
#pragma omp critical
	  {
	    cout << "Unable to find usable text/xml resources in the DIDL: "
		 << file << endl;
	    succes = false;
	  }
	}
	else {
	  unordered_map<string,string> urns;
	  unordered_map<string,string> downloaded_files;
	  if ( download( alto_cache, resources, urns, downloaded_files ) ){
	    if ( downloaded_files.empty() ){
#pragma omp critical
	      {
		cerr << "unable to find downloadable files " <<  file << endl;
	      }
	      succes = false;
	    }
	    else {
	      cout << "Downloaded files " << endl;
	      list<xmlNode*> meta = TiCC::FindNodes( didl, "//didl:Item/didl:Component" );
	      for ( const auto& m : meta ){
		string id = TiCC::getAttribute( m, "identifier");
		if ( !id.empty() ){
		  const auto it = downloaded_files.find( id );
		  if ( it != downloaded_files.end() ){
		    solveBook( it->second, id, urns[id], outDir,
			       outputType,
			       command,
			       do_strings );
		  }
		}
	      }
	    }
	  }
	}
      }
      else {
#pragma omp critical
	{
	  cerr << "no didl" << endl;
	}
	succes = false;
      }
    }
    else {
#pragma omp critical
      {
	cerr << "no metadata" << endl;
      }
      succes = false;
    }
    xmlFreeDoc( xmldoc );
  }
  else {
#pragma omp critical
    {
      cerr << "XML failed: " << file << endl;
    }
    succes = false;
  }
  if ( succes ){
#pragma omp critical
    {
      cout << "resolved " << file << endl;
    }
  }
  else {
#pragma omp critical
    {
      cout << "FAILED: " << file << endl;
    }
  }
}

void solveDirectAlto( const string& full_file,
		      const string& outDir,
		      zipType outputType,
		      const string& command,
		      bool do_strings ){
#pragma omp critical
  {
    cout << "resolving direct on " << full_file << endl;
  }
  zipType type;
  xmlDoc *xmldoc = getXml( full_file, type );
  string filename = TiCC::basename( full_file );
  string docid = filename.substr( 0, filename.find(".") );
  docid = replaceColon(docid,'.');
  docid = prefix + docid;
  string outName = outDir + docid + ".folia.xml";
  list<xmlNode*> texts = TiCC::FindNodes( xmldoc, "//*:TextBlock" );
  folia::Document doc( "xml:id='" + docid + "'" );
  folia::processor *proc = add_provenance( doc, "FoLiA-alto", command );
  processor_id = proc->id();
  folia::KWargs args;
  args["processor"] = processor_id;
  if ( do_strings ){
    doc.declare( folia::AnnotationType::STRING, setname, args );
  }
  else {
    doc.declare( folia::AnnotationType::TOKEN, setname, args );
  }
  args.clear();
  args["xml:id"] =  docid + ".text";
  folia::Text *text = doc.create_root<folia::Text>( args );
  createFile( text, xmldoc, "", texts, do_strings );
  vector<folia::Paragraph*> pv = doc.paragraphs();
  if ( pv.size() == 0 ||
       ( pv.size() == 1 && pv[0]->size() == 0 ) ){
    // no paragraphs, or just 1 without data
#pragma omp critical
    {
      cerr << "skipped empty result : " << filename << endl;
    }
  }
  else {
    if ( outputType == BZ2 ){
      outName += ".bz2";
    }
    else if ( outputType == GZ ){
      outName += ".gz";
    }
    doc.save( outName );
    if ( verbose ){
#pragma omp critical
      {
	cout << "created: " << outName << endl;
      }
    }
  }
}

void usage(){
  cerr << "Usage: alto [options] file/dir" << endl;
  cerr << "\t--cache\t\t alto cache directory " << endl;
  cerr << "\t--clear\t\t clear cached Alto files at start" << endl;
  cerr << "\t--direct\t read alto files directly. (so NO Didl)" << endl;
  cerr << "\t-t <threads> or\n\t--threads <threads> Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t-h or --help\t this messages " << endl;
  cerr << "\t-O\t\t output directory " << endl;
  cerr << "\t--type\t\t Type of document ('krant' or 'boek' Default: 'krant')" << endl;
  cerr << "\t--prefix='pre'\t add this prefix to ALL created files. (default 'FA-') " << endl;
  cerr << "\t\t\t use 'none' for an empty prefix. (can be dangerous)" << endl;
  cerr << "\t--oldstrings\t Fall back to old version that creates <str> nodes" << endl;
  cerr << "\t\t\t The default is to create <w> nodes." << endl;
  cerr << "\t--setname=<set>\t the FoLiA setname of the string or word nodes. "
    "(default '" << setname << "')" << endl;
  cerr << "\t--class=<cls>\t the FoLiA class of the string nodes. "
    "(default '" << classname << "')" << endl;
  cerr << "\t--compress=<c>\t create zipped files." << endl;
  cerr << "\t\t\t 'c'=b creates bzip2 files (.bz2)" << endl;
  cerr << "\t\t\t 'c'=g creates gzip files (.gz)" << endl;
  cerr << "\t-v\t\t verbose output " << endl;
  cerr << "\t-V or --version\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts;
  try {
    opts.set_short_options( "vVt:O:h" );
    opts.set_long_options( "cache:,clear,class:,direct,setname:,compress:,"
			   "type:,help,prefix:,version,threads:,oldstrings" );
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
  string alto_cache = "/tmp/altocache/";
  bool do_direct;
  string outputDir;
  string kind = "krant";
  zipType outputType = NORMAL;
  opts.extract( "prefix", prefix );
  if ( prefix == "none" ){
    prefix.clear();
  }
  string value;
  if ( opts.extract('h') || opts.extract("help") ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('V') || opts.extract("version") ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  string orig_command = "FoLiA-alto " + opts.toString();
  verbose = opts.extract( 'v' );
  do_direct = opts.extract("direct");
  bool do_strings = opts.extract("oldstrings");
  if ( !do_direct ){
    clearCachedFiles = opts.extract( "clear" );
    opts.extract( "cache", alto_cache );
  }
  if ( opts.extract( "type", kind ) ){
    if ( kind != "krant" && kind != "boek" ){
      cerr << "unknown type: use 'krant' or 'boek' (default='krant')" << endl;
      exit(EXIT_FAILURE);
    }
  }
  if ( opts.extract( "compress", value ) ){
    if ( value == "b" )
      outputType = BZ2;
    else if ( value == "g" ){
      outputType = GZ;
    }
    else {
      cerr << "unknown compression: use 'b' or 'g'" << endl;
      exit( EXIT_FAILURE );
    }
  }
  if ( opts.extract('t', value )
       || opts.extract("threads", value ) ){
#ifdef HAVE_OPENMP
    int numThreads=1;
    if ( TiCC::lowercase(value) == "max" ){
      numThreads = omp_get_max_threads() - 2;
    }
    else if ( !TiCC::stringTo(value,numThreads) ) {
      cerr << "illegal value for -t (" << value << ")" << endl;
      exit( EXIT_FAILURE );
    }
    omp_set_num_threads( numThreads );
#else
    cerr << "-t option does not work, no OpenMP support in your compiler?" << endl;
    exit(EXIT_FAILURE);
#endif
  }
  opts.extract( "setname", setname );
  opts.extract( "class", classname );
  if ( !alto_cache.empty() && alto_cache[alto_cache.length()-1] != '/' )
    alto_cache += "/";
  opts.extract( 'O', outputDir );
  if ( !outputDir.empty() && outputDir[outputDir.length()-1] != '/' )
    outputDir += "/";
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  vector<string> fileNames = opts.getMassOpts();
  if ( fileNames.empty() ){
    if ( clearCachedFiles ){
      if ( clear_alto_files( alto_cache ) ){
	exit(EXIT_SUCCESS);
      }
      else {
	exit(EXIT_FAILURE);
      }
    }
    else {
      cerr << "missing input file(s)" << endl;
      usage();
      exit(EXIT_FAILURE);
    }
  }
  if ( !outputDir.empty() ){
    if ( !TiCC::isDir(outputDir) ){
      if ( !TiCC::createPath( outputDir ) ){
	cerr << "outputdir '" << outputDir
	     << "' doesn't exist and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  if ( !do_direct ){
    if ( kind == "krant" ){
      string name = outputDir + "artikel/";
      if ( !TiCC::isDir(name) ){
	if ( !TiCC::createPath( name ) ){
	  cerr << "outputdir '" << name
	       << "' doesn't exist and can't be created" << endl;
	  exit(EXIT_FAILURE);
	}
      }
      name = outputDir + "overige/";
      if ( !TiCC::isDir(name) ){
	if ( !TiCC::createPath( name ) ){
	  cerr << "outputdir '" << name
	       << "' doesn't exist and can't be created" << endl;
	  exit(EXIT_FAILURE);
	}
      }
    }
  }
  if ( fileNames.size() == 1 ){
    string name = fileNames[0];
    if ( !( TiCC::isFile(name) || TiCC::isDir(name) ) ){
      cerr << "'" << name << "' doesn't seem to be a file or directory"
	   << endl;
      exit(EXIT_FAILURE);
    }
    if ( !TiCC::isFile(name) ){
      if ( do_direct ){
	fileNames = TiCC::searchFilesMatch( name, "*.xml" );
      }
      else {
	fileNames = TiCC::searchFilesMatch( name, "*:mpeg21.xml*" );
      }
    }
  }
  else if ( !do_direct ) {
    // sanity check, not for direct reading
    auto it = fileNames.begin();
    while ( it != fileNames.end() ){
      if ( it->find( ":mpeg21.xml" ) == string::npos ){
	if ( verbose ){
	  cerr << "skipping non didl file: " << *it << endl;
	}
	it = fileNames.erase(it);
      }
      else {
	++it;
      }
    }
  }
  size_t toDo = fileNames.size();
  cout << "start processing of " << toDo << " files " << endl;

  int fail_count = 0;
#pragma omp parallel for shared(fileNames,fail_count) schedule(dynamic)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    if ( do_direct ){
      try {
	solveDirectAlto( fileNames[fn], outputDir, outputType, orig_command,
			 do_strings );
      }
      catch ( const exception& e ){
	cerr << fileNames[fn] << " failed: " << e.what() << endl;
	if ( ++fail_count > 5 ){
	  cerr << "more then 5 failures. Terminated" << endl;
	  exit(EXIT_FAILURE);
	}
      }
    }
    else {
      if ( kind == "krant" )
	try {
	  solveArtAlto( alto_cache, fileNames[fn], outputDir, outputType,
			orig_command, do_strings );
	}
	catch ( const exception& e ){
	  cerr << fileNames[fn] << " failed: " << e.what() << endl;
	  if ( ++fail_count > 5 ){
	    cerr << "more then 5 failures. Terminated" << endl;
	    exit(EXIT_FAILURE);
	  }
	}
      else {
	try {
	  solveBookAlto( alto_cache, fileNames[fn], outputDir, outputType,
			 orig_command, do_strings );
	}
	catch ( const exception& e ){
	  cerr << fileNames[fn] << " failed: " << e.what() << endl;
	  if ( ++fail_count > 5 ){
	    cerr << "more then 5 failures. Terminated" << endl;
	    exit(EXIT_FAILURE);
	  }
	}
      }
    }
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
