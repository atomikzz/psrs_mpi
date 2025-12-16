#!/usr/bin/env bash
set -e

# config
EXEC=./psrs_mpi
METRICS=metrics.csv
OUT=/dev/null

Ns=(1000000 2000000 4000000 8000000 16000000)
Ps=(1 2 3 4)

# set up virtual python
if [ ! -d "venv" ]; then
  echo "Creating virtual environment..."
  python3 -m venv venv
  source venv/bin/activate
  pip install --upgrade pip
  pip install pandas matplotlib
else
  source venv/bin/activate
fi

echo "Using Python: $(which python)"

# MPI tests
for p in "${Ps[@]}"; do
  for n in "${Ns[@]}"; do
    echo "Running PSRS: p=$p n=$n"
    mpirun -np "$p" "$EXEC" --n "$n" --out "$OUT" --metrics "$METRICS"
  done
done

# plots based on python code
echo "Plotting results..."
python psrs_plot.py

deactivate
