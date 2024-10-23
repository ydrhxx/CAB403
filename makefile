# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2

# Target files
TARGETS = car controller call internal safety

# Source files for each target
CAR_SRC = car.c
CONTROLLER_SRC = controller.c
CALL_SRC = call.c
INTERNAL_SRC = internal.c
SAFETY_SRC = safety.c

# Default target
all: $(TARGETS)

# Individual build targets
car: $(CAR_SRC)
	$(CC) $(CFLAGS) -o $@ $^

controller: $(CONTROLLER_SRC)
	$(CC) $(CFLAGS) -o $@ $^

call: $(CALL_SRC)
	$(CC) $(CFLAGS) -o $@ $^

internal: $(INTERNAL_SRC)
	$(CC) $(CFLAGS) -o $@ $^

safety: $(SAFETY_SRC)
	$(CC) $(CFLAGS) -o $@ $^

# Optional clean target
clean:
	rm -f $(TARGETS)
