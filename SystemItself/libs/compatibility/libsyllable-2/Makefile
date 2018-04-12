name				:= LibSyllable-2
package				:= $(name).zip
system-libs-path	:= /system/libs
install-path		:= $(system-libs-path)
distro-path			:= $(IMAGE)/system/libs

.PHONY: all deps clean install dist

all:
	@for subdir in $(SUBDIRS); do\
		PWD=$(PWD)/$$subdir;\
		if ! $(MAKE) -C $(PWD)/$$subdir; then exit 1; fi;\
	done


clean:
	@for subdir in $(SUBDIRS); do\
		PWD=$(PWD)/$$subdir;\
		if ! $(MAKE) -C $(PWD)/$$subdir clean; then exit 1; fi;\
	done

install:
	unzip $(package) -d $(install-path)

dist:
	unzip -o $(package) -d $(distro-path)

