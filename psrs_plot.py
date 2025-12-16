import pandas as pd
import matplotlib.pyplot as plt

data_frame = pd.read_csv("metrics.csv")

# Example 1: PSRS time vs p for a fixed n (strong scaling)
N = data_frame["n"].max()          # or set N = 100000000
data_frame_sorted = data_frame[data_frame["n"] == N].sort_values("p") # sort n by rank count

plt.figure()
plt.plot(data_frame_sorted["p"], data_frame_sorted["psrs_time"], marker="o")
plt.xlabel("MPI ranks (p)")
plt.ylabel("PSRS time (s) (no gather/IO)")
plt.title(f"Strong scaling (n={N})")
plt.grid(True)
plt.savefig("strong_scaling_time.png", dpi=200)

# Determine speedup and efficiency
t1 = data_frame_sorted[data_frame_sorted["p"] == d["p"].min()]["psrs_time"].iloc[0]
speedup = t1 / data_frame_sorted["psrs_time"]
eff = speedup / data_frame_sorted["p"]

plt.figure()
plt.plot(data_frame_sorted["p"], speedup, marker="o")
plt.xlabel("p")
plt.ylabel("Speedup")
plt.title(f"Speedup (n={N})")
plt.grid(True)
plt.savefig("strong_scaling_speedup.png", dpi=200)

plt.figure()
plt.plot(data_frame_sorted["p"], eff, marker="o")
plt.xlabel("p")
plt.ylabel("Parallel efficiency")
plt.title(f"Efficiency (n={N})")
plt.grid(True)
plt.savefig("strong_scaling_efficiency.png", dpi=200)
