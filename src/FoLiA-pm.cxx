/*
  Copyright (c) 2014 - 2016
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
#include <list>
#include <map>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/zipper.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;

bool verbose = false;

KWargs getAllAttributes( const xmlNode *node ){
  KWargs atts;
  if ( node ){
    xmlAttr *a = node->properties;
    while ( a ){
      atts[(char*)a->name] = (char *)a->children->content;
      a = a->next;
    }
  }
  return atts;
}

void process_stage( Division *, xmlNode * );

string extract_embedded( xmlNode *p ){
  string result;
  if ( p == 0 ){
    return result;
  }
  if ( verbose ){
#pragma omp critical
    {
      cerr << "extract embedded: " << TiCC::Name(p) << endl;
    }
  }
  p = p->children;
  while ( p ){
    if ( verbose ){
#pragma omp critical
      {
	cerr << "examine: " << TiCC::Name(p) << endl;
      }
    }
    string content = TiCC::XmlContent(p);
    if ( !content.empty() ){
      if ( verbose ){
#pragma omp critical
	{
	  cerr << "embedded add: " << content << endl;
	}
      }
      result += content;
    }
    else {
      string part = extract_embedded( p->children );
      result += part;
    }
    p = p->next;
  }
  return result;
}

void add_par( Division *root, xmlNode *p ){
  string id = TiCC::getAttribute( p, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "add_par: id=" << id << endl;
    }
  }
  KWargs args;
  args["id"] = id;
  Paragraph *par = new Paragraph( args, root->doc() );
  TextContent *tc = new TextContent();
  par->append( tc );
  p = p->children;
  bool first = true;
  while ( p ){
    if ( p->type == XML_TEXT_NODE ){
      xmlChar *tmp = xmlNodeGetContent( p );
      if ( tmp ){
	string part = std::string( (char *)tmp );
	XmlText *txt = new XmlText();
	txt->setvalue( part );
	tc->append( txt );
	xmlFree( tmp );
	first = false;
      }
    }
    else if ( p->type == XML_ELEMENT_NODE ){
      string tag = TiCC::Name( p );
      if ( tag == "tagged" ){
	if ( TiCC::getAttribute( p, "type" ) == "reference" ) {
	  if ( verbose ){
#pragma omp critical
	    {
	      cerr << "add_par: REFERENCE" << endl;
	    }
	  }
	  string text_part;
	  string ref;
	  string type;
	  string sub_type;
	  string status;
	  xmlNode *t = p->children;
	  while ( t ){
	    if ( t->type == XML_TEXT_NODE ){
	      xmlChar *tmp = xmlNodeGetContent( t );
	      if ( tmp ){
		text_part = std::string( (char *)tmp );
		xmlFree( tmp );
	      }
	    }
	    else if ( TiCC::Name(t) == "tagged-entity" ){
	      ref = TiCC::getAttribute( t, "reference" );
	      type = TiCC::getAttribute( t, "type" );
	      sub_type = TiCC::getAttribute( t, "sub-type" );
	      status = TiCC::getAttribute( t, "status" );
	    }
	    else {
#pragma omp critical
	      {
		cerr << "tagged" << id << ", unhandled: "
		     << TiCC::Name(t) << endl;
	      }
	    }
	    t = t->next;
	  }
	  if ( !ref.empty()
	       && type == "reference" ){
	    KWargs args;
	    args["href"] = ref;
	    args["type"] = "locator";
	    if ( !sub_type.empty() ){
	      args["role"] = sub_type;
	    }
	    if ( !status.empty() ){
	      args["label"] = status;
	    }
	    if ( !text_part.empty() ){
	     args["text"] = text_part;
	    }
	    TextMarkupString *tm = new TextMarkupString( args );
	    // args.clear();
	    // args["subset"] = "sub-type";
	    // args["class"] = sub_type;
	    // Feature *feat = new Feature( args );
	    // tm->append(feat);
	    tc->append( tm );
	    first = false;
	  }
	  else {
#pragma omp critical
	    {
	      cerr << "tagged: " << id << ", unhandled type=" << type << endl
		   << " sub_type=" << sub_type << " ref=" << ref << endl;
	    }
	  }
	}
      }
      else if ( tag == "stage-direction" ){
	string embedded = extract_embedded( p );
	string id = TiCC::getAttribute( p, "id" );
	if ( embedded.empty() ){
#pragma omp critical
	  {
	    cerr << "paragraph:stage-direction: " << id << " without text?"
		 << endl;
	  }
	}
	else {
	  XmlText *txt = new XmlText();
	  if ( !first  ){
	    embedded = " " + embedded;
	  }
	  if ( p->next != 0 ){
	    // not last
	    embedded += " ";
	  }
	  txt->setvalue( embedded );
	  tc->append( txt );
	}
      }
      else {
#pragma omp critical
	{
	  cerr << "paragraph: " << id << ", unhandled tag : " << tag << endl;
	}
      }
    }
    p = p->next;
  }
  root->append( par );
}

void process_chair( Division *root, xmlNode *chair ){
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_chair" << endl;
    }
  }
  xmlNode *p = chair->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "p" ){
      add_par( root, p );
    }
    else if ( label == "chair" ){
      string speaker = TiCC::getAttribute( p, "speaker" );
      string member = TiCC::getAttribute( p, "member-ref" );
      KWargs args;
      args["subset"] = "speaker";
      args["class"] = speaker;
      Feature *feat = new Feature( args );
      root->append( feat );
      args["subset"] = "member-ref";
      args["class"] = member;
      feat = new Feature( args );
      root->append( feat );
    }
    else {
#pragma omp critical
      {
	cerr << "chair, unhandled: " << label << endl;
      }
    }
    p = p->next;
  }
}

void process_speech( Division *root, xmlNode *speech ){
  KWargs atts = getAllAttributes( speech );
  string id = atts["id"];
  if ( verbose ){
#pragma omp critical
    {
      using TiCC::operator<<;
      cerr << "process_speech: id=" << atts << endl;
    }
  }
  string type = atts["type"];
  KWargs args;
  args["id"] = id;
  args["class"] = type;
  Division *div = new Division( args, root->doc() );
  root->append( div );
  for ( const auto& att : atts ){
    if ( att.first == "id"
	 || att.first == "type" ){
      continue;
    }
    else if ( att.first == "speaker"
	      || att.first == "function"
	      || att.first == "party"
	      || att.first == "role"
	      || att.first == "party-ref"
	      || att.first == "member-ref" ){
      KWargs args;
      args["subset"] = att.first;
      args["class"] = att.second;
      Feature *feat = new Feature( args );
      div->append( feat );
    }
    else {
#pragma omp critical
      {
	cerr << "unsupported attribute: " << att.first << " on speech: "
	     << id << endl;
      }
    }
  }

  xmlNode *p = speech->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "p" ){
      add_par( div, p );
    }
    else if ( label == "stage-direction" ){
      process_stage( div, p );
    }
    else {
#pragma omp critical
      {
	cerr << "speech: " << id << ", unhandled: " << label << endl;
      }
    }
    p = p->next;
  }
}

void process_scene( Division *root, xmlNode *scene ){
  KWargs atts = getAllAttributes( scene );
  string id = atts["id"];
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_scene: id=" << id << endl;
    }
  }
  string type = atts["type"];
  KWargs args;
  args["id"] = id;
  args["class"] = type;
  Division *div = new Division( args, root->doc() );
  root->append( div );
  for ( const auto& att : atts ){
    if ( att.first == "id"
	 || att.first == "type" ){
      continue;
    }
    else if ( att.first == "speaker"
	      || att.first == "function"
	      || att.first == "party"
	      || att.first == "role"
	      || att.first == "party-ref"
	      || att.first == "member-ref" ){
      KWargs args;
      args["subset"] = att.first;
      args["class"] = att.second;
      Feature *feat = new Feature( args );
      div->append( feat );
    }
    else {
#pragma omp critical
      {
	cerr << "unsupported attribute: " << att.first << " on scene:"
	     << id << endl;
      }
    }
  }

  xmlNode *p = scene->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "speech" ){
      process_speech( div, p );
    }
    else if ( label == "stage-direction" ){
      process_stage( div, p );
    }
    else {
#pragma omp critical
      {
	cerr << "scene: " << id << ", unhandled: " << label << endl;
      }
    }
    p = p->next;
  }
}

void process_break( Division *root, xmlNode *brk ){
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_break" << endl;
    }
  }
  KWargs args;
  args["pagenr"] = TiCC::getAttribute( brk, "originalpagenr");
  args["newpage"] = "yes";
  Linebreak *pb = new Linebreak( args );
  root->append( pb );
  args.clear();
  args["class"] = "page";
  args["format"] = "image/jpeg";
  args["href"] = TiCC::getAttribute( brk, "source");
  Alignment *align = new Alignment( args, root->doc() );
  pb->append( align );
}

void process_members( Division *root, xmlNode *members ){
  ForeignData *fd = new ForeignData();
  root->append( fd );
  fd->set_data( members );
}

void process_stage( Division *root, xmlNode *_stage ){
  KWargs args;
  string id = TiCC::getAttribute( _stage, "id" );
  string type = TiCC::getAttribute( _stage, "type" );
  args["id"] = id;
  if ( type.empty() ){
    args["class"] = "stage-direction";
  }
  else {
    args["class"] = type;
  }
  Division *div = new Division( args, root->doc() );
  root->append( div );
  xmlNode *stage = _stage->children;
  while ( stage ){
    string id = TiCC::getAttribute( stage, "id" );
    string type = TiCC::getAttribute( stage, "type" );
    string label = TiCC::Name( stage );
    if ( type == "chair" || label == "chair" ){
      process_chair( div, stage );
    }
    else if ( type == "pagebreak" ){
      process_break( div, stage->children );
    }
    else if ( type == "header"
	      || type == "footer"
	      || type == "subject" ){
      KWargs args;
      args["id"] = id;
      args["class"] = type;
      Division *div1 = new Division( args, root->doc() );
      div->append( div1 );
      xmlNode *p = stage->children;
      while ( p ){
	string label = TiCC::Name(p);
	if ( label == "p" ){
	  add_par( div1, p );
	}
	else {
#pragma omp critical
	  {
	    cerr << "stage-direction: " << id << ", unhandled :" << label
		 << " type=" << type << endl;
	  }
	}
	p = p->next;
      }
    }
    else if ( type == "speech" ){
      process_speech( div, stage->children );
    }
    else if ( type == "" ){ //nested or?
      if ( label == "stage-direction" ){
	process_stage( div, stage );
      }
      else if ( label == "p" ){
	add_par( div, stage );
      }
      else if ( label == "pagebreak" ){
	process_break( div, stage );
      }
      else if ( label == "speech" ){
	process_speech( div, stage );
      }
      else if ( label == "members" ){
	process_members( div, stage );
      }
      else {
#pragma omp critical
	{
	  cerr << "stage-direction: " << id << ", unhandled nested: "
	       << label << endl;
	}
      }
    }
    else {
#pragma omp critical
      {
	cerr << "stage-direction: " << id << ", unhandled type: "
	     << type << endl;
      }
    }
    stage = stage->next;
  }
}

void process_topic( Division *root, xmlNode *topic ){
  string id = TiCC::getAttribute( topic, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_topic: id=" << id << endl;
    }
  }
  KWargs args;
  args["id"] = id;
  args["class"] = "topic";
  Division *div = new Division( args, root->doc() );
  root->append( div );
  string title = TiCC::getAttribute( topic, "title" );
  if ( !title.empty() ){
    Head *hd = new Head( );
    hd->settext( title );
    div->append( hd );
  }
  xmlNode *p = topic->children;
  while ( p ){
    string tag = TiCC::Name(p);
    if ( tag == "stage-direction" ){
      process_stage( div, p );
    }
    else if ( tag == "speech" ){
      process_speech( div, p );
    }
    else if ( tag == "scene" ){
      process_scene( div, p );
    }
    else {
#pragma omp critical
      {
	cerr << "topic: " << id << ", unhandled: " << tag << endl;
      }
    }
    p = p->next;
  }
}

void process_proceeding( Text *root, xmlNode *proceed ){
  string id = TiCC::getAttribute( proceed, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_proceeding: id=" << id << endl;
    }
  }
  KWargs args;
  args["id"] = id;
  args["class"] = "proceedings";
  Division *div = new Division( args, root->doc() );
  root->append( div );
  list<xmlNode*> topics = TiCC::FindNodes( proceed, "*:topic" );
  for ( const auto& topic : topics ){
    process_topic( div, topic );
  }
}

void convert_to_folia( const string& file,
		       const string& outDir ){
  bool succes = true;
#pragma omp critical
  {
    cout << "converting: " << file << endl;
  }
  xmlDoc *xmldoc = xmlReadFile( file.c_str(),
				0,
				XML_PARSE_NOBLANKS|XML_PARSE_HUGE );
  if ( xmldoc ){
    string base = TiCC::basename( file );
    string docid = base;
    Document doc( "id='" + docid + "'" );
    doc.declare( folia::AnnotationType::DIVISION, "polmash", "annotator='FoLiA-pm', annotatortype='auto', datetime='now()'" );
    doc.declare( folia::AnnotationType::ALIGNMENT, "polmash", "annotator='FoLiA-pm', annotatortype='auto', datetime='now()'" );
    xmlNode *root = xmlDocGetRootElement( xmldoc );
    xmlNode *metadata = TiCC::xPath( root, "//meta" );
    if ( metadata ){
      doc.set_foreign_metadata( metadata );
      // xmlNode *p = metadata;
      // while ( p ){
      // 	cerr << "Node: " << TiCC::Name( p ) << endl;
      // 	std::map<std::string,std::string> ns = TiCC::getDefinedNS( p );
      // 	using TiCC::operator<<;
      // 	cerr << ns << endl;
      // 	p = p->next;
      // }
    }
    else {
#pragma omp critical
      {
	cerr << "no metadata" << endl;
      }
      succes = false;
    }
    try {
      Text *text = new Text( getArgs( "id='" + docid + ".text'"  ));
      doc.append( text );
      xmlNode *p = metadata->next;
      while ( p ){
	if ( TiCC::Name( p ) == "proceedings" ){
	  process_proceeding( text, p );
	}
	p = p->next;
      }
      string outname = outDir+docid;
      string::size_type pos = outname.rfind( ".xml" );
      if ( pos != string::npos ){
	outname = outname.substr(0,pos);
      }
      outname += ".folia.xml";
#pragma omp critical
      {
	cout << "save " << outname << endl;
      }
      doc.save( outname );
    }
    catch ( const exception& e ){
#pragma omp critical
      {
	cerr << "error processing " << file << endl
	     << e.what() << endl;
	succes = false;
      }
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
  if ( !succes ){
#pragma omp critical
    {
      cerr << "FAILED: " << file << endl;
    }
  }
}


void usage(){
  cerr << "Usage: FoLiA-pm [options] file/dir" << endl;
  cerr << "\t convert Political Mashup XML files to FoLiA" << endl;
  cerr << "\t-t\t number_of_threads" << endl;
  cerr << "\t-h\t this messages " << endl;
  cerr << "\t-O\t output directory " << endl;
  cerr << "\t-v\t verbose output " << endl;
  cerr << "\t-V\t show version " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts;
  try {
    opts.set_short_options( "vVt:O:h" );
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
  int numThreads=1;
  string outputDir;
  if ( opts.extract('h' ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('V' ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  verbose = opts.extract( 'v' );
  string value;
  if ( opts.extract( 't', value ) ){
    numThreads = TiCC::stringTo<int>( value );
  }
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
    cerr << "missing input file(s)" << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  string dirName;
  if ( !outputDir.empty() ){
    if ( !TiCC::isDir(outputDir) ){
      if ( !TiCC::createPath( outputDir ) ){
	cerr << "outputdir '" << outputDir
	     << "' doesn't exist and can't be created" << endl;
	exit(EXIT_FAILURE);
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
    if ( TiCC::isFile(name) ){
      string::size_type pos = name.rfind( "/" );
      if ( pos != string::npos )
	dirName = name.substr(0,pos);
    }
    else {
      fileNames = TiCC::searchFilesMatch( name, "*.xml" );
    }
  }
  else {
    // sanity check
    vector<string>::iterator it = fileNames.begin();
    while ( it != fileNames.end() ){
      if ( it->find( ".xml" ) == string::npos ){
	if ( verbose ){
	  cerr << "skipping file: " << *it << endl;
	}
	it = fileNames.erase(it);
      }
      else
	++it;
    }
  }
  size_t toDo = fileNames.size();
  cout << "start processing of " << toDo << " files " << endl;
  if ( numThreads >= 1 ){
#ifdef HAVE_OPENMP
    omp_set_num_threads( numThreads );
#else
    numThreads = 1;
#endif
  }

#pragma omp parallel for shared(fileNames)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    convert_to_folia( fileNames[fn], outputDir );
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
