# Legacy scaling benchmark (no external deps)
add_executable(gravel_bench bench_scaling.cpp)
target_link_libraries(gravel_bench PRIVATE gravel)

# Google Benchmark-based performance suite (S2-10.2)
add_executable(gravel_perf bench_fragility.cpp)
target_link_libraries(gravel_perf PRIVATE gravel benchmark::benchmark)
