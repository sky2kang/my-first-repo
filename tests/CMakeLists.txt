add_executable(test_ecpri test_ecpri.c)
target_link_libraries(test_ecpri PRIVATE oru_core)
add_test(NAME ecpri COMMAND test_ecpri)

add_executable(test_fronthaul test_fronthaul.c)
target_link_libraries(test_fronthaul PRIVATE oru_core)
add_test(NAME fronthaul COMMAND test_fronthaul)

add_executable(test_bfp test_bfp.c)
target_link_libraries(test_bfp PRIVATE oru_core)
add_test(NAME bfp COMMAND test_bfp)

add_executable(test_fh_sched test_fh_sched.c)
target_link_libraries(test_fh_sched PRIVATE oru_core)
add_test(NAME fh_sched COMMAND test_fh_sched)
