#include <ctype.h>
#include <assert.h>
#include "polygon.h"

typedef struct
{
    int before;
    int after;
} CriticalValue; 

void cc_polygon_init(CcPolygon* p)
{
    p->count = 0;
    p->capacity = 0;
    p->points = NULL;
}

void cc_polygon_shutdown(CcPolygon* p)
{
    cc_polygon_clear(p);
}

void cc_polygon_add(CcPolygon* p, CcCoord x)
{   
    int k = p->count;

    if (k + 1 > p->capacity)
    {
        int n = MAX(16, p->capacity * 2);
        p->points = realloc(p->points, sizeof(CcCoord) * n);
        p->capacity = n;
    }
    p->points[k] = x;
    ++p->count;
}

void cc_polygon_clear(CcPolygon* p)
{
    if (p->capacity > 0)
    {
        free(p->points);
        p->capacity = 0;
        p->count = 0;
    }
}

void cc_polygon_update_last(CcPolygon* p, CcCoord x, int align)
{
    int k = p->count - 1;
    if (align)
    {
        assert(p->count >= 2);
        cc_line_align_to_45(p->points[k - 1].x, p->points[k - 1].y, x.x, x.y, &x.x, &x.y);
    }
    else
    {
        assert(p->count >= 1);
    }
    p->points[k] = x;
}

CcRect cc_polygon_rect(const CcPolygon* p)
{
    return cc_rect_around_points(p->points, p->count);
}

void cc_polygon_shift(CcPolygon* p, CcCoord shift)
{
    for (int i = 0; i < p->count; ++i)
    {
        p->points[i].x += shift.x;
        p->points[i].y += shift.y;
    }
}

static
void polygon_critical_values_x_(const CcCoord* points, int n, CriticalValue* out)
{
    assert(n >= 3);
    int i = 0;
    out[i].before = points[n - 1].x - points[i].x;
    out[i].after = points[i + 1].x - points[i].x;
    ++i;

    while (i < (n - 1))
    {
        out[i].before = points[i - 1].x - points[i].x;
        out[i].after = points[i + 1].x - points[i].x;
        ++i;
    }
    out[i].before = points[i - 1].x - points[i].x;
    out[i].after = points[0].x - points[i].x;
    ++i;
}

static
void polygon_critical_values_y_(const CcCoord* points, int n, CriticalValue* out)
{
    assert(n >= 3);
    int i = 0;
    out[i].before = points[n - 1].y - points[i].y;
    out[i].after = points[i + 1].y - points[i].y;
    ++i;

    while (i < (n - 1))
    {
        out[i].before = points[i - 1].y - points[i].y;
        out[i].after = points[i + 1].y - points[i].y;
        ++i;
    }
    out[i].before = points[i - 1].y - points[i].y;
    out[i].after = points[0].y - points[i].y;
    ++i;
}

int remove_axis_colinear_points_(CcCoord* points, CriticalValue* dirs, int n)
{
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        if (dirs[i].before == 0 && dirs[i].after == 0)
        {
            // ignore point
        }
        else
        {
            points[count] = points[i];
            dirs[count] = dirs[i];
            ++count;
        }
    }
    return count;
}

void cc_polygon_remove_duplicates_open(CcPolygon* p)
{
    // TODO:
    assert(0);
    int n = p->count;
    if (n < 3) return;

    CriticalValue* dirs = malloc(sizeof(CriticalValue) * n);
    --n;
    polygon_critical_values_x_(p->points + 1, n, dirs);
    n = remove_axis_colinear_points_(p->points + 1, dirs, n);

    polygon_critical_values_y_(p->points + 1, n, dirs);
    n = remove_axis_colinear_points_(p->points, dirs, n);

    // copy last
    p->points[n] = p->points[p->count -= 1];
    ++n;
    p->count = n;

    free(dirs);
}

void cc_polygon_remove_duplicates_closed(CcPolygon* p)
{
    int n = p->count;
    if (n < 3) return;

    CriticalValue* dirs = malloc(sizeof(CriticalValue) * n);

    polygon_critical_values_x_(p->points, n, dirs);
    n = remove_axis_colinear_points_(p->points, dirs, n);

    polygon_critical_values_y_(p->points, n, dirs);
    n = remove_axis_colinear_points_(p->points, dirs, n);
    p->count = n;

    free(dirs);
}

void cc_polygon_cleanup(CcPolygon* p, int closed)
{
    if (closed)
    {
        cc_polygon_remove_duplicates_closed(p);
    }
    else
    {
        cc_polygon_remove_duplicates_open(p);
    }
}

void cc_bitmap_stroke_polygon(
        CcBitmap* dst,
        const CcCoord* points,
        int n,
        int closed,
        int width,
        uint32_t color
        )
{
    for (int i = 0; i < n - 1; ++i)
    {
        cc_bitmap_interp_square(
                dst,
                points[i].x,
                points[i].y,
                points[i + 1].x,
                points[i + 1].y,
                width,
                color
        );
    }

    if (closed && n > 2)
    {
        cc_bitmap_interp_square(
                dst,
                points[n - 1].x,
                points[n - 1].y,
                points[0].x,
                points[0].y,
                width,
                color
        );
    }
}


// requires: polygon is closed
//           n >= 3
//           polygon is not colinear
//           no duplicate polygon points 
static
int scanline_crossings_(const CcCoord* points, const CriticalValue* y_dirs, int n, int scan_y, int* out_x)
{
    // The key insight is to look at the perspective of the entire polygon.
    // What are all the places this polygon passes the given scan line?
    // https://stackoverflow.com/a/35551300/425756
    assert(n >= 3);
    int crossings = 0;

    for (int i = 0; i < n; ++i)
    {
        CcCoord start = points[i];
        if (start.y == scan_y)
        {
            CriticalValue dir = y_dirs[i];
            // special case: vertex on scanline.

            if (dir.before == 0)
            {
                // end of a horizontal scan line
                // ignore.
                // it will be handled when we get around the loop.
            }
            else if (dir.after == 0)
            {
                // start of a horizontal line
                CriticalValue next_dir = y_dirs[(i + 1) % n];
                assert(next_dir.after != 0);

                if (next_dir.after * dir.before < 0)
                {
                    out_x[crossings] = start.x;
                    ++crossings;
                }
            }
            else if (dir.before * dir.after < 0)
            {
                // signs of y differ. This is a crossing at a vertex.
                out_x[crossings] = start.x;
                ++crossings;
            }
            // signs of y same. This is an extrema, not a crossing.
        }
        else
        {
            // normal case: find the intersection with the line.
            CcCoord end = points[(i + 1) % n];
            if ((start.y < scan_y && scan_y < end.y) 
               || (end.y < scan_y && scan_y < start.y))
            {
                assert(end.y != start.y);
                int inv_m = (end.x - start.x) * 10000 / (end.y - start.y);
                int fy = scan_y - start.y;
                int fx = (fy * inv_m) / 10000;

                out_x[crossings] = start.x + fx;
                ++crossings;
            }
        }
    }
    return crossings;
}

static
int crossing_compare_(const void *a, const void *b)
{
    return *((int*)a) - *((int*)b);
}

void cc_bitmap_fill_polygon(
        CcBitmap* dst,
        const CcCoord* points,
        int n,
        uint32_t color
        )
{
    // Scan line algorithm for arbitrary polygons
    // Overview: https://web.cs.ucdavis.edu/~ma/ECS175_S00/Notes/0411_b.pdf

    if (n < 3) return;

    CriticalValue* y_dirs = malloc(sizeof(CriticalValue) * n);
    polygon_critical_values_y_(points, n, y_dirs);

    CcRect rect = cc_rect_around_points(points, n);
    if (!cc_rect_intersect(rect, cc_bitmap_rect(dst), &rect)) return;

    int* crossings = malloc(sizeof(int) * n);
    for (int y = rect.y; y < rect.y + rect.h; ++y)
    {
        int crossing_count = scanline_crossings_(points, y_dirs, n, y, crossings);
        qsort(crossings, crossing_count, sizeof(int), crossing_compare_);

        /* DEBUG
        for (int i = 0; i < crossing_count; ++i)
        {
            printf("%d ", crossings[i]);
        }
        printf("\n");
        */

        uint32_t* data = dst->data + dst->w * y;

        int i = 0;
        while (i + 1 < crossing_count)
        {
            int start_x = interval_clamp(crossings[i], rect.x, rect.x + rect.w);
            int end_x = interval_clamp(crossings[i + 1], rect.x, rect.x + rect.w);

            for (int j = start_x; j < end_x; ++j) data[j] = color;
            i += 2;
        }
    }

    free(crossings);
    free(y_dirs);
}


