export ATHEOS_SRC := $(PWD)
export ATHEOS_INCLUDE_PATH := $(PWD)/sys/include
export ATHEOS_LIB_PATH := $(PWD)/libs
export OBJDIR := $(PWD) /stage/temp

export BUILD_TOOLS := $(ATHEOS_SRC)/build-tools
export DIST_DIR := $(PWD)/stage/image




SUBDIRS := Blue amiga drow next red win98 Winter beish mulvane photon warpspeed

all:
	@for subdir in $(SUBDIRS); do\
		PWD=$(PWD)/$$subdir;\
		if ! $(MAKE) -C $(PWD)/$$subdir; then exit 1; fi;\
	done


install:
	@for subdir in $(SUBDIRS); do\
		PWD=$(PWD)/$$subdir;\
		if ! $(MAKE) -C $(PWD)/$$subdir install; then exit 1; fi;\
	done

clean:
	@for subdir in $(SUBDIRS); do\
		PWD=$(PWD)/$$subdir;\
		if ! $(MAKE) -C $(PWD)/$$subdir clean; then exit 1; fi;\
	done

