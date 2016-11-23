TARGET = dividi
SRC_DIR = dividi
INC_DIR = -I$(SRC_DIR)
TEST_DIR = test
TARGET_DIR = build
LIBS = -lpthread -lcrypto -lssl
ifeq ($(OS),Windows_NT)
	LIBS += -lws2_32
endif
SRCS=$(wildcard $(SRC_DIR)/*.c)
OBJS=$(patsubst  %.c, %.o, $(SRCS))
CFLAGS = -Wall
MKDIR_P = mkdir -p
CP_VR = cp -vr
CC = gcc

.PHONY: directories clean

all:  directories $(TARGET)
debug: CFLAGS += -DDEBUG -g
debug: all
test: CFLAGS += -DDEBUG -g
test: directories serial_test queue_test

directories:
	@echo '### Creating build folder ###'
	$(MKDIR_P) $(TARGET_DIR)/$(SRC_DIR)
	$(MKDIR_P) $(TARGET_DIR)/$(TEST_DIR)

%.o : %.c
	@echo '### Building ###'
	$(CC) -c $(CFLAGS) $(DEBUG) $(INC_DIR) -o $(TARGET_DIR)/$@ $<

$(TARGET): $(OBJS)
	@echo '### Linking ###'
	-cd $(TARGET_DIR)
	$(CC) $(CFLAGS) $(addprefix $(TARGET_DIR)/, $(OBJS)) -o $(TARGET_DIR)/$(SRC_DIR)/$@ $(LIBS)

queue_test:
	$(CC) $(CFLAGS) -o $(TARGET_DIR)/$(TEST_DIR)/queue_test -DTEST $(INC_DIR) $(TEST_DIR)/queue_test.c $(LIBS)

serial_test:
	$(CC) $(CFLAGS) -o $(TARGET_DIR)/$(TEST_DIR)/serial_test -DTEST $(INC_DIR) $(TEST_DIR)/serial_test.c -lm $(LIBS)
	$(CP_VR) $(TEST_DIR)/dummy $(TARGET_DIR)/$(TEST_DIR)

example: debug

clean:
	$(RM) -r  $(TARGET_DIR) 
