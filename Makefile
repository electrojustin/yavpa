all: yavpu yavpp
yavpu: vpu.c vp.h
	gcc -g vpu.c -o yavpu
yavpp: vpp.c vp.h
	gcc -g vpp.c -o yavpp
install:
	install -c yavpu /usr/bin/yavpu
	install -c yavpp /usr/bin/yavpp
