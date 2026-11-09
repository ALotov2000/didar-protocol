function(didar_test name label)
  add_executable(test_${name} test_${name}.cpp)
  target_link_libraries(test_${name} PRIVATE didar)
  target_compile_definitions(test_${name} PRIVATE
    DIDARD="$<TARGET_FILE:didard>" DIDAR_AGENT="$<TARGET_FILE:didar-agent>")
  add_dependencies(test_${name} didard didar-agent)
  add_test(NAME ${name} COMMAND test_${name})
  set_tests_properties(${name} PROPERTIES LABELS ${label})
endfunction()

foreach(name fr1 fr4 fr5 sr3 sr4 pr1)
  didar_test(${name} requirement)
endforeach()
didar_test(pr2 perf)
