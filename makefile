CC = gcc
# -g: Includes debug information (line numbers, variable names) in 
# the executable so GDB can map code to source.
# -O0: Disables compiler optimizations. Optimizations (like -O2 or -O3) 
# can rearrange, inline, or eliminate code, making debugging harder.
# CFLAGS = -Wall -Wextra -g -O0 -fsanitize=address
CFLAGS = -Wall -Wextra -g -O0
TARGET = main
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
OUT = $(BUILD_DIR)/$(TARGET)

SRC = 	src/main.c 				\
		src/lexer/lexer.c		\
		src/parser/parser.c		\
		src/codegen/codegen.c

OBJ = 	$(OBJ_DIR)/main.o		\
		$(OBJ_DIR)/lexer.o		\
		$(OBJ_DIR)/parser.o		\
		$(OBJ_DIR)/codegen.o	

all: $(BUILD_DIR) $(OBJ_DIR) $(OUT)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Compile main.c to object file
$(OBJ_DIR)/main.o: src/main.c
	$(CC) $(CFLAGS) -c src/main.c -o $(OBJ_DIR)/main.o

# Compile lexer.c to object file
$(OBJ_DIR)/lexer.o: src/lexer/lexer.c
	$(CC) $(CFLAGS) -c src/lexer/lexer.c -o $(OBJ_DIR)/lexer.o

# Compile parser.c to object file
$(OBJ_DIR)/parser.o: src/parser/parser.c
	$(CC) $(CFLAGS) -c src/parser/parser.c -o $(OBJ_DIR)/parser.o

# Compile codegen.c to object file
$(OBJ_DIR)/codegen.o: src/codegen/codegen.c
	$(CC) $(CFLAGS) -c src/codegen/codegen.c -o $(OBJ_DIR)/codegen.o

# Link object files into executable
$(OUT): $(OBJ)
	$(CC) $(OBJ) -o $(OUT) $(CFLAGS)

run: test.asm
	nasm -f elf64 test.asm -o test.o
	ld test.o -o test
# ./test
# echo $?

clean:
	rm -rf $(BUILD_DIR)
