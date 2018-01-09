//C-  -*- C++ -*-
//C- -------------------------------------------------------------------
//C- DjVuLibre-3.5
//C- Copyright (c) 2002  Leon Bottou and Yann Le Cun.
//C- Copyright (c) 2001  AT&T
//C-
//C- This software is subject to, and may be distributed under, the
//C- GNU General Public License, Version 2. The license should have
//C- accompanied the software or you may obtain a copy of the license
//C- from the Free Software Foundation at http://www.fsf.org .
//C-
//C- This program is distributed in the hope that it will be useful,
//C- but WITHOUT ANY WARRANTY; without even the implied warranty of
//C- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//C- GNU General Public License for more details.
//C- 
//C- DjVuLibre-3.5 is derived from the DjVu(r) Reference Library
//C- distributed by Lizardtech Software.  On July 19th 2002, Lizardtech 
//C- Software authorized us to replace the original DjVu(r) Reference 
//C- Library notice by the following text (see doc/lizard2002.djvu):
//C-
//C-  ------------------------------------------------------------------
//C- | DjVu (r) Reference Library (v. 3.5)
//C- | Copyright (c) 1999-2001 LizardTech, Inc. All Rights Reserved.
//C- | The DjVu Reference Library is protected by U.S. Pat. No.
//C- | 6,058,214 and patents pending.
//C- |
//C- | This software is subject to, and may be distributed under, the
//C- | GNU General Public License, Version 2. The license should have
//C- | accompanied the software or you may obtain a copy of the license
//C- | from the Free Software Foundation at http://www.fsf.org .
//C- |
//C- | The computer code originally released by LizardTech under this
//C- | license and unmodified by other parties is deemed "the LIZARDTECH
//C- | ORIGINAL CODE."  Subject to any third party intellectual property
//C- | claims, LizardTech grants recipient a worldwide, royalty-free, 
//C- | non-exclusive license to make, use, sell, or otherwise dispose of 
//C- | the LIZARDTECH ORIGINAL CODE or of programs derived from the 
//C- | LIZARDTECH ORIGINAL CODE in compliance with the terms of the GNU 
//C- | General Public License.   This grant only confers the right to 
//C- | infringe patent claims underlying the LIZARDTECH ORIGINAL CODE to 
//C- | the extent such infringement is reasonably necessary to enable 
//C- | recipient to make, have made, practice, sell, or otherwise dispose 
//C- | of the LIZARDTECH ORIGINAL CODE (or portions thereof) and not to 
//C- | any greater extent that may be necessary to utilize further 
//C- | modifications or combinations.
//C- |
//C- | The LIZARDTECH ORIGINAL CODE is provided "AS IS" WITHOUT WARRANTY
//C- | OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
//C- | TO ANY WARRANTY OF NON-INFRINGEMENT, OR ANY IMPLIED WARRANTY OF
//C- | MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//C- +------------------------------------------------------------------
// 
// $Id$
// $Name:  $

#ifdef __GNUG__
#pragma implementation
#endif
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/** @name jb2unify

    {\bf Synopsis}
    \begin{verbatim}
        jb2unify [-v] <inputdjvufile> <outputdjvufile> [dictionaryname]
    \end{verbatim}

    {\bf Description}

    File #"jb2unify.cpp"# demonstrates creation of shared JB2 shape
    dictionaries. It can be used to post-process multipage documents
    with separately created bilevel or palettized pages.
    Writes a bundled DjVu document.

    {\bf Bugs}

    If there are shared dictionaries with other names than the one
    picked, they are not removed, and neither are references to them.
    By default it picks dictXXXX.iff where XXXX is the number of pages,
    since that was the behaviour I observed in Any2DjVu.

    @memo
    Simple JB2 shape dictionary unifier.
    @author
    Yann Vernier <yann@algonet.se>
    @version
    $Id$ */
//@{
//@}


#include "DjVuGlobal.h"
#include "Arrays.h"
#include "GException.h"
#include "GSmartPointer.h"
#include "GBitmap.h"
#include "JB2Image.h"
#include "DjVuImage.h"
#include "GURL.h"
#include "DjVmDoc.h"
#include "DjVuDocument.h"
#include "DjVuMessage.h"
#include "IFFByteStream.h"
#include "DataPool.h"
#include <locale.h>
#include <stdlib.h>
#define NO_TSEARCH


// TODO list:
// * Detect preexisting shared dictionaries? Right now they'll be caught
//   the same way as duplicated shapes, but probably needlessly copied
//   into the new document (unless they're named the same as ours).
//   I haven't verified the behaviour, it is likely to break.
// * Create multiple shared dictionaries by which pages use them.
//   This also should skip JB2 pages with no shared shapes.
//   Requires somewhat more exhaustive statistics on shape usage.
// * Split this code into readable functions
// * See if we can avoid using both DjVm and DjVuDocument
// * Somehow optimize the equal shape lookup, it's the primary bottleneck

#ifndef NO_TSEARCH
GArray<JB2Shape> allshapes;
void *shapetree=NULL;

int ts_compar(const void *left, const void *right)
 {
  JB2Shape &l=allshapes[*((int *)left)], &r=allshapes[*((int *)right)];

  if(l.parent<r.parent)
    return -1;
  if(l.parent>r.parent)
    return 1;

  unsigned int lsize, rsize;
  const unsigned char *lrle=l.bits->get_rle(lsize),
  	*rrle=r.bits->get_rle(rsize);

  if(lsize<rsize)
    return -1;
  else if(lsize>rsize)
    return 1;
  else
    return memcmp(lrle, rrle, lsize);
 }
#endif

// Gather full shape statistics from all pages
// Populates masks, allshapes, and shapesperpage;
// shapesperpage will get pointers allocated with new int[]
// only pointers for pages which have a mask in masks are valid!!
// should probably move to a class for per-page statistics
void gathershapes(const GP<DjVuDocument> &doc, GPMap<int, JB2Image> &masks,
    GArray<JB2Shape> &allshapes, int **shapesperpage, const int verbose)
 {
  allshapes.empty();
  int totalshapes=0;
  allshapes.touch(totalshapes);
  if(verbose)
    DjVuFormatErrorUTF8("Collecting all shapes");
  for(int page=0; page<doc->get_pages_num(); page++)
   {
    GP<DjVuImage> dimg=doc->get_page(page);

    GP<JB2Image> jb2=dimg->get_fgjb();
    if(!jb2)
      continue;

    masks[page]=jb2;

    // Make sure it's all RLE for our comparisons
    jb2->compress();
    // Gather all the shapes from this page
    const int shapecount=jb2->get_shape_count();
    int *shapes=new int[shapecount];
    for(int shapeno=0; shapeno<shapecount; shapeno++)
     {
      int thisshape=totalshapes;
      // Private copy; at this stage, we don't know if we hit shared
      // dictionaries. We'll be replacing the whole dictionaries later on.
      JB2Shape shape=jb2->get_shape(shapeno);
      if(shape.parent>=0)
	shape.parent=shapes[shape.parent];
      shape.userdata=0;	// we use this for use count
#ifdef NO_TSEARCH
      // Scan through old shapes to see if there is a match
      unsigned int oursize;
      const unsigned char *ourrle=shape.bits->get_rle(oursize);
      while(thisshape--)
       {	// see if this shape is equal to another
	const JB2Shape &theirshape=allshapes[thisshape];
	if(theirshape.parent!=shape.parent)
	  continue;	// Mismatching parents
	unsigned int theirsize;
	const unsigned char *theirrle=theirshape.bits->get_rle(theirsize);
	if(oursize!=theirsize)
	  continue;
	if(!memcmp(ourrle, theirrle, oursize))
	  break;	// Found a match!
       }
      if(thisshape>=0)	// Found a match
       {
	if(verbose>1)
	  DjVuFormatErrorUTF8("page %d shape %d matched shape %d", page,
	      shapeno, thisshape);
       }
      else		// New shape to be added
       {
	allshapes[thisshape=totalshapes++]=shape;
	allshapes.touch(totalshapes);
	if(verbose>1) {
	    DjVuFormatErrorUTF8("page %d shape %d added as shape %d", page,
				shapeno, thisshape);
	    char fname[50];
	    sprintf(fname, "shape%05d.pgm", shapeno);
	    GP<ByteStream> bs=ByteStream::create(GURL::Filename::UTF8(fname), "wb");
	    shape.bits->save_pgm(*bs);
	    }
       }
#else // TSEARCH
      allshapes[totalshapes]=shape;
      // They really got the prototype for tsearch wrong.
      // It returns a pointer to the value you fed it, which should be void**
      thisshape=(long)*(void**)tsearch((const void *)totalshapes, &shapetree, ts_compar);
      if(thisshape==totalshapes)
       {
	totalshapes++;
	allshapes.touch(totalshapes);
	if(verbose>1)
	  DjVuFormatErrorUTF8("page %d shape %d added as shape %d", page,
	      shapeno, thisshape);
       }
      else
       {
	if(verbose>1)
	  DjVuFormatErrorUTF8("page %d shape %d matched shape %d", page,
	      shapeno, thisshape);
       }
#endif
      shapes[shapeno]=thisshape;
     }
    shapesperpage[page]=shapes;

    // Collect usage statistics for the shapes
    const int blits=jb2->get_blit_count();
    int thispageuses=0;
    bool usedshapes[shapecount];
    memset(usedshapes, 0, sizeof usedshapes);
    for(int blitno=0; blitno<blits; blitno++)
     {
      int weuse=jb2->get_blit(blitno)->shapeno;
      // Loop to get all the parents too
      while(!usedshapes[weuse])
       {
	thispageuses++;
	usedshapes[weuse]=true;
	allshapes[shapes[weuse]].userdata++;
	if(allshapes[shapes[weuse]].parent>=0)
	  weuse=jb2->get_shape(weuse).parent;
	else
	  break;	// no more parents
       }
     }
    if(verbose)
      DjVuFormatErrorUTF8("Page %d uses %d/%d shapes in %d places", page, thispageuses,
	  shapecount, blits);
   }
 }

void
jb2unify(const GURL &inputdjvuurl, const GURL &outputdjvuurl, int verbose, GUTF8String &dictid)
{
  GP<DjVmDoc> djvm=DjVmDoc::create();
  djvm->read(inputdjvuurl);
  GP<DjVuDocument> doc=DjVuDocument::create_wait(inputdjvuurl);
  GP<DjVmDir> dir=djvm->get_djvm_dir();

  const int pages=dir->get_pages_num();
#ifdef NO_TSEARCH
  GArray<JB2Shape> allshapes;
#endif
  GPMap<int, JB2Image> masks;
  int *shapesperpage[pages];

  if(!dictid.length())
   {
    GP<ByteStream> bs=ByteStream::create();
    bs->format("dict%04d.iff", pages);
    bs->seek(0);
    dictid=bs->getAsUTF8();
   }

  // Gather full shape statistics from all pages
  gathershapes(doc, masks, allshapes, shapesperpage, verbose);

  // Create shared dictionary for all multi-page shapes
  if(verbose)
    DjVuFormatErrorUTF8("Creating shared dictionary");
  GP<JB2Dict> sharedshapes=JB2Dict::create();
  // We actually have a spare slot on top, and hbound is faster than size
  const int allshapescount=allshapes.hbound();
  // This loop changes userdata from use count to shared dictionary index
  for(int shapeno=0; shapeno<allshapescount; shapeno++)
   {
    JB2Shape shape=allshapes[shapeno];
    if(shape.userdata<2)
      allshapes[shapeno].userdata=-1;	// unshared shape
    else
     {
      // Fix up the parent
      if(shape.parent>=0)
	shape.parent=allshapes[shape.parent].userdata;
      int newid=sharedshapes->add_shape(shape);		// shared shape
      allshapes[shapeno].userdata=newid;
      if(verbose>1)
	DjVuFormatErrorUTF8("shape %d with %d users is shared %d", shapeno,
	    shape.userdata, newid);
     }
   }
  if(verbose)
    DjVuFormatErrorUTF8("%d/%d shapes shared", 
	sharedshapes->get_shape_count(), allshapescount);
  // Turn the dictionary into a proper DjVu IFF
  GP<ByteStream> dictiff=ByteStream::create();
  GP<IFFByteStream> shareddictionary=IFFByteStream::create(dictiff);
  shareddictionary->put_chunk("FORM:DJVI");
  shareddictionary->put_chunk("Djbz");
  sharedshapes->encode(shareddictionary->get_bytestream());
  shareddictionary->close_chunk();
  shareddictionary->close_chunk();
  dictiff->seek(0);
  // Store it in the DjVmDoc
  if(dir->id_to_file(dictid))
    djvm->delete_file(dictid);
  djvm->insert_file(*dictiff, DjVmDir::File::INCLUDE,
      dictid, dictid, dictid, 0);

  if(verbose)
    DjVuFormatErrorUTF8("Recreating dictionary for each page");
  // Recreate dictionaries for pages
  for(int page=0; page<pages; page++)
   {
    // TODO: skip this page if it doesn't use any shared shapes
    // or just skip the inheritance to make this thing remove unused shapes
    // right now we force ALL pages to inherit a single shared dictionary

    if(!masks.contains(page))
      continue;		// No mask in that page

    GP<DjVmDir::File> file=dir->page_to_file(page);
    int pos=dir->get_file_pos(file);
    GUTF8String fileid=file->get_load_name(), chunkid;
    GP<ByteStream> outbuf=ByteStream::create();
    GP<IFFByteStream> outiff=IFFByteStream::create(outbuf);
    GP<IFFByteStream> iniff=IFFByteStream::create(djvm->get_data(fileid)->get_stream());

    if (! iniff->get_chunk(chunkid))
      G_THROW("Malformed DJVU file");
    if (chunkid != "FORM:DJVU" && chunkid != "FORM:DJVI" )
      G_THROW("Found a mask here before, now it's not a layered DjVu image!");

    outiff->put_chunk(chunkid);

    while(iniff->get_chunk(chunkid))
     {
      if(chunkid=="INCL")
       {	// Check if it happens to be for our dictionary
	GUTF8String incl=iniff->get_bytestream()->getAsUTF8();
	if(dictid!=incl)
	 {
	  outiff->put_chunk(chunkid);
	  outiff->get_bytestream()->writestring(incl);
	  outiff->close_chunk();
         }
       }
      else if(chunkid=="Sjbz")
       {
	GP<JB2Image> jb2=masks[page];
	int shapecount=jb2->get_shape_count();

	// Copy everything except the shapes (we already have those)
	const int width=jb2->get_width(), height=jb2->get_height(),
	    blits=jb2->get_blit_count();
	JB2Blit blit[blits];	// Setting this to 600 alters the effects of a bug
	if(verbose>1)
	  DjVuFormatErrorUTF8("Page %d has %d blits", page, blits);
	for(int i=0; i<blits; i++)
	 {
	  blit[i]=*jb2->get_blit(i);
	  if(verbose>1)
	    DjVuFormatErrorUTF8("blit %d: shape %d to %d,%d", i,
		blit[i].shapeno, blit[i].left, blit[i].bottom);
	 }
	// Clear the JB2 image
	jb2->init();
	jb2->set_dimension(width, height);
	// Reinsert the blits along with the needed shapes
	int *gshapes=shapesperpage[page];	// allshapes ids
	int pshapes[shapecount];		// page ids
	// Add the shape dictionary
	int pageshares=0;
	jb2->set_inherited_dict(sharedshapes);
	for(int i=0; i<shapecount; i++)
	 {
	  pshapes[i]=allshapes[gshapes[i]].userdata;
	  if(pshapes[i]>=0)
	    pageshares++;
	  else
	   {
	    JB2Shape shape=allshapes[gshapes[i]];
	    // Remapping parents again
	    if(shape.parent>=0)
	      shape.parent=allshapes[shape.parent].userdata;
	    pshapes[i]=jb2->add_shape(shape);
	    // Store IDs for local shapes so the above mapping will work
	    allshapes[gshapes[i]].userdata=pshapes[i];
	   }
	 }
	delete gshapes;
	if(verbose)
	  DjVuFormatErrorUTF8("Page %d uses %d/%d shared and %d private shapes in %d places",
	      page, pageshares, sharedshapes->get_shape_count(), shapecount-pageshares, blits);
	// Now for the blits
	for(int i=0; i<blits; i++)
	 {
	  // Local page dictionary mapping
	  blit[i].shapeno=pshapes[blit[i].shapeno];
	  jb2->add_blit(blit[i]);
	  if(verbose>1)
	    DjVuFormatErrorUTF8("blit %d: shape %d to %d,%d", i,
		blit[i].shapeno, blit[i].left, blit[i].bottom);
	 }
	// Finally, write the chunk, including INCL
	outiff->put_chunk("INCL");
	outiff->get_bytestream()->writestring(dictid);
	outiff->close_chunk();
	outiff->put_chunk(chunkid);
	jb2->encode(outiff->get_bytestream());
	outiff->close_chunk();
	masks.del(page);	// Possibly saves some memory
       }
      else	// Other chunks
       {
	outiff->put_chunk(chunkid);
	outiff->copy(*iniff->get_bytestream());
	outiff->close_chunk();
       }
      iniff->close_chunk();
     }
    outiff->close_chunk();
    outbuf->seek(0);
    // Finally, replace the page
    GP<DataPool> outmem=DataPool::create(outbuf);
    outmem->load_file();
    djvm->delete_file(fileid);
    djvm->insert_file(outmem, DjVmDir::File::PAGE,
	fileid, fileid, fileid, pos);
   }

  if(verbose)
    DjVuFormatErrorUTF8("Writing output to %s", outputdjvuurl.get_string().getbuf());
  GP<ByteStream> outfile=ByteStream::create(outputdjvuurl, "wb");
  djvm->write(outfile);
 }

// --------------------------------------------------
// MAIN
// --------------------------------------------------

void
usage()
{
  DjVuPrintErrorUTF8(
#ifdef DJVULIBRE_VERSION
         "JB2UNIFY --- DjVuLibre-" DJVULIBRE_VERSION "\n"
#endif
         "Simple JB2 shape dictionary unifier\n\n"
         "Usage: jb2unify [-v] <inputdjvufile> <outputdjvufile> [dictionaryname]\n"
         "Output is a bundled DjVu document.\n" );
  exit(10);
}


int 
main(int argc, const char **argv)
{
  setlocale(LC_ALL,"");
  djvu_programname(argv[0]);
  GArray<GUTF8String> dargv(0,argc-1);
  int verbose=0;
  for(int i=0;i<argc;++i)
    dargv[i]=GNativeString(argv[i]);
  G_TRY
    {
      GURL inputdjvuurl;
      GURL outputdjvuurl;
      GUTF8String dictid;
      // Parse options
      for (int i=1; i<argc; i++)
        {
          GUTF8String arg = dargv[i];
          if (arg == "-v")
            verbose++;
          else if (arg[0] == '-' && arg[1])
            usage();
          else if (inputdjvuurl.is_empty())
            inputdjvuurl = GURL::Filename::UTF8(arg);
          else if (outputdjvuurl.is_empty())
            outputdjvuurl = GURL::Filename::UTF8(arg);
          else if (!dictid.length())
            dictid = arg;
          else
            usage();
        }
      if (inputdjvuurl.is_empty() || outputdjvuurl.is_empty())
        usage();
      // Execute
      jb2unify(inputdjvuurl, outputdjvuurl, verbose, dictid);
    }
  G_CATCH(ex)
    {
      ex.perror();
      exit(1);
    }
  G_ENDCATCH;
  return 0;
}

