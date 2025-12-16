# PSRS MPI Implementation

MPI implementation of Parallel Sorting by Regular Sampling (PSRS), with strong scaling experiments for PSRS phase runtime.

## Description
This project implements PSRS using MPI and includes strong scaling
experiments measuring the PSRS phase runtime.

## Build / Run
Build by:

mpicc psrs_mpi.c -o "output_name"

Usage (manually):

  mpirun -np P ./a.out [--n N] [--in input.txt] [--out output.csv] [--metrics metrics.csv]

        If --in is given: reads ints from file (CSV/whitespace separated). --n optionally truncates.

        If --in not given: generates N random ints (default N=INT_MAX).\n

        If metrics.csv file exists: writes after the last row.

        If output.csv file exists: --out writes on top of the same file.

Also run_psrs.sh for auto run.

## Notes
Timing excludes I/O and the final gathering of local partitions.

At smaller N (e.g. 1.6e7), runtime is dominated by MPI communication. Crucially, parallel efficiency is very low (~0.20) for higher ranks.

At larger N (e.g. 1.6e8), computation dominates more and scaling improves. To summarize, increasing rank count yields increase in parallel efficiency and speedup, and reduction in algorithm time. We reach a more stable relative efficiency (with respect to p = 1) of ~0.80. Total algorithm time is reduced by a factor of 3 from p = 1 to p = 4.


## Testing algorithm time results per rank
![test1](runs-1e6/strong_scaling_time.png)
![test2](runs-1e7/strong_scaling_time.png)

## Testing algorithm speedup by rank
![test1](runs-1e6/strong_scaling_speedup.png)
![test2](runs-1e7/strong_scaling_speedup.png)

## Testing algorithm efficiency by rank
![test1](runs-1e6/strong_scaling_efficiency.png)
![test2](runs-1e7/strong_scaling_efficiency.png)

