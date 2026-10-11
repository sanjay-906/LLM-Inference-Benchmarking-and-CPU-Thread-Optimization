## Key Conclusions

LLM: `Qwen2.5-1.5B-Instruct(Q4_K_M)`

- **Optimal Thread Count:** 7 threads delivered the best overall performance, achieving 61.05 prompt-processing tok/s and 16.04 generation tok/s.
- **Prompt Processing Speedup:** Achieved a maximum 2.84× speedup at 7 threads compared to single-thread execution.
- **Generation Speedup:** Achieved a maximum 2.37× speedup at 7 threads, improving from 6.76 tok/s to 16.04 tok/s.
- **Diminishing Returns:** Performance gains generally increased with thread count, but 8 threads reduced prompt throughput to 59.12 tok/s and generation throughput to 14.48 tok/s, indicating that additional threads did not improve performance.
- **CPU Utilization:** CPU utilization increased from 12.43% at 1 thread to 82.25% at 7 threads, then remained approximately flat at 81.93% with 8 threads.
- **Memory Stability:** Peak RAM usage remained relatively stable at approximately 1.69 GiB across all thread configurations, indicating minimal memory variation during benchmarking.
- **Generation Consistency:** Generation throughput improved steadily from 1 to 7 threads, while the 8-thread configuration showed a noticeable regression.
- **Benchmark Reliability:** Each configuration used 5 repetitions, with reported means and deviations to help assess performance consistency.
- **Hardware Context:** Results were collected on a system featuring Intel UHD Graphics 620. The tested `llama-bench` configuration evaluates CPU-thread scaling; the GPU model alone does not establish that GPU acceleration was used.
- **Practical Recommendation:** For this model, quantization, and workload (512 prompt tokens and 128 generated tokens), 7 threads provided the best measured balance of throughput and resource utilization. Results may differ with other models, quantization formats, or hardware.

| threads | prompt_tps | prompt_deviation | generation_tps | generation_deviation | prompt_speedup | generation_speedup | average_cpu_percent | peak_ram_mib | peak_commit_mib |
| ------- | ---------- | ---------------- | -------------- | -------------------- | -------------- | ------------------ | ------------------- | ------------ | --------------- |
| 1       | 21.53      | 0.27             | 6.76           | 0.09                 | 1              | 1                  | 12.4328             | 1688.2       | 1042.2          |
| 2       | 36.62      | 0.25             | 10.69          | 0.12                 | 1.70088        | 1.58136            | 24.6142             | 1688.79      | 1042.41         |
| 3       | 46.87      | 2.4              | 13.01          | 0.26                 | 2.17696        | 1.92456            | 36.5248             | 1685.1       | 1042.94         |
| 4       | 46.46      | 0.7              | 13.62          | 0.12                 | 2.15792        | 2.01479            | 48.6503             | 1689.46      | 1043.5          |
| 5       | 51.45      | 0.57             | 14.48          | 0.09                 | 2.38969        | 2.14201            | 60.5709             | 1689.66      | 1044.03         |
| 6       | 55.65      | 0.76             | 15.04          | 0.28                 | 2.58477        | 2.22485            | 71.14               | 1690.31      | 1044.89         |
| 7       | 61.05      | 0.67             | 16.04          | 0.44                 | 2.83558        | 2.37278            | 82.2492             | 1690.79      | 1045.48         |
| 8       | 59.12      | 0.16             | 14.48          | 0.21                 | 2.74594        | 2.14201            | 81.929              | 1690.86      | 1045.86         |
