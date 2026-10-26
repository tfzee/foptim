
get_target_property(FOPTIM_BINARY_DIR foptim_main BINARY_DIR)
set(FOPTIM_ARTIFACT_DIR "${FOPTIM_BINARY_DIR}")

configure_file(lit.site.cfg.in lit.site.cfg @ONLY)
add_my_lit_suite(check-backend "${CMAKE_CURRENT_BINARY_DIR}/backend" foptim_main)
add_my_lit_suite(check-opt "${CMAKE_CURRENT_BINARY_DIR}/opt" foptim_main)
configure_file(unit/lit.site.cfg.in unit/lit.site.cfg @ONLY)
file(CREATE_LINK "${FOPTIM_ARTIFACT_DIR}/foptim_unit_test"
     "${CMAKE_CURRENT_BINARY_DIR}/unit/foptim_unit_test" SYMBOLIC)
add_my_lit_suite(check-unit-lit "${CMAKE_CURRENT_BINARY_DIR}/unit" foptim_unit_test)
