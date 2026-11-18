add_executable(sftool
  sftool_main.cpp
)

target_compile_options(sftool PRIVATE ${SF_WARN_FLAGS})
target_link_libraries(sftool PRIVATE
  soulsframe_core
  soulsframe_probe_core
)

add_test(NAME sftool_help_smoke COMMAND sftool --help)
