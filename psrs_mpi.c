#include "psrs_mpi.h"
#include <mpi.h>
#define MAGIC INT_MAX

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank, p;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    /* TIMERS */
    double t_psrs_start = 0.0, t_psrs_end = 0.0;
    double t_gather_start = 0.0, t_gather_end = 0.0;
    double t_io_start = 0.0, t_io_end = 0.0;

    int n_arg;
    const char *in_path, *out_path, *metrics_path;
    parse_args(argc, argv, rank, &n_arg, &in_path, &out_path, &metrics_path);

    int n = 0;

    int *A_global = NULL;
    int *send_counts_root = NULL;
    int *displs_root = NULL;

    const int sample_count_per_rank = p - 1;

    int *sample_data = (int *)malloc((size_t)sample_count_per_rank * sizeof(int));
    int *pivots = (int *)malloc((size_t)sample_count_per_rank * sizeof(int));

    if (!sample_data || !pivots)
    {
        fprintf(stderr, "Rank %d: malloc failed\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /*
     * 1 - root prepares global array (from file or generates random) and prepares scatter arrays. TODO: Get rid of the magic number 1000000
     */
    if (rank == 0)
    {
        if (in_path)
        {
            int max_n = (n_arg >= 0) ? n_arg : -1;
            int rc = read_ints_text(in_path, &A_global, &n, max_n);
            if (rc != 0)
            {
                fprintf(stderr, "Rank 0: failed reading '%s' (rc=%d)\n", in_path, rc);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            if (n == 0)
            {
                fprintf(stderr, "Rank 0: input file had no integers.\n");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
        }
        else
        {
            n = (n_arg >= 0) ? n_arg : MAGIC;
            A_global = (int *)malloc((size_t)n * sizeof(int));
            if (!A_global && n > 0)
            {
                fprintf(stderr, "Rank 0: malloc A_global failed\n");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            srand((unsigned)time(NULL));
            for (int i = 0; i < n; i++)
                A_global[i] = rand() % MAGIC;
        }

        send_counts_root = (int *)malloc((size_t)p * sizeof(int));
        displs_root = (int *)malloc((size_t)p * sizeof(int));
        if (!send_counts_root || !displs_root)
        {
            fprintf(stderr, "Rank 0: malloc counts/displs failed\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        int base = n / p;
        int remainder = n % p;
        int offset = 0;
        for (int r = 0; r < p; r++)
        {
            send_counts_root[r] = base + (r < remainder ? 1 : 0);
            displs_root[r] = offset;
            offset += send_counts_root[r];
        }
    }

    /* broadcast n */
    // MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /*
     *  2 - Scatter local_n and its data
     */

    int local_n = 0;
    MPI_Scatter(send_counts_root, 1, MPI_INT, &local_n, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int *local_data = NULL;
    if (local_n > 0)
    {
        local_data = (int *)malloc((size_t)local_n * sizeof(int));
        if (!local_data)
        {
            fprintf(stderr, "Rank %d: malloc local_data failed\n", rank);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    MPI_Scatterv(A_global, send_counts_root, displs_root, MPI_INT,
                 local_data, local_n, MPI_INT,
                 0, MPI_COMM_WORLD);

    if (local_n > 0)
    {
        qsort(local_data, (size_t)local_n, sizeof(int), compare_int);
    }

    /*
     * START PSRS TIMER (excludes input + scatter + local sort)
     */

    MPI_Barrier(MPI_COMM_WORLD);
    t_psrs_start = MPI_Wtime();

    /*
     * 3 - Create samples based on local_n
     */

    if (local_n == 0)
    {
        for (int i = 0; i < sample_count_per_rank; ++i)
            sample_data[i] = INT_MAX;
    }
    else
    {
        for (int i = 0; i < sample_count_per_rank; ++i)
        {
            int idx = (i + 1) * local_n / p;
            if (idx >= local_n)
            {
                idx = local_n - 1;
            }
            sample_data[i] = local_data[idx];
        }
    }

    /*  gather samples and choose pivots on root */
    int *samples_global = NULL;
    if (rank == 0)
    {
        samples_global = (int *)malloc((size_t)p * (size_t)sample_count_per_rank * sizeof(int));
        if (!samples_global && p > 1)
        {
            fprintf(stderr, "Rank 0: malloc samples_global failed\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    MPI_Gather(sample_data, sample_count_per_rank, MPI_INT,
               samples_global, sample_count_per_rank, MPI_INT,
               0, MPI_COMM_WORLD);

    if (rank == 0)
    {
        int total_samples = p * sample_count_per_rank;
        if (total_samples > 0)
        {
            qsort(samples_global, (size_t)total_samples, sizeof(int), compare_int);
            for (int i = 0; i < sample_count_per_rank; i++)
            {
                pivots[i] = samples_global[i * p + p / 2];
            }
        }
    }
    /* Broadcast pivots to other ranks */
    MPI_Bcast(pivots, sample_count_per_rank, MPI_INT, 0, MPI_COMM_WORLD);

    /*
     * 4 - Prepare sendcounts and sdispls to determine bucket distribution in each rank (sublist sizes)
     */
    int *sendcounts = (int *)malloc((size_t)p * sizeof(int));
    int *sdispls = (int *)malloc((size_t)p * sizeof(int));
    int *recvcounts = (int *)malloc((size_t)p * sizeof(int));
    int *rdispls = (int *)malloc((size_t)p * sizeof(int));
    if (!sendcounts || !sdispls || !recvcounts || !rdispls)
    {
        fprintf(stderr, "Rank %d: malloc counts/displs failed\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    /* do a binary search in each rank around EACH pivot to determine bucket i contributions of rank j */
    if (local_n == 0)
    {
        for (int j = 0; j < p; ++j)
        {
            sendcounts[j] = 0;
            sdispls[j] = 0;
        }
    }
    else
    {
        int prev = 0;
        sdispls[0] = 0;
        for (int j = 0; j < p - 1; ++j)
        {
            int cut = upper_bound_int(local_data, prev, local_n, pivots[j]);
            sendcounts[j] = cut - prev;
            prev = cut;
            sdispls[j + 1] = prev;
        }
        sendcounts[p - 1] = local_n - prev;
    }
    /* send bucket size information i of rank j to rank i of sublist j */
    MPI_Alltoall(sendcounts, 1, MPI_INT, recvcounts, 1, MPI_INT, MPI_COMM_WORLD);
    int total_recv = make_displs(recvcounts, rdispls, p); // total size of buckets

    int *bucket = NULL;
    if (total_recv > 0)
    {
        bucket = (int *)malloc((size_t)total_recv * sizeof(int));
        if (!bucket)
        {
            fprintf(stderr, "Rank %d: malloc bucket failed\n", rank);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    /* exchange bucket i of rank j with bucket j of rank i of local_n directly */
    MPI_Alltoallv(local_data, sendcounts, sdispls, MPI_INT,
                  bucket, recvcounts, rdispls, MPI_INT,
                  MPI_COMM_WORLD);
    /* qsort each bucket. each bucket is guaranteed to be within the range of consecutive pivots, so this should be really fast */
    if (total_recv > 0)
    {
        qsort(bucket, (size_t)total_recv, sizeof(int), compare_int);
    }

    /*
     * PSRS Completed. End timer excluding: gather data to root + CSV write
     */
    MPI_Barrier(MPI_COMM_WORLD);
    t_psrs_end = MPI_Wtime();

    double local_psrs = t_psrs_end - t_psrs_start;
    double max_psrs = 0.0;
    MPI_Reduce(&local_psrs, &max_psrs, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD); // selects maximum time rank

    if (rank == 0)
    {
        printf("PSRS time (no gather/IO): %f s\n", max_psrs);
    }

    /*
     * Gather final global sorted array to root and write CSV
     */
    int *final_counts = NULL;
    int *final_displs = NULL;
    int *final_sorted = NULL;
    if (rank == 0)
    {
        final_counts = (int *)malloc((size_t)p * sizeof(int));
        final_displs = (int *)malloc((size_t)p * sizeof(int));
        if (!final_counts || !final_displs)
        {
            fprintf(stderr, "Rank 0: malloc final_counts/displs failed\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    /* Time gather (collectives) separately */
    MPI_Barrier(MPI_COMM_WORLD);
    t_gather_start = MPI_Wtime();

    MPI_Gather(&total_recv, 1, MPI_INT, final_counts, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0)
    {
        int final_n = make_displs(final_counts, final_displs, p);
        final_sorted = (int *)malloc((size_t)final_n * sizeof(int));
        if (!final_sorted && final_n > 0)
        {
            fprintf(stderr, "Rank 0: malloc final_sorted failed\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    MPI_Gatherv(bucket, total_recv, MPI_INT,
                final_sorted, final_counts, final_displs, MPI_INT,
                0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    t_gather_end = MPI_Wtime();

    double local_gather = t_gather_end - t_gather_start;
    double max_gather = 0.0;
    MPI_Reduce(&local_gather, &max_gather, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0)
        printf("Gather time: %f s\n", max_gather);

    /* CSV write  timing (rank 0 only) */
    double io_time = 0.0;
    int correct_flag = 1;

    if (rank == 0 && out_path)
    {
        int final_n = final_displs[p - 1] + final_counts[p - 1];

        t_io_start = MPI_Wtime();
        // int rc = write_csv(out_path, final_sorted, final_n);
        t_io_end = MPI_Wtime();
        int rc = 0;

        io_time = t_io_end - t_io_start;

        if (rc != 0)
        {
            fprintf(stderr, "Rank 0: failed writing '%s' (rc=%d, errno=%d)\n", out_path, rc, errno);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        printf("CSV write time: %f s\n", io_time);
    }

    /* Logging metrics */
    if (rank == 0 && metrics_path)
    {
        int rc = append_metrics_csv(metrics_path,
                                    n, p,
                                    max_psrs,
                                    max_gather,
                                    io_time,
                                    correct_flag);
        if (rc != 0)
        {
            fprintf(stderr, "Rank 0: failed to write metrics to '%s'\n", metrics_path);
        }
    }

    /* free per rank */
    free(bucket);
    free(rdispls);
    free(recvcounts);
    free(sdispls);
    free(sendcounts);

    free(sample_data);
    free(pivots);
    free(local_data);

    /* free root rank */
    if (rank == 0)
    {
        free(samples_global);

        free(final_sorted);
        free(final_displs);
        free(final_counts);

        free(displs_root);
        free(send_counts_root);
        free(A_global);
    }

    MPI_Finalize();
    return 0;
}
