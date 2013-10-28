#include <unistd.h> // getopt, unlink
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <list>
#include <map>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "libfolia/document.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/StringOps.h"
#include "ticcutils/zipper.h"
#include "ticcutils/FileUtils.h"
#include "config.h"
#ifdef HAVE_OPENMP
#include "omp.h"
#endif

using namespace	std;

bool verbose = false;
bool predict = false;
bool clearCachedFiles = false;

enum zipType { NORMAL, GZ, BZ2, UNKNOWN };

xmlNode *getNode( xmlNode *pnt, const string& tag ){
  while ( pnt ){
    if ( pnt->type == XML_ELEMENT_NODE && TiCC::Name(pnt) == tag ){
      return pnt;
    }
    else {
      xmlNode *res  = getNode( pnt->children, tag );
      if ( res )
	return res;
    }
    pnt = pnt->next;
  }
  return 0;
}

class docCache {
public:
  ~docCache() { clear(); };
  void clear();
  void fill( const string&, const list<xmlNode*>& );
  void add( const string&, const string& );
  xmlDoc *find( const string& ) const;
  map<string, xmlDoc*> cache;
};

void docCache::clear(){
  map<string,xmlDoc*>::const_iterator it = cache.begin();
  while ( it != cache.end() ){
    xmlFreeDoc( it->second );
    ++it;
  }
}

void docCache::add( const string& dir, const string& f ){
  string file = dir + f;
  if ( cache.find( f ) == cache.end() ){
    ifstream is( file.c_str() );
    if ( is ){
      if ( verbose ){
#pragma omp critical
	{
	  cout << "open file " << file << endl;
	}
      }
      xmlDoc *xmldoc = xmlReadFile( file.c_str(), 0, XML_PARSE_NOBLANKS );
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

void docCache::fill( const string& altoDir, const list<xmlNode*>& blocks ){
  list<xmlNode*>::const_iterator it = blocks.begin();
  while ( it != blocks.end() ){
    string alt = TiCC::getAttribute( *it, "alto" );
    if ( !alt.empty() ){
      add( altoDir, alt );
    }
    ++it;
  }
}

xmlDoc *docCache::find( const string& f ) const {
  map<string,xmlDoc*>::const_iterator it = cache.find( f );
  if ( it == cache.end() )
    return 0;
  else
    return it->second;
}

xmlNode *findPart2Level( const xmlNode *start ){
  if ( start && start->next ){
    xmlNode *pnt = start->next->children;
    while ( pnt != 0 ){
      if ( pnt->type == XML_ELEMENT_NODE &&
	   TiCC::Name(pnt) == "String" ){
	break;
      }
      pnt = pnt->next;
    }
    if ( pnt ){
      if ( TiCC::Name(pnt) == "String" ){
	string sub = TiCC::getAttribute( pnt, "SUBS_TYPE" );
	if ( sub == "HypPart2" )
	  return pnt;
      }
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
      pnt = pnt->children;
      while ( pnt != 0 ){
	if ( pnt->type == XML_ELEMENT_NODE &&
	     TiCC::Name(pnt) == "String" ){
	  break;
	}
	pnt = pnt->next;
      }
      if ( pnt ){
	if ( TiCC::Name(pnt) == "String" ){
	  string sub = TiCC::getAttribute( pnt, "SUBS_TYPE" );
	  if ( sub == "HypPart2" ){
	    return pnt;
	  }
	}
      }
    }
  }
  return 0;
}

void addStr( folia::Paragraph *par, string& txt,
	     const xmlNode *pnt, const string& altoFile ){
  folia::KWargs atts = folia::getAttributes( pnt );
  folia::KWargs args;
  args["id"] = atts["ID"];
  folia::String *s = new folia::String( par->doc(), args );
  par->append( s );
  s->settext( atts["CONTENT"], txt.length(), "OCR" );
  txt += " " + atts["CONTENT"];
  folia::Alignment *h = new folia::Alignment( "href='" + altoFile + "'" );
  s->append( h );
  folia::AlignReference *a =
    new folia::AlignReference( "id='" + atts["ID"] + "', type='str'" );
  h->append( a );
}

void createFile( folia::FoliaElement *text,
		 xmlDoc *alt_doc,
		 const string& altoFile,
		 const list<xmlNode*>& textblocks ){
  xmlNode *root = xmlDocGetRootElement( alt_doc );
  list<xmlNode*>::const_iterator bit = textblocks.begin();
  xmlNode *keepPart1 = 0;
  set<string> ids;
  while ( bit != textblocks.end() ){
    string id = TiCC::getAttribute( *bit, "ID" );
    if ( ids.find(id) != ids.end() ){
      if ( verbose ){
#pragma omp critical
	{
	  cout << "skip duplicate ID " << id << endl;
	}
      }
      ++bit;
      continue;
    }
    else {
      ids.insert(id);
    }
    folia::Paragraph *p =
      new folia::Paragraph( text->doc(), "id='" + text->id() + ".p." + altoFile + "." + id + "'" );
    text->append( p );
    string ocr_text;
    list<xmlNode*> v =
      TiCC::FindNodes( root,
			"//*[local-name()='TextBlock' and @ID='"
			+ id + "']" );
    if ( v.size() == 1 ){
      xmlNode *node = v.front();
      string tb_id = TiCC::getAttribute( node, "ID" );
      list<xmlNode*> lv = TiCC::FindNodes( node, "*[local-name()='TextLine']" );
      list<xmlNode*>::const_iterator lit = lv.begin();
      while ( lit != lv.end() ){
	xmlNode *pnt = (*lit)->children;
	while ( pnt != 0 ){
	  if ( pnt->type == XML_ELEMENT_NODE ){
	    if ( TiCC::Name(pnt) == "String" ){
	      string sub = TiCC::getAttribute( pnt, "SUBS_TYPE" );
	      if ( sub == "HypPart2" ){
		if ( keepPart1 == 0 ){
		  addStr( p, ocr_text, pnt, altoFile );
		}
		else {
		  folia::KWargs atts = folia::getAttributes( keepPart1 );
		  folia::KWargs args;
		  args["id"] = atts["ID"];
		  args["class"] = "OCR";
		  folia::String *s = new folia::String( text->doc(), args );
		  p->append( s );
		  s->settext( atts["SUBS_CONTENT"], ocr_text.length(), "OCR" );
		  ocr_text += " " + atts["SUBS_CONTENT"];
		  folia::Alignment *h =
		    new folia::Alignment( "href='" + altoFile + "'" );
		  s->append( h );
		  folia::AlignReference *a = 0;
		  a = new folia::AlignReference( "id='" + atts["ID"] + "', type='str'" );
		  h->append( a );
		  a = new folia::AlignReference( "id='" +
						 TiCC::getAttribute( pnt, "ID" )
						 + "', type='str'" );
		  h->append( a );
		  keepPart1 = 0;
		}
		pnt = pnt->next;
		continue;
	      }
	      else if ( sub == "HypPart1" ){
		// see if there is a Part2 in next line at this level.
		xmlNode *part2 = findPart2Level( *lit );
		if ( part2 ){
		  keepPart1 = pnt;
		  break; //continue with next TextLine
		}
		else {
		  // no second part on this level. seek in the next TextBlock
		  part2 = findPart2Block( node );
		  if ( !part2 ){
		    // Ok. Just ignore this and take the CONTENT
		    addStr( p, ocr_text, pnt, altoFile );
		  }
		  else {
		    keepPart1 = pnt;
		    break; //continue with next TextLine, but this should
		    // be empty, so in fact we go to the next block
		  }
		}
	      }
	      else {
		addStr( p, ocr_text, pnt, altoFile );
	      }
	    }
	  }
	  pnt = pnt->next;
	}
	++lit;
      }
    }
    else if ( v.size() == 0 ){
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
    if ( !ocr_text.empty() )
      p->settext( ocr_text.substr(1), "OCR" );
    ++bit;
  }
}

void processBlocks( folia::FoliaElement *text,
		    const list<xmlNode*>& blocks,
		    const docCache& cache ){
  list<xmlNode*>::const_iterator it = blocks.begin();
  while ( it != blocks.end() ){
    string alt = TiCC::getAttribute( *it, "alto" );
    xmlDoc *alt_doc = cache.find( alt );
    if ( alt_doc ){
      list<xmlNode*> texts = TiCC::FindNodes( *it, "dcx:TextBlock" );
      createFile( text, alt_doc, alt, texts );
    }
    else {
#pragma omp critical
      {
	cerr << "didn't find doc " << alt << endl;
      }
    }
    ++it;
  }
}

string replaceColon( const string& f ){
  string result = f;
  for( size_t i=0; i < result.length(); ++i ){
    if ( result[i] == ':' )
      result[i] = '.';
  }
  return result;
}

void processArticle( const string& f,
		     const string& subject,
		     const list<xmlNode*>& parts,
		     const docCache& cache,
		     const string& outDir,
		     const zipType inputType,
		     const zipType outputType ){
  if ( verbose ){
#pragma omp critical
    {
      cout << "start handling " << f << " (" << subject << ")" << endl;
    }
  }
  string docid = replaceColon(f);
  folia::Document doc( "id='" + docid + "'" );
  doc.declare( folia::AnnotationType::STRING, "alto", "annotator='alto'" );
  doc.set_metadata( "genre", subject );
  folia::Text *text = new folia::Text( "id='" + docid + ".text'" );
  doc.append( text );

  list<xmlNode*>::const_iterator it = parts.begin();
  while ( it != parts.end() ){
    list<xmlNode*> blocks = TiCC::FindNodes( *it, "dcx:blocks" );
    if ( blocks.size() < 1 ){
#pragma omp critical
      {
	cerr << "found no blocks" << endl;
      }
    }
    processBlocks( text, blocks, cache );
    ++it;
  }
  string outName = outDir;
  if ( subject == "artikel" )
    outName += "artikel/";
  else
    outName += "overige/";
  outName += docid + ".folia.xml";
  zipType type = inputType;
  if ( outputType != NORMAL )
    type = outputType;
  if ( type == BZ2 )
    outName += ".bz2";
  else if ( type == GZ )
    outName += ".gz";
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
		  const zipType outputType ){
  list<xmlNode *> parts =  TiCC::FindNodes( zone, "dcx:article-part" );
  if ( parts.size() >= 1 ){
    processArticle( id, subject, parts, cache, outDir, inputType, outputType );
  }
}

bool download( const string& altoDir, const list<xmlNode*>& resources, set<string>& downloaded ){
  downloaded.clear();
  struct stat sbuf;
  int d_cnt = 0;
  int c_cnt = 0;
  int res = stat( altoDir.c_str(), &sbuf );
  if ( res == -1 || !S_ISDIR(sbuf.st_mode) ){
    res = mkdir( altoDir.c_str(), S_IRWXU|S_IRWXG );
    if ( res ){
#pragma omp critical
      {
	cerr << "problem finding or creating alto dir '" << altoDir
	     << "' : " << res << endl;
      }
      return false;
    }
  }
  list<xmlNode*>::const_iterator it = resources.begin();
  map<string,string> ref_file_map;
  while ( it != resources.end() ){
    string ref = TiCC::getAttribute( *it, "ref" );
    string fn = TiCC::getAttribute( *it, "filename" );
    fn = altoDir + fn;
    if ( ref.find( ":alto" ) != string::npos ){
      ifstream is( fn.c_str() );
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
    ++it;
  }

  int retry = 3;
  while ( retry-- > 0 ){
    map<string,string>::iterator it = ref_file_map.begin();
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
    map<string,string>::const_iterator it = ref_file_map.begin();
#pragma omp critical
    {
      while ( it != ref_file_map.end() ){
	cerr << "reference:" << it->first
	     << " filename: " << it->second << endl;
	++it;
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

void clear_files( const set<string>& files ){
  set<string>::const_iterator it = files.begin();
  while( it != files.end() ){
    int res = unlink( it->c_str() );
    if ( res ){
#pragma omp critical
      {
	cerr << "problems removing file " << *it << endl;
      }
    }
    else if ( verbose ){
#pragma omp critical
      {
	cout << "cleared file " << *it << endl;
      }
    }
    ++it;
  }
}

xmlDoc *getXml( const string& file, zipType& type ){
  type = UNKNOWN;
  if ( TiCC::match_back( file, ":mpeg21.xml" ) ){
    type = NORMAL;
  }
  else if ( TiCC::match_back( file, ":mpeg21.xml.gz" ) ){
    type = GZ;
  }
  else if ( TiCC::match_back( file, ":mpeg21.xml.bz2" ) ){
    type = BZ2;
  }
  if ( type == UNKNOWN ){
    cerr << "problem detecting type of file: " << file << endl;
    return 0;
  }
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

void solveAlto( const string& altoDir,
		const string& file,
		const string& outDir,
		zipType outputType ){
  bool succes = true;
#pragma omp critical
  {
    cout << "resolving " << file << endl;
  }
  zipType inputType;
  xmlDoc *xmldoc = getXml( file, inputType );
  if ( xmldoc ){
    xmlNode *root = xmlDocGetRootElement( xmldoc );
    xmlNode *metadata = getNode( root, "metadata" );
    if ( metadata ){
      xmlNode *didl = getNode( metadata, "DIDL" );
      if ( didl ){
	list<xmlNode*> resources = TiCC::FindNodes( didl, "//didl:Component/didl:Resource[@mimeType='text/xml']" );
	set<string> downloaded_files;
	if ( download( altoDir, resources, downloaded_files ) ){
	  list<xmlNode*> blocks = TiCC::FindNodes( didl, "//dcx:blocks" );
	  docCache cache; // each thread it own cache...
	  cache.fill( altoDir, blocks );
	  list<xmlNode*> items = TiCC::FindNodes( didl, "didl:Item/didl:Item/didl:Item" );
	  set<string> article_names;
	  if ( items.size() > 0 ){
	    list<xmlNode*>::const_iterator it = items.begin();
	    while ( it != items.end() ){
	      string art_id = TiCC::getAttribute( *it, "article_id" );
	      if ( art_id.empty() ){
#pragma omp critical
		{
		  cerr << "no article ID in " << TiCC::getAttribute( *it, "identifier" )  << endl;
		}
		succes = false;
	      }
	      else {
		article_names.insert( art_id );
	      }
	      ++it;
	    }
	    set<string>::const_iterator art_it = article_names.begin();
	    while ( art_it != article_names.end() ){
	      string subject;
	      list<xmlNode*> meta = TiCC::FindNodes( didl, "//didl:Item/didl:Component[@dc:identifier='" + *art_it + ":metadata']" );
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
		  cerr << "problems with metadata in " << *art_it << endl;
		}
		succes = false;
	      }

	      list<xmlNode*> comps = TiCC::FindNodes( didl, "//didl:Item/didl:Component[@dc:identifier='" + *art_it + ":zoning']" );
	      if ( comps.size() == 1 ){
		list<xmlNode*> zones = TiCC::FindNodes( comps.front(), "didl:Resource/dcx:zoning" );
		if ( zones.size() == 1 ){
		  processZone( *art_it, subject, zones.front(), cache, outDir,
			       inputType, outputType );
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
		  cerr << "problems with Components in " << *art_it << endl;
		}
		succes = false;
	      }
	      ++art_it;
	    }
	  }
	  if ( clearCachedFiles )
	    clear_files( downloaded_files );
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

size_t predictAlto( const string& altoDir, const string& file ){
  size_t article_count = 0;
#pragma omp critical
  {
    cout << "resolving " << file << endl;
  }
  zipType inputType;
  xmlDoc *xmldoc = getXml( file, inputType );
  if ( xmldoc ){
    xmlNode *root = xmlDocGetRootElement( xmldoc );
    xmlNode *metadata = getNode( root, "metadata" );
    if ( metadata ){
      xmlNode *didl = getNode( metadata, "DIDL" );
      if ( didl ){
	list<xmlNode*> resources = TiCC::FindNodes( didl, "//didl:Component/didl:Resource[@mimeType='text/xml']" );
	set<string> downloaded_files;
	if ( download( altoDir, resources, downloaded_files ) ){
	  list<xmlNode*> blocks = TiCC::FindNodes( didl, "//dcx:blocks" );
	  docCache cache; // each thread it own cache...
	  cache.fill( altoDir, blocks );
	  list<xmlNode*> items = TiCC::FindNodes( didl, "didl:Item/didl:Item/didl:Item" );
	  if ( items.size() > 0 ){
	    list<xmlNode*>::const_iterator it = items.begin();
	    while ( it != items.end() ){
	      string art_id = TiCC::getAttribute( *it, "article_id" );
	      if ( art_id.empty() ){
#pragma omp critical
		{
		  cerr << "no article ID in " << TiCC::getAttribute( *it, "identifier" )  << endl;
		}
	      }
	      else {
		++article_count;
	      }
	      ++it;
	    }
	  }
	  if ( clearCachedFiles )
	    clear_files( downloaded_files );
	}
      }
      else {
#pragma omp critical
	{
	  cerr << "no didl" << endl;
	}
      }
    }
    else {
#pragma omp critical
      {
	cerr << "no metadata" << endl;
      }
    }
    xmlFreeDoc( xmldoc );
  }
  else {
#pragma omp critical
    {
      cerr << "XML failed: " << file << endl;
    }
  }
  return article_count;
}

int main( int argc, char *argv[] ){
  if ( argc < 2	){
    cerr << "Usage: [-t number_of_threads] [-o outputdir] [-a altodir] dir/filename " << endl;
    exit(EXIT_FAILURE);
  }
  int opt;
  int numThreads=1;
  string altoDir = "/tmp/altocache/";
  string outputDir;
  zipType outputType = NORMAL;
  while ((opt = getopt(argc, argv, "a:bcght:vVo:p")) != -1) {
    switch (opt) {
    case 'b':
      outputType = BZ2;
      break;
    case 'c':
      clearCachedFiles = true;
      break;
    case 'g':
      outputType = GZ;
      break;
    case 't':
      numThreads = atoi(optarg);
      break;
    case 'v':
      verbose = true;
      break;
    case 'p':
      predict = true;
      break;
    case 'V':
      cerr << PACKAGE_STRING << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'h':
      cerr << "Usage: alto [options] file/dir" << endl;
      cerr << "\t-a\t alto cache directory " << endl;
      cerr << "\t-c\t clear cached Alto files" << endl;
      cerr << "\t-t\t number_of_threads" << endl;
      cerr << "\t-h\t this messages " << endl;
      cerr << "\t-p\t prediction mode. only tell how many articles would be generated " << endl;
      cerr << "\t-o\t output directory " << endl;
      cerr << "\t-b\t create bzip2 files (.bz2)" << endl;
      cerr << "\t-g\t create gzip files (.gz)" << endl;
      cerr << "\t-v\t verbose output " << endl;
      cerr << "\t-V\t show version " << endl;
      exit(EXIT_SUCCESS);
      break;
    case 'a':
      altoDir = string(optarg) + "/";
      break;
    case 'o':
      outputDir = string(optarg) + "/";
      break;
    default: /* '?' */
      cerr << "Usage: alto [-t number_of_threads] [-o output_dir] dir/filename " << endl;
      exit(EXIT_FAILURE);
    }
  }
  vector<string> fileNames;
  string dirName;
  if ( !outputDir.empty() ){
    string name = outputDir;
    if ( !TiCC::isDir(name) ){
      int res = mkdir( name.c_str(), S_IRWXU|S_IRWXG );
      if ( res < 0 ){
	cerr << "outputdir '" << name
	     << "' doesn't existing and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
    name = outputDir + "artikel/";
    if ( !TiCC::isDir(name) ){
      int res = mkdir( name.c_str(), S_IRWXU|S_IRWXG );
      if ( res < 0 ){
	cerr << "outputdir '" << name
	     << "' doesn't existing and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
    name = outputDir + "overige/";
    if ( !TiCC::isDir(name) ){
      int res = mkdir( name.c_str(), S_IRWXU|S_IRWXG );
      if ( res < 0 ){
	cerr << "outputdir '" << name
	     << "' doesn't existing and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  else {
    string name = "artikel/";
    if ( !TiCC::isDir(name) ){
      int res = mkdir( name.c_str(), S_IRWXU|S_IRWXG );
      if ( res < 0 ){
	cerr << "outputdir '" << name
	     << "' doesn't existing and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
    name = "overige/";
    if ( !TiCC::isDir(name) ){
      int res = mkdir( name.c_str(), S_IRWXU|S_IRWXG );
      if ( res < 0 ){
	cerr << "outputdir '" << name
	     << "' doesn't existing and can't be created" << endl;
	exit(EXIT_FAILURE);
      }
    }
  }
  if ( !argv[optind] ){
    if ( clearCachedFiles ){
      if ( clear_alto_files( altoDir ) )
	exit(EXIT_SUCCESS);
      else
	exit(EXIT_FAILURE);
    }
    else {
      exit(EXIT_FAILURE);
    }
  }
  string name = argv[optind];
  if ( !( TiCC::isFile(name) || TiCC::isDir(name) ) ){
    cerr << "parameter '" << name << "' doesn't seem to be a file or directory"
	 << endl;
    exit(EXIT_FAILURE);
  }
  if ( TiCC::isFile(name) ){
    if ( TiCC::match_back( name, ".tar" ) ){
      cerr << "TAR files are not supported yet." << endl;
      exit(EXIT_FAILURE);
    }
    else {
      fileNames.push_back( name );
      string::size_type pos = name.rfind( "/" );
      if ( pos != string::npos )
	dirName = name.substr(0,pos);
    }
  }
  else {
    fileNames = TiCC::searchFilesMatch( name, "*:mpeg21.xml*" );
  }
  size_t toDo = fileNames.size();
  if ( toDo > 1 )
    cout << "start processing of " << toDo << " files " << endl;
  if ( numThreads >= 1 ){
    omp_set_num_threads( numThreads );
  }

  if ( predict ) {
    size_t total = 0;
#pragma omp parallel for shared(fileNames, total )
    for ( size_t fn=0; fn < fileNames.size(); ++fn ){
      total += predictAlto( altoDir, fileNames[fn] );
    }
    cout << total << " FoLiA files will be generated from " << name << endl;
  }
  else {
#pragma omp parallel for shared(fileNames)
    for ( size_t fn=0; fn < fileNames.size(); ++fn ){

      solveAlto( altoDir, fileNames[fn], outputDir, outputType );
    }
  }
  cout << "done" << endl;
  exit(EXIT_SUCCESS);
}
