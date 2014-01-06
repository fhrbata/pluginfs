ifneq ($(KERNELRELEASE),)

# kbuild part of makefile
include Kbuild

else

# See Documentation/kbuild/modules.txt

# extra => pluginfs, default is /lib/modules/$(KERNELRELEASE)/
MDIR ?= pluginfs
# module installation directory prefix
MPATH ?=
KDIR ?= /lib/modules/`uname -r`
MINC := -I$(PWD)/pluginfs

.PHONY: cscope cscope-clean ctags ctags-clean tags tags-clean help all clean \
	modules-install install modules-uninstall uninstall

default: all

help:
	@echo "all               - build modules, libs, utils and create tags, default"
	@echo "help              - display this help"
	@echo "modules           - build modules"
	@echo "modules-clean     - delete created modules"
	@echo "modules-install   - install modules"
	@echo "modules-uninstall - uninstall modules"
	@echo "tags              - generate cscope and tags indexes"
	@echo "tags-clean        - delete cscope and tags indexes"
	@echo "cscope            - generate cscope index"
	@echo "cscope-clean      - delete cscope index"
	@echo "ctags             - generate tags index"
	@echo "ctags-clean       - delete tags index"
	@echo "install           - install modules, libs and utils"
	@echo "uninstall         - uninstall modules, libs and utils"
	@echo "clean             - delete modules, libs, utils and cscope and tags indexes"
	@echo "libs              - build libs"
	@echo "libs-install      - install libs"
	@echo "libs-uninstall    - uninstall libs"
	@echo "libs-clean        - delete libs"

modules:
	$(MAKE) -C $(KDIR)/build M=$(PWD) EXTRA_CFLAGS=$(MINC) modules

modules-clean:
	$(MAKE) -C $(KDIR)/build M=$(PWD) EXTRA_CFLAGS=$(MINC) clean

modules-install: modules
	$(MAKE) -C $(KDIR)/build M=$(PWD) EXTRA_CFLAGS=$(MINC) \
	INSTALL_MOD_PATH=$(MPATH) INSTALL_MOD_DIR=$(MDIR) modules_install
	depmod

modules-uninstall:
	rm -rf $(KDIR)/$(MDIR)

libs:
	$(MAKE) -C libav

libs-install:
	$(MAKE) -C libav install

libs-uninstall:
	$(MAKE) -C libav uninstall

libs-clean:
	$(MAKE) -C libav clean

cscope:
	find . -type f -iregex '^.*\.\(c\|h\)' > cscope.files
	cscope -b

cscope-clean:
	rm -rf cscope*

ctags:
	ctags -R

ctags-clean:
	rm -rf tags

tags: cscope ctags

tags-clean: cscope-clean ctags-clean

all: modules libs tags

clean: modules-clean libs-clean tags-clean

install: modules-install libs-install

uninstall: modules-uninstall libs-uninstall

endif
