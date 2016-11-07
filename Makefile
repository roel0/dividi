TARGET = dividi
SRC_DIR = dividi
TARGET_DIR = build
LIBS = -lpthread
SRCS=$(wildcard $(SRC_DIR)/*.c)
OBJS=$(patsubst  %.c, %.o, $(SRCS))
CFLAGS = -Wall
MKDIR_P = mkdir -p

.PHONY: directories clean

all:  directories $(TARGET)
debug: CFLAGS += -DDEBUG -g
debug: all

directories:
	@echo '### Creating build folder ###'
	$(MKDIR_P) $(TARGET_DIR)/$(SRC_DIR)

%.o : %.c
	@echo '### Building ###'
	$(CC) -c $(CFLAGS) $(DEBUG) -o $(TARGET_DIR)/$@ $<

$(TARGET): $(OBJS)
	@echo '### Linking ###'
	-cd $(TARGET_DIR)
	$(CC) $(CFLAGS) $(LIBS) $(addprefix $(TARGET_DIR)/, $(OBJS)) -o $(TARGET_DIR)/$(SRC_DIR)/$@

clean:
	$(RM) -r  $(TARGET_DIR) 
