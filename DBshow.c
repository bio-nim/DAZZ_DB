/*******************************************************************************************
 *
 *  Display a specified set of reads of a database in fasta format.
 *
 *  Author:  Gene Myers
 *  Date  :  September 2013
 *  Mod   :  With DB overhaul, made this a routine strictly for printing a selected subset
 *             and created DB2fasta for recreating all the fasta files of a DB
 *  Date  :  April 2014
 *  Mod   :  Added options to display QV streams
 *  Date  :  July 2014
 *
 ********************************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "DB.h"

#ifdef HIDE_FILES
#define PATHSEP "/."
#else
#define PATHSEP "/"
#endif

static char *Usage[] =
    { "[-unqaUQA] [-w<int(80)>] [-m<track>]+",
      "           <path:db|dam> [ <reads:FILE> | <reads:range> ... ]"
    };

#define LAST_READ_SYMBOL   '$'
#define MAX_BUFFER       10001

typedef struct
  { FILE  *input;
    int    lineno;
    int    read;
    int    beg;
    int    end;
  } File_Iterator;

File_Iterator *init_file_iterator(FILE *input)
{ File_Iterator *it;

  it = Malloc(sizeof(File_Iterator),"Allocating file iterator");
  it->input = input;
  it->lineno = 1;
  rewind(input);
  return (it);
}

int next_read(File_Iterator *it)
{ static char nbuffer[MAX_BUFFER];

  char *eol;
  int   x;

  if (fgets(nbuffer,MAX_BUFFER,it->input) == NULL)
    { if (feof(it->input))
        return (1);
      SYSTEM_ERROR;
    }
  if ((eol = index(nbuffer,'\n')) == NULL)
    { fprintf(stderr,"%s: Line %d in read list is longer than %d chars!\n",
                     Prog_Name,it->lineno,MAX_BUFFER-1);
      return (1);
    }
  *eol = '\0';
  x = sscanf(nbuffer," %d %d %d",&(it->read),&(it->beg),&(it->end));
  if (x == 1)
    it->beg = -1;
  else if (x != 3)
    { fprintf(stderr,"%s: Line %d of read list is improperly formatted\n",Prog_Name,it->lineno);
      return (1);
    }
  it->lineno += 1;
  return (0);
}

int main(int argc, char *argv[])
{ HITS_DB    _db, *db = &_db;
  FILE       *hdrs = NULL;

  int         nfiles;
  char      **flist = NULL;
  int        *findx = NULL;

  int            reps, *pts;
  int            input_pts;
  File_Iterator *iter = NULL;
  FILE          *input;

  int         TRIM, UPPER;
  int         DOSEQ, DOQVS, DOARR, QUIVA, ARROW, DAM;
  int         WIDTH;

  int         MMAX, MTOP;
  char      **MASK;

  //  Process arguments

  { int  i, j, k;
    int  flags[128];
    char *eptr;

    ARG_INIT("DBshow")

    WIDTH = 80;
    MTOP  = 0;
    MMAX  = 10;
    MASK  = (char **) Malloc(MMAX*sizeof(char *),"Allocating mask track array");
    if (MASK == NULL)
      exit (1);

    j = 1;
    for (i = 1; i < argc; i++)
      if (argv[i][0] == '-')
        switch (argv[i][1])
        { default:
            ARG_FLAGS("unqaUQA")
            break;
          case 'w':
            ARG_NON_NEGATIVE(WIDTH,"Line width")
            break;
          case 'm':
            if (MTOP >= MMAX)
              { MMAX = 1.2*MTOP + 10;
                MASK = (char **) Realloc(MASK,MMAX*sizeof(char *),"Reallocating mask track array");
                if (MASK == NULL)
                  exit (1);
              }
            MASK[MTOP++] = argv[i]+2;
            break;
        }
      else
        argv[j++] = argv[i];
    argc = j;

    DAM   = 0;
    TRIM  = 1-flags['u'];
    UPPER = 1+flags['U'];
    DOQVS = flags['q'];
    DOARR = flags['a'];
    DOSEQ = 1-flags['n'];
    QUIVA = flags['Q'];
    ARROW = flags['A'];
    if ((QUIVA || DOQVS) && (ARROW || DOARR))
      { fprintf(stderr,"%s: Cannot request both Quiver (-Q,-q) and Arrow (-A,a) information\n",
                       Prog_Name);
        exit (1);
      }

    if (QUIVA)
      { DOQVS = 1;
        DOSEQ = 0;
        MTOP  = 0;
      }
    if (ARROW)
      { DOARR = 1;
        DOSEQ = 0;
        MTOP  = 0;
      }

    if (argc <= 1)
      { fprintf(stderr,"Usage: %s %s\n",Prog_Name,Usage[0]);
        fprintf(stderr,"       %*s %s\n",(int) strlen(Prog_Name),"",Usage[1]);
        exit (1);
      }
  }

  //  Open DB or DAM, and if a DAM open also .hdr file

  { char *pwd, *root;
    int   status;

    status = Open_DB(argv[1],db);
    if (status < 0)
      exit (1);
    if (status == 1)
      { root   = Root(argv[1],".dam");
        pwd    = PathTo(argv[1]);

        if (db->part > 0)
          *rindex(root,'.') = '\0';
        hdrs = Fopen(Catenate(pwd,PATHSEP,root,".hdr"),"r");
        if (hdrs == NULL)
          exit (1);
        DAM = 1;
        if (DOQVS || DOARR)
          { fprintf(stderr,"%s: -q, a, Q and A options not compatible with a .dam DB\n",Prog_Name);
            exit (1);
          }

        free(root);
        free(pwd);
      }

    if (DOQVS)
      { if (db->reads[0].coff < 0 || (db->allarr & DB_ARROW) != 0)
          { fprintf(stderr,"%s: -q or Q option but no Quiver data in DB!\n",Prog_Name);
            exit (1);
          }
      }
    if (DOARR)
      { if ((db->allarr & DB_ARROW) == 0)
          { fprintf(stderr,"%s: -a or A option but no Arrow data in DB!\n",Prog_Name);
            exit (1);
          }
      }
  }

  //  Load QVs if requested

  if (DOQVS)
    { if (Load_QVs(db) < 0)
        { fprintf(stderr,"%s: QVs requested, but no .qvs for data base\n",Prog_Name);
          exit (1);
        }
    }

  //  Check tracks and load tracks for untrimmed DB

  { int i, status, kind;

    for (i = 0; i < MTOP; i++)
      { status = Check_Track(db,MASK[i],&kind);
        if (status == -2)
          printf("%s: Warning: -m%s option given but no track found.\n",Prog_Name,MASK[i]);
        else if (status == -1)
          printf("%s: Warning: %s track not sync'd with db.\n",Prog_Name,MASK[i]);
        else if (kind != MASK_TRACK)
          printf("%s: Warning: %s track is not a mask track.\n",Prog_Name,MASK[i]);
        else if (status == 0)
          Load_Track(db,MASK[i]);
        else if (status == 1 && !TRIM)
          printf("%s: Warning: %s track is for a trimmed db but -u is set.\n",Prog_Name,MASK[i]);
      }
  }

  //  If not a DAM then get prolog names and index ranges from the .db file 

  if (!DAM)
    { char *pwd, *root;
      FILE *dstub;
      int   i;

      root   = Root(argv[1],".db");
      pwd    = PathTo(argv[1]);
      if (db->part > 0)
        *rindex(root,'.') = '\0';
      dstub  = Fopen(Catenate(pwd,"/",root,".db"),"r");
      if (dstub == NULL)
        exit (1);
      free(pwd);
      free(root);

      if (fscanf(dstub,DB_NFILE,&nfiles) != 1)
        SYSTEM_ERROR

      flist = (char **) Malloc(sizeof(char *)*nfiles,"Allocating file list");
      findx = (int *) Malloc(sizeof(int *)*(nfiles+1),"Allocating file index");
      if (flist == NULL || findx == NULL)
        exit (1);

      findx += 1;
      findx[-1] = 0;

      for (i = 0; i < nfiles; i++)
        { char prolog[MAX_NAME], fname[MAX_NAME];
  
          if (fscanf(dstub,DB_FDATA,findx+i,fname,prolog) != 3)
            SYSTEM_ERROR
          if ((flist[i] = Strdup(prolog,"Adding to file list")) == NULL)
            exit (1);
        }

      fclose(dstub);

      //  If TRIM (the default) then "trim" prolog ranges and the DB

      if (TRIM)
        { int        nid, oid, lid;
          int        cutoff, allflag;
          HITS_READ *reads;

          reads  = db->reads - db->ufirst;
          cutoff = db->cutoff;
          if ((db->allarr & DB_ALL) != 0)
            allflag = 0;
          else
            allflag = DB_BEST;
 
          nid = 0;
          oid = db->ufirst;
          lid = oid + db->nreads;
          for (i = 0; i < nfiles; i++)
            { while (oid < findx[i] && oid < lid)
                { if ((reads[oid].flags & DB_BEST) >= allflag && reads[oid].rlen >= cutoff)
                    nid++;
                  oid += 1;
                }
              findx[i] = nid;
            }
        }

      else if (db->part > 0)
        { for (i = 0; i < nfiles; i++)
            findx[i] -= db->ufirst;
        }
    }

  if (TRIM)
    { int i, status, kind;

      Trim_DB(db);

      //  Load tracks for trimmed DB

      for (i = 0; i < MTOP; i++)
        { status = Check_Track(db,MASK[i],&kind);
          if (status < 0)
            continue;
          else if (status == 1 && kind == MASK_TRACK)
            Load_Track(db,MASK[i]);
        }
    }

  //  Process read index arguments into a list of read ranges

  input_pts = 0;
  if (argc == 3)
    { if (argv[2][0] != LAST_READ_SYMBOL || argv[2][1] != '\0')
        { char *eptr, *fptr;
          int   b, e;

          b = strtol(argv[2],&eptr,10);
          if (eptr > argv[2] && b > 0)
            { if (*eptr == '-')
                { if (eptr[1] != LAST_READ_SYMBOL || eptr[2] != '\0')
                    { e = strtol(eptr+1,&fptr,10);
                      input_pts = (fptr <= eptr+1 || *fptr != '\0' || e <= 0);
                    }
                }
              else
                input_pts = (*eptr != '\0');
            }
          else
            input_pts = 1;
        }
    }

  if (input_pts)
    { input = Fopen(argv[2],"r");
      if (input == NULL)
        exit (1);

      iter = init_file_iterator(input);
    }
  else
    { pts  = (int *) Malloc(sizeof(int)*2*(argc-1),"Allocating read parameters");
      if (pts == NULL)
        exit (1);

      reps = 0;
      if (argc > 2)
        { int   c, b, e;
          char *eptr, *fptr;

          for (c = 2; c < argc; c++)
            { if (argv[c][0] == LAST_READ_SYMBOL)
                { b = db->nreads;
                  eptr = argv[c]+1;
                }
              else
                b = strtol(argv[c],&eptr,10);
              if (eptr > argv[c])
                { if (b <= 0)
                    { fprintf(stderr,"%s: %d is not a valid index\n",Prog_Name,b);
                      exit (1);
                    }
                  if (*eptr == 0)
                    { pts[reps++] = b;
                      pts[reps++] = b;
                      continue;
                    }
                  else if (*eptr == '-')
                    { if (eptr[1] == LAST_READ_SYMBOL)
                        { e = db->nreads;
                          fptr = eptr+2;
                        }
                      else
                        e = strtol(eptr+1,&fptr,10);
                      if (fptr > eptr+1 && *fptr == 0 && e > 0)
                        { pts[reps++] = b;
                          pts[reps++] = e;
                          if (b > e)
                            { fprintf(stderr,"%s: Empty range '%s'\n",Prog_Name,argv[c]);
                              exit (1);
                            }
                          continue;
                        }
                    }
                }
              fprintf(stderr,"%s: argument '%s' is not an integer range\n",Prog_Name,argv[c]);
              exit (1);
            }
        }
      else
        { pts[reps++] = 1;
          pts[reps++] = db->nreads;
        }
    }

  //  Display each read (and/or QV streams) in the active DB according to the
  //    range pairs in pts[0..reps) and according to the display options.

  { HITS_READ  *reads;
    HITS_TRACK *first;
    char       *read, *arrow, **entry;
    int         c, b, e, i;
    int         hilight, substr;
    int         map;
    int       (*iscase)(int);

    read  = New_Read_Buffer(db);
    if (DOQVS)
      { entry = New_QV_Buffer(db);
        first = db->tracks->next;
      }
    else
      { entry = NULL;
        first = db->tracks;
      }
    if (DOARR)
      arrow = New_Read_Buffer(db);
    else
      arrow = NULL;

    if (UPPER == 1)
      { hilight = 'A'-'a';
        iscase  = islower;
      }
    else
      { hilight = 'a'-'A';
        iscase  = isupper;
      }

    map    = 0;
    reads  = db->reads;
    substr = 0;

    c = 0;
    while (1)
      { if (input_pts)
          { if (next_read(iter))
              break;
            e = iter->read;
            b = e-1;
            substr = (iter->beg >= 0);
          }
        else
          { if (c >= reps)
              break;
            b = pts[c]-1;
            e = pts[c+1];
            if (e > db->nreads)
              e = db->nreads;
            c += 2;
          }

        for (i = b; i < e; i++)
          { int         len;
            int         fst, lst;
            int         flags, qv;
            float       snr[4];
            HITS_READ  *r;
            HITS_TRACK *track;

            r   = reads + i;
            len = r->rlen;
            if (substr)
              { fst = iter->beg;
                lst = iter->end;
              }
            else
              { fst = 0;
                lst = len;
              }

            flags = r->flags;
            qv    = (flags & DB_QV);
            if (DOARR)
              { uint64 big;
                int    j;

                big   = *((uint64 *) &(r->coff));
                for (j = 0; j < 4; j++)
                  { snr[3-j] = (big & 0xffff) / 100.;
                    big    >>= 16;
                  }
              }
            if (DAM)
              { char header[MAX_NAME];

                fseeko(hdrs,r->coff,SEEK_SET);
                fgets(header,MAX_NAME,hdrs);
                header[strlen(header)-1] = '\0';
                printf("%s :: Contig %d[%d,%d]",header,r->origin,r->fpulse+fst,r->fpulse+lst);
              }
            else
              { while (i < findx[map-1])
                  map -= 1;
                while (i >= findx[map])
                  map += 1;
                if (QUIVA)
                  printf("@%s/%d/%d_%d",flist[map],r->origin,r->fpulse+fst,r->fpulse+lst);
                else if (ARROW)
                  printf(">%s",flist[map]);
                else
                  printf(">%s/%d/%d_%d",flist[map],r->origin,r->fpulse+fst,r->fpulse+lst);
                if (qv > 0)
                  printf(" RQ=0.%3d",qv);
                if (DOARR)
                  printf(" SN=%.2f,%.2f,%.2f,%.2f",snr[0],snr[1],snr[2],snr[3]);
              }
            printf("\n");

            if (DOQVS)
              Load_QVentry(db,i,entry,UPPER);
            if (DOSEQ)
              Load_Read(db,i,read,UPPER);
            if (DOARR)
              Load_Arrow(db,i,arrow,1);

            for (track = first; track != NULL; track = track->next)
              { int64 *anno;
                int   *data;
                int64  s, f, j;
                int    bd, ed, m;

                anno = (int64 *) track->anno;
                data = (int *) track->data;

                s = (anno[i] >> 2);
                f = (anno[i+1] >> 2);
                if (s < f)
                  { for (j = s; j < f; j += 2)
                      { bd = data[j];
                        ed = data[j+1];
                        if (DOSEQ)
                          for (m = bd; m < ed; m++)
                            if (iscase(read[m]))
                              read[m] = (char) (read[m] + hilight);
                        if (j == s)
                          printf("> %s:",track->name);
                        printf(" [%d,%d]",bd,ed);
                      }
                    printf("\n");
                  }
              }

            if (QUIVA)
              { int k;

                for (k = 0; k < 5; k++)
                  printf("%.*s\n",lst-fst,entry[k]+fst);
              }
            else if (ARROW)
              { int k;
    
                for (k = fst; k+WIDTH < lst; k += WIDTH)
                  printf("%.*s\n",WIDTH,arrow+k);
                if (k < lst)
                  printf("%.*s\n",lst-k,arrow+k);
              }
            else
              { if (DOQVS)
                  { int j, k;

                    printf("\n");
                    for (j = fst; j+WIDTH < lst; j += WIDTH)
                      { if (DOSEQ)
                          printf("%.*s\n",WIDTH,read+j);
                        for (k = 0; k < 5; k++)
                          printf("%.*s\n",WIDTH,entry[k]+j);
                        printf("\n");
                      }
                    if (j < lst)
                      { if (DOSEQ)
                          printf("%.*s\n",lst-j,read+j);
                        for (k = 0; k < 5; k++)
                          printf("%.*s\n",lst-j,entry[k]+j);
                        printf("\n");
                      }
                  }
                else if (DOARR)
                  { int j;

                    printf("\n");
                    for (j = fst; j+WIDTH < lst; j += WIDTH)
                      { if (DOSEQ)
                          printf("%.*s\n",WIDTH,read+j);
                        printf("%.*s\n\n",WIDTH,arrow+j);
                      }
                    if (j < lst)
                      { if (DOSEQ)
                          printf("%.*s\n",lst-j,read+j);
                        printf("%.*s\n\n",lst-j,arrow+j);
                      }
                  }
                else if (DOSEQ)
                  { int j;
    
                    for (j = fst; j+WIDTH < lst; j += WIDTH)
                      printf("%.*s\n",WIDTH,read+j);
                    if (j < lst)
                      printf("%.*s\n",lst-j,read+j);
                  }
              }
          }
      }
  }

  if (input_pts)
    { fclose(input);
      free(iter);
    }
  else
    free(pts);

  if (DAM)
    fclose(hdrs);
  else
    { int i;

      for (i = 0; i < nfiles; i++)
        free(flist[i]);
      free(flist);
      free(findx-1);
    }
  Close_DB(db);

  exit (0);
}
