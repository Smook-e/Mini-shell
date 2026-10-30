CXX       := g++
CXXFLAGS  := -std=c++17 -Wall -Wextra
TARGET    := myshell
SOURCES   := command.cc tokenizer.cc
OBJECTS   := $(SOURCES:.cc=.o)
TEST_TARGET := command_tests

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

$(TEST_TARGET): command.cc tokenizer.cc command.h tokenizer.h command_tests.cc
	$(CXX) $(CXXFLAGS) -DCOMMAND_TEST_BUILD command.cc tokenizer.cc command_tests.cc -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJECTS)
