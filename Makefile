all: build test

.PHONY: test

build: src/contextify.cc
	node-waf distclean && node-waf configure build

test:
	node_modules/.bin/nodeunit test/
