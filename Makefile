# Makefile do compressor PPM-C
#   make        -> compila o programa "ppmc"
#   make test   -> roda o teste de integridade (comprime, descomprime e compara)
#   make clean  -> apaga o executável

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

ppmc: src/main.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) -o ppmc src/main.cpp

test: ppmc
	bash scripts/verificar.sh

clean:
	rm -f ppmc

.PHONY: test clean
