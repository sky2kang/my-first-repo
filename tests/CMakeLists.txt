add_executable(test_ecpri test_ecpri.c)
target_link_libraries(test_ecpri PRIVATE oru_core)
add_test(NAME ecpri COMMAND test_ecpri)

add_executable(test_fronthaul test_fronthaul.c)
target_link_libraries(test_fronthaul PRIVATE oru_core)
add_test(NAME fronthaul COMMAND test_fronthaul)

add_executable(test_bfp test_bfp.c)
target_link_libraries(test_bfp PRIVATE oru_core)
add_test(NAME bfp COMMAND test_bfp)

add_executable(test_mulaw test_mulaw.c)
target_link_libraries(test_mulaw PRIVATE oru_core)
add_test(NAME mulaw COMMAND test_mulaw)

add_executable(test_modcomp test_modcomp.c)
target_link_libraries(test_modcomp PRIVATE oru_core)
add_test(NAME modcomp COMMAND test_modcomp)

add_executable(test_fh_sched test_fh_sched.c)
target_link_libraries(test_fh_sched PRIVATE oru_core)
add_test(NAME fh_sched COMMAND test_fh_sched)

add_executable(test_datapath test_datapath.c)
target_link_libraries(test_datapath PRIVATE oru_core)
add_test(NAME datapath COMMAND test_datapath)

add_executable(test_slot_loop test_slot_loop.c)
target_link_libraries(test_slot_loop PRIVATE oru_core)
add_test(NAME slot_loop COMMAND test_slot_loop)

add_executable(test_yang test_yang.c)
target_link_libraries(test_yang PRIVATE oru_core)
add_test(NAME yang COMMAND test_yang)

add_executable(test_cu_match test_cu_match.c)
target_link_libraries(test_cu_match PRIVATE oru_core)
add_test(NAME cu_match COMMAND test_cu_match)

add_executable(test_comp_bench test_comp_bench.c)
target_link_libraries(test_comp_bench PRIVATE oru_core)
add_test(NAME comp_bench COMMAND test_comp_bench)

add_executable(test_fh_packet test_fh_packet.c)
target_link_libraries(test_fh_packet PRIVATE oru_core)
add_test(NAME fh_packet COMMAND test_fh_packet)

add_executable(test_prach test_prach.c)
target_link_libraries(test_prach PRIVATE oru_core)
add_test(NAME prach COMMAND test_prach)

add_executable(test_beamform test_beamform.c)
target_link_libraries(test_beamform PRIVATE oru_core)
add_test(NAME beamform COMMAND test_beamform)

add_executable(test_cfr test_cfr.c)
target_link_libraries(test_cfr PRIVATE oru_core)
add_test(NAME cfr COMMAND test_cfr)

add_executable(test_dpd test_dpd.c)
target_link_libraries(test_dpd PRIVATE oru_core)
add_test(NAME dpd COMMAND test_dpd)

add_executable(test_perf test_perf.c)
target_link_libraries(test_perf PRIVATE oru_core)
add_test(NAME perf COMMAND test_perf)

add_executable(test_fm test_fm.c)
target_link_libraries(test_fm PRIVATE oru_core)
add_test(NAME fm COMMAND test_fm)
