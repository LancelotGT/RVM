#### RVM Library Makefile

CFLAGS  = -Wall -g -g3 -I.
LFLAGS  =
CC      = gcc
RM      = /bin/rm -rf
AR      = ar rc
RANLIB  = ranlib
PROJ_DIR = ./
TEST_DIR = ./testcases
BIN = ./bin

LIBRARY = librvm.a

LIB_SRC = rvm.c

LIB_OBJ = $(patsubst %.c,%.o,$(LIB_SRC))

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(LIBRARY): $(LIB_OBJ)
	$(AR) $(LIBRARY) $(LIB_OBJ)
	$(RANLIB) $(LIBRARY)

tests: $(LIBRARY)
	@mkdir -p bin
	$(CC) -o $(BIN)/basic $(TEST_DIR)/basic.c $(CFLAGS) -L. -lrvm 
	$(CC) -o $(BIN)/abort $(TEST_DIR)/abort.c $(CFLAGS) -L. -lrvm  
	$(CC) -o $(BIN)/multi $(TEST_DIR)/multi.c $(CFLAGS) -L. -lrvm   
	$(CC) -o $(BIN)/multi-abort $(TEST_DIR)/multi-abort.c $(CFLAGS) -L. -lrvm    
	$(CC) -o $(BIN)/truncate $(TEST_DIR)/truncate.c $(CFLAGS) -L. -lrvm    

clean:
	$(RM) $(LIBRARY) $(LIB_OBJ)
	$(RM) bin
