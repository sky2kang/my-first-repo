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

add_executable(test_fh_sched test_fh_sched.c)
target_link_libraries(test_fh_sched PRIVATE oru_core)
add_test(NAME fh_sched COMMAND test_fh_sched)

add_executable(test_datapath test_datapath.c)
target_link_libraries(test_datapath PRIVATE oru_core)
add_test(NAME datapath COMMAND test_datapath)

add_executable(test_yang test_yang.c)
target_link_libraries(test_yang PRIVATE oru_core)
add_test(NAME yang COMMAND test_yang)
