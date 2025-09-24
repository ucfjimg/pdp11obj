#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct object {
    uint8_t *data;
    size_t len;
};

#define WORD(o, i)   (*(uint16_t*)((o)->data+i))

static char *rectypes[] = 
{
    NULL,
    "GSD",
    "ENDGSD",
    "TXT",
    "RLD",
    "ISD",
    "ENDMOD",
    "LIB",
    "LIBEND",
};

static int nrectypes = sizeof(rectypes) / sizeof(rectypes[0]);

struct flag {
    char *on;
    char *off;
};

typedef struct flag flags[8];

#define REC_GSD         1
#define REC_ENDGSD      2
#define REC_TXT         3
#define REC_RLD         4
#define REC_ISD         5
#define REC_ENDMOD      6
#define REC_LIB         7
#define REC_LIBEND      8


static struct object *read_obj(char *fn);
static void rad50_symbol(char *sym, struct object *obj, size_t offset);
static void print_flags(uint8_t fl, flags defs);
static void gsd(struct object *obj, size_t offset, size_t len);
static void text(struct object *obj, size_t offset, size_t len);


int main(int argc, char *argv[]) 
{
    if (argc != 2) {
        fprintf(stderr, "pdp11obj: object-filename\n");
        return 1;
    }

    struct object *obj = read_obj(argv[1]);
    if (!obj) {
        return 1;
    }

    size_t i = 0;

    //
    // Every block starts with 000001. Every block contains at least 7 bytes:
    //
    // +0  word 000001
    // +2  word length of block
    // +4  word type of block
    // ...
    //     byte checksum of block
    //
    while (i < obj->len - 1 && WORD(obj, i) == 1) {
        size_t start = i;
       
        if (i > obj->len - 6) {
            fprintf(stderr, "?OBJ - Block at %06lo has a truncated header.\n", start);
            break;
        }

        uint16_t blklen = WORD(obj, i + 2);
        uint16_t blktyp = WORD(obj, i + 4);

        if (start + blklen + 1 > obj->len) {
            fprintf(stderr, "?OBJ - Block at %06lo is truncated.\n", start);
            break;
        }

        printf("%06lo | Length %06o Type %06o", start, blklen, blktyp);

        if (blktyp < nrectypes && rectypes[blktyp]) {
            printf(" %s", rectypes[blktyp]);
        }
        printf("\n");

        i += blklen + 1;

        uint8_t chksum = 0;
        for (size_t j = 0; j < blklen + 1; j++) {
            chksum += obj->data[start+j];
        }

        if (chksum) {
            printf("?OBJ - Block has an incorrect checksum.\n");
        }

        switch (blktyp) {
        case REC_GSD:
            gsd(obj, start + 6, blklen - 6);
            break;

        case REC_TXT:
            text(obj, start + 6, blklen - 6);
            break;
        }

    }


    return 0;
}


struct object *read_obj(char *fn)
{
    FILE *fp = fopen(fn, "rb");
    if (!fp) {
        perror(fn);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    if (len == -1) {
        fprintf(stderr, "could not determine length of %s.\n", fn);
        fclose(fp);
        return NULL;
    }

    uint8_t *data = malloc(len);
    struct object *obj = malloc(sizeof(struct object));

    if (data == NULL || obj == NULL) {
        fprintf(stderr, "out of memory.\n");
        fclose(fp);
        free(data);
        free(obj);
        return NULL;
    }

    fseek(fp, 0, SEEK_SET);
    if (fread(data, 1, len, fp) != len) {
        fprintf(stderr, "could not read %s.\n", fn);
        fclose(fp);
        free(data);
        free(obj);
        return NULL;
    }

    obj->len = len;
    obj->data = data;
}

/*
 * Read a 2-word symbol name and convert it from RAD50 to ASCII
 * in `sym`. The caller is responsible for `offset` being valid.
 */
void rad50_symbol(char *sym, struct object *obj, size_t offset)
{
    static const char xlat[40] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";


    int s = 0;
    
    for (int i = 0; i < 2; i++) {
        uint16_t w = WORD(obj, offset + 2*i);

        for (int j = 0; j < 3; j++) {
            sym[s + 2 - j] = xlat[w % 40];
            w /= 40;
        }

        s += 3;
    }

    sym[s] = '\0';
}

void print_flags(uint8_t fl, flags defs)
{
    int first = 1;

    for (int i = 7; i >= 0; i--) {
        char *s = (fl & (1 << i)) ? defs[i].on : defs[i].off;

        if (s) {
            if (!first) {
                printf("+");
            }
            printf("%s", s);
            first = 0;
        }
    }
}

#define GSD_MODNAME     0
#define GSD_CSECT       1
#define GSD_INTSYM      2
#define GSD_TRANSFER    3
#define GSD_GBLSYM      4
#define GSD_PSECT       5
#define GSD_IDENT       6
#define GSD_VSECT       7

static flags gblsym_flags = {
    //              1      0
    /* bit 0 */ { "WEAK", NULL  },
    /* bit 1 */ { NULL,   NULL  },
    /* bit 2 */ { NULL,   NULL  },
    /* bit 3 */ { "DEF",  "REF" },
    /* bit 4 */ { NULL,   NULL  },
    /* bit 5 */ { "REL",  "ABS" },
    /* bit 6 */ { NULL,   NULL  },
    /* bit 7 */ { NULL,   NULL  },
};

static flags psect_flags = {
    //              1      0
    /* bit 0 */ { "SAV",  NULL  },
    /* bit 1 */ { NULL,   NULL  },
    /* bit 2 */ { "OVR",  "CON" },
    /* bit 3 */ { NULL,   NULL  },
    /* bit 4 */ { "R/O",  "R/W" },
    /* bit 5 */ { "REL",  "ABS" },
    /* bit 6 */ { "GBL",  "LCL" },
    /* bit 7 */ { "D",    "I"   },
};


void gsd(struct object *obj, size_t offset, size_t len)
{
    char sym[7];
    size_t end = offset + len;

    while (offset < end) {
        if (end - offset < 6) {
            printf("?OBJ - GSD record is truncated.\n");
            break;
        }

        rad50_symbol(sym, obj, offset);

        int type = obj->data[offset + 5];
        int flags = obj->data[offset + 4];


        switch (type) {
            case GSD_MODNAME:
                printf("%06lo |  GSD Module Name [%s]\n", offset, sym);
                break;

            case GSD_CSECT:
                printf("%06lo |  GSD CSECT [%s] Maximum Length %06o\n", offset, sym, WORD(obj, offset + 6));
                break;

            case GSD_INTSYM:
                printf("%06lo |  GSD Internal Symbol [%s]\n", offset, sym);
                break;

            case GSD_TRANSFER:
                printf("%06lo |  GSD Transfer Address [%s]+%06o\n", offset, sym, WORD(obj, offset + 6));
                break;

            case GSD_GBLSYM:
                printf("%06lo |  GSD Global Symbol [%s] ", offset, sym);
                print_flags(flags, gblsym_flags);
                printf("\n");
                break;

            case GSD_PSECT:
                printf("%06lo |  GSD PSECT [%s] Maximum Length %06o ", offset, sym, WORD(obj, offset + 6));
                print_flags(flags, psect_flags);
                printf("\n");
                break;

            case GSD_IDENT:
                printf("%06lo |  GSD Program Version [%s]\n", offset, sym);
                break;


            case GSD_VSECT:
                printf("%06lo |  Mapped Array [%s] Length %06o\n", offset, sym, WORD(obj, offset + 6));
                break;

            default:
                printf("%06lo |  Unknown GSD Record [%s] Type %03o\n", offset, sym, type);
                break;
        }

        offset += 8;
    }
}

void text(struct object *obj, size_t offset, size_t len)
{
    if (len < 2) {
        printf("?OBJ - This TXT record is truncated.\n");
        return;
    }

    uint16_t load = WORD(obj, offset);
    
    printf("%06lo |  Load Address %06o\n", offset, load);

    offset += 2;
    len -= 2;

    for (size_t i = 0; i < len; i += 2) {
        if (i % 16 == 0) {
            if (i) {
                printf("\n");
            }
            printf("%06lo |  ", offset + i);
        } 

        if (len - i >= 2) {
            printf("%06o ", WORD(obj, offset + i));
        }
    } 

    printf("\n");
}
