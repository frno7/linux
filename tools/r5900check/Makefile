# SPDX-License-Identifier: GPL-2.0
#
# Define V=1 for more verbose compile.
# Define S=1 for sanitation checks.

CFLAGS += -g -O2 -Wall -Iinclude -D_GNU_SOURCE

ifeq "$(S)" "1"
CFLAGS += -fsanitize=address -fsanitize=leak -fsanitize=undefined	\
	  -fsanitize-address-use-after-scope -fstack-check
endif

ALL_CFLAGS += $(CFLAGS) $(BASIC_CFLAGS)

R5900CHECK := r5900check

.PHONY: tool
all: $(R5900CHECK)

SRC := $(filter-out elf.c, $(wildcard *.c))
OBJ = $(patsubst %.c, %.o, $(SRC))

$(R5900CHECK): $(OBJ)
	$(QUIET_LINK)$(CC) $(ALL_CFLAGS) -o $@ $^

$(OBJ): %.o : %.c
	$(QUIET_CC)$(CC) $(ALL_CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	$(QUIET_RM)$(RM) -f *.o *.o.d $(R5900CHECK)

V             = @
Q             = $(V:1=)
QUIET_AR      = $(Q:@=@echo    '  AR      '$@;)
QUIET_AS      = $(Q:@=@echo    '  AS      '$@;)
QUIET_CC      = $(Q:@=@echo    '  CC      '$@;)
QUIET_GEN     = $(Q:@=@echo    '  GEN     '$@;)
QUIET_LINK    = $(Q:@=@echo    '  LD      '$@;)
QUIET_TEST    = $(Q:@=@echo    '  TEST    '$@;)
QUIET_RM      = $(Q:@=@echo    '  RM      '$@;)

BASIC_CFLAGS += -Wp,-MMD,$(@D)/$(@F).d -MT $(@D)/$(@F)

ifneq "$(MAKECMDGOALS)" "clean"
    DEP_FILES := $(addsuffix .d, $(OBJ))
    $(if $(DEP_FILES),$(eval -include $(DEP_FILES)))
endif
