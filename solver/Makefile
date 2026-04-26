CXX      = g++
CXXFLAGS = -O3 -std=c++17 -I src

SRCS = src/geometry.cpp src/csv_io.cpp src/case_io.cpp src/spatial.cpp src/scoring.cpp src/solver_core.cpp main.cpp
OBJS = $(SRCS:.cpp=.o)

TARGET = solver
GREEDY = greedy

all: $(TARGET) $(GREEDY)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(GREEDY): greedy.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(GREEDY)

.PHONY: all clean
