########################################################################
####################### Makefile Template ##############################
########################################################################

# Compiler settings - Can be customized.
CC = gcc
CXXFLAGS = -std=c11 -Wall -g
LDFLAGS = 

# Makefile settings - Can be customized.
APPNAME = brace
EXT = .c
SRCDIR = src
HEADERDIR = $(SRCDIR)/headers
BINDIR = bin
OBJDIR = $(BINDIR)/obj
DEPDIR = $(BINDIR)/dep

############## Do not change anything from here downwards! #############
SRC = $(wildcard $(SRCDIR)/*$(EXT))
# HEADERS = $(wildcard $(HEADERDIR)/*.h)
OBJ = $(SRC:$(SRCDIR)/%$(EXT)=$(OBJDIR)/%.o)
DEPS = $(SRC:$(SRCDIR)/%$(EXT)=$(DEPDIR)/%.d)
APP = $(BINDIR)/$(APPNAME)
DEP = $(OBJ:$(OBJDIR)/%.o=%.d)

DEBUGDEFS = -DDEBUG_TRACE_EXECUTION -DDEBUG_PRINT_CODE
DEBUG_GC_LOG_DEFS = -DDEBUG_LOG_GC
DEBUG_GC_STRESS_DEGS = -DDEBUG_STRESS_GC

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
# Windows OS variables & settings
DEL = del
EXE = .exe
WDELOBJ = $(SRC:$(SRCDIR)/%$(EXT)=$(OBJDIR)\\%.o)

########################################################################
####################### Targets beginning here #########################
########################################################################

.MAIN: $(APP)
all: $(APP)

# Builds the app
$(APP): $(OBJ) | makedirs
	@printf "[final] compiling final product $(notdir $@)..."
	@$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@cp $(APP) "."
	@printf "\b\b done!\n"

# Creates the dependecy rules
%.d: $(SRCDIR)/%$(EXT) | makedirs
# $(DEPS): $(SRCDIR)/%$(EXT) | makedirs
	@printf "dep!!! compiling $(notdir $<) into $(notdir $@)..."
	$(CPP) $(CFLAGS) $< -MM -MT $(DEPS) >$@
	@printf "\b\b done!\n"

# Includes all .h files
-include $(DEPS)

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
	@$(RM) -rf $(DEPDIR)
	@$(RM) -rf $(BINDIR)

# # Cleans only all files with the extension .d
# .PHONY: cleandep
# cleandep:
# 	$(RM) $(DEP)

# #################### Cleaning rules for Windows OS #####################
# # Cleans complete project
# .PHONY: cleanw
# cleanw:
# 	$(DEL) $(WDELOBJ) $(DEP) $(APPNAME)$(EXE)

# # Cleans only all files with the extension .d
# .PHONY: cleandepw
# cleandepw:
# 	$(DEL) $(DEP)

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
	@$(MKDIR) -p $(DEPDIR)

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

debug-gc-stress: CXXFLAGS  += $(DEBUG_GC_STRESS_DEFS)
debug-gc-stress: printdebug-gc
debug-gc-stress: all
