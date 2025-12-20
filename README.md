# Project documentation

This project concerns the MPI implementation of Parallel Sorting by Regular Sampling (PSRS), with strong scaling experiments for PSRS phase runtime.Through comparative experiments, this implementation demonstrates results in agreement with expectations. We observe up to threefold linear increase in speedup for quadruple number of ranks for high number of data elements, successfully demonstrating strong scaling. For lower number of data elements, the runtime is communication dominated. I plan to add a more rigorous analysis for this by measuring the communication time for high overhead functions such as Alltoallv.

## PSRS algorithm

The PSRS algorithm is designed to improve efficiency on multiprocessor systems by addressing common issues such as memory contention and poor load balancing. PSRS uses regular sampling to select optimal pivots, ensuring that work is divided almost perfectly across all processors. The authors demonstrate that this approach is theoretically optimal and achieves significant speedups on various architectures, including shared memory, distributed memory, and hypercube machines. 

## Build / Run
Build by:

mpicc psrs_mpi.c -o "output_name"

Run (manually):

  mpirun -np P ./output_name --n <N> --in <input.csv> --out <output.csv> --metrics <metrics.csv>

        If --in is provided: reads ints from input.csv file.

        If --in or --n command is not provided: generates N=INT_MAX random integers.

        If metrics.csv file exists: --metrics command writes after the last row.

        If output.csv file exists: --out command clears the previous version and writes.

Test run: run_psrs.sh auto test run.

# Tests and findings
In this implementation, I test the strong scaling using the standard formula for different numbers of integers. The timing excludes I/O and the final gathering of local partitions.

At smaller N (e.g. 1.6e7), runtime is dominated by MPI communication. Crucially, parallelization efficiency is very low (~0.20) for higher ranks.

At larger N (e.g. 1.6e8), computation dominates more and scaling improves. Increasing rank count yields increase in parallel efficiency and speedup, and reduction in algorithm time. We reach a more stable relative efficiency (with respect to p = 1) of ~0.80. PSRS algorithm time is reduced by a factor of 3 from p = 1 to p = 4. 


## Testing algorithm time results per rank
![test1](runs-1e6/strong_scaling_time.png)
![test2](runs-1e7/strong_scaling_time.png)

## Testing algorithm speedup by rank
![test1](runs-1e6/strong_scaling_speedup.png)
![test2](runs-1e7/strong_scaling_speedup.png)

## Testing algorithm efficiency by rank
![test1](runs-1e6/strong_scaling_efficiency.png)
![test2](runs-1e7/strong_scaling_efficiency.png)

# References

- Schaeffer et al., *Parallel Sorting by Regular Sampling*, 2006  
  [PDF](https://webdocs.cs.ualberta.ca/~jonathan/publications/parrallel_computing_publications/psrs1.pdf)