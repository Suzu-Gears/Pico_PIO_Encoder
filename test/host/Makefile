CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror
CXXFLAGS += -I../../src

test_estimator: test_estimator.cpp ../../src/substep_encoder_estimator.h
	$(CXX) $(CXXFLAGS) test_estimator.cpp -o $@

.PHONY: run clean
run: test_estimator
	./test_estimator

clean:
	rm -f test_estimator
