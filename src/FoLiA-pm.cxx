/*
  Copyright (c) 2014 - 2020
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
#include <cassert>
#include "ticcutils/StringOps.h"
#include "libfolia/folia.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/zipper.h"
#include "ticcutils/CommandLine.h"
#include "ticcutils/FileUtils.h"
#include "foliautils/common_code.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;
using namespace	folia;
using TiCC::operator<<;

bool verbose = false;
string setname = "polmash";
string processor_id;

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

void add_reference( TextContent *tc, xmlNode *p ){
  if ( verbose ){
#pragma omp critical
    {
      cerr << "add_reference" << endl;
    }
  }
  string text_part;
  string ref;
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
      sub_type = TiCC::getAttribute( t, "sub-type" );
      status = TiCC::getAttribute( t, "status" );
    }
    else {
#pragma omp critical
      {
	cerr << "reference unhandled: "
	     << TiCC::Name(t) << endl;
      }
    }
    t = t->next;
  }
  if ( ref.empty() && !text_part.empty() ){
    ref = "unknown";
  }
  if ( !ref.empty() ){
    KWargs args;
    args["xlink:href"] = ref;
    args["xlink:type"] = "locator";
    if ( !sub_type.empty() ){
      args["xlink:role"] = sub_type;
    }
    if ( !status.empty() ){
      args["xlink:label"] = status;
    }
    if ( !text_part.empty() ){
      args["text"] = text_part;
    }
    TextMarkupString *tm = new TextMarkupString( args );
    tc->append( tm );
  }
}

void add_note( Note *root, xmlNode *p ){
  //  cerr << "add note: " << TiCC::getAttributes( p ) << endl;
  string id = TiCC::getAttribute( p, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "add_note: id=" << id << endl;
    }
  }
  KWargs args;
  args["processor"] = processor_id;
  root->doc()->declare( folia::AnnotationType::PARAGRAPH, setname, args );
  args.clear();
  args["xml:id"] = id;
  Paragraph *par = new Paragraph( args, root->doc() );
  root->append( par );
  TextContent *tc = new TextContent();
  par->append( tc );
  p = p->children;
  while ( p ){
    if ( p->type == XML_TEXT_NODE ){
      xmlChar *tmp = xmlNodeGetContent( p );
      if ( tmp ){
	string part = std::string( (char *)tmp );
	XmlText *txt = new XmlText();
	txt->setvalue( part );
	tc->append( txt );
	xmlFree( tmp );
      }
    }
    else if ( p->type == XML_ELEMENT_NODE ){
      string tag = TiCC::Name( p );
      if ( tag == "tagged" ){
	string tag_type = TiCC::getAttribute( p, "type" );
	if ( tag_type == "reference" ){
	  add_reference( tc, p );
	}
	else {
#pragma omp critical
	  {
	    cerr << "tagged: " << id << ", unhandled type=" << tag_type << endl;
	  }
	}
      }
      else {
#pragma omp critical
	{
	  cerr << "note: " << id << ", unhandled tag : " << tag << endl;
	}
      }
    }
    p = p->next;
  }
}

void add_entity( FoliaElement* root, xmlNode *p ){
  if ( verbose ){
#pragma omp critical
    {
      cerr << "add_entity "<< endl;
    }
  }
  string text_part;
  string mem_ref;
  string part_ref;
  string id = TiCC::getAttribute( p, "id" );
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
      mem_ref = TiCC::getAttribute( t, "member-ref" );
      part_ref = TiCC::getAttribute( t, "party-ref" );
    }
    else {
#pragma omp critical
      {
	cerr << "entity" << id << ", unhandled: " << TiCC::Name(t) << endl;
      }
    }
    t = t->next;
  }
  KWargs args;
  args["processor"] =  processor_id;
  root->doc()->declare( folia::AnnotationType::ENTITY,
			setname, args );
  args.clear();
  EntitiesLayer *el = new EntitiesLayer();
  root->append( el );
  args["class"] = "member";
  Entity *ent = new Entity( args, root->doc() );
  el->append(ent);
  args.clear();
  args["subset"] = "member-ref";
  if ( !mem_ref.empty() ){
    args["class"] = mem_ref;
  }
  else {
    args["class"] = "unknown";
  }
  Feature *f = new Feature( args );
  ent->append( f );
  args.clear();
  args["subset"] = "party-ref";
  if ( !part_ref.empty() ){
    args["class"] = part_ref;
  }
  else {
    args["class"] = "unknown";
  }
  f = new Feature( args );
  ent->append( f );
  args.clear();
  args["subset"] = "name";
  if ( !text_part.empty() ){
    args["class"] = text_part;
  }
  else {
    args["class"] = "unknown";
  }
  f = new Feature( args );
  ent->append( f );
}

void add_stage_direction( TextContent *tc, xmlNode *p ){
  string embedded = extract_embedded( p );
  string id = TiCC::getAttribute( p, "id" );
  if ( embedded.empty() ){
#pragma omp critical
    {
      cerr << "stage-direction: " << id << " without text?"
	   << endl;
    }
  }
  else {
    XmlText *txt = new XmlText();
    if ( p->next != 0 ){
      // not last
      embedded += " ";
    }
    txt->setvalue( embedded );
    tc->append( txt );
  }
}

void add_notes( FoliaElement *par, const list<Note*>& notes ){
  KWargs args;
  args["xml:id"] = par->id() + ".d.1";
  args["class"] = "notes";
  Division *div = new Division( args, par->doc() );
  par->parent()->append( div );
  for ( const auto& note : notes ){
    div->append( note );
  }
}

Paragraph *add_par( Division *root, xmlNode *p, list<Note*>& notes ){
  string id = TiCC::getAttribute( p, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "add_par: id=" << id << endl;
    }
  }
  folia::Document *doc = root->doc();
  KWargs args;
  args["processor"] = processor_id;
  doc->declare( folia::AnnotationType::PARAGRAPH, setname, args );
  args.clear();
  args["xml:id"] = id;
  Paragraph *par = new Paragraph( args, root->doc() );
  TextContent *tc = new TextContent();
  par->append( tc );
  root->append( par );
  p = p->children;
  while ( p ){
    if ( p->type == XML_TEXT_NODE ){
      xmlChar *tmp = xmlNodeGetContent( p );
      if ( tmp ){
	string part = std::string( (char *)tmp );
	XmlText *txt = new XmlText();
	txt->setvalue( part );
	tc->append( txt );
	xmlFree( tmp );
      }
    }
    else if ( p->type == XML_ELEMENT_NODE ){
      string tag = TiCC::Name( p );
      if ( tag == "tagged" ){
	string tag_type = TiCC::getAttribute( p, "type" );
	if ( tag_type == "reference" ){
	  add_reference( tc, p );
	}
	else if ( tag_type == "named-entity" ) {
	  add_entity( root, p );
	}
      }
      else if ( tag == "stage-direction" ){
	add_stage_direction( tc, p );
      }
      else if ( tag == "note" ){
	KWargs args;
	string id = TiCC::getAttribute( p, "id" );
	string ref = TiCC::getAttribute( p, "ref" );
	string number;
	string text;
	xmlNode *pp = p->children;
	while ( pp ){
	  if ( number.empty() ){
	    number = TiCC::XmlContent( pp );
	  }
	  else {
	    text = TiCC::XmlContent( pp );
	  }
	  pp=pp->next;
	}
	if ( number.empty() || text.empty() ){
#pragma omp critical
	  {
	    cerr << "problem with note id= " << id
		 << " No index or text found? " << endl;
	  }
	}
	else {
	  folia::XmlText *t = new folia::XmlText();
	  t->setvalue( " " );
	  tc->append( t );
	  KWargs args;
	  args["xml:id"] = id;
	  args["id"] = ref;
	  args["type"] = "note";
	  args["text"] = number;
	  TextMarkupReference *txt = new TextMarkupReference( args );
	  tc->append( txt );
	  t = new folia::XmlText();
	  t->setvalue( " " );
	  tc->append( t );
	}
	if ( verbose ){
#pragma omp critical
	  {
	    cerr << "paragraph:note: " << id << ", ref= " << ref << endl;
	  }
	}
	Note *note = 0;
	if ( ref.empty() ){
	  args["xml:id"] = id;
	  note = new Note( args, doc );
	}
	else {
	  if ( !isNCName( ref ) ){
	    ref = "v." + ref;
	    if ( !isNCName( ref ) ){
	      throw ( "the ref attribute in note cannot be converted to an ID" );
	    }
	  }
	  args.clear();
	  args["xml:id"] = ref;
	  note = new Note( args, doc );
	}
	notes.push_back( note );
	xmlNode *pnt = p->children;
	while ( pnt ){
	  string tag = TiCC::Name(pnt);
	  if ( tag == "p" ){
	    add_note( note, pnt );
	  }
	  else {
#pragma omp critical
	    {
	      cerr << "note: " << id << ", unhandled tag : " << tag << endl;
	    }
	  }
	  pnt = pnt->next;
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
  return par;
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
      list<Note*> notes;
      Paragraph *par = add_par( root, p, notes );
      if ( !notes.empty() ){
	add_notes( par, notes );
      }
    }
    else if ( label == "chair" ){
      string speaker = TiCC::getAttribute( p, "speaker" );
      if ( speaker.empty() ){
	speaker = "unknown";
      }
      string member = TiCC::getAttribute( p, "member-ref" );
      if ( member.empty() ){
	member = "unknown";
      }
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

void add_entity( EntitiesLayer *root, xmlNode *p ){
  string id = TiCC::getAttribute( p, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "add_entity: id=" << id << endl;
    }
  }
  KWargs args;
  p = p->children;
  while ( p ){
    string tag = TiCC::Name( p );
    if ( tag == "tagged" ){
      string tag_type = TiCC::getAttribute( p, "type" );
      if ( tag_type == "named-entity" ) {
	if ( verbose ){
#pragma omp critical
	  {
	    cerr << "add_entity: " << tag_type << endl;
	  }
	}
	string text_part;
	string mem_ref;
	string part_ref;
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
	    mem_ref = TiCC::getAttribute( t, "member-ref" );
	    part_ref = TiCC::getAttribute( t, "party-ref" );
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
	args.clear();
	args["class"] = "member";
	args["xml:id"] = id;
	Entity *ent = new Entity( args, root->doc() );
	root->append(ent);
	args.clear();
	args["subset"] = "member-ref";
	if ( !mem_ref.empty() ){
	  args["class"] = mem_ref;
	}
	else {
	  args["class"] = "unknown";
	}
	Feature *f = new Feature( args );
	ent->append( f );
	args.clear();
	args["subset"] = "party-ref";
	if ( !part_ref.empty() ){
	  args["class"] = part_ref;
	}
	else {
	  args["class"] = "unknown";
	}
	f = new Feature( args );
	ent->append( f );
	args.clear();
	args["subset"] = "name";
	if ( !text_part.empty() ){
	  args["class"] = text_part;
	}
	else {
	  args["class"] = "unknown";
	}
	f = new Feature( args );
	ent->append( f );
      }
    }
    else {
#pragma omp critical
      {
	cerr << "add_entity: " << id << ", unhandled tag : " << tag << endl;
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
      cerr << "process_speech: atts" << atts << endl;
    }
  }
  string type = atts["type"];
  KWargs args;
  args["xml:id"] = id;
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
      string cls =  att.second;
      if ( cls.empty() ){
	cls = "unknown";
      }
      args["class"] = cls;
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
      list<Note*> notes;
      Paragraph *par = add_par( div, p, notes );
      if ( !notes.empty() ){
	add_notes( par, notes );
      }
    }
    else if ( label == "stage-direction" ){
      process_stage( div, p );
    }
    else if ( label == "speech" ){
      process_speech( div, p );
    }
    else {
#pragma omp critical
      {
	cerr << "speech: " << id << ", unhandled: " << label << endl;
	cerr << "speech-content " << TiCC::XmlContent(p) << endl;
      }
    }
    p = p->next;
  }
}


void add_actor( FoliaElement *root, xmlNode *act ){
  Document *doc = root->doc();
  KWargs args;
  args["class"] = "actor";
  Division *div = new Division( args, doc );
  root->append( div );
  xmlNode *p = act->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "person" ){
      string speaker = TiCC::getAttribute( p, "speaker" );
      if ( speaker.empty() ){
	speaker = "unknown";
      }
      string ref = TiCC::getAttribute( p, "member-ref" );
      if ( ref.empty() ){
	ref = "unknown";
      }
      KWargs args;
      args["subset"] = "speaker";
      args["class"] = speaker;
      Feature *feat = new Feature( args );
      div->append( feat );
      args.clear();
      args["subset"] = "member-ref";
      args["class"] = ref;
      feat = new Feature( args );
      div->append( feat );
    }
    else if ( label == "organization" ){
      string name = TiCC::getAttribute( p, "name" );
      if ( name.empty() ){
	name = "unknown";
      }
      string function = TiCC::getAttribute( p, "function" );
      if ( function.empty() ){
	function = "unknown";
      }
      string ref = TiCC::getAttribute( p, "ref" );
      KWargs args;
      args["subset"] = "name";
      args["class"] = name;
      Feature *feat = new Feature( args );
      div->append( feat );
      args.clear();
      args["subset"] = "function";
      args["class"] = function;
      feat = new Feature( args );
      div->append( feat );
      if ( !ref.empty() ){
	args.clear();
	args["subset"] = "ref";
	args["class"] = ref;
	feat = new Feature( args );
	div->append( feat );
      }
    }
    else {
#pragma omp critical
      {
	cerr << "add actor, unhandled :" << label << endl;
      }
    }
    p = p->next;
  }
}


void add_submit( FoliaElement *root, xmlNode *sm ){
  Document *doc = root->doc();
  KWargs args;
  args["class"] = "submitted-by";
  Division *div = new Division( args, doc );
  root->append( div );
  xmlNode *p = sm->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "actor" ){
      add_actor( div, p );
    }
    else {
#pragma omp critical
      {
	cerr << "add sumitted, unhandled :" << label << endl;
      }
    }
    p = p->next;
  }
}

void add_information( FoliaElement *root, xmlNode *info ){
  Document *doc = root->doc();
  KWargs args;
  args["class"] = "information";
  Division *div = new Division( args, doc );
  root->append( div );
  xmlNode *p = info->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "dossiernummer"
	 || label == "ondernummer"
	 || label == "rijkswetnummer"
	 || label == "introduction"
	 || label == "part"
	 || label == "outcome" ){
      KWargs args;
      args["subset"] = label;
      string cls = TiCC::XmlContent( p );
      if ( cls.empty() ){
	cls = "unknown";
      }
      args["class"] = cls;
      Feature *f = new Feature( args );
      div->append( f );
    }
    else if ( label == "submitted-by" ){
      add_submit( div, p );
    }
    else {
#pragma omp critical
      {
	cerr << "add information, unhandled :" << label << endl;
      }
    }
    p = p->next;
  }
}

void add_about( Division *root, xmlNode *p ){
  KWargs atts = getAllAttributes( p );
  if ( verbose ){
#pragma omp critical
    {
      using TiCC::operator<<;
      cerr << "add_vote, atts=" << atts << endl;
    }
  }
  string title = atts["title"];
  string voted_on = atts["voted_on"];
  string ref =  atts["ref"];
  KWargs args;
  args["class"] = "about";
  Division *div = new Division( args, root->doc() );
  root->append( div );
  if ( !title.empty() ){
    args.clear();
    args["class"] = title;
    args["subset"] = "title";
    Feature *f = new Feature( args );
    div->append( f );
  }
  if ( !voted_on.empty() ){
    args.clear();
    args["class"] = voted_on;
    args["subset"] = "voted-on";
    Feature *f = new Feature( args );
    div->append( f );
  }
  if ( !ref.empty() ){
    args.clear();
    args["class"] = ref;
    args["subset"] = "ref";
    Feature *f = new Feature( args );
    div->append( f );
  }
  p = p->children;
  while ( p ){
    string tag = TiCC::Name( p );
    if ( tag == "information" ){
      add_information( div, p );
    }
    else {
#pragma omp critical
      {
	cerr << "about vote, unhandled: " << tag << endl;
      }
    }
    p = p->next;
  }
}

void process_vote( Division *div, xmlNode *vote ){
  KWargs atts = getAllAttributes( vote );
  string vote_type = atts["vote-type"];
  if ( vote_type.empty() ){
    vote_type = "unknown";
  }
  string outcome = atts["outcome"];
  if ( outcome.empty() ){
    outcome = "unknown";
  }
  string id = atts["id"];
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_vote: id = " << id << endl;
    }
  }
  KWargs args;
  args["class"] = vote_type;
  args["subset"] = "vote-type";
  Feature *f = new Feature( args );
  div->append( f );
  args.clear();
  args["subset"] = "outcome";
  args["class"] = outcome;
  f = new Feature( args );
  div->append( f );
  xmlNode *p = vote->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "about" ){
      add_about( div, p );
    }
    else if ( label == "consequence" ){
      KWargs args;
      args["subset"] = label;
      string cls = TiCC::XmlContent( p );
      if ( cls.empty() ){
	cls = "unknown";
      }
      args["class"] = cls;
      Feature *f = new Feature( args );
      div->append( f );
    }
    else if ( label == "division" ){
#pragma omp critical
      {
	cerr << "vote: skip division stuff" << endl;
      }
    }
    else {
#pragma omp critical
      {
	cerr << "vote: " << id << ", unhandled: " << label << endl;
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
  args["xml:id"] = id;
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
      string cls = att.second;
      if ( cls.empty() ){
	cls = "unknown";
      }
      args["class"] = cls;
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
  args["processor"] = processor_id;
  root->doc()->declare( folia::AnnotationType::LINEBREAK, setname, args );
  args.clear();
  args["pagenr"] = TiCC::getAttribute( brk, "originalpagenr");
  args["newpage"] = "yes";
  Linebreak *pb = new Linebreak( args );
  root->append( pb );
  args.clear();
  args["class"] = "page";
  args["format"] = "image/jpeg";
  args["xlink:href"] = TiCC::getAttribute( brk, "source");
  Relation *align = new Relation( args, root->doc() );
  pb->append( align );
}

void process_members( Division *root, xmlNode *members ){
  ForeignData *fd = new ForeignData();
  root->append( fd );
  fd->set_data( members );
}

void add_h_c_t( FoliaElement *root, xmlNode *block ){
  Document *doc = root->doc();
  string id = TiCC::getAttribute( block, "id" );
  string type = TiCC::getAttribute( block, "type" );
  KWargs args;
  args["xml:id"] = id;
  args["class"] = type;
  Division *div = new Division( args, doc );
  xmlNode *p = block->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "p" ){
      list<Note*> notes;
      Paragraph *par = add_par( div, p, notes );
      if ( !notes.empty() ){
	add_notes( par, notes );
      }
    }
    else if ( label == "stage-direction" ){
      process_stage( div, p );
    }
    else {
#pragma omp critical
      {
	cerr << "create_" << type << ", unhandled :" << label
	     << " id=" << id << endl;
      }
    }
    p = p->next;
  }
  root->append( div );
}

void process_stage( Division *root, xmlNode *_stage ){
  KWargs args;
  string id = TiCC::getAttribute( _stage, "id" );
  string type = TiCC::getAttribute( _stage, "type" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_stage: " << type << " ID=" << id << endl;
    }
  }
  args["xml:id"] = id;
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
    if ( verbose ){
#pragma omp critical
      {
	cerr << "process_stage: LOOP:" << label << " type=" << type << " ID=" << id << endl;
      }
    }
    if ( type == "chair" || label == "chair" ){
      process_chair( div, stage );
    }
    else if ( type == "pagebreak" ){
      process_break( div, stage->children );
    }
    else if ( type == "header"
	      || type == "title"
	      || type == "subtitle"
	      || type == "other"
	      || type == "question"
	      || type == "table"
	      || type == "label"
	      || type == "motie"
	      || type == "footer"
	      || type == "subject" ){
      add_h_c_t( div, stage );
    }
    else if ( type == "speech" ){
      process_speech( div, stage );
    }
    else if ( label == "vote" ){
      process_vote( div, stage );
    }
    else if ( label == "stage-direction" ){
      process_stage( div, stage );
    }
    else if ( type == "" ){ //nested or?
      if ( label == "stage-direction" ){
	process_stage( div, stage );
      }
      else if ( label == "p" ){
	list<Note*> notes;
	Paragraph *par = add_par( div, stage, notes );
	if ( !notes.empty() ){
	  add_notes( par, notes );
	}
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

folia::Document *create_basedoc( const string& docid,
				 const string& command,
				 xmlNode *metadata = 0,
				 xmlNode *docinfo = 0 ){
  Document *doc = new Document( "xml:id='" + docid + "'" );
  processor *proc = add_provenance( *doc, "FoLiA-pm", command );
  KWargs args;
  if ( processor_id.empty() ){
    processor_id = proc->id();
  }
  args["processor"] = processor_id;
  doc->declare( folia::AnnotationType::DIVISION, setname, args );
  doc->declare( folia::AnnotationType::RELATION, setname, args );
  if ( metadata ){
    if ( metadata->nsDef == 0 ){
      xmlNewNs( metadata,
		(const xmlChar*)"http://www.politicalmashup.nl",
		0 );
    }
    doc->set_foreign_metadata( metadata );
  }
  if ( docinfo ){
    doc->set_foreign_metadata( docinfo );
  }
  return doc;
}

void process_topic( const string& outDir,
		    const string& prefix,
		    const string& command,
		    Text* base_text,
		    xmlNode *topic,
		    bool no_split ){
  string id = TiCC::getAttribute( topic, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_topic: id=" << id << endl;
    }
  }
  KWargs args;
  Document *doc = 0;
  FoliaElement *root;
  if ( no_split ){
    doc = base_text->doc();
    args["generate_id"] = base_text->id();
    args["class"] = "proceedings";
    root = new Division( args, doc );
    base_text->append( root );
  }
  else {
    id = prefix + id;
    doc = create_basedoc( id, command );
    args["xml:id"] = id + ".text";
    Text *txt = new Text( args, doc );
    doc->append( txt );
    root = txt;
  }
  args.clear();
  args["xml:id"] = id + ".div";
  args["class"] = "topic";
  Division *div = new Division( args, doc );
  root->append( div );
  string title = TiCC::getAttribute( topic, "title" );
  if ( !title.empty() ){
    args.clear();
    args["processor"] = processor_id;
    doc->declare( folia::AnnotationType::HEAD,
		  setname, args );
    args.clear();
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
  if ( !no_split ){
    string filename = outDir+id+".folia.xml";
    doc->save( filename );
    delete doc;
#pragma omp critical
    {
      cout << "saved external file: " << filename << endl;
    }

    args.clear();
    args["xml:id"] = id;
    args["src"] = id + ".folia.xml";
    folia::External *ext = new External( args );
    base_text->append( ext );
  }
}

void process_proceeding( const string& outDir,
			 const string& prefix,
			 const string& command,
			 Text *root,
			 xmlNode *proceed,
			 bool no_split ){
  string id = TiCC::getAttribute( proceed, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_proceeding: id=" << id << endl;
    }
  }
  list<xmlNode*> topics = TiCC::FindNodes( proceed, "*:topic" );
  for ( const auto& topic : topics ){
    process_topic( outDir, prefix, command, root, topic, no_split );
  }
}

void process_sub_block( Division *, xmlNode * );

void add_signed( FoliaElement *root, xmlNode* block ){
  string id = TiCC::getAttribute( block, "id" );
  string type = TiCC::getAttribute( block, "type" );
  Document *doc = root->doc();
  KWargs args;
  args["processor"] = processor_id;
  doc->declare( folia::AnnotationType::ENTITY,
		setname, args );
  args.clear();
  args["xml:id"] = id;
  args["class"] = type;
  Division *div = new Division( args, doc );
  EntitiesLayer *el = new EntitiesLayer();
  div->append( el );
  xmlNode *p = block->children;
  while ( p ){
    string label = TiCC::Name(p);
    if ( label == "p" ){
      add_entity( el, p );
    }
    else {
#pragma omp critical
      {
	cerr << "create_signed: " << id << ", unhandled :" << label
	     << " type=" << type << endl;
      }
    }
    p = p->next;
  }
  root->append( div );
}

void add_section( FoliaElement *root, xmlNode* block ){
  Document *doc = root->doc();
  string id = TiCC::getAttribute( block, "id" );
  string type = TiCC::getAttribute( block, "type" );
  string section_id = TiCC::getAttribute( block, "section-identifier" );
  string section_path =TiCC::getAttribute( block, "section-path" );
  KWargs args;
  args["xml:id"] = id;
  args["class"] = type;
  if ( !section_id.empty() ){
    args["n"] = section_id;
  }
  Division *div = new Division( args, doc );
  if ( !section_path.empty() ){
    args.clear();
    args["subset"] = "section-path";
    args["class"] = section_path;
    Feature *f = new Feature( args );
    div->append( f );
  }
  process_sub_block( div, block );
  root->append( div );
}

void add_block( FoliaElement *root, xmlNode *block ){
  string id = TiCC::getAttribute( block, "id" );
  string type = TiCC::getAttribute( block, "type" );
  KWargs args;
  args["xml:id"] = id;
  args["class"] = type;
  Division *div = new Division( args, root->doc() );
  process_sub_block( div, block );
  root->append( div );
}

void add_heading( FoliaElement *root, xmlNode *block ){
  FoliaElement *el = new Head( );
  try {
    root->append( el );
  }
  catch (...){
    // so if another Head is already there, append it as a Div
    KWargs args;
    args["class"] = "subheading";
    el = new Division( args, root->doc() );
    root->append( el );
  }
  string txt = TiCC::XmlContent(block);
  if ( !txt.empty() ){
    el->settext( txt );
  }
}

void process_sub_block( Division *root, xmlNode *_block ){
  KWargs args;
  xmlNode *block = _block->children;
  list<Note*> notes;
  while ( block ){
    string id = TiCC::getAttribute( block, "id" );
    string type = TiCC::getAttribute( block, "type" );
    string label = TiCC::Name(block);
    if ( verbose ){
#pragma omp critical
      {
	cerr << "process_block_children: id=" << id
	     << " tag=" << label << " type=" << type << endl;
      }
    }
    if ( type == "signed-by" ){
      add_signed( root, block );
    }
    else if ( label == "p" ){
      add_par( root, block, notes );
    }
    else if ( label == "heading" ){
      add_heading( root, block );
    }
    else if ( type == "header"
	      || type == "content"
	      || type == "title" ){
      add_h_c_t( root, block );
    }
    else if ( type == "section" ){
      add_section( root, block );
    }
    else if ( label == "block" ){
      add_block( root, block );
    }
    else {
#pragma omp critical
      {
	cerr << "block: " << id << ", unhandled type: "
	     << type << endl;
      }
    }
    block = block->next;
  }
  if ( !notes.empty() ){
    add_notes( root, notes );
  }
}

void process_block( Text* base_text,
		    xmlNode *block ){
  string id = TiCC::getAttribute( block, "id" );
  string type = TiCC::getAttribute( block, "type" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_block: id=" << id << " type=" << type << endl;
    }
  }
  KWargs args;
  Document *doc = 0;
  doc = base_text->doc();
  args["xml:id"] = id;
  args["class"] = type;
  Division *root = new Division( args, doc );
  base_text->append( root );
  process_sub_block( root, block );
}

void process_parldoc( Text *root,
		      xmlNode *pdoc ){
  string id = TiCC::getAttribute( pdoc, "id" );
  if ( verbose ){
#pragma omp critical
    {
      cerr << "process_parldoc: id=" << id << endl;
    }
  }
  list<xmlNode*> blocks = TiCC::FindNodes( pdoc, "*:block" );
  for ( const auto& block : blocks ){
    process_block( root, block );
  }
}

void convert_to_folia( const string& file,
		       const string& outDir,
		       const string& prefix,
		       const string& command,
		       bool no_split ){
  bool succes = true;
#pragma omp critical
  {
    cout << "converting: " << file << endl;
  }
  xmlDoc *xmldoc = xmlReadFile( file.c_str(),
				0,
				::XML_PARSER_OPTIONS );
  if ( xmldoc ){
    xmlNode *root = xmlDocGetRootElement( xmldoc );
    xmlNode *metadata = TiCC::xPath( root, "//meta" );
    xmlNode *docinfo = TiCC::xPath( root, "//*[local-name()='docinfo']" );
    if ( !metadata ){
#pragma omp critical
      {
	cerr << "no metadata" << endl;
      }
      succes = false;
    }
    else {
      string base = TiCC::basename( file );
      string docid = prefix + base;
      Document *doc = create_basedoc( docid, command, metadata, docinfo );
      string::size_type pos = docid.rfind( ".xml" );
      if ( pos != string::npos ){
	docid = docid.substr(0,pos);
      }
      folia::KWargs args;
      args["xml:id"] = docid + ".text";
      folia::Text *text = new folia::Text( args, doc );
      doc->append( text );
      try {
	xmlNode *p = root->children;
	while ( p ){
	  if ( TiCC::Name( p ) == "proceedings" ){
	    process_proceeding( outDir, prefix, command, text, p, no_split );
	  }
	  else if ( TiCC::Name( p ) == "parliamentary-document" ){
	    process_parldoc( text, p );
	  }
	  p = p->next;
	}
	string outname = outDir + docid + ".folia.xml";
#pragma omp critical
	{
	  cout << "save " << outname << endl;
	}
	doc->save( outname );
      }
      catch ( const exception& e ){
#pragma omp critical
	{
	  cerr << "error processing " << file << endl
	       << e.what() << endl;
	  succes = false;
	}
      }
      delete doc;
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
  cerr << "Usage: FoLiA-pm [options] files/dir" << endl;
  cerr << "\t convert Political Mashup XML files to FoLiA" << endl;
  cerr << "\t when a dir is given, all '.xml' files in that dir are processed"
       << endl;
  cerr << "\t-t or --threads <threads>\t Number of threads to run on." << endl;
  cerr << "\t\t\t If 'threads' has the value \"max\", the number of threads is set to a" << endl;
  cerr << "\t\t\t reasonable value. (OMP_NUM_TREADS - 2)" << endl;
  cerr << "\t--nosplit\t don't create separate topic files" << endl;
  cerr << "\t--prefix='pre'\t add this prefix to ALL created files. (default 'FPM-') " << endl;
  cerr << "\t\t\t use 'none' for an empty prefix. (can be dangerous)" << endl;
  cerr << "\t-O\t\t output directory " << endl;
  cerr << "\t-v\t\t verbose output " << endl;
  cerr << "\t-V or --version\t show version " << endl;
  cerr << "\t-h or --help \t this messages " << endl;
}

int main( int argc, char *argv[] ){
  TiCC::CL_Options opts;
  try {
    opts.set_short_options( "vVt:O:h" );
    opts.set_long_options( "nosplit,help,version,prefix:,threads:" );
    opts.init( argc, argv );
  }
  catch( TiCC::OptionError& e ){
    cerr << e.what() << endl;
    usage();
    exit( EXIT_FAILURE );
  }
  string outputDir;
  if ( opts.extract( 'h' )
       || opts.extract( "help" ) ){
    usage();
    exit(EXIT_SUCCESS);
  }
  if ( opts.extract('V' )
       || opts.extract( "version" ) ){
    cerr << PACKAGE_STRING << endl;
    exit(EXIT_SUCCESS);
  }
  const string command = "FoLiA-pm " + opts.toString();
  verbose = opts.extract( 'v' );
  string value;
  if ( opts.extract( 't', value )
       || opts.extract( "threads", value ) ){
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
    exit( EXIT_FAILURE );
#endif
  }
  opts.extract( 'O', outputDir );
  if ( !outputDir.empty() && outputDir[outputDir.length()-1] != '/' )
    outputDir += "/";
  bool no_split = opts.extract( "nosplit" );
  if ( !opts.empty() ){
    cerr << "unsupported options : " << opts.toString() << endl;
    usage();
    exit(EXIT_FAILURE);
  }
  string prefix = "FPM-";
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
    if ( !TiCC::isFile(name) ){
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

#pragma omp parallel for shared(fileNames) schedule(dynamic)
  for ( size_t fn=0; fn < fileNames.size(); ++fn ){
    convert_to_folia( fileNames[fn], outputDir, prefix, command, no_split );
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
