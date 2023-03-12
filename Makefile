# Check running envrionment
# DOCKER ?= no 
# LAB_ENV_ID=/home/ubuntu/Desktop/lab.desktop
# ifneq ($(LAB_ENV_ID),$(wildcard $(LAB_ENV_ID)))
#   ifneq (../../configs/linux-0.11-lab, $(wildcard ../../configs/linux-0.11-lab))
#     $(error ERR: No 'Cloud Lab' found, please refer to 'Download the lab' part of README.md)
#   else
#     $(error ERR: Please not try Linux 0.11 Lab in local host, but use it with 'Cloud Lab', please refer to 'Install the environment' part of README.md)
#   endif
# endif

include Makefile.head

all: Image

$(LINUX_SRC): $(LINUX_VERSION)
	$(Q)rm -rf $@
	$(Q)ln -sf $< $@

Image: $(LINUX_SRC)
	$(Q)(cd $(LINUX_SRC); make $@)

clean: $(LINUX_SRC)
	$(Q)(cd $(ROOTFS_DIR); make $@)
	$(Q)(cd $(CALLGRAPH_DIR); make $@)
	$(Q)(cd $(LINUX_SRC); make $@)
	$(Q)rm -rf bochsout.txt

distclean: clean
	$(Q)(cd $(LINUX_SRC); make $@)

# Test on emulators with different prebuilt rootfs
include Makefile.emu

# Tags for source code reading
include Makefile.tags

# For Call graph generation
include Makefile.cg

# For help
include Makefile.help
