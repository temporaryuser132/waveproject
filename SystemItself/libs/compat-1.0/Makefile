DIST_TARGET_PATH := $(IMAGE)/system/libs

SYS_COMPAT_ARCHIVE := sys-compat.bin.tgz
LIBC_COMPAT_ARCHIVE := libc-compat.bin.tgz

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



dist:
	tar -xzpf $(SYS_COMPAT_ARCHIVE) -C $(DIST_TARGET_PATH)
#	tar -xzpf $(LIBC_COMPAT_ARCHIVE) -C $(DIST_TARGET_PATH)

clean:

