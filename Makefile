CXX      ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -flto
LDFLAGS  ?= -flto

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
BIN := ripper

.PHONY: all clean debug

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

debug: CXXFLAGS := -std=c++20 -O0 -g -Wall -Wextra -Wno-unused-parameter
debug: LDFLAGS :=
debug: clean $(BIN)

clean:
	rm -f $(OBJ) $(BIN)
