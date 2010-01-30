/*  -*- mode: c; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; c-file-style: "stroustrup"; -*-
 *
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * Gallium Rubidium Oxygen Manganese Argon Carbon Silicon
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <string.h>
#include "typedefs.h"
#include "strdb.h"
#include "string2.h"
#include "smalloc.h"
#include "symtab.h"
#include "index.h"
#include "futil.h"
#include "fflibutil.h"

typedef struct {
  char *res;
  char *atom;
  char *replace;
} t_xlate_atom;

static void *get_xlatoms(const char *fn,FILE *fp,
                         int *nptr,t_xlate_atom **xlptr)
{
    char line[STRLEN];
    char rbuf[1024],abuf[1024],repbuf[1024],dumbuf[1024];
    char *_ptr;
    int  n,na,idum;
    t_xlate_atom *xl;
    
    n  = *nptr;
    xl = *xlptr;

    while (get_a_line(fp,line,STRLEN))
    {
        na = sscanf(line,"%s%s%s",rbuf,abuf,repbuf,dumbuf);
        /* Check if we are reading an old format file with the number of items
         * on the first line.
         */
        if (na == 1 && n == *nptr && sscanf(rbuf,"%d",&idum) == 1)
        {
            continue;
        }
        if (na != 3)
        {
            gmx_fatal(FARGS,"Expected a residue name and two atom names in file '%s', not '%s'",fn,line);
        }
        
        srenew(xl,n+1);
        
        /* Use wildcards... */
        if (strcmp(rbuf,"*") != 0)
        {
            xl[n].res = strdup(rbuf);
        }
        else
        {
            xl[n].res = NULL;
        }
        
        /* Replace underscores in the string by spaces */
        while ((_ptr = strchr(abuf,'_')) != 0)
        {
            *_ptr = ' ';
        }
        
        xl[n].atom = strdup(abuf);
        xl[n].replace = strdup(repbuf);
        n++;
    }

    *nptr  = n;
    *xlptr = xl;
}

static void done_xlatom(int nxlate,t_xlate_atom **xlatom)
{
  int i;
  
  for(i=0; (i<nxlate); i++) {
    if ((*xlatom)[i].res)
      sfree((*xlatom)[i].res);
    if ((*xlatom)[i].atom)
      sfree((*xlatom)[i].atom);
    if ((*xlatom)[i].replace)
      sfree((*xlatom)[i].replace);
  }
  sfree(*xlatom);
  *xlatom = NULL;
}

void rename_atoms(const char *xlfile,const char *ffdir,
                  t_atoms *atoms,t_symtab *symtab,t_aa_names *aan,
                  bool bReorderNum,bool bVerbose)
{
    FILE *fp;
    int nxlate,a,i;
    t_xlate_atom *xlatom;
    int  nf;
    char **f;
    char c,*res,atombuf[32],*ptr0,*ptr1;
    bool bReorderedNum,bRenamed,bMatch;

    nxlate = 0;
    xlatom = NULL;
    if (xlfile != NULL)
    {
        fp = libopen(xlfile);
        get_xlatoms(xlfile,fp,&nxlate,&xlatom);
        fclose(fp);
    }
    else
    {
        nf = fflib_search_file_end(ffdir,".arn",FALSE,&f);
        for(i=0; i<nf; i++)
        {
            fp = fflib_open(f[i]);
            get_xlatoms(xlfile,fp,&nxlate,&xlatom);
            fclose(fp);
            sfree(f[i]);
        }
        sfree(f);
    }

    for(a=0; (a<atoms->nr); a++)
    {
        res = *(atoms->resinfo[atoms->atom[a].resind].name);
        strcpy(atombuf,*(atoms->atomname[a]));
        bReorderedNum = FALSE;
        if (bReorderNum)
        {
            if (isdigit(atombuf[0]))
            {
                c = atombuf[0];
                for (i=0; ((size_t)i<strlen(atombuf)-1); i++)
                {
                    atombuf[i] = atombuf[i+1];
                }
                atombuf[i] = c;
                bReorderedNum = TRUE;
            }
        }
        bRenamed=FALSE;
        for(i=0; (i<nxlate) && !bRenamed; i++) {
            /* Match the residue name */
            bMatch = (xlatom[i].res == NULL ||
                      (strcasecmp("protein",xlatom[i].res) == 0 &&
                       is_protein(aan,res)));
            if (!bMatch)
            {
                ptr0 = res;
                ptr1 = xlatom[i].res;
                while (ptr0[0] != '\0' && ptr1[0] != '\0' &&
                       (ptr0[0] == ptr1[0] || ptr1[0] == '?'))
                {
                    ptr0++;
                    ptr1++;
                }
                bMatch = (ptr0[0] == '\0' && ptr1[0] == '\0');
            }
            if (bMatch && strcmp(atombuf,xlatom[i].atom) == 0)
            {
                /* We have a match. */
                /* Don't free the old atomname, since it might be in the symtab.
                 */
                ptr0 = strdup(xlatom[i].replace);
                if (bVerbose)
                {
                    printf("Renaming atom '%s' in '%s' to '%s'\n",
                           *atoms->atomname[a],res,ptr0);
                }
                atoms->atomname[a] = put_symtab(symtab,ptr0);
                bRenamed = TRUE;
            }
        }
        if (bReorderedNum && !bRenamed)
        {
            atoms->atomname[a] = put_symtab(symtab,atombuf);
        }
    }

    done_xlatom(nxlate,&xlatom);
}

