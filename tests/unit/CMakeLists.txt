add_executable(soulsframe_unit_tests
  test_config.cpp
  test_content_hash.cpp
  test_frame_pacer.cpp
  test_frame_stats.cpp
  test_fsr1_math.cpp
  test_gpu_join.cpp
  test_gpu_timing.cpp
  test_jsonl_writer.cpp
  test_module_registry.cpp
  test_offline_guard.cpp
  test_particles.cpp
  test_pe_dump.cpp
  test_pgo_script.cpp
  test_present_policy.cpp
  test_probe_trace.cpp
  test_profile.cpp
  test_security_inputs.cpp
  test_sha256.cpp
  test_sig_db.cpp
  test_sigscan.cpp
)

target_compile_options(soulsframe_unit_tests PRIVATE ${SF_WARN_FLAGS})
target_link_libraries(soulsframe_unit_tests PRIVATE
  soulsframe_core
  soulsframe_probe_core
  doctest::doctest
)

add_test(NAME soulsframe_unit_tests COMMAND soulsframe_unit_tests)
