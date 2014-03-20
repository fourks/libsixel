#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "sixel.h"

/* exported function */
void LibSixel_ImageToSixel(LibSixel_ImagePtr im,
                           LibSixel_OutputContextPtr context);

/* implementation */

typedef struct _SixNode {
    struct _SixNode *next;
    int pal;
    int sx;
    int mx;
    unsigned char *map;
} SixNode;

static SixNode *node_top = NULL;
static SixNode *node_free = NULL;

static int save_pix = 0;
static int save_count = 0;
static char init_palet[PALETTE_MAX];
static int act_palet = (-1);

static long use_palet[PALETTE_MAX];
static unsigned char conv_palet[PALETTE_MAX];

static void PutFlash(LibSixel_OutputContextPtr context)
{
    int n;

#ifdef USE_VT240        // VT240 Max 255 ?
    while ( save_count > 255 ) {
        context->printf("!%d%c", 255, save_pix);
        save_count -= 255;
    }
#endif

    if ( save_count > 3 ) {
        // DECGRI Graphics Repeat Introducer                ! Pn Ch

        context->printf("!%d%c", save_count, save_pix);

    } else {
        for ( n = 0 ; n < save_count ; n++ ) {
            context->putc(save_pix);
        }
    }

    save_pix = 0;
    save_count = 0;
}

static void PutPixel(LibSixel_OutputContextPtr context, int pix)
{
    if ( pix < 0 || pix > 63 )
        pix = 0;

    pix += '?';

    if ( pix == save_pix ) {
        save_count++;
    } else {
        PutFlash(context);
        save_pix = pix;
        save_count = 1;
    }
}

static void PutPalet(LibSixel_OutputContextPtr context,
                     LibSixel_ImagePtr im,
                     int pal)
{
    // DECGCI Graphics Color Introducer                        # Pc ; Pu; Px; Py; Pz

    if ( init_palet[pal] == 0 ) {
        context->printf("#%d;2;%d;%d;%d",
                        conv_palet[pal],
                        (im->red[pal] * 100 + 128) / 256,
                        (im->green[pal] * 100 + 128) / 256,
                        (im->blue[pal] * 100 + 128) / 256);
        init_palet[pal] = 1;

    } else if ( act_palet != pal )
        context->printf("#%d", conv_palet[pal]);

    act_palet = pal;
}

static void PutCr(LibSixel_OutputContextPtr context)
{
    // DECGCR Graphics Carriage Return

    context->puts("$\n");
    // x = 0;
}

static void PutLf(LibSixel_OutputContextPtr context)
{
    // DECGNL Graphics Next Line

    context->puts("-\n");
    // x = 0;
    // y += 6;
}

static void NodeFree()
{
    SixNode *np;

    while ( (np = node_free) != NULL ) {
        node_free = np->next;
        free(np);
    }
}

static void NodeDel(SixNode *np)
{
    SixNode *tp;

    if ( (tp = node_top) == np )
        node_top = np->next;

    else {
        while ( tp->next != NULL ) {
            if ( tp->next == np ) {
                tp->next = np->next;
                break;
            }
            tp = tp->next;
        }
    }

    np->next = node_free;
    node_free = np;
}

static void NodeAdd(int pal,
                    int sx,
                    int mx,
                    unsigned char *map)
{
    SixNode *np, *tp, top;

    if ( (np = node_free) != NULL ) {
        node_free = np->next;
    } else if ( (np = (SixNode *)malloc(sizeof(SixNode))) == NULL ) {
        return;
    }

    np->pal = pal;
    np->sx = sx;
    np->mx = mx;
    np->map = map;

    top.next = node_top;
    tp = &top;

    while ( tp->next != NULL ) {
        if ( np->sx < tp->next->sx ) {
            break;
        } else if ( np->sx == tp->next->sx && np->mx > tp->next->mx ) {
            break;
        }
        tp = tp->next;
    }

    np->next = tp->next;
    tp->next = np;
    node_top = top.next;
}

static void NodeLine(int pal,
                     int width,
                     unsigned char *map)
{
    int sx, mx, n;

    for ( sx = 0 ; sx < width ; sx++ ) {
        if ( map[sx] == 0 )
            continue;

        for ( mx = sx + 1 ; mx < width ; mx++ ) {
            if ( map[mx] != 0 )
                continue;

            for ( n = 1 ; (mx + n) < width ; n++ ) {
                if ( map[mx + n] != 0 )
                    break;
            }

            if ( n >= 10 || (mx + n) >= width )
                break;
            mx = mx + n - 1;
        }

        NodeAdd(pal, sx, mx, map);
        sx = mx - 1;
    }
}

static int PutNode(LibSixel_OutputContextPtr context,
                   LibSixel_ImagePtr im,
                   int x, SixNode *np)
{
    PutPalet(context, im, np->pal);

    for ( ; x < np->sx ; x++ )
        PutPixel(context, 0);

    for ( ; x < np->mx ; x++ )
        PutPixel(context, np->map[x]);

    PutFlash(context);

    return x;
}

static int PalUseCmp(const void *src, const void *dis)
{
    return use_palet[*((unsigned char *)dis)] - use_palet[*((unsigned char *)src)];
}

static int GetColIdx(LibSixel_ImagePtr im, int col)
{
    int i, r, g, b, d;

    int red   = (col & 0xFF0000) >> 16;
    int green = (col & 0x00FF00) >> 8;
    int blue  = col & 0x0000FF;
    int idx   = (-1);
    int min   = 0xFFFFFF;    // 255 * 255 * 3 + 255 * 255 * 9 + 255 * 255 = 845325 = 0x000CE60D

    for ( i = 0 ; i < im->ncolors ; i++ ) {
        if ( i == im->keycolor )
            continue;
        r = im->red[i] - red;
        g = im->green[i] - green;
        b = im->blue[i] - blue;
        d = r * r * 3 + g * g * 9 + b * b;
        if ( min > d ) {
            idx = i;
            min = d;
        }
    }
    return idx;
}

void LibSixel_ImageToSixel(LibSixel_ImagePtr im,
                           LibSixel_OutputContextPtr context)
{
    int x, y, i, n, c;
    int maxPalet;
    int width, height;
    int len, pix, skip;
    int back = (-1);
    unsigned char *map;
    SixNode *np;
    unsigned char list[PALETTE_MAX];

    width  = im->sx;
    height = im->sy;

    maxPalet = im->ncolors;
    back = im->keycolor;
    len = maxPalet * width;

    if ( (map = (unsigned char *)malloc(len)) == NULL )
        return;

    memset(map, 0, len);

    for ( n = 0 ; n < maxPalet ; n++ )
        conv_palet[n] = list[n] = n;

    memset(init_palet, 0, sizeof(init_palet));

    // Pass 1 Palet count

    memset(use_palet, 0, sizeof(use_palet));
    skip = (height / 240) * 6;

    for ( y = i = 0 ; y < height ; y++ ) {
        for ( x = 0 ; x < width ; x++ ) {
            pix = im->pixels[y][x];
            if ( pix >= 0 && pix < maxPalet && pix != back )
                map[pix * width + x] |= (1 << i);
        }

        if ( ++i < 6 && (y + 1) < height )
            continue;

        for ( n = 0 ; n < maxPalet ; n++ )
            NodeLine(n, width, map + n * width);

        for ( x = 0 ; (np = node_top) != NULL ; ) {
            if ( x > np->sx )
                x = 0;

            use_palet[np->pal]++;

            x = np->mx;
            NodeDel(np);
            np = node_top;

            while ( np != NULL ) {
                if ( np->sx < x ) {
                    np = np->next;
                    continue;
                }

                use_palet[np->pal]++;

                x = np->mx;
                NodeDel(np);
                np = node_top;
            }
        }

        i = 0;
        memset(map, 0, len);
        y += skip;
    }

    qsort(list, maxPalet, sizeof(unsigned char), PalUseCmp);

    for ( n = 0 ; n < maxPalet ; n++ ) {
        conv_palet[list[n]] = n;
    }

    /*************
        for ( n = 0 ; n < maxPalet ; n++ )
            fprintf(stderr, "%d %d=%d\n", n, list[n], conv_palet[list[n]]);
    **************/

    context->puts("\033P");
    context->putc('q');
    context->printf("\"1;1;%d;%d", width, height);
    context->putc('\n');

    for ( y = i = 0 ; y < height ; y++ ) {
        for ( x = 0 ; x < width ; x++ ) {
            pix = im->pixels[y][x];
            if ( pix >= 0 && pix < maxPalet && pix != back ) {
                map[pix * width + x] |= (1 << i);
            }
        }

        if ( ++i < 6 && (y + 1) < height ) {
            continue;
        }

        for ( n = 0 ; n < maxPalet ; n++ ) {
            NodeLine(n, width, map + n * width);
        }

        for ( x = 0 ; (np = node_top) != NULL ; ) {
            if ( x > np->sx ) {
                PutCr(context);
                x = 0;
            }

            x = PutNode(context, im, x, np);
            NodeDel(np);
            np = node_top;

            while ( np != NULL ) {
                if ( np->sx < x ) {
                    np = np->next;
                    continue;
                }

                x = PutNode(context, im, x, np);
                NodeDel(np);
                np = node_top;
            }
        }

        PutLf(context);

        i = 0;
        memset(map, 0, len);
    }

    context->puts("\033\\");

    NodeFree();
    free(map);
}

// EOF