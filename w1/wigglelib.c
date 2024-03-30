/*
 * Create and combine wiggles, i.e. tag density profiles
 * Several formats are recognized as input or output
 *   probealignbest hit plain and binary output
 *   three formats from UCSC
 *   the AW binary format genberated by the present program
 * Create letter profiles from a fasta or fastc file
 */

#include "wiggle.h"

/*************************************************************************************/
/************************** Wiggles **************************************************/
/*************************************************************************************/

static int wigglePointOrder (const void *va, const void *vb)
{
  const WIGGLEPOINT *a = (const WIGGLEPOINT *) va, *b = (const WIGGLEPOINT *) vb ;
  int x ;
  x = a->x - b->x ; if (x) return x ;
  return 0 ;
}

/*************************************************************************************/
/*************************************************************************************/
/* target is the mRNA or gene listed in the HIT file, with hit at [x1,x2]
 * the objective is to remap it onto the chromosome, possibly as discontinuous alignment
 */
static BOOL sxRemapOne (WIGGLE *sx, SXMAP *sxmap, int target_map, int map, int x1, int x2, int *remapp, int *aa1, int *aa2, int naa)
{
  BOOL isDown = TRUE ;
  int a1, a2 ;

  if ( (target_map && sxmap->target_map != target_map) || sxmap->map != map)
    return FALSE ;
  if (*remapp && *remapp != sxmap->remap)
    return FALSE ;

  if (x1 > x2)
    { int x = x1 ; x1 = x2 ; x2 = x ; isDown = FALSE ; }

  x1 = (x1 > sxmap->x1 ? x1 : sxmap->x1) ;
  x2 = (x2 < sxmap->x2 ? x2 : sxmap->x2) ;
  if (x1 > x2)
    return FALSE ;

  if (sxmap->a1 < sxmap->a2)
    { 
      a1 = sxmap->a1 + x1 - sxmap->x1 ;
      a2 = sxmap->a1 + x2 - sxmap->x1 ;
    }
  else
    { 
      a1 = sxmap->a1 - x1 + sxmap->x1 ;
      a2 = sxmap->a1 - x2 + sxmap->x1 ;
    }

  aa1[naa] = isDown ? a1 : a2 ; 
  aa2[naa] = isDown ? a2 : a1 ; 
  *remapp = sxmap->remap ;

  return TRUE ;
} /* sxRemapOne */

static BOOL sxRemap (WIGGLE *sx, int target_map, int map, int x1, int x2, int *remapp, int *aa1, int *aa2, int *naap)
{
  int ii ;
  KEY *kp ;
  int naa = 0 ;
  SXMAP *sxmap ;
  static KEYSET map2sxmap = 0 ;

  if (! map2sxmap)
    {
      map2sxmap = arrayHandleCreate(dictMax(sx->mapDict)+1, KEY, sx->h) ;
      for (ii = 0, sxmap = arrayp (sx->targets, 0, SXMAP); ii < arrayMax (sx->targets) ; sxmap++, ii++)
	{
	  kp = arrayp (map2sxmap,sxmap->map, KEY) ;
	  if (! *kp) *kp = ii + 1 ;
	}
    }
  *remapp = 0 ;
  kp = arrayp (map2sxmap,map, KEY) ;
  if (*kp)
    {
      for (ii = *kp - 1, sxmap = arrayp (sx->targets, ii, SXMAP) ; naa < 1024 && ii < arrayMax (sx->targets) && sxmap->map == map ; sxmap++, ii++)
	if ((!target_map || sxmap->target_map == target_map) && sxmap->map == map && sxRemapOne (sx, sxmap, target_map, map, x1, x2, remapp, aa1, aa2, naa))
	  naa++ ;
    }

  *naap = naa ;
    
  return naa > 0 ? TRUE : FALSE ;
} /* sxRemap */

/*************************************************************************************/

void sxWiggleParse (WIGGLE *sx, int z1, int z2)
{
  ACEIN ai = sx->ai ;
  Array *aap = 0 ;
  WIGGLEPOINT *wp ;
  int nn = 0, stepOut = 1, stepIn = 1, span = 1, x, x0 = 0, x1, x2, ln, ali, score, mult, nT ;
  int map = 0, remap = 0, remap1 = 0, target_class,target_genome,  oldScore = 0, oldTarget_class = 0 ;
  float y = 0 ;
  int nRejected = 0 ;
  /*   int wiggle_weight = 1 ; */
  /*   BOOL hasWiggle_weight = FALSE ; not implemented 2014_05_28 */
  const char *ccp = 0 ;
  char *cp, *cq, cc, *cr ;
  int bufferSize = 32*1024 ;
  char buffer[bufferSize] ;
  int *ip, iBuffer[7] ;
  int lnTitle, lnTrackName, lnTarget ;
  int ixx, inxx = 1, xx1[1024], xx2[1024] ;
  long int nBp = 0 ;
  char tagName[1024], oldTagName[1024] ;
  char geneName[1024], oldGeneName[1024] ;

  memset (xx1, 0, sizeof(xx1)) ;
  memset (xx2, 0, sizeof(xx2)) ;
  memset (tagName, 0, sizeof(tagName)) ;
  memset (oldTagName, 0, sizeof(oldTagName)) ;

  if (! sx->remapDict)
    {
      sx->remapDict = dictHandleCreate (1000, sx->h) ;
      dictAdd (sx->remapDict, "toto", 0) ;
    }
  
  stepIn = (sx->in_step  ? sx->in_step : 1) ;
  stepOut = (sx->out_step  ? sx->out_step : 1) ;
  switch (sx->in)
    {
    case COUNT:
    case AM:
    case AG:
      messcrash ("AM/AG formats are not recognized as input format") ;
      break ;
    case AF:
      aceInBinary (ai, (char*)iBuffer, 7 * sizeof (int)) ;
      sx->in_x1 = x1 = iBuffer[0] ;
      sx->in_step = stepIn = iBuffer[1] ;
      sx->in_span = span = iBuffer[2]  ? iBuffer[2] : iBuffer[1]  ;
      nn = iBuffer[3] ;
      lnTrackName = iBuffer[4] ;
      lnTitle = iBuffer[5] ;
      lnTarget = iBuffer[6] ;

      if (sx->targets)
	messcrash ("remapping not programmed yet in AF format") ;
      if (lnTrackName) 
	{
	  char *buf = halloc (lnTrackName + 1, sx->h) ;
	  aceInBinary (ai, buf, lnTrackName) ;
	  sx->trackName = buf ;
	  map = 0 ;
	  if (sx->targets)
	    {
	      if (! dictFind (sx->mapDict, buf, &map))
		messcrash ("remapping not programmed yet in AF format") ;
	    }
	  else
	    dictAdd (sx->remapDict, buf, &remap1) ;
	}
      if (lnTitle) 
	{
	  char *buf = halloc (lnTitle + 1, sx->h) ;
	  aceInBinary (ai, buf, lnTitle) ;
	  sx->title = buf ;
	}
      if (lnTarget) 
	{
	  char *buf = halloc (lnTarget + 1, sx->h) ;
	  aceInBinary (ai, buf, lnTarget) ;
	  map = 0 ;
	  if (sx->targets)
	    {
	      if (! dictFind (sx->mapDict, buf, &map))
		messcrash ("remapping not programmed yet in AF format") ;
	    }
	  else
	    dictAdd (sx->remapDict, buf, &remap1) ;
	}

      if (z2 > 0)
	{
	  x1 = (z1 - sx->in_x1 + stepIn - 1)/stepIn ; x2 = (z2 - sx->in_x1)/stepIn ; 
	  if (x1 < 0) x1 = 0 ;
	  if (x2 >= nn) x2 = nn - 1 ;
	  z1 = sx->in_x1 ;
	  if (nn > z2) nn = z2 ;
	}
      else
	{ z1 = sx->in_x1 ; x1 = 0 ; x2 = nn - 1 ; }
      ip = (int*) halloc (nn * sizeof (int), 0) ;
      aceInBinary (ai, (char *) ip, nn * sizeof (int)) ;
      remap = remap1 ;
      if (sx->noRemap) remap = 0 ;
      aap = arrayp (sx->aaa, remap, Array) ;
      if (! *aap)
	*aap = arrayHandleCreate (x2 - x1 + 1, WIGGLEPOINT, sx->h) ;
      if (stepIn == stepOut)
	{
	  array (*aap, x2 - x1, WIGGLEPOINT).y = 0 ; /* make room */
	  for (x = x1, wp = arrp (*aap, x, WIGGLEPOINT) ; x <= x2 ; x++, wp++)
	    {
	      wp->x = z1 + x * stepIn ;
	      wp->y += ip[x] ;
	      nBp +=  ip[x] ;
	    }
	}
      else
	{
	  int a1, a2, b, b1, b2 ;

	  for (x = x1 ; x <= x2 ; x++)
	    {
	      a1 = x * stepIn ;
	      a2 = x * stepIn + span - 1 ;
	      b1 = (a1 + stepOut - 1)/stepOut ; b2 = (a2)/stepOut ; 
	      for (b = b1 ; b <= b2 ; b++)
		{
		  wp = arrayp (*aap, b, WIGGLEPOINT) ; 
		  wp->x = z1 + b * stepOut ;
		  wp->y = ip[x] ;
		  nBp += ip[x] ;
		}
	    }
	}
      ac_free (ip) ;
      break ;
    case AW:  /* read a collection of concatenated binary files */
      oneByteInitialize (FALSE) ;
      while (aceInBinary (ai, buffer, 1024))
	{
	  if (sscanf(buffer,"%d%d%d",&x1, &stepIn, &nn) != 3)
	    messcrash ("cannot read 3 values in the header of the AW file") ;
	  sx->in_x1 = x1 ;
	  sx->in_step = stepIn ;
	  cp = buffer + 3 * sizeof (int) ;
	  cq = strstr(cp, "\t") ;
	  if (cq) {*cq=0;}
	  if (!strncmp (cp, "MRNA:", 5)) cp += 5 ;

	  if (*cp)
	    {
	      map = 0 ;
	      if (sx->targets)
		{
		  if (! dictFind (sx->mapDict, cp, &map))
		    { map = 0 ; continue ; }
		}
	      else
		dictAdd (sx->remapDict, ccp, &remap1) ;
	    }
	  sx->trackName = strnew (cq+1, sx->h) ;
      if (sx->targets)
	messcrash ("remapping not programmed yet in AW format") ;
	  
      remap = remap1 ;
	  if (sx->noRemap) remap = 0 ;
	  aap = arrayp (sx->aaa, remap, Array) ;
	  if (! *aap)
	    *aap = arrayHandleCreate (100000, WIGGLEPOINT, sx->h) ;
	  array (*aap, nn, WIGGLEPOINT).x = 0 ; /* make room */
	  cp = cq = halloc (nn, 0) ;
	  aceInBinary (ai, cp, nn) ;
	  for (x = 0 ; x < nn ; cq++, x++)
	    {
	      x2 = (x1 + x * stepIn) ;
	      if (x2 % sx->out_step == 0)
		{
		  float u = oneByteDecode (*cq) ;

		  wp = arrayp (*aap, x2/sx->out_step, WIGGLEPOINT) ;
		  wp->x = x2 ;
		  wp->y += u ;
		  nBp += u ;
		}
	    }
	  ac_free (cp) ;
	}
      break ;
    case BF:
    case BV:
    case TABIX:
      x1 = sx->in_x1 - stepIn ;
      while ((cp = aceInCard (ai)))
	{
	  int dummy, dx = 0 ;

	  if(! strncmp(cp, "WIGGLE_WEIGHT", strlen("WIGGLE_WEIGHT")) &&
	     sscanf (cp, "WIGGLE_WEIGHT %d", &dummy) == 1 
	     )
	    {
	      /*
	       *wiggle_weight = dummy ;
	       *  hasWiggle_weight = TRUE ; 
	       */
	    }	  
	  if (*cp == 0 || *cp == '#')
	    continue ;
	  if (! strncmp (cp, "//", 2))
	    continue ;
	  if (strstr (cp, "track"))
	    continue ;
	  if (strstr (cp, "Step"))
	    {
	      span = sx->in_span  ? sx->in_span  : 1 ;
	      if (strstr (cp, "fixedStep"))
		sx->in = BF ;
	      if (strstr (cp, "variableStep"))
		sx->in = BV ;
	      map = remap = 0 ;
	      if ((cq=strstr (cp, "chrom=")))
		{
		  cq = cr = cq+6 ;
		  while (*cr != ' ' && *cr != 0) cr++ ;
		  cc = * cr ; *cr = 0 ;
		  if (!strncmp (cq, "MRNA:", 5)) cq += 5 ;

		  if (sx->targets)
		    {
		      if (! dictFind (sx->mapDict, cq, &map))
			{ map = 0; continue ; }
		    }
		  else
		    dictAdd (sx->remapDict, cq, &remap1) ;
		  *cr = cc ;
		}
	      if ((cq=strstr (cp, "start=")))
		{
		  cq = cr = cq+6 ;
		  while (*cr >= '0' && *cr <= '9') cr++ ;
		  cc = *cr ; *cr = 0 ;
		  x0 = atoi (cq) - 1 ; stepIn = 1 ; 
		  *cr = cc ;
		}
	      if ((cq=strstr (cp, "step=")))
		{
		  cq = cr = cq+5 ;
		  while (*cr >= '0' && *cr <= '9') cr++ ;
		  cc = * cr ; *cr = 0 ;
		  stepIn = atoi (cq) ;
		  *cr = cc ;
		  x0 = x0 + 1 - stepIn ;
		  span = stepIn ;
		}
	      if ((cq=strstr (cp, "span=")))
		{
		  cq = cr = cq+5 ;
		  while (*cr >= '0' && *cr <= '9') cr++ ;
		  cc = *cr ; *cr = 0 ;
		  span = atoi (cq) ;
		  *cr = cc ;
		}
	      continue ;
	    }
	  remap = remap1 ;
	  switch (sx->in)
	    {
	    case BF:
	      if (! map && ! remap) 
		continue ;  
	      aceInNext (ai) ;
	      if (! aceInFloat (ai, &y))
		{
		  messcrash ("Missing value in BF input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      x0 += stepIn ;
	      inxx = 1 ; xx1[0] = x0 ; xx2[0] = x2 = x0 + stepIn - 1 ;
	      if (sx->targets && !sxRemap (sx, 0, map, x0, x2, &remap, xx1, xx2, &inxx))
		continue ;
	      x1 = xx1[0] ; x2 = xx2[0] ;
	      if ((sx->strand && x1 > x2) || (sx->antistrand && x1 < x2)) continue ;
	      dx = stepOut/2 > 1 ? stepOut/2 - 1 : 0 ;
	      x1 = (x1 - z1 + dx)/stepOut ; 
	      x2 = (x2 - z1 + (dx > stepIn ? dx - stepIn : 0))/stepOut ;
	      { int ny = x2 > x1 ? x2 - x1 : x1 - x2 ; y /= (1+ny) ; } /* since we are spreading the point */

	      break ;
	    case BV:
	      if (! map && ! remap) 
		continue ;  
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &x0))
		{
		  messcrash ("Missing position in BV input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      aceInNext (ai) ;
	      if (! aceInFloat (ai, &y))
		{
		  messcrash ("Missing position in BV input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      inxx = 1 ; xx1[0] = x0 ; xx2[0] = x2 = x0 + span - 1 ;
	      if (sx->targets && !sxRemap (sx, 0, map, x0, x2, &remap, xx1, xx2, &inxx))
		continue ;
	      x1 = xx1[0] ; x2 = xx2[0] ;
	      if ((sx->strand && x1 > x2) || (sx->antistrand && x1 < x2)) continue ;
	      dx = stepOut/2 > 1 ? stepOut/2 - 1 : 0 ;
	      x1 = (x1 - z1 +  dx)/stepOut ; 
	      x2 = (x2 - z1 + (dx  > span ? dx - span : 0))/stepOut ;
	      { int ny = x2 > x1 ? x2 - x1 : x1 - x2 ; y /= (1+ny) ; } /* since we are spreading the point */
	      break ;

	    case TABIX:
	      if (0 && ! map && ! remap) 
		continue ;  
	      cp = aceInWord (ai) ;
	      if (strstr(cp, ",")) continue ;
	      if (!strncmp (cp, "MRNA:", 5)) cp += 5 ;

	      if (sx->targets)
		{
		  if (! dictFind (sx->mapDict, cp, &map))
		    continue ;
		}
	      else
		{
		  if (1)
		    remap = 0 ;
		  else
		    dictAdd (sx->remapDict, cp, &remap) ;
		}
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &x0))
		{
		  messcrash ("Missing position in TABIX input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      aceInNext (ai) ;
	      if (! aceInFloat (ai, &y))
		{
		  messcrash ("Missing value in TABIX input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}

	      inxx = 1 ; xx1[0] = x0 ; xx2[0] = x2 = x0 + span - 1 ;
	      if (sx->targets && !sxRemap (sx, 0, map, x0, x2, &remap, xx1, xx2, &inxx))
		continue ;
	      x1 = xx1[0] ; x2 = xx2[0] ;
	      if ((sx->strand && x1 > x2) || (sx->antistrand && x1 < x2)) continue ;
	      dx = stepOut/2 > 1 ? stepOut/2 - 1 : 0 ;
	      x1 = (x1 - z1 + dx)/stepOut ; 
	      x2 = (x2 - z1 + (dx > stepIn ? dx - stepIn : 0))/stepOut ;
	      { int ny = x2 > x1 ? x2 - x1 : x1 - x2 ; y /= (1+ny) ; } /* since we are spreading the point */

	      break ;


	    default: /* not applicable */
	      break ;
	    }
	  if (sx->noRemap) remap = 0 ;
	  if (x1 < x2 && sx->antistrand) continue ;
	  if (x1 > x2)
	    {
	      if (sx->strand) continue ;
	      x = x1 ; x1 = x2 ; x2 = x ;
	    }
	  aap = arrayp (sx->aaa, remap, Array) ;
	  if (! *aap)
	    *aap = arrayHandleCreate (100000, WIGGLEPOINT, sx->h) ;
	  nn++ ;
	  if (sx->in != BG)
	    { x1 *= stepOut ; x2 *= stepOut ; }
	  for (x = x1 ; x <= x2 ; x++)
	    {
	      if (x % stepOut) continue ; 
	      if (z2 == 0 || (x >= 0 && z1 + x  <= z2))
		{
		  aap = arrayp (sx->aaa, remap, Array) ;
		  if (! *aap)
		    *aap = arrayHandleCreate (100000, WIGGLEPOINT, sx->h) ;
		  {
		    int x0 ;
		    for (x0 = arrayMax (*aap) ; x0 <= x/stepOut ; x0++)
		      {
			wp = arrayp (*aap, x0, WIGGLEPOINT) ;
			wp->x = z1 + x0 * stepOut ;
		      }
		  }
		  wp = arrayp (*aap, x/stepOut, WIGGLEPOINT) ;
		  wp->x = z1 + x ;
		  wp->y += y ;
		  nBp +=  y ;
		}
	    }
	  if (z2 > 0 && x0 > z2)
	    break ;
	  continue ;
	}
      break ;
    case BG:
    case BHIT:
      dictAdd (sx->target_mapDict, "Z_genome", &target_genome) ;
      while (aceInCard (ai))
	{
	  ccp = aceInWord (ai) ; 
	  if (! ccp || *ccp == '#')
	    continue ;

	  switch (sx->in)
	    {
	    case BG:
	      if (! strncmp (ccp, "//", 2))
		continue ;
	      if (strstr (ccp, "track"))
		continue ;

	      if (sx->targets)
		{
		  if (! dictFind (sx->mapDict, ccp, &map))
		    { map = 0 ; continue ; }
		}
	      else
		dictAdd (sx->remapDict, ccp, &remap) ;
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &x0))
		{
		  messcrash ("Missing first position in BG input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &x2))
		{
		  messcrash ("Missing second position in BG input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      if (! aceInFloat (ai, &y))
		{
		  messcrash ("Missing multiplicity in BG input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      if (x0 < x2) 
		{ ali = x2 - x0 ; x0++ ; } /* first coordinate is zero based, so we know the orientation */
	      else 
		{ ali = x0 - x2 ; x2++ ; }
	      inxx = 1 ; xx1[0] = x0 ; xx2[0] = x2 ;
	      if (sx->targets && !sxRemap (sx, 0, map, x0, x2, &remap, xx1, xx2, &inxx))
		continue ;
	      mult = y ; 
	      if (sx->lengthCoverage) 
		mult *= ali ;
	      nT = 1 ;

	      break ;
	      
	    case BHIT:
	      if (sx->rejectDict && dictFind (sx->rejectDict, ccp, 0))
		{ nRejected++ ;continue ; }
	      if (sx->selectDict && ! dictFind (sx->selectDict, ccp, 0))
		{ nRejected++ ;continue ; }
	      strncpy (tagName, ccp, 1000) ;
	      /* dictAdd (sx->dict, ccp, &tag) ; */
	      
	      /* score */
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &score))
		{
		  messcrash ("Missing score column 2 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      if (strcmp (tagName, oldTagName))
		{
		  oldScore = 0 ; oldTarget_class = 0 ; *oldGeneName = 0 ;
		}
	      if (score < oldScore)
		continue ;
	      oldScore = score ;
	      
	      /* multiplicity */
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &mult))
		{
		  messcrash ("Missing multiplicity column 3 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      /* length to align */
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &ln))
		{
		  messcrash ("Missing clipped_length column 4 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      /* length to align */
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &ali))
		{
		  messcrash ("Missing aligned_length column 5 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      if (sx->lengthCoverage)
		mult *= ali ;
	      /* x1 (in this progrma, the probe coordinates x1/x2 are not used and overwritten by a1/a2 below */
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &x1))
		{
		  messcrash ("Missing x1 position column 6 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      
	      /* x2 */
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &x2))
		{
		  messcrash ("Missing x2 position column 7 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      /* target class */
	      aceInNext (ai) ;
	      if (!(ccp = aceInWord (ai)))
		{
		  messcrash ("Missing target class column 8 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      if (sx->target_class && strcmp (sx->target_class, ccp)) /* select a target_class */
		continue ;
	      
	      /* Reject future hits of same tag in worse classes, except among transcriptome classes */
	      dictAdd (sx->target_mapDict, ccp, &target_class) ;
	      
	      /* gene */
	      aceInNext (ai) ;
	      if (!(ccp = aceInWord (ai)))
		{
		  messcrash ("Missing gene column 9 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      strncpy (geneName, ccp, 999) ;
	      /* accept a single hit per gene, allow multiple in genome */
	      if (! sx->forceUnique 
		  && target_class != target_genome 
		  && ! strcmp (tagName, oldTagName) 
		  && ! strcmp (geneName, oldGeneName)
		  )
		continue ;
	      /* accept a single target_class per tag */
	      if (! strcmp (tagName, oldTagName) 
		  && target_class != oldTarget_class
		  )
		continue ;
	      
	      /* target multiplicity */
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &nT))
		{
		  messcrash ("Missing target_multiplicity column 10 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      if (nT < 0) nT = -nT ; 
	      if (sx->forceUnique && nT > 1) nT = 1 ;
	      if (sx->unique && nT > 1) continue ;
	      
	      /* read the mrna so it is used as target */
	      if (!(ccp = aceInWord (ai)))
		{
		  messcrash ("Missing target (mRNA,chromosome) in column 11 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      map = 0 ;
	      if (!strncmp (ccp, "MRNA:", 5)) ccp += 5 ;
	      if (sx->targets)
		{
		  if (target_class && dictFind (sx->mapDict, messprintf("%s:%s",dictName(sx->target_mapDict, target_class),ccp), &map)) ;
		  else
		    if (! dictFind (sx->mapDict, ccp, &map))
		      { map = 0; continue ; }
		}
	      else
		dictAdd (sx->remapDict, ccp, &remap) ;
	      
	      /* x1 (called x1/x2 in this program, usually called a1/a2*/
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &x1))
		{
		  messcrash ("Missing a1 position column 12 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      
	      /* x2 */
	      aceInNext (ai) ;
	      if (! aceInInt (ai, &x2))
		{
		  messcrash ("Missing a2 position column 13 in BHIT input file %s, line %d:\n"
			     , sx->inFileName, aceInStreamLine (ai)) ;
		}
	      
	      /* missmatches and overhangs, reported in col 14 to 21 are mostly ignored in this program */
	      if (sx->minErrRate)
		{
		  int nN = 0, nErr = 0 ;
		  int dx = x1 < x2 ? x2 - x1 + 1 : x1 - x2 + 1 ;
		  
		  if (! aceInInt (ai, &nN)) continue ;
		  if (! aceInInt (ai, &nErr)) continue ;
		  if (100 * nErr < sx->minErrRate * dx) continue ;
		}
	      /* remapping : reject cases that do not remap */
	      
	      inxx = 1 ; xx1[0] = x1 ; xx2[0] = x2 ;
	      if (sx->targets && !sxRemap (sx, target_class, map, x1, x2, &remap, xx1, xx2, &inxx))
		continue ;
	      
	      /* register this tag so it is counted only once */
	      strncpy (oldTagName, tagName, 1024) ;
	      strncpy (oldGeneName, geneName, 1024) ;
	      oldTarget_class = target_class ;
	      aceInNext (ai) ;

	      break ;

	    default: /* not applicable */
	      break ;
	    }

	  if (sx->strand || sx->antistrand)
	    {
	      BOOL ok = FALSE ;
	      for (ixx = 0 ; !ok && ixx < inxx ; ixx++)
		{
		  x1 = xx1[ixx] ; x2 = xx2[ixx] ;
		  if (sx->strand && x1 < x2)
		    ok = TRUE ;
		  if (sx->antistrand && x1 > x2)
		    ok = TRUE ;
		}
	      if (! ok)
		{ nRejected++ ; continue ; }
	    }
	  for (ixx = 0 ; ixx < inxx ; ixx++)
	    {
	      x1 = xx1[ixx] ; x2 = xx2[ixx] ;
	  
	      if (x1 > x2) {x = x1 ; x1 = x2 ; x2 = x ; }
	      x1 += sx->delta ; x2 -= sx->delta ;
	      x1 = (x1 - z1 + stepOut - 1)/stepOut ; x2 = (x2 - z1)/stepOut ; y = ((double)mult) / nT ;
	      nn++ ;
	      for (x = x1 ; x <= x2 ; x++)
		{
		  if (z2 == 0 || (x >= 0 && z1 + x * stepOut <= z2))
		    {
		      if (sx->noRemap) remap = 0 ;
		      aap = arrayp (sx->aaa, remap, Array) ;
		      if (! *aap)
			{
			  *aap = arrayHandleCreate (1000, WIGGLEPOINT, sx->h) ;
			  if (0 && sx->forceUnique)
			    { /* the idea is to set a poit at the origin to align the files, but it does not work */
			      wp = arrayp (*aap, x, WIGGLEPOINT) ;
			      wp->x = 1 ;
			      wp->y = 1 ;
			    }
			}
		      {
			int x0 = arrayMax (*aap) ;
			wp = arrayp (*aap, x, WIGGLEPOINT) ; /* make room, may change arrayMax so we use x0 */
			for ( ; x0 <= x ; x0++)
			  {
			    wp = arrp (*aap, x0, WIGGLEPOINT) ;
			    wp->x = z1 + x0 * stepOut ;
			  }
		      }
		      wp = arrayp (*aap, x, WIGGLEPOINT) ;
		      wp->x = z1 + x * stepOut ;
		      wp->y += y ;
		      nBp +=  y ;
		    }
		}
	    }
	}
      break ;
    default:
      break ;
    }

  if (0)
    {
      Array aa = array (sx->aaa, remap, Array) ;
      for (x = 0, wp = arrp (aa, 0, WIGGLEPOINT) ; x < arrayMax (aa) ; x++, wp++)
	fprintf (stderr, "%d\t%.1f\n", wp->x, wp->y) ;

    }
  fprintf (stderr, "// Parsed %d positions, %ld Mb, rejected %d positions, in file %s\n", nn, nBp*stepOut/1000000, nRejected, sx->inFileName) ;      

  for (nn = 0 ; nn < arrayMax (sx->aaa) ; nn++)
    {
      aap = arrayp (sx->aaa, nn, Array) ;
      if (*aap)
	arraySort (*aap, wigglePointOrder) ;
    }
}  /* sxWiggleParse */

/*************************************************************************************/
/* aa: possibly gaussed wiggle, bb original wiggle */
/* In the multipeaks option
 *  try to export a collection of non overlapping peaks
 *  with a succession of thresholds
 *  arranged in a log scale
 * every factor multiPeaks, starting a 1 
 */
typedef struct mpkStruct { int x1, x2, ln, yMin, yMax, cover, level ; } MPK ;
static void sxWiggleExportMultiPeaks (WIGGLE *sx, Array aa0, Array bb, int remap)
{
  WIGGLEPOINT *wp, *wq ;
  int ii, jj, nn ;
  Array aa = bb ? bb : aa0 ;
  int iiMax = aa ? arrayMax (aa) : 0 ;
  int x, y ;
  int ratio = sx->multiPeaks ;
  int minCover = sx->minCover ;
  int step = (sx->out_step  ? sx->out_step : 1) ;
  int level, imm = 0 ;
  double aliBp = 0, area = 0 ;
  ACEOUT ao = sx->aoPeaks ;
  Array mmm = arrayCreate (10000, MPK) ;
  MPK *mp ;
  BOOL debug = FALSE ;

  if (ratio < 2) ratio = 2 ;
  if (minCover < 1)
    minCover = 1 ;
  if (! ao)
    ao = sx->aoPeaks = aceOutCreate (sx->outFileName, ".multiPeaks", sx->gzo, sx->h) ;
  if (! ao)
    return ;
  fprintf (stderr, "// multiPeaks are exported in file %s.multiPeaks\n", sx->outFileName) ;   
  
  for (aliBp = 0, nn = 0, area = 0, ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; ii < iiMax ; ii++, wp++) 
    { 
      x = wp->x ;
      y = wp->y ; if (y < 0) y = 0 ;
      aliBp += y ;
      if (y >= minCover) { nn += step ; area += y * step ; }
    }
      
  aceOutf(ao, "#Chromosome %s Length %.3f Mb, Usable %.3f Mb, Covered over %d : %.1f%%, cumulatedArea Mb %g, Aligned Mb %g\tLow level\tHigh level\n"
	  , dictName (sx->remapDict, remap)
	  , x/1000000.0, nn/1000000.0, minCover, 100.0 * (double)nn/(x ? x : 1), area/1000000.0, step*aliBp/1000000.0
	  ) ;

  aceOut (ao, "#Target\ta1\ta2\tlength bp\tmax cover\taverage cover\taligned bp\tlevel1\tlevel2\n") ;
  for (ii = jj = 0,  level = minCover ; ii < iiMax ; ii++) 
    {
      wp = arrp (aa, ii, WIGGLEPOINT) ;
      x = wp->x ;
      y = wp->y ; if (y < 0) y = 0 ;
      if (y < minCover)
	continue ;
      if (y >= level)  /* we just reached this level, span that terrace and export it */
	{
	  int level2 = level * ratio ;
	  
	  while (1)
	    {
	      int ln = 0, yMax = 0, yMin = level2 ;
	      double cover = 0 ;

	      for (wq = wp, jj = ii ;  jj < iiMax ; jj++, wq++)
		{
		  y = wq->y ; if (y < 0) y = 0 ;
		  if (wq->x > x + step)
		    y = 0 ;
		  x = wq->x ;
		  if (y >= level && y < level2) 
		    {
		      ln++ ;
		      cover += y ;
		      if (y > yMax) yMax = y ;
		      if (y < yMin) yMin = y ;
		    }
		  else
		    break ;
		}
	      if (ln > 0)
		{
		  if (level == 1 || cover > 1.5 * minCover) /* avoid fluctuations just around minCover */
		    {
		      mp = arrayp (mmm, imm++, MPK) ;
		      mp->x1 = wp->x - step/2 ; 
		      mp->x2 = x - step/2 ; if (jj == iiMax)  mp->x2 += step ;
		      mp->ln = ln ;
		      mp->yMin = yMin ;
		      mp->yMax = yMax ;
		      mp->cover = cover ;
		      mp->level = level ;
		    }
		  break ;
		}
	      else
		{
		  level = level2 ; level2 = level * ratio ;
		}
	    }
	  /* This system allows multi valued XH exon support */
	  if (y >= level2)
	    { 
	      while (y >= level2)
		{ level = level2 ; level2 *= ratio ; }
	    }
	  else if (y < level && level > minCover)
	    {
	      while  (y < level && level > minCover)
		{ level2 = level ; level /= ratio ; }
	    }
	  wp = wq - 1 ; ii = jj - 1 ;
	}
    }

  if (debug)
    {
      aceOutf (ao, "\nBefore spikes\n") ;
      for (ii = 0, mp = arrp (mmm, ii, MPK) ; ii < imm ; mp++, ii++)
	aceOutf (ao, "%s\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\n"
		 , dictName (sx->remapDict, remap)
		 , mp->x1, mp->x2, mp->ln * step
		 , mp->yMax, mp->cover * 1.0/(mp->ln), mp->cover * step
	     , mp->level, mp->level * ratio
		 ) ;
    }

  /* compress the spikes */
  for (ii = 0, mp = arrp (mmm, ii, MPK) ; ii < imm - 2 ; mp++, ii++)
    {
      MPK *mp1 = mp + 1 ;
      MPK *mp2 = mp + 2 ;
      
      /* local spikes,  merge the 3 regions */
      if (mp1->x1 == mp->x2 && mp2->x1 == mp1->x2 &&
	  mp->level == mp2->level &&  mp->level * ratio == mp1->level &&
	  mp1->yMax < mp1->level * (1 + ratio)/2.0
	  )
	{
	  mp2->x1 = mp->x1 ;
	  mp2->ln = mp->ln + mp1->ln + mp2->ln ;
	  mp2->cover = mp->cover + mp1->cover + mp2->cover ;
	  mp2->yMax = mp1->yMax ;
	  if (mp->yMin < mp2->yMin) mp2->yMin = mp->yMin ;
	  mp->cover = mp1->cover = 0 ;
	  ii++ ; mp++ ;
	}
    }
  /* compress */
  for (ii = jj = 0, mp = arrp (mmm, ii, MPK) ; ii < imm ; mp++, ii++)
    {
      if (mp->cover)
	 {
	   if (jj < ii)
	     {
	       MPK *mp1 =  arrp (mmm, jj, MPK) ;
	       *mp1 = *mp ;
	     }
	   jj++ ;	   
	 }
    }
  imm = jj ;
  if (debug)
    {
      aceOutf (ao, "\nBefore dips\n") ;
      for (ii = 0, mp = arrp (mmm, ii, MPK) ; ii < imm ; mp++, ii++)
	aceOutf (ao, "%s\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\n"
		 , dictName (sx->remapDict, remap)
		 , mp->x1, mp->x2, mp->ln * step
	     , mp->yMax, mp->cover * 1.0/(mp->ln), mp->cover * step
		 , mp->level, mp->level * ratio
		 ) ;
    }

  /* compress the dips */
  for (ii = 0, mp = arrp (mmm, ii, MPK) ; ii < imm - 2 ; mp++, ii++)
    {
      MPK *mp1 = mp + 1 ;
      MPK *mp2 = mp + 2 ;

      /* local spikes,  merge the 3 regions */
      if (mp1->x1 == mp->x2 && mp2->x1 == mp1->x2 &&
	  mp->level == mp2->level &&  mp1->level * ratio == mp->level &&
	  mp1->yMin > mp1->level * (1 + ratio)/2.0
	  )
	{
	  mp2->x1 = mp->x1 ;
	  mp2->ln = mp->ln + mp1->ln + mp2->ln ;
	  mp2->cover = mp->cover + mp1->cover + mp2->cover ;
	  mp2->yMin = mp1->yMin ;
	  if (mp->yMax > mp2->yMax) mp2->yMax = mp->yMax ;
	  mp->cover = mp1->cover = 0 ;
	  ii++ ; mp++ ;
	}
    }
  /* compress */
  for (ii = jj = 0, mp = arrp (mmm, ii, MPK) ; ii < imm ; mp++, ii++)
    {
      if (mp->cover)
	{
	  if (jj < ii)
	    {
	      MPK *mp1 =  arrp (mmm, jj, MPK) ;
	      *mp1 = *mp ;
	    }
	  jj++ ;	   
	}
    }
  imm = jj ;
  

  /* export */
  if (debug)
    aceOutf (ao, "\nAfter dips\n") ;

  for (ii = 0, mp = arrp (mmm, ii, MPK) ; ii < imm ; mp++, ii++)
    aceOutf (ao, "%s\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\n"
	     , dictName (sx->remapDict, remap)
	     , mp->x1, mp->x2, mp->ln * step
	     , mp->yMax, mp->cover * 1.0/(mp->ln), mp->cover * step
	     , mp->level, mp->level * ratio
	     ) ;

  /* zero terminate */
  aceOutf (ao, "%s\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%d\t%d\n"
	   , dictName (sx->remapDict, remap)
	   , (mp -1)->x2 + 3*step/2, (mp-1)->x2 + 5*step/2, step
	   , 0, 0.0, 0.0
	   , 0, 0
	   ) ;
  ac_free (mmm) ;
  return ;
} /* sxWiggleExportMultiPeaks */

/*************************************************************************************/
/* aa: possibly gaussed wiggle, bb original wiggle */
/* In the transcriptsEnds option
 *  try to locate the transcript ends inthe ELF, ELR, ERF, ERR wiggles
 *  defined as region of high E[L/F]x contrast and adequate minimal cover\n"
 */
static void sxWiggleExportTranscriptsEnds (WIGGLE *sx, Array aa0, Array bb, int remap)
{
  WIGGLEPOINT *wp, *wq ;
  int ii, jj, nn ;
  Array aa = bb ? bb : aa0 ;
  int iiMax = aa ? arrayMax (aa) : 0 ;
  int x, y ;
  int minCover = sx->minCover ;
  int step = (sx->out_step  ? sx->out_step : 1) ;
  int level, imm = 0 ;
  double aliBp = 0, area = 0 ;
  ACEOUT ao = sx->aoPeaks ;
  Array mmm = arrayCreate (10000, MPK) ;
  MPK *mp ;
  BOOL debug = FALSE ;

  if (minCover < 10)
    minCover = 10 ;
  if (! ao)
    return ;
  fprintf (stderr, "// transcriptsEnds are exported in file %s.transcriptsEnds\n", sx->outFileName) ;   
  
  for (aliBp = 0, nn = 0, area = 0, ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; ii < iiMax ; ii++, wp++) 
    { 
      x = wp->x ;
      y = wp->y ; if (y < 0) y = 0 ;
      aliBp += y ;
      if (y >= minCover) { nn += step ; area += y * step ; }
    }
      
  aceOutf(ao, "#Chromosome %s Length %.3f Mb, Usable %.3f Mb, Covered over %d : %.1f%%, cumulatedArea Mb %g, Aligned Mb %g\tLow level\tHigh level\n"
	  , dictName (sx->remapDict, remap)
	  , x/1000000.0, nn/1000000.0, minCover, 100.0 * (double)nn/(x ? x : 1), area/1000000.0, step*aliBp/1000000.0
	  ) ;

  aceOut (ao, "#Target\ta1\ta2\tlength bp\tmax cover\taverage cover\taligned bp\tlevel1\tlevel2\n") ;
  for (ii = jj = 0,  level = minCover ; ii < iiMax ; ii++) 
    {
      wp = arrp (aa, ii, WIGGLEPOINT) ;
      x = wp->x ;
      y = wp->y ; if (y < 0) y = 0 ;
      if (y < minCover)
	continue ;
      if (1)  /* we just reached minLevel, span that terrace and export it */
	{
	  while (1)
	    {
	      int ln = 0, yMax = 0, yMin = 1000 * minCover + 1000 ;
	      double cover = 0 ;

	      for (wq = wp, jj = ii ;  jj < iiMax ; jj++, wq++)
		{
		  y = wq->y ; if (y < 0) y = 0 ;
		  if (wq->x > x + step)
		    y = 0 ;
		  x = wq->x ;
		  if (y >= level)
		    {
		      ln++ ;
		      cover += y ;
		      if (y > yMax) yMax = y ;
		      if (y < yMin) yMin = y ;
		    }
		  else
		    break ;
		}
	      if (ln > 0)
		{
		  if (cover > 1.5 * minCover) /* avoid fluctuations just around minCover */
		    {
		      mp = arrayp (mmm, imm++, MPK) ;
		      mp->x1 = wp->x - step/2 ; 
		      mp->x2 = x - step/2 ; if (jj == iiMax)  mp->x2 += step ;
		      mp->ln = ln ;
		      mp->yMin = yMin ;
		      mp->yMax = yMax ;
		      mp->cover = cover ;
		      mp->level = level ;
		    }
		  break ;
		}
	    }
	  wp = wq - 1 ; ii = jj - 1 ;
	}
    }

  if (debug)
    {
      aceOutf (ao, "\nEnds  stranded\n") ;
      for (ii = 0, mp = arrp (mmm, ii, MPK) ; ii < imm ; mp++, ii++)
	aceOutf (ao, "%s\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\n"
		 , dictName (sx->remapDict, remap)
		 , mp->x1, mp->x2, mp->ln * step
		 , mp->yMax, mp->cover * 1.0/(mp->ln), mp->cover * step
		 , mp->level, mp->level 
		 ) ;
    }


  /* export */

  for (ii = 0, mp = arrp (mmm, ii, MPK) ; ii < imm ; mp++, ii++)
    aceOutf (ao, "%s\t%d\t%d\t%d\t%d\t%.1f\t%d\t%d\t%d\n"
	     , dictName (sx->remapDict, remap)
	     , mp->x1, mp->x2, mp->ln * step
	     , mp->yMax, mp->cover * 1.0/(mp->ln), mp->cover * step
	     , mp->level, mp->level 
	     ) ;

  /* zero terminate */
  aceOutf (ao, "%s\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%d\t%d\n"
	   , dictName (sx->remapDict, remap)
	   , (mp -1)->x2 + 3*step/2, (mp-1)->x2 + 5*step/2, step
	   , 0, 0.0, 0.0
	   , 0, 0
	   ) ;
  ac_free (mmm) ;
  return ;
} /* sxWiggleExportTrnacriptsEnds */

/*************************************************************************************/
/* aa: possibly gaussed wiggle, bb original wiggle */
static void sxWiggleExportPeaks (WIGGLE *sx, Array aa, Array bb, int remap)
{
  WIGGLEPOINT *wp,*wpb, *zp, *zp0 = 0 ;
  int nn = 0,  step = (sx->out_step  ? sx->out_step : 1) ;
  int ii, ii1, jj, jj1, jj2, jj0 = 0, iiMax = aa ? arrayMax (aa) : 0 ;
  int minCover = sx->minCover ;
  int oldx1 = -1, oldx2 = 0, x1 = 0, n ;
  double y, yMax = 0, aliBp = 0, area = 0, cumulatedArea = 0 ;
  ACEOUT ao = sx->aoPeaks ;

  if (sx->multiPeaks)
    return sxWiggleExportMultiPeaks (sx, aa, bb, remap) ;
  if (sx->transcriptsEndsFileName)
    {
      sxWiggleExportTranscriptsEnds (sx, aa, bb, remap) ;
      return ;
    }

  if (! ao)
    ao = sx->aoPeaks = aceOutCreate (sx->outFileName, ".peaks", sx->gzo, sx->h) ;
  if (! ao)
    return ;
  fprintf (stderr, "// Peaks are exported in file %s.peaks\n", sx->outFileName) ;   
  
  aceOut (ao, "#Target\ta1\ta2\tlength bp\tmax cover\taverage cover\taligned bp\n") ;
  for (ii = 0, wpb = bb ? arrp (bb, ii, WIGGLEPOINT) :  arrp (aa, ii, WIGGLEPOINT) ; ii < iiMax ; ii++, wpb++) 
    aliBp += wpb->y ;
  for (ii = ii1 = 0,  wp = arrp (aa, ii, WIGGLEPOINT), wpb = bb ? arrp (bb, ii, WIGGLEPOINT) : 0 ; ii <= iiMax ; ii++, wp++, wpb++) 
    {
      if (ii < iiMax)
	{
	  x1 = wp->x - step ; 
	}
      if (ii == iiMax || (oldx2 > 0 && x1 > oldx2 + 50))
	{
	  if (oldx2 > 0 && area > 1.5 * (oldx2 - oldx1) * minCover)
	    {
	      if (bb) /* squeeze back on the unsmoothed data */
		{
		  zp0 = arrp (bb, jj0, WIGGLEPOINT) ;
		  y =  zp0->y > minCover ? zp0->y : 0 ;
		  area = -y ; yMax = y ; oldx1 = oldx2 = zp0->x ;
		  for (zp = zp0, jj1 = jj0, n = 1 ; n > 0 && jj1 >= 0 ; zp--, jj1--)
		    {
		      if (zp->y >= minCover)
			{ if (n < 4) n++  ; oldx1 = zp->x ; y = zp->y ; area += y ; if (y > yMax) yMax = y ; }
		      else
			n-- ;
		    }
		  for (zp = zp0 , jj2 = jj0, n = 1 ; n > 0 && jj2 < iiMax ; zp++, jj2++)
		    {
		      if (zp->y >= minCover)
			{ if (n < 4) n++  ; oldx2 = zp->x ; y = zp->y ; area += y ; if (y > yMax) yMax = y ; } 
		      else
			n-- ;
		    }
		  for (jj = jj1, zp = arrp (aa, jj, WIGGLEPOINT) ; jj <= jj2 ; zp++, jj++)
		    if (zp->y > 0) 
		      zp->y = -zp->y ;
		  for (jj = jj1, zp = arrp (bb, jj, WIGGLEPOINT) ; jj <= jj2 ; zp++, jj++)
		    zp->y = 0 ;
		}
	      else
		{ oldx1 += step ; oldx2 += step ; }
	      area *= step ;
	      if (area > step)
		{
		  aceOutf (ao, "%s\t%d\t%d\t%d\t%.1f\t%.1f\t%.1f\n"
			   , dictName (sx->remapDict, remap)
			   , oldx1 - step/2, oldx2 + step/2, oldx2 - oldx1 + step + 1
			   , yMax , area/(oldx2 - oldx1 + step + 1), area
			   ) ;
		  cumulatedArea += area ;
		  nn += oldx2 - oldx1 + 1 ;
		  if (bb) 
		    { 
		      ii = ii1 ; 
		      wp = arrp (aa, ii, WIGGLEPOINT) ;
		      wpb = arrp (bb, ii, WIGGLEPOINT) ;
		      x1 = wp->x ;
		    }		
		}  
	    }
	  ii1 = ii ; oldx2 = oldx1 = -1 ; yMax = 0 ; area = 0 ;
	}
      if (ii < iiMax && wp->y >= minCover && (!bb || wpb->y >= minCover))
	{
	  if (oldx1 < 0) oldx1 = x1 ;
	  oldx2 = x1 ;
	  y = wp->y ;
	  if (y > yMax)
	    { yMax = y ; jj0 = ii ; }
	  area += step * y ;
	}
    }
  
  oldx2 = iiMax > 0  ? (arrp(aa,iiMax-1,WIGGLEPOINT))->x : 0 ;
  aceOutf(ao, "#Chromosome %s Length %.3f Mb, Usable %.3f Mb, Covered over %d : %.1f%%, cumulatedArea Mb %g, Aligned Mb %g\n"
	  , dictName (sx->remapDict, remap)
	  , oldx2/1000000.0, nn/1000000.0, minCover, 100.0 * (double)nn/(oldx2 ? oldx2 : 1), cumulatedArea/1000000.0, step*aliBp/1000000.0
	  ) ;

  if (bb)
    for (ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; ii <= iiMax ; ii++, wp++) 
      if (wp->y < 0) wp->y = -wp->y ;
  
  return ;
} /* sxWiggleExportPeaks */

/*************************************************************************************/
/*************************************************************************************/
/* perform the convolution */
static int sxWiggleGaussOne (WIGGLE *sx, Array aa, Array bb)
{
  int nn = 0, ii, jj, dx, iiMin ;
  int stepOut = (sx->out_step  ? sx->out_step : 1) ;
  double sigma = sx->gauss ;
  double s2 = 2*sigma*sigma ; /* precompute 2*sigma^2 and the exponentials */
  double z, wTotal, weight[10000] ;
  WIGGLEPOINT *wp, *zp ;

  if (sigma < 1)
    return 0 ;
  
  memset (weight, 0, sizeof(weight)) ;
  for (wTotal = -1, nn = 0 ; nn < 10000 ; nn++)
    {
      dx = nn * stepOut ;
      z = exp (- dx * dx /s2) ;
      if (z < 1.0/1000000)
	break ;
      weight[nn]  = z ;
      wTotal += 2 * z ;
    }
  for (ii = 0 ; ii < nn ; ii++)
    weight[ii]  /= wTotal ;
  for (wTotal = weight[0], ii = 1 ; ii < nn ; ii++)
    wTotal += 2*weight[ii] ;
 
  for (ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; ii < arrayMax (aa) ; ii++, wp++) 
    if (wp->y > 0) break ;
  iiMin = ii - nn ;
  if (iiMin < 0) iiMin = 0 ;
  wp = arrp (aa, iiMin, WIGGLEPOINT) ;
  ii = arrayMax (aa) - iiMin ;
  if (ii > 0)
    {
      memset (wp, 0, ii * sizeof (WIGGLEPOINT))  ;
      /* equivalent of
       *  for (ii = iiMin, wp = arrp (aa, ii, WIGGLEPOINT) ; ii < arrayMax (aa) ; ii++, wp++) 
       *    wp->y = 0 ;
       */
    }

  for (ii = 0, wp = arrp (aa, 0, WIGGLEPOINT), zp = arrp (bb, 0, WIGGLEPOINT) ; ii < arrayMax (aa) ; ii++, zp++, wp++) 
    {
      if (zp->y > 0)      
	for (jj = 0 ; jj < nn ; jj++)
	  {
	    z = weight[jj] * zp->y ;
	    if (ii - jj >= iiMin)
	      (wp - jj)->y += z ;
	    if (jj && ii + jj < arrayMax(aa))
	      (wp + jj)->y += z ;
	  }
    }

  return arrayMax (aa) ;
} /* sxWiggleGaussOne */

/***********************************************************************/
/* Gauss smooth
 */

int sxWiggleGauss (WIGGLE *sx)
{
  Array aa, bb ;
  int nn = 0, target ;
  
  if (sx->gauss > 1)
    for (target = nn = 0 ; target < arrayMax (sx->aaa) ; target++)
      {
	aa =  arr (sx->aaa, target, Array) ;
	if (arrayExists (aa) && arrayMax (aa))
	  {
	    bb = arrayCopy (aa) ;
	    nn += sxWiggleGaussOne (sx, aa, bb) ;
	    if (sx->peaks)
	      sxWiggleExportPeaks (sx, aa, bb, target) ;
	    arrayDestroy (bb) ;
	  }
      }

  return nn ; /* number of scanned data points */
} /* sxWiggleGauss */

/*************************************************************************************/
/*************************************************************************************/

static void sxWiggleExportOne (WIGGLE *sx, int remap, Array aa, int *limits, long int *cumul, long int *pos_covered)
{
  AC_HANDLE h = ac_new_handle () ;
  ACEOUT ao = sx->ao ;
  WIGGLEPOINT *wp ;
  int ii, stepOut, spanOut, x, oldx = 0, x1, iy, jBuf = 0, nn = 0 ;
  int i, *limitp ;
  int iiMax = arrayMax (aa) ;

  float y, dy ;
  int bufferSize = 32*1024 ;
  char buffer[bufferSize] ;
  int *ip, *jp, iBuffer[7] ;


  stepOut = (sx->out_step  ? sx->out_step : 1) ;
  spanOut = (sx->out_span  ? sx->out_span : 1) ;

  for (ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; ii < arrayMax (aa) ; ii++, wp++) 
    {
      for (i = 0, limitp = limits ; *limitp > -1 ; i++, limitp++)
	if (wp->y >= *limitp)
	  {
	    pos_covered[i] += stepOut ;
	    cumul[i] += stepOut * wp->y ;
	  }
    }

  for (ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; wp->y == 0 && ii < iiMax  ; ii++, wp++) ;
  x1 = (wp)->x ;
  /* export some header */
  switch (sx->out)
    {
    case COUNT:
      goto done ;
      break ;
    case AF:
      iBuffer [0] = x1 ;
      iBuffer [1] = stepOut ;
      iBuffer [2] = spanOut ;
      iBuffer [3] = iy = arrayMax (aa) - ii ;
      iBuffer [4] = sx->trackName ? strlen(sx->trackName) : 0 ;
      iBuffer [5] = sx->title ? strlen(sx->title) : 0 ;
      iBuffer [6] = remap ? strlen (dictName (sx->remapDict, remap)) : 0 ;
      aceOutBinary (ao, (char*)iBuffer, 7 * sizeof (int)) ;      
      if (sx->trackName) aceOutBinary (ao, sx->trackName, strlen(sx->trackName)) ;
      if (sx->title) aceOutBinary (ao, sx->title, strlen(sx->title)) ;
      if (remap) aceOutBinary (ao, dictName (sx->remapDict, remap), strlen (dictName (sx->remapDict, remap))) ;
      jp = ip = halloc (iy * sizeof (int), 0) ;
      for (nn = arrayMax(aa) ; ii < nn ; ii++, wp++, jp++)
	*jp = (wp->y >= sx->minCover && wp->y < sx->maxCover ? wp->y : 0) ;
      nn = jp - ip ;
      aceOutBinary (ao, (char*)ip, iy * sizeof (int)) ;      
      ac_free (ip) ;
      goto done ;
    case AM:
      if (! sx->trackName) messcrash ("-trackName missing in AM export format") ;
      aceOutf (ao, "mRNA %s\n", ac_protect (dictName (sx->remapDict, remap), h)) ; 
      break ;
    case AG:
      if (! sx->trackName) messcrash ("-trackName missing in AG export format") ;
      if (! sx->wiggleFeature) messcrash ("-feature missing in AG export format") ;
      aceOutf (ao, "Sequence %s\n", ac_protect (dictName (sx->remapDict, remap), h)) ; 
      break ;
    case BF:
      aceOutf (ao, "# %s, %s\n", timeShowNow(), sx->title ? sx->title : "") ; 
      aceOutf (ao, "track type=wiggle_0") ;
      if (sx->trackName) aceOutf (ao, " name=\"%s\"", sx->trackName) ;
      if (sx->title) aceOutf (ao, " description=\"%s\"", sx->title) ;
      aceOutf (ao, "\nfixedStep") ;
      if (remap) aceOutf (ao, " chrom=%s", dictName (sx->remapDict, remap)) ;
      aceOutf (ao, " start=%d", x1) ;
      aceOutf (ao, " step=%d", stepOut) ;
      if (spanOut > 1) aceOutf (ao, " span=%d", spanOut) ;
      if (0) aceOutf (ao, " span=%d", stepOut) ;
      aceOutf (ao, "\n") ;
      
      break ;
    case BV:
      aceOutf (ao, "# %s, %s\n", timeShowNow(), sx->title ? sx->title : "") ; 
      aceOutf (ao, "track type=wiggle_0") ;
      if (sx->trackName) aceOutf (ao, " name=\"%s\"", sx->trackName) ;
      if (sx->title) aceOutf (ao, " description=\"%s\"", sx->title) ;
      aceOutf (ao, "\nvariableStep") ;
      if (remap) aceOutf (ao, " chrom=%s", dictName (sx->remapDict, remap)) ;
      if (0) aceOutf (ao, " start=%d", stepOut*x1) ;
      if (0) aceOutf (ao, " step=%d", stepOut) ;
      if (0) aceOutf (ao, " span=%d", spanOut) ;
      aceOutf (ao, "\n") ;
      break ;
    case BG:
      aceOutf (ao, "# %s, %s\n", timeShowNow(), sx->title ? sx->title : "") ; 
      aceOutf (ao, "trackName type=bedGraph") ;
      if (sx->trackName) aceOutf (ao, " name=\"%s\"", sx->trackName) ;
      if (sx->title) aceOutf (ao, " description=\"%s\"", sx->title) ;
      aceOutf (ao, "\n") ;
      break ;      
    case AW:
      oneByteInitialize (FALSE) ;
      sprintf(buffer,"%d%d%d%s\t%s",x1,stepOut, (int)arrayMax (aa) - ii
	      , dictName (sx->remapDict, remap)
	      , sx->trackName ? sx->trackName : "-"
	      ) ;
      if (strlen(buffer + 3 * sizeof(int)) > 1000)
	messcrash ("In AW format the cumulated length of remap + track names\ncannot exceed 1000 characters\n:remap= #%s#\ntrackName = #%s#" 
		   , dictName (sx->remapDict, remap)
		   , sx->trackName ? sx->trackName : "-"
		     ) ;
      aceOutBinary (ao, buffer, 1024) ;
      break ;
    default:
      break ;
    }
  
  for ( nn = oldx = 0 ; ii < arrayMax (aa) ; nn++, ii++, wp++) 
    {
      y = wp->y >= sx->minCover && wp->y < sx->maxCover ? wp->y : 0 ;
      iy = y + .001 ;
      dy = 10 * (y - iy) ;
      switch (sx->out)
	{
	case AM:
	  if (y > 0)
	    {
	      aceOutf (ao, "Wiggle %s %d ", sx->trackName, wp->x) ;
	      if (y > 100 || dy < 1)
		aceOutf (ao, "%d\n", iy) ;
	      else
		aceOutf (ao, "%.2f\n", y) ;
	    }
	  break ;
	case AG:
	  if (y > 0)
	    {
	      aceOutf (ao, "Feature %s %d", sx->wiggleFeature, wp->x) ;
	      for (;  ii < arrayMax (aa) && wp->y == y ;  ii++, wp++) ;
	      ii-- ; wp-- ; /* restore */
	      aceOutf (ao, " %d", wp->x + 1) ;
	      if (y > 100 || dy < 1)
		aceOutf (ao, " %d", iy) ;
	      else
		aceOutf (ao, " %.2f\n", y) ;
	      aceOutf (ao, " %s\n", sx->trackName, wp->x) ;
	    }
	  break ;
	case BF:
	  if (nn)
	    for (x = oldx + stepOut + 1 ; x < wp->x ; x += stepOut)
	      { nn++ ; aceOutf (ao, "0\n") ; }
	  /* export with at most 2 decimals, but if possible as an integer */
	  if (y > 100 || dy < 1)
	    aceOutf (ao, "%d\n", iy) ;
	  else
	    aceOutf (ao, "%.2f\n", y) ;
	  oldx = wp->x ;
	  break ;
	case BV:
	  if (y == 0) continue ;
	  if (y > 100 || dy < 1)
	    aceOutf (ao, "%d\t%d\n", wp->x, iy) ;
	  else
	    aceOutf (ao, "%d\t%.2f\n", wp->x, y) ;
	  break ;
	case BG:
	  if (y > 0)
	    {
	      aceOutf (ao, "%s", dictName (sx->remapDict, remap)) ;
	      aceOutf (ao, "\t%d", wp->x - (stepOut-1)/2) ; /* 0 based */
	    }
	  for (y = (wp->y  >= sx->minCover && wp->y < sx->maxCover ? wp->y : 0) ;  ii < arrayMax (aa) && y == (wp->y >= sx->minCover  && wp->y < sx->maxCover ? wp->y : 0) ;  ii++, wp++) ;
	  ii-- ; wp-- ; /* restore */
	  if (y > 0)
	    { /* wp->x 1-based, so we always know the strand */
	      if (y > 100 || dy < 1)
		aceOutf (ao, "\t%d\t%d\n", wp->x + (stepOut)/2, iy) ;
	      else
		aceOutf (ao, "\t%d\t%.2f\n", wp->x + (stepOut)/2, y) ;
	    }
	  break ;
	case AW:
	  buffer[jBuf++] = oneByteEncode (y) ;
	  if (jBuf == bufferSize)
	    {
	      aceOutBinary (ao, buffer, bufferSize) ;
	      jBuf = 0 ;
	    }
	default:
	  break ;
	}
    }
  
  switch (sx->out)
    {
    case AW:
      if (jBuf > 0)
	aceOutBinary (ao, buffer, jBuf) ;
      break ;
    case AM:
    case AG:
      aceOut (ao, "\n") ;
      break ;
    default:
      break ;
    }
 done:
  if (sx->peaks && ! sx->gauss)
    sxWiggleExportPeaks (sx, aa, 0, remap) ;
  ac_free (h) ;
} /* sxWiggleExportOne */

/*************************************************************************************/

void sxWiggleExport (WIGGLE *sx)
{
  Array aa ;
  int remap ;
  long int cumul[128], pos_covered[128] ;
  int limits[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000, 10000000, -1} ;

  memset (cumul, 0, sizeof(cumul)) ;
  memset (pos_covered, 0, sizeof(pos_covered)) ;

  for (remap = 0 ; remap < arrayMax (sx->aaa) ; remap++)
    {
      aa =  arr (sx->aaa, remap, Array) ;
      if (arrayExists (aa) && arrayMax (aa))
	sxWiggleExportOne (sx, remap, aa, limits, cumul, pos_covered) ;
    }

  if (sx->cumul)
    {
      AC_HANDLE h = ac_new_handle () ;
      ACEOUT ao = aceOutCreate (sx->outFileName, ".cumul", sx->gzo, h) ;
      if (ao)
	{
	  int i, *limitp ;
	  fprintf (stderr, "// Cumuls are in file %s\n", sx->outFileName) ;   
	  for (i = 0, limitp = limits ; *limitp > -1 ; i++, limitp++)
	    if (sx->cumul && cumul[i])
	      aceOutf (ao, "Cumul_at_level_%d\t%ld\tcovering\t%ld\tpositions_average_cover\t%ld\n"
		       , *limitp
		       , cumul[i], pos_covered[i], cumul[i]/pos_covered[i]
		       ) ;
	}
      ac_free (h) ;
    }

} /* sxWiggleExport */

/*************************************************************************************/
/*************************************************************************************/

static void sxWiggleScaleOne (WIGGLE *sx, Array aa, float scale)
{ 
  int ii, iiMax = aa ? arrayMax (aa) : 0 ;
  WIGGLEPOINT *wp ;
  
  if (aa && scale != 0)
    {
      for (ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; ii < iiMax ; ii++, wp++) 
	wp->y *= scale ;
    }
  return ;
} /* sxWiggleScaleOne */

/*************************************************************************************/

void sxWiggleScale (WIGGLE *sx, float scale)
{
  Array aa ;
  int remap ;
  
  for (remap = 0 ; remap < arrayMax (sx->aaa) ; remap++)
    {
      aa =  arr (sx->aaa, remap, Array) ;
      if (arrayExists (aa) && arrayMax (aa))
	sxWiggleScaleOne (sx, aa, scale) ;
    }
} /* sxWiggleScale */

/*************************************************************************************/
/*************************************************************************************/

static void sxWiggleShiftOne (WIGGLE *sx, Array aa, float delta)
{ 
  int ii, iiMax = aa ? arrayMax (aa) : 0 ;
  WIGGLEPOINT *wp ;
  
  if (aa && delta != 0)
    {
      for (ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; ii < iiMax ; ii++, wp++) 
	wp->y += delta ;
    }
  return ;
} /* sxWiggleShiftOne */

/*************************************************************************************/

void sxWiggleShift (WIGGLE *sx, float delta)
{
  Array aa ;
  int remap ;
  
  for (remap = 0 ; remap < arrayMax (sx->aaa) ; remap++)
    {
      aa =  arr (sx->aaa, remap, Array) ;
      if (arrayExists (aa) && arrayMax (aa))
	sxWiggleShiftOne (sx, aa, delta) ;
    }
} /* sxWiggleShift */

/*************************************************************************************/
/*************************************************************************************/

static void sxWiggleFloorOne (WIGGLE *sx, Array aa, float mini)
{ 
  int ii, iiMax = aa ? arrayMax (aa) : 0 ;
  WIGGLEPOINT *wp ;
  
  if (aa)
    {
      for (ii = 0, wp = arrp (aa, ii, WIGGLEPOINT) ; ii < iiMax ; ii++, wp++) 
	if (wp->y < mini) 
	  wp->y = mini ;
    }
  return ;
} /* sxWiggleFloorOne */

/*************************************************************************************/

void sxWiggleFloor (WIGGLE *sx, float mini)
{
  Array aa ;
  int remap ;
  
  for (remap = 0 ; remap < arrayMax (sx->aaa) ; remap++)
    {
      aa =  arr (sx->aaa, remap, Array) ;
      if (arrayExists (aa) && arrayMax (aa))
	sxWiggleFloorOne (sx, aa, mini) ;
    }
} /* sxWiggleFloor */

/*************************************************************************************/
/*************************************************************************************/
/* the 'ratio' 
 * ATTENTION  synchronize with htileSolexaEndRatios 
 */
static void sxWiggleEndRatioOne (WIGGLE *sx, Array aa, Array bb)
{ 
  int ii, iiMax = aa ? arrayMax (aa) : 0 ;
  int jjMax = aa ? arrayMax (bb) : 0 ;
  WIGGLEPOINT *wp, *zp ;
  
  /* if iiMax > jjMax, do not touch the large ii values */
  if (iiMax > jjMax) iiMax = jjMax ;

  if (aa)
    {
      float *rp ;
      Array rr = arrayCreate (iiMax, float) ;
      array (rr, iiMax, float) = 0 ;

      for (ii = 6, wp = arrp (aa, ii, WIGGLEPOINT), zp = arrp (bb, ii, WIGGLEPOINT), rp = arrp (rr,ii, float) ; ii < iiMax - 6 ; ii++, wp++, zp++, rp++) 
	{
	  float x = 0, y = 0, u ;
	  int damper = 10 ;
	  float seuil = .7 ;
	  float zoom = 40 ;

	  /* average over 11 steps (generally 100 bases */
	  for (int j = -5 ; j < 6 ; j++)
	    {
	      x += wp[j].y ;
	      y += zp[j].y ;
	    }
	  x /= 11 ;
	  y /= 11 ;
	  u =  (x + damper) / (x + y + 2 * damper) - seuil ;
	  if (u < 0) u = 0 ;
	  *rp = zoom * u * x ;
	}
    
      for (ii = 0, wp = arrp (aa, ii, WIGGLEPOINT), rp = arrp (rr, ii, float) ; ii < iiMax ; ii++, wp++, rp++) 
	wp->y = *rp ; 
      arrayDestroy (rr) ;
    }
  return ;
} /* sxWiggleEndRatioOne */

/*************************************************************************************/

void sxWiggleEndRatio (WIGGLE *sx)
{
  Array aa, bb ; 
  int remap ;
  
  if (arrayExists (sx->aaa) && arrayExists (sx->aaaCopy) &&
      arrayMax (sx->aaa) == arrayMax (sx->aaaCopy)
      )
    for (remap = 0 ; remap < arrayMax (sx->aaa) ; remap++)
      {
	aa =  arr (sx->aaa, remap, Array) ;
	bb =  arr (sx->aaaCopy, remap, Array) ;
	if (arrayExists (aa) && arrayMax (bb))
	  sxWiggleEndRatioOne (sx, aa, bb) ;
      }
} /* sxWiggleEndRatio */

/*************************************************************************************/
/*************************************************************************************/
/* get a, b  = stranded wiggles, if damper == 10 
 * compute alpha = a + 10 / a + b + 20
 *          beta = b + 10 / a + b + 20
 *    => alpha + beta = 1, so we only need to compute alpha
 * if (a+b < 20) -> -1 (NA)
 */
Array sxWiggleStrandedRatio (WIGGLE *sx, Array aaa, Array bbb, int damper, AC_HANDLE h)
{
  Array aa, bb, cc ; 
  Array alpha ;
  int map, i, j, iMax, jMax, delta ;
  WIGGLEPOINT *wap, *wbp, *wcp ;

  if (damper <= 0)
    messcrash ("Damper = %d, must be strictly positive", damper) ;
  map = arrayMax (aaa) < arrayMax (aaa) ? arrayMax (aaa) : arrayMax (bbb) ;
  alpha = arrayHandleCreate (map+1, Array, h) ;

  for (map = 0 ; map < arrayMax (aaa) ; map++)
    {
      aa =  arr (aaa, map, Array) ;
      iMax = aa ? arrayMax (aa) : 0 ;
      if (! iMax)
	continue ;
      bb = map < arrayMax (bbb) ?  arr (bbb, map, Array) : 0 ;
      jMax = bb ? arrayMax (bb) : 0 ;
      cc =  arrayHandleCreate (arrayMax (aa), WIGGLEPOINT, h) ;
      array (alpha, map, Array) = cc ;
      wcp = arrayp (cc, iMax -1, WIGGLEPOINT) ; /* make room */
      wap = arrp (aa, 0, WIGGLEPOINT) ;
      wbp = bb ? arrp (bb, 0, WIGGLEPOINT) : 0 ;
      delta = (wbp ? wap->x - wbp->x : 0) / sx->out_step ;
      
      for (i = 0, wap = arrp (aa, 0, WIGGLEPOINT), wcp = arrp (cc, 0, WIGGLEPOINT) ; 
	   i < iMax ; i++, wap++, wcp++)
	{
	  float a, b = 0 ;
	  wcp->x = wap->x ;
	  j = i + delta ;
	  wbp = bb && j >= 0 && j < jMax ? arrp (bb, j, WIGGLEPOINT) : 0 ;
	  a = wap->y  + damper ;
	  b = (wbp ? wbp->y : 0)  + damper ;
	  wcp->y = a+b> 20+2*damper ? a / (a+b) : -1 ;
	}
    }

  return alpha ;
} /* sxWiggleStrandedRatio */

/*************************************************************************************/
/*************************************************************************************/
/* At each point of sx->aaa, which contains z = sum of the non stranded wiggle
 * get the corresponding bbb value rho = ratio of the stranded wiggle
 * compute wap->y = rho * z giving the restranded wiggle
 * if ratio = -1 (NA), do not edit the values
 */
void sxWiggleMultiplyLocally (WIGGLE *sx, Array bbb, Array aaa1)
{
  Array aaa = sx->aaa, aa, aa1, bb ;
  int map, i, j, iMax, jMax, delta ;
  WIGGLEPOINT *wap, *wap1, *wbp ;

  for (map = 0 ; map < arrayMax (aaa) ; map++)
    {
      aa =  arr (aaa, map, Array) ;
      iMax = aa ? arrayMax (aa) : 0 ;
      if (! iMax)
	continue ;
      aa1 = aaa1 && map < arrayMax (aaa1) ?  arr (aaa1, map, Array) : 0 ;
      bb = map < arrayMax (bbb) ?  arr (bbb, map, Array) : 0 ;
      jMax = bb ? arrayMax (bb) : 0 ;
      wap1 = aa1 ? arrp (aa1, 0, WIGGLEPOINT) : 0 ;
      wap = arrp (aa, 0, WIGGLEPOINT) ;
      wbp = bb ? arrp (bb, 0, WIGGLEPOINT) : 0 ;
      delta = (wbp ? wap->x - wbp->x : 0) / sx->out_step ;
      
      for (i = 0, wap = arrp (aa, 0, WIGGLEPOINT) ; i < iMax ; i++, wap++)
	{
	  j = i + delta ;
	  wbp = bb && j >= 0 && j < jMax ? arrp (bb, j, WIGGLEPOINT) : 0 ;
	  if (wbp)
	    {
	      if (wbp->y < 0)
		{
		  wap1 = aa1 && i < arrayMax (aa1) ? arrp (aa1, i, WIGGLEPOINT) : 0 ;
		  wap->y = wap1 ? wap1->y : 0 ;
		}
	      else
		wap->y = wap->y * wbp->y ; 
	    }
	  else
	    wap->y = 0 ;
	}
    }

  return ;
} /* sxWiggleMultiplyLocally */

/*************************************************************************************/
/*************************************************************************************/

void sxWiggleCopy (WIGGLE *sx)
{
  sx->aaaCopy = sx->aaa ; sx->aaa = 0 ;
} /* sxWiggleCopy */

/*************************************************************************************/
/*************************************************************************************/

BOOL sxCheckFormat (const char *io, WFORMAT *ip, const char *ccp, char *ftype)
{
  int i ;
  const char **f ;
  const char *ff[] = { "BF", "BV", "BG", "AF", "AM", "AG", "AW", "BHIT", "TABIX", "Count", 0} ;

  for (i = 0 , f = ff ; *f ; i++, f++)
    if (! strcasecmp (*f, ccp))
      { 
	*ip = i ; 
	if (*io == 'O')
	  strcpy (ftype, ff[i]) ;
	return TRUE ;
      }

  return FALSE ;
} /* checkFormat */

/*************************************************************************************/
/***************************** Public interface **************************************/
/*************************************************************************************/

Array sxGetWiggleZone (Array aa, const char *fNam, char *type, int step, const char *chrom, int z1, int z2, AC_HANDLE h0)
{
  AC_HANDLE h = 0 ;
  WIGGLE *sx = 0 ;
  char *rtype = 0 ;
  const char *ccp, *ch ; 

  ccp = filName (fNam, 0, rtype) ;
  if (! ccp)
    return 0 ;
  h = ac_new_handle () ;
  sx = (WIGGLE*) halloc (sizeof(WIGGLE), h) ;

  if (! sxCheckFormat ("I", &(sx->in), type, sx->fileType))
    messcrash ("sxGetWiggle unknown file format %s, should be one of AW BF BV BHIT", type) ;
  rtype = (sx->in == AW ? "rb" : "r") ;
  sx->inFileName = strnew (ccp, h) ;
  sx->out_step = step ;
  {
    ccp = sx->inFileName + strlen(sx->inFileName) - 3 ;
    
    if (chrom && sx->in == TABIX)
      {
	ch = chrom ; 
	if (!strncmp (ch, "CHROMOSOME_", 11))
	  ch += 11 ;
	sx->ai = aceInCreateFromPipe (hprintf (h, "tabix %s %s:%d-%d", sx->inFileName,ch,z1,z2), rtype, 0, h) ;
      }
    else if (! strcmp (ccp, ".gz"))
      sx->ai = aceInCreateFromPipe (hprintf (h, "gunzip -c %s", sx->inFileName), rtype, 0, h) ;
    else
      sx->ai = aceInCreateFromFile (sx->inFileName, rtype, 0, h) ;
  }
  if (! sx->ai)
    return 0 ;

  sx->noRemap = TRUE ;
  sx->aaa = arrayHandleCreate (12, Array, h) ;	
  if (! aa)
    aa = arrayHandleCreate (100000, WIGGLEPOINT, h0) ;
  array (sx->aaa, 0, Array) = aa ; 
  sxWiggleParse (sx, z1, z2) ;

  ac_free (h) ;
  return aa ;
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

