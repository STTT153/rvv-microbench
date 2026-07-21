CC = clang
CFLAGS = -O2 -std=c11 -march=rv64gcv -Wall -Wextra -Wpedantic
LDFLAGS =
TARGET = indexed_load
SOURCES = indexed_load.c indexed_load.S

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)
