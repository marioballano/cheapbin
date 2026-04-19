all:
	[ ! -d b ] && mkdir b && cmake -Cb || true
	$(MAKE) -Cb
	b/cheapbin /bin/ls
