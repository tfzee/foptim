
################################################
#lit-tests
configure_file(lit.site.cfg.in lit.site.cfg.py @ONLY)
add_custom_target(check
  COMMAND lit "${CMAKE_CURRENT_BINARY_DIR}" -v
  DEPENDS foptim_main)
