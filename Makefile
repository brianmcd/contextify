all: build

.PHONY: test clean

clean:
	node-waf distclean

build: src/contextify.cc
	node-waf distclean && node-waf configure build

test:
	npm test
