KERNEL ?= $(shell uname -r)
DESTDIR=/opt/ioriot
PWD=$(shell pwd)
all:
	$(MAKE) -C systemtap
	$(MAKE) -C ioriot
install:
	$(MAKE) -C systemtap install
	$(MAKE) -C ioriot install
uninstall:
	test ! -z $(DESTDIR) && test -d $(DESTDIR) && rm -Rfv $(DESTDIR) || exit 0
deinstall: uninstall
clean:
	$(MAKE) -C ioriot clean
	$(MAKE) -C systemtap clean
astyle:
	$(MAKE) -C ioriot astyle
loc:
	wc -l ./systemtap/src/*.stp ./ioriot/src/*.{h,c} ./ioriot/src/*/*.{h,c} | tail -n 1
doxygen:
	doxygen ./doc/doxygen.conf
test:
	$(MAKE) -C ioriot test
checkdockerkernel:
	case "$(KERNEL)" in *.el9*) ;; \
	*) echo "KERNEL must be set to a Rocky Linux 9 kernel release, e.g. make dockerbuild KERNEL=5.14.0-611.36.1.el9_7.x86_64"; exit 1 ;; \
	esac
dockerbuild: checkdockerkernel
	bash -c 'test ! -d $(PWD)/docker/opt/ && mkdir -p $(PWD)/docker/opt/; exit 0'
	bash -c 'test -f /etc/fedora-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	bash -c 'test -f /etc/centos-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	bash -c 'test -f /etc/redhat-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	sed s/KERNEL/$(KERNEL)/ Dockerfile.in > Dockerfile
	docker build . -t ioriot:$(KERNEL)
	docker run -v $(PWD)/docker/opt:/opt -e 'KERNEL=$(KERNEL)' -it ioriot:$(KERNEL) make all test install
dockerbuildarchive: checkdockerkernel
	bash -c 'test ! -d $(PWD)/docker/opt/ && mkdir -p $(PWD)/docker/opt/; exit 0'
	bash -c 'test -f /etc/fedora-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	bash -c 'test -f /etc/centos-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	bash -c 'test -f /etc/redhat-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	sed s/KERNEL/$(KERNEL)/ Dockerfile.archive.in > Dockerfile.archive
	docker build --network host -f Dockerfile.archive . -t ioriot-archive:$(KERNEL)
	docker run --rm -v $(PWD)/docker/opt:/opt -e 'KERNEL=$(KERNEL)' ioriot-archive:$(KERNEL) make all test install
dockerclean:
	bash -c 'test -d $(PWD)/docker && rm -Rfv $(PWD)/docker; exit 0'
jenkins: checkdockerkernel
	bash -c 'test ! -d $(PWD)/docker/opt/ && mkdir -p $(PWD)/docker/opt/; exit 0'
	bash -c 'test -f /etc/fedora-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	bash -c 'test -f /etc/centos-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	bash -c 'test -f /etc/redhat-release && sudo chcon -Rt svirt_sandbox_file_t $(PWD)/docker/opt; exit 0'
	sed s/KERNEL/$(KERNEL)/ Dockerfile.in > Dockerfile
	docker build . -t ioriot:latest
	docker run -v $(PWD)/docker/opt:/opt -e 'KERNEL=$(KERNEL)' -it ioriot:latest make all test install
