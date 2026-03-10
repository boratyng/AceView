# Makefile

ifeq ($(shell uname),Darwin)
    STLINC = /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1
    UNAME_ARCH = $(shell uname -m)
    ifeq ($(UNAME_ARCH),aarch64)
        ARCH = arm
    else ifeq ($(UNAME_ARCH),arm64)
        ARCH = arm
    else
        ARCH = x86
    endif
    OS = mac-$(ARCH)

    # === Critical fix for NGS SDK on macOS ===
    CXXFLAGS += -std=c++11 -stdlib=libc++ -D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS
    CXXFLAGS += -include cstddef -include cstdlib

else
    GCC_VERSION = "$(shell g++ --version | awk '/^g++/ {split($$NF, a, "."); print a[1];}')"
    STLINC = /usr/include/c++/$(GCC_VERSION)
    OS = linux
endif

# NGS-SDK version
VERSION = 3.2.1
NGSSDK = ngs-sdk.$(VERSION)-$(OS)
URL = https://ftp-trace.ncbi.nlm.nih.gov/sra/ngs/$(VERSION)/$(NGSSDK).tar.gz

LIBS = -L$(NGSSDK)/lib64 -lncbi-ngs-c++-static -lncbi-ngs-static -lncbi-vdb-static -lngs-c++-static -lpthread -ldl

all: $(NGSSDK)/README
	$(MAKE) clean
	$(MAKE) libsraread.a

# Download and unpack NGS SDK
$(NGSSDK)/README:
	curl -O $(URL)
	tar xfz $(NGSSDK).tar.gz

# ==================== Compilation Rules ====================

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# This is the important rule for .cpp files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -I$(NGSSDK)/include -I$(NGSSDK)/include/ncbi-vdb -I$(STLINC) -c -o $@ $<

# Build the library
libsraread.a: sra_read.o
	ar rcs $@ $^


demo: demo.o libsraread.a
	g++ -o $@ $^ $(LIBS)

clean:
	rm -f *.o
	rm -f *.a
	rm -f demo
