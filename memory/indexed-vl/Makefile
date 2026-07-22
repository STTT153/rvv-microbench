CC = clang
CFLAGS = -O2 -std=c11 -march=rv64gcv -Wall -Wextra -Wpedantic
LDFLAGS =
TARGET = indexed_load
SOURCES = main.c indexed_load.S baseline.S
RUN_ARGS =

.PHONY: all clean run scalar scalar-run

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES)

run: $(TARGET)
	./$(TARGET) $(RUN_ARGS)

scalar:
	$(MAKE) -C ../indexed-scalar

scalar-run:
	$(MAKE) -C ../indexed-scalar run RUN_ARGS="$(RUN_ARGS)"

clean:
	$(RM) $(TARGET)
	$(MAKE) -C ../indexed-scalar clean
