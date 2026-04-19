all:
	[ ! -d b ] && mkdir b && cmake -Cb || true
	$(MAKE) -Cb
run:
	b/cheapbin /bin/ls
