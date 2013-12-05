ifneq ($(KERNELRELEASE),)

# kbuild part of makefile
include Kbuild

else

# See Documentation/kbuild/modules.txt

# extra => pluginfs, default is /lib/modules/$(KERNELRELEASE)/extra/
MDIR ?= pluginfs
# module installation directory prefix
MPATH ?=
KDIR ?= /lib/modules/`uname -r`
MINC := -I$(PWD)/pluginfs

.PHONY: cscope cscope-clean ctags ctags-clean tags tags-clean help all clean \
	modules-install install modules-uninstall uninstall

default: all

help:
	@echo "tags"
	@echo "tags-clean"
	@echo "cscope"
	@echo "cscope-clean"
	@echo "ctags"
	@echo "ctags-clean"

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

all: modules tags

clean: modules-clean tags-clean

install: modules-install

uninstall: modules-uninstall

endif

