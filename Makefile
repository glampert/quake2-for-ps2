
#
# !!! NOTE: Header files are completely ignored !!!
# If you change a header file, either make clean or
# change the accompanying source files to force a rebuild!
#

# ---------------------------------------------------------
#  Quake 2 source files:
# ---------------------------------------------------------

# TODO experiment with dlmalloc
# ps2/dlmalloc/malloc.c

# All files used by the game and engine:
SRC_FILES = \
	ps2/tests/test_draw2d.c \
	ps2/builtin/palette.c \
	ps2/builtin/conchars.c \
	ps2/builtin/conback.c \
	ps2/builtin/inventory.c \
	ps2/builtin/backtile.c \
	ps2/builtin/help.c \
	ps2/debug_print.c \
	ps2/mem_alloc.c \
	ps2/sys_ps2.c \
	ps2/vid_ps2.c \
	ps2/ref_ps2.c \
	ps2/tex_image.c \
	ps2/model.c \
	ps2/math_funcs.c \
	ps2/net_ps2.c \
	ps2/main_ps2.c \
	null/in_null.c \
	null/cd_null.c \
	null/snddma_null.c \
	common/cmd.c \
	common/cmodel.c \
	common/common.c \
	common/crc.c \
	common/cvar.c \
	common/filesys.c \
	common/md4.c \
	common/net_chan.c \
	common/pmove.c \
	game/g_ai.c \
	game/g_chase.c \
	game/g_cmds.c \
	game/g_combat.c \
	game/g_func.c \
	game/g_items.c \
	game/g_main.c \
	game/g_misc.c \
	game/g_monster.c \
	game/g_phys.c \
	game/g_save.c \
	game/g_spawn.c \
	game/g_svcmds.c \
	game/g_target.c \
	game/g_trigger.c \
	game/g_turret.c \
	game/g_utils.c \
	game/g_weapon.c \
	game/m_actor.c \
	game/m_berserk.c \
	game/m_boss2.c \
	game/m_boss3.c \
	game/m_boss31.c \
	game/m_boss32.c \
	game/m_brain.c \
	game/m_chick.c \
	game/m_flash.c \
	game/m_flipper.c \
	game/m_float.c \
	game/m_flyer.c \
	game/m_gladiator.c \
	game/m_gunner.c \
	game/m_hover.c \
	game/m_infantry.c \
	game/m_insane.c \
	game/m_medic.c \
	game/m_move.c \
	game/m_mutant.c \
	game/m_parasite.c \
	game/m_soldier.c \
	game/m_supertank.c \
	game/m_tank.c \
	game/p_client.c \
	game/p_hud.c \
	game/p_trail.c \
	game/p_view.c \
	game/p_weapon.c \
	game/q_shared.c \
	client/cl_cin.c \
	client/cl_ents.c \
	client/cl_fx.c \
	client/cl_input.c \
	client/cl_inv.c \
	client/cl_main.c \
	client/cl_newfx.c \
	client/cl_parse.c \
	client/cl_pred.c \
	client/cl_scrn.c \
	client/cl_tent.c \
	client/cl_view.c \
	client/console.c \
	client/keys.c \
	client/menu.c \
	client/qmenu.c \
	client/snd_dma.c \
	client/snd_mem.c \
	client/snd_mix.c \
	server/sv_ccmds.c \
	server/sv_ents.c \
	server/sv_game.c \
	server/sv_init.c \
	server/sv_main.c \
	server/sv_send.c \
	server/sv_user.c \
	server/sv_world.c

# IOP modules pulled from the PS2DEV SDK:
IRX_FILES = \
	usbd.irx

# ---------------------------------------------------------
#  Libs from the PS2DEV SDK:
# ---------------------------------------------------------

PS2_LIBS =    \
	-ldma     \
	-lgraph   \
	-ldraw    \
	-lpatches \
	-lpacket  \
	-lmf      \
	-lc       \
	-lkernel

# ---------------------------------------------------------
#  Make flags/macros:
# ---------------------------------------------------------

#
# Where to find the source files and where to dump the
# intermediate object files and the final ELF executable.
#
OUTPUT_DIR     = build
SRC_DIR        = src
IOP_OUTPUT_DIR = irx

#
# Name of the binary executable (QPS2) and
# object (.o) filenames derived from source filenames.
#
BIN_TARGET = $(OUTPUT_DIR)/QPS2.ELF
OBJ_FILES  = $(addprefix $(OUTPUT_DIR)/$(SRC_DIR)/, $(patsubst %.c, %.o, $(SRC_FILES)))

#
# The IRX IOProcessor modules we embed:
#
IOP_MODULES = $(addprefix $(OUTPUT_DIR)/$(IOP_OUTPUT_DIR)/, $(patsubst %.irx, %.o, $(IRX_FILES)))

#
# Global #defines, C-flags and include paths:
#
PS2_GLOBAL_DEFS = -D_EE -DGAME_HARD_LINKED -DPS2_QUAKE
PS2_CFLAGS      = $(PS2_GLOBAL_DEFS) -O3 -G0
PS2_INCS        = -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I$(SRC_DIR)

#
# Custom GCC for the PS2 EE CPU (based on GCC v3):
#
PS2_CC = ee-gcc

#
# Linker stuff:
#
PS2_LDFLAGS = -L$(PS2SDK)/ee/lib
PS2_LINKCMD = -mno-crt0 -T$(PS2SDK)/ee/startup/linkfile
PS2_STARTUP_FILE = $(PS2SDK)/ee/startup/crt0.o

#
# The same structure of src/ is recreated in build/, so we need mkdir.
#
MKDIR_CMD = mkdir -p

# ---------------------------------------------------------
#  Define VERBOSE somewhere before this to get the
#  full make command printed to stdout.
#  If VERBOSE is NOT defined you can also:
#   - Define QUIET_W_CFLAGS to print the CFLAGS
# ---------------------------------------------------------
ifndef VERBOSE
QUIET = @
# Colored text if the terminal supports ANSI color codes:
COLOR_ALT1    = "\033[36;1m"
COLOR_ALT2    = "\033[35;1m"
COLOR_DEFAULT = "\033[0;1m"
ifdef QUIET_W_CFLAGS
ECHO_COMPILING = @echo $(COLOR_ALT1)"-> Compiling "$(PS2_CFLAGS) $(COLOR_ALT2)$<$(COLOR_ALT1)" ..."$(COLOR_DEFAULT)
else # !QUIET_W_CFLAGS
ECHO_COMPILING = @echo $(COLOR_ALT1)"-> Compiling "$(COLOR_ALT2)$<$(COLOR_ALT1)" ..."$(COLOR_DEFAULT)
endif # QUIET_W_CFLAGS
ECHO_LINKING  = @echo $(COLOR_ALT1)"-> Linking ..."$(COLOR_DEFAULT)
ECHO_CLEANING = @echo $(COLOR_ALT1)"-> Cleaning ..."$(COLOR_DEFAULT)
ECHO_BUILDING_IOP_MODS = @echo $(COLOR_ALT1)"-> Assembling IOP module '$@' ..."$(COLOR_DEFAULT)
endif # VERBOSE

# ---------------------------------------------------------
#  Make rules:
# ---------------------------------------------------------

all: $(BIN_TARGET)
	$(QUIET) ee-strip --strip-all $(BIN_TARGET)

#
# C source files => OBJ files:
#
$(BIN_TARGET): $(OBJ_FILES) $(IOP_MODULES)
	$(ECHO_LINKING)
	$(QUIET) $(PS2_CC) $(PS2_LINKCMD) $(PS2_CFLAGS) -o \
		$(BIN_TARGET) $(PS2_STARTUP_FILE) $(OBJ_FILES) \
		$(IOP_MODULES) $(PS2_LDFLAGS) $(PS2_LIBS)

$(OBJ_FILES): $(OUTPUT_DIR)/%.o: %.c
	$(ECHO_COMPILING)
	$(QUIET) $(MKDIR_CMD) $(dir $@)
	$(QUIET) $(PS2_CC) $(PS2_CFLAGS) $(PS2_INCS) -c $< -o $@

#
# IOP/IRX modules, compiled into the program:
#
$(IOP_MODULES): $(OUTPUT_DIR)/$(IOP_OUTPUT_DIR)/%.o: $(OUTPUT_DIR)/$(IOP_OUTPUT_DIR)/%.s
	$(ECHO_BUILDING_IOP_MODS)
	$(QUIET) $(PS2_CC) $(PS2_CFLAGS) -c $< -o $@

$(OUTPUT_DIR)/$(IOP_OUTPUT_DIR)/%.s:
	$(QUIET) $(MKDIR_CMD) $(dir $@)
	$(QUIET) bin2s $(PS2SDK)/iop/irx/$*.irx $@ $*_irx

# ---------------------------------------------------------
#  Custom 'run' rule:
# ---------------------------------------------------------

ifndef INSTALL_PATH
INSTALL_PATH = /Applications/pcsx2.app/Contents/Resources/pcsx2
endif

ifndef RUN_CMD
RUN_CMD = open -a pcsx2
endif

# Copies the executable file to the "install dir" and calls the user-defined
# run command, which normally is the shell command to start the emulator app.
run: $(BIN_TARGET)
	cp $(BIN_TARGET) $(INSTALL_PATH)/$(notdir $(BIN_TARGET))
	$(RUN_CMD)

# ---------------------------------------------------------
#  Custom 'clean' rule:
# ---------------------------------------------------------

# Using 'find' because we need to recursively clear all subdirs.
clean:
	$(ECHO_CLEANING)
	$(QUIET) rm -f $(BIN_TARGET)
	$(QUIET) rm -f $(OUTPUT_DIR)/$(IOP_OUTPUT_DIR)/*.s
	$(QUIET) rm -f $(OUTPUT_DIR)/$(IOP_OUTPUT_DIR)/*.o
	$(QUIET) rm -f $(INSTALL_PATH)/$(notdir $(BIN_TARGET))
	$(QUIET) find $(OUTPUT_DIR) -name "*.o" -type f -delete

