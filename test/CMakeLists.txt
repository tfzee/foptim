
get_target_property(FOPTIM_BINARY_DIR foptim_main BINARY_DIR)
set(FOPTIM_ARTIFACT_DIR "${FOPTIM_BINARY_DIR}")

configure_file(lit.site.cfg.in lit.site.cfg @ONLY)
add_my_lit_suite(check-backend "${CMAKE_CURRENT_BINARY_DIR}/backend" foptim_main)
