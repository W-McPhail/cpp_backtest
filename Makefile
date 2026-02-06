# Backtester Makefile (run from build/)
CXX     ?= g++
CXXFLAGS = -std=c++17 -Wall -I../include -I../strategies
SRCDIR   = ..
VPATH    = $(SRCDIR)/src $(SRCDIR)/strategies

SOURCES  = main.cpp data_source.cpp simulator.cpp backtester.cpp report.cpp example_sma_strategy.cpp ctm_strategy.cpp orb_strategy.cpp
OBJS     = $(SOURCES:.cpp=.o)
TARGET   = backtester

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(SRCDIR)/src/$< -o $@ 2>/dev/null || \
	$(CXX) $(CXXFLAGS) -c $(SRCDIR)/strategies/$< -o $@

main.o: ../src/main.cpp
	$(CXX) $(CXXFLAGS) -c ../src/main.cpp -o $@
data_source.o: ../src/data_source.cpp
	$(CXX) $(CXXFLAGS) -c ../src/data_source.cpp -o $@
simulator.o: ../src/simulator.cpp
	$(CXX) $(CXXFLAGS) -c ../src/simulator.cpp -o $@
backtester.o: ../src/backtester.cpp
	$(CXX) $(CXXFLAGS) -c ../src/backtester.cpp -o $@
report.o: ../src/report.cpp
	$(CXX) $(CXXFLAGS) -c ../src/report.cpp -o $@
example_sma_strategy.o: ../strategies/example_sma_strategy.cpp
	$(CXX) $(CXXFLAGS) -c ../strategies/example_sma_strategy.cpp -o $@
ctm_strategy.o: ../strategies/ctm_strategy.cpp
	$(CXX) $(CXXFLAGS) -c ../strategies/ctm_strategy.cpp -o $@
orb_strategy.o: ../strategies/orb_strategy.cpp
	$(CXX) $(CXXFLAGS) -c ../strategies/orb_strategy.cpp -o $@

clean:
	rm -f $(OBJS) $(TARGET) backtester.exe
