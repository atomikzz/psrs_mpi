#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

/*
 * HELPERS
 */
static int compare_int(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}
static int append_metrics_csv(const char *path, int n, int p, double psrs_time, double gather_time, double io_time, int correct)
{
    int run_id = 1;

    /* First, determine next run_id by scanning existing file */
    FILE *fr = fopen(path, "r");
    if (fr)
    {
        char line[4096];
        int last_id = 0;

        while (fgets(line, sizeof(line), fr))
        {
            int id;
            /* parse first column: run_id */
            if (sscanf(line, "%d,", &id) == 1)
                last_id = id;
        }
        fclose(fr);
        run_id = last_id + 1;
    }

    /* Now append */
    FILE *f = fopen(path, "a");
    if (!f)
        return -1;

    /* write header if file is empty */
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0)
    {
        fprintf(f,
                "run_id,n,p,psrs_time,gather_time,io_time,correct\n");
    }

    fprintf(f, "%d,%d,%d,%.6f,%.6f,%.6f,%d\n",
            run_id, n, p,
            psrs_time, gather_time, io_time, correct);

    fclose(f);
    return 0;
}

/* returns first index i in [lo, hi) such that a[i] > x */
static int upper_bound_int(const int *a, int lo, int hi, int pivot)
{
    while (lo < hi)
    {
        int midpoint = lo + (hi - lo) / 2;
        if (a[midpoint] <= pivot)
            lo = midpoint + 1;
        else
            hi = midpoint;
    }
    return lo;
}

/* builds displacements from counts  returns total count */
static int make_displs(const int *counts, int *displs, int p)
{
    displs[0] = 0;
    for (int i = 1; i < p; ++i)
        displs[i] = displs[i - 1] + counts[i - 1];
    return displs[p - 1] + counts[p - 1];
}

/* integer parser: reads signed ints separated by anything non numeric */
static int read_ints_text(const char *path, int **out, int *out_n, int max_n /* -1 = no limit */)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    size_t cap = 1 << 20; /* start with ~1M ints (4MB) */
    int *a = (int *)malloc(cap * sizeof(int));
    if (!a)
    {
        fclose(f);
        return -2;
    }

    int c;
    long sign = 1;
    long val = 0;
    int in_num = 0;
    int count = 0;

    while ((c = fgetc(f)) != EOF)
    {
        if (!in_num)
        {
            if (c == '-')
            {
                sign = -1;
                val = 0;
                in_num = 1;
            }
            else if (c >= '0' && c <= '9')
            {
                sign = 1;
                val = (c - '0');
                in_num = 1;
            }
            else
                continue; /* delimiter */
        }
        else
        {
            if (c >= '0' && c <= '9')
            {
                val = val * 10 + (c - '0');
                if (val > (long)INT_MAX + 1L)
                { /* allow INT_MIN */
                    free(a);
                    fclose(f);
                    return -3;
                }
            }
            else
            {
                long x = sign * val;
                if (x < INT_MIN || x > INT_MAX)
                {
                    free(a);
                    fclose(f);
                    return -3;
                }

                if (max_n >= 0 && count >= max_n)
                    break;

                if ((size_t)count == cap)
                {
                    cap *= 2;
                    int *tmp = (int *)realloc(a, cap * sizeof(int));
                    if (!tmp)
                    {
                        free(a);
                        fclose(f);
                        return -2;
                    }
                    a = tmp;
                }
                a[count++] = (int)x;

                in_num = 0;
                sign = 1;
                val = 0;
            }
        }
    }

    /* flush last number if file ended mid-number */
    if (in_num)
    {
        long x = sign * val;
        if (x < INT_MIN || x > INT_MAX)
        {
            free(a);
            fclose(f);
            return -3;
        }
        if (max_n < 0 || count < max_n)
        {
            if ((size_t)count == cap)
            {
                cap *= 2;
                int *tmp = (int *)realloc(a, cap * sizeof(int));
                if (!tmp)
                {
                    free(a);
                    fclose(f);
                    return -2;
                }
                a = tmp;
            }
            a[count++] = (int)x;
        }
    }

    fclose(f);

    /* shrink */
    if (count == 0)
    {
        free(a);
        *out = NULL;
        *out_n = 0;
        return 0;
    }
    int *shrunk = (int *)realloc(a, (size_t)count * sizeof(int));
    if (shrunk)
        a = shrunk;

    *out = a;
    *out_n = count;
    return 0;
}

static int write_csv(const char *path, const int *a, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    for (int i = 0; i < n; ++i)
    {
        if (i)
            fputc(',', f);
        fprintf(f, "%d", a[i]);
    }
    fputc('\n', f);

    if (fclose(f) != 0)
        return -2;
    return 0;
}

/* simple CLI: --n N --in path --out path */
static void parse_args(int argc, char **argv, int rank, int *n,
                       const char **in_path,
                       const char **out_path,
                       const char **metrics_path)

{
    *n = -1;
    *in_path = NULL;
    *out_path = NULL;
    *metrics_path = NULL;

    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--n") && i + 1 < argc)
        {
            *n = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--in") && i + 1 < argc)
        {
            *in_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
        {
            *out_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--metrics") && i + 1 < argc)
        {
            *metrics_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--help"))
        {
            if (rank == 0)
            {
                fprintf(stderr,
                        "Usage:\n"
                        "  mpirun -np P ./a.out [--n N] [--in input.txt] [--out output.csv] [--metrics metrics.csv]\n"
                        "\n"
                        "If --in is given: reads ints from file (CSV/whitespace separated). --n optionally truncates.\n"
                        "If --in not given: generates N random ints (default N=INT_MAX).\n");
            }
            return;
        }
    }

    if (*n < 0 && *in_path == NULL)
        *n = 1000000; /* default random size */
    if (*n < 0)
        *n = -1; /* means infer from file */
}
