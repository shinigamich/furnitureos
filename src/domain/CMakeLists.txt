fos_add_module(domain SOURCES src/part.cpp src/model.cpp src/fixtures.cpp DEPS fos::core fos::geometry)
fos_add_tests(domain SOURCES tests/domain_tests.cpp)
