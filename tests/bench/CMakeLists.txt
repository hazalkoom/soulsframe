add_executable(soulsframe_benchmarks
  main.cpp
)

target_compile_options(soulsframe_benchmarks PRIVATE ${SF_WARN_FLAGS})
target_link_libraries(soulsframe_benchmarks PRIVATE
  soulsframe_core
  soulsframe_probe_core
)

add_test(NAME soulsframe_benchmarks_smoke COMMAND soulsframe_benchmarks --quick)
