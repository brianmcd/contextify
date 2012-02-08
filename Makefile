all: build

.PHONY: test clean

clean:
	node-gyp clean

build: clean src/contextify.cc
	node-gyp configure
	node-gyp build

test:
	npm test
