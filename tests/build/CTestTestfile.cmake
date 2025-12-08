# CMake generated Testfile for 
# Source directory: /home/user/cat-annihilation/tests
# Build directory: /home/user/cat-annihilation/tests/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[UnitTests]=] "/home/user/cat-annihilation/tests/build/unit_tests")
set_tests_properties([=[UnitTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/user/cat-annihilation/tests/CMakeLists.txt;105;add_test;/home/user/cat-annihilation/tests/CMakeLists.txt;0;")
add_test([=[IntegrationTests]=] "/home/user/cat-annihilation/tests/build/integration_tests")
set_tests_properties([=[IntegrationTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/user/cat-annihilation/tests/CMakeLists.txt;106;add_test;/home/user/cat-annihilation/tests/CMakeLists.txt;0;")
