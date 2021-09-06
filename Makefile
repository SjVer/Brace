########################################################################
####################### Makefile Template ##############################
########################################################################

# Compiler settings - Can be customized.
CC = gcc
libpath = /usr/lib/brace
CXXFLAGS = -std=c11 -Wall -lm -D COMPILER=\"$(CC)\" -D BRACE_LIB_PATH=\"$(libpath)\"
LDFLAGS = 

# Makefile settings - Can be customized.
APPNAME = brace
EXT = .c
SRCDIR = src
HEADERDIR = $(SRCDIR)/headers
BINDIR = bin
OBJDIR = $(BINDIR)/obj

############## Do not change anything from here downwards! #############
SRC = $(wildcard $(SRCDIR)/*$(EXT))
# HEADERS = $(wildcard $(HEADERDIR)/*.h)
OBJ = $(SRC:$(SRCDIR)/%$(EXT)=$(OBJDIR)/%.o)
APP = $(BINDIR)/$(APPNAME)
DEP = $(OBJ:$(OBJDIR)/%.o=%.d)

DEBUGDEFS = -D DEBUG_TRACE_EXECUTION -D DEBUG_PRINT_CODE
DEBUG_GC_LOG_DEFS = -D DEBUG_LOG_GC
DEBUG_GC_STRESS_DEGS = -D DEBUG_STRESS_GC

OBJCOUNT_NOPAD = $(shell v=`echo $(OBJ) | wc -w`; echo `seq 1 $$(expr $$v)`)
# LAST = $(word $(words $(OBJCOUNT_NOPAD)), $(OBJCOUNT_NOPAD))
# L_ZEROS = $(shell printf '%s' '$(LAST)' | wc -c)
# OBJCOUNT = $(foreach v,$(OBJCOUNT_NOPAD),$(shell printf '%0$(L_ZEROS)d' $(v)))
OBJCOUNT = $(foreach v,$(OBJCOUNT_NOPAD),$(shell printf '%02d' $(v)))

# DEBUG = false

# UNIX-based OS variables & settings
RM = rm
MKDIR = mkdir
DELOBJ = $(OBJ)

########################################################################
####################### Targets beginning here #########################
########################################################################

.MAIN: $(APP)
all: $(APP)

# Builds the app
$(APP): $(OBJ) | makedirs
	@printf "[final] compiling final product $(notdir $@)..."
	@$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@#cp $(APP) "."
	@printf "\b\b done!\n"

# Building rule for .o files and its .c/.cpp in combination with all .h
# $(OBJDIR)/%.o: $(SRCDIR)/%$(EXT) | makedirs
$(OBJDIR)/%.o: $(SRCDIR)/%$(EXT) | makedirs
	@printf "[$(word 1,$(OBJCOUNT))/$(words $(OBJ))] compiling $(notdir $<) into $(notdir $@)..."
	@$(CC) $(CXXFLAGS) -I $(HEADERDIR) -o $@ -c $<
	@printf "\b\b done!\n"
	$(eval OBJCOUNT = $(filter-out $(word 1,$(OBJCOUNT)),$(OBJCOUNT)))

################### Cleaning rules for Unix-based OS ###################
# Cleans complete project
.PHONY: clean
clean:
	@# $(RM) $(DELOBJ) $(DEP) $(APP)
	@$(RM) -rf $(OBJDIR)
	@$(RM) -rf $(BINDIR)

.PHONY: run
run: $(APP)
	@printf "============ Running \"$(APP)\" with file \"$(file)\" ============\n\n"
	@$(APP) $(file)

.PHONY: routine
routine: $(APP) run clean

.PHONY: makedirs
makedirs:
	@$(MKDIR) -p $(BINDIR)
	@$(MKDIR) -p $(OBJDIR)

.PHONY: remake
remake: clean $(APP)

.PHONY: printdebug
printdebug:
	@printf "debug mode set!\n"
printdebug-gc:
	@printf "gc debug mode set!\n"

# .PHONY: debug
debug: CXXFLAGS += $(DEBUGDEFS)
debug: printdebug
debug: all

debug-gc-log: CXXFLAGS += $(DEBUG_GC_LOG_DEFS)
debug-gc-log: printdebug-gc
debug-gc-log: all

debug-gc-stress: CXXFLAGS  += $(DEBUG_GC_STRESS_DEFS) -g -ggdb
debug-gc-stress: printdebug-gc
debug-gc-stress: all
