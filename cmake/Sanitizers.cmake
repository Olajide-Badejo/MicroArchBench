# cmake/Sanitizers.cmake
# Optionally enable AddressSanitizer and UndefinedBehaviorSanitizer.
# Never combine sanitizers with benchmarking runs -- they add significant
# overhead and will produce incorrect throughput and latency numbers.
# Use sanitizers only for correctness testing (ctest targets).

if(BENCH_SANITIZE)
    message(WARNING
        "BENCH_SANITIZE=ON: ASan/UBSan add substantial overhead. "
        "Do NOT use sanitizer builds for performance measurement. "
        "Use them only for running the tests/ targets.")

    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
