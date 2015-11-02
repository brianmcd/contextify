'use strict';
const Contextify = require('../lib/contextify.js');
const chai = require('chai');
const sinon = require('sinon');
const sinonChai = require('sinon-chai');
const expect = chai.expect;
chai.use(sinonChai);

describe('Contextify', () => {
    it('shouldn\'t fail with a blank context', () => {
        const ctx = Contextify({});

        expect(ctx).not.to.be.null;
        expect(ctx).not.to.be.undefined;
    });

    it('shouldn\'t change existing sandbox properties', () => {
        const sandbox = {
            prop1 : 'prop1',
            prop2 : 'prop2'
        };

        Contextify(sandbox);
        expect(sandbox.prop1).to.equal('prop1');
        expect(sandbox.prop2).to.equal('prop2');
    });

    it('should create extra functions on sandbox object', () => {
        const sandbox = Contextify({});

        expect(sandbox.run).not.to.be.undefined;
        expect(sandbox.getGlobal).not.to.be.undefined;
        expect(sandbox.dispose).not.to.be.undefined;
    });

    it('should create an empty context if passed undefined', () => {
        expect(Contextify(undefined, undefined)).not.to.be.undefined;
        expect(Contextify()).not.to.be.undefined;
    });

    it('should search sandbox prototype properties', () => {
        const sandbox = {};
        sandbox.__proto__ = {
            prop1 : 'test'
        };

        Contextify(sandbox);
        expect(sandbox.getGlobal().prop1).to.equal('test');
    });

    it('should handle nonexistent properties', () => {
        const global = Contextify({}).getGlobal();

        expect(global.test1).to.be.undefined;
    });

    it('should support properties with the value of "undefined"', () => {
        const sandbox = { x: undefined };
        Contextify(sandbox);
        sandbox.run("_x = x");

        expect(sandbox._x).to.be.undefined;
    });

    it('should support "undefined" variables', () => {
        var sandbox = {};
        Contextify(sandbox);
        // In JavaScript a declared variable is set to 'undefined'.
        sandbox.run("var y; (function() { var _y ; y = _y })()");
        expect(sandbox._y).to.be.undefined;

        // This should apply to top-level variables (global properties).
        sandbox.run("var z; _z = z");
        expect(sandbox._z).to.be.undefined;

        // Make sure nothing wacky happens when accessing global declared but
        // undefined variables.
        expect(sandbox.getGlobal().z).to.be.undefined;
    });

    it('should support run with filename', () => {
        const sandbox = Contextify();
        sandbox.run('var x = 3', 'test.js');

        expect(sandbox.x).to.equal(3);
    });

    it('should support accessors on sandbox', () => {
        const sandbox = {};
        sandbox.__defineGetter__('test', function () { return 3;});
        sandbox.__defineSetter__('test2', function (val) { this.x = val;});
        Contextify(sandbox);
        const global = sandbox.getGlobal();

        expect(global.test).to.equal(3);

        sandbox.test2 = 5;
        expect(sandbox.x).to.equal(5);

        global.test2 = 7;
        expect(global.x).to.equal(7);
        expect(sandbox.x).to.equal(7);
    });

    it('should clean up the sandbox', () => {
        const sandbox = Contextify();

        expect(sandbox.run).not.to.be.undefined;
        expect(sandbox.getGlobal).not.to.be.undefined;
        expect(sandbox.dispose).not.to.be.undefined;

        sandbox.dispose();

        expect(() => sandbox.run()).to.throw(Error);
        expect(() => sandbox.getGlobal()).to.throw(Error);
        expect(() => sandbox.dispose()).to.throw(Error);
    });

    it('should write global variables in scripts on sandbox', () => {
        const sandbox = {
            prop1: 'prop1',
            prop2: 'prop2'
        };
        Contextify(sandbox);
        sandbox.run('x = 3');

        expect(sandbox.x).to.equal(3);
    });

    it('should expose sandbox properties as globals', () => {
        const sandbox = {
            prop1 : 'prop1',
            prop2 : 'prop2'
        };
        Contextify(sandbox);
        sandbox.run(`test1 = (prop1 == 'prop1');
                    test2 = (prop2 == 'prop2');`);

        expect(sandbox.test1).to.be.true;
        expect(sandbox.test2).to.be.true;
    });

    it('should prevent multiple contexts from interfering with each other', () => {
        const sandbox1 = {
            prop1 : 'prop1',
            prop2 : 'prop2'
        };
        const sandbox2 = {
            prop1 : 'prop1',
            prop2 : 'prop2'
        };
        const global1 = Contextify(sandbox1).getGlobal();
        const global2 = Contextify(sandbox2).getGlobal();

        expect(global1.prop1).to.equal('prop1');
        expect(global2.prop1).to.equal('prop1');

        sandbox1.run('test = 3');
        sandbox2.run('test = 4');

        expect(sandbox1.test).to.equal(3);
        expect(global1.test).to.equal(3);
        expect(sandbox2.test).to.equal(4);
        expect(global2.test).to.equal(4);
    });

    it('should allow function calls on sandbox properties', () => {
        const sandbox = {
            console: {log: sinon.spy()},
            prop1: 'prop1'
        };

        Contextify(sandbox);

        expect(() => sandbox.run('console.log(prop1);')).not.to.throw();
        expect(sandbox.console.log).to.have.been.calledWith(sandbox.prop1);
    });

    describe('createContext', () => {
        it('should work with an object sandbox', () => {
            const sandbox = {
                prop1: 'prop1',
                prop2: 'prop2'
            };
            const context = Contextify.createContext(sandbox);

            expect(sandbox.prop1).to.equal('prop1');
            expect(sandbox.prop2).to.equal('prop2');
        });

        it('should not modify the sandbox', () => {
            const sandbox = {};
            Contextify.createContext(sandbox);

            expect(sandbox.run).to.be.undefined;
            expect(sandbox.getGlobal).to.be.undefined;
            expect(sandbox.dispose).to.be.undefined;
        });

        it('should support properties with the value of "undefined"', () => {
            const sandbox = { x: undefined };
            const context = Contextify.createContext(sandbox);
            context.run("_x = x");

            expect(sandbox._x).to.be.undefined;
        });

        it('should support running scripts', () => {
            var sandbox = {};
            var context = Contextify.createContext(sandbox);
            context.run('var x = 3', 'test.js');

            expect(sandbox.x).to.equal(3);
        });
    });

    describe('createScript', () => {
        it('should create a script object', () => {
            const script = Contextify.createScript('var x = 3', 'test.js');

            expect(typeof script.runInContext).to.equal('function');
        });

        it('should throw if not passed a source code string', () => {
            expect(() => Contextify.createScript()).to.throw();
            expect(() => Contextify.createScript(true)).to.throw();
            expect(() => Contextify.createScript(null)).to.throw();
            expect(() => Contextify.createScript(1)).to.throw();
        });

        it('should run in a context', () => {
            const sandbox = {};
            const script = Contextify.createScript('var x = 3', 'test.js');
            const context = Contextify.createContext(sandbox);

            script.runInContext(context);
            expect(sandbox.x).to.equal(3);
        });
    });

    describe('getGlobal()', () => {
        it('should forward globals', () => {
            const sandbox = {
                prop1 : 'prop1',
                prop2 : 'prop2'
            };

            Contextify(sandbox);
            const global = sandbox.getGlobal();

            expect(global).not.to.be.null;
            expect(global).not.to.be.undefined;

            expect(global.prop1).to.equal('prop1');
            expect(global.prop2).to.equal('prop2');

            global.prop3 = 'prop3';
            expect(sandbox.prop3).to.equal('prop3');
        });

        it('should self reference the global object', () => {
            const sandbox = Contextify({});
            const global = sandbox.getGlobal();

            sandbox.ref1 = global;
            sandbox.ref2 = {ref2 : global};
            sandbox.run(`test1 = (this == ref1);
                     test2 = (this == ref2.ref2);`);

            expect(sandbox.test1).to.be.true;
            expect(sandbox.test2).to.be.true;
        });

        it('should enumerate the global object correctly', () => {
            const sandbox = {
                prop1 : 'prop1',
                prop2 : 'prop2'
            };
            const global = Contextify(sandbox).getGlobal();
            const globalProps = Object.keys(global);

            expect(globalProps.length).to.equal(5);
            expect(globalProps.indexOf('prop1')).not.to.equal(-1);
            expect(globalProps.indexOf('prop2')).not.to.equal(-1);
            expect(globalProps.indexOf('run')).not.to.equal(-1);
            expect(globalProps.indexOf('getGlobal')).not.to.equal(-1);
            expect(globalProps.indexOf('dispose')).not.to.equal(-1);
        });

        it('should delete properties from teh global object correctly', () => {
            const sandbox = {
                prop1 : 'prop1',
                prop2 : 'prop2'
            };
            const global = Contextify(sandbox).getGlobal();

            expect(Object.keys(global).length).to.equal(5);
            expect(Object.keys(sandbox).length).to.equal(5);

            delete global.prop1;
            expect(Object.keys(global).length).to.equal(4);
            expect(Object.keys(sandbox).length).to.equal(4);

            delete global.prop2;
            expect(Object.keys(global).length).to.equal(3);
            expect(Object.keys(sandbox).length).to.equal(3);

            delete global.run;
            expect(Object.keys(global).length).to.equal(2);
            expect(Object.keys(sandbox).length).to.equal(2);

            delete global.getGlobal;
            expect(Object.keys(global).length).to.equal(1);
            expect(Object.keys(sandbox).length).to.equal(1);

            delete global.dispose;
            expect(Object.keys(global).length).to.equal(0);
            expect(Object.keys(sandbox).length).to.equal(0);
        });

        it('should set the global object\'s class name', () => {
            function DOMWindow () {};

            const sandbox = Contextify(new DOMWindow());
            const global = sandbox.getGlobal();

            expect(sandbox.constructor.name).to.equal('DOMWindow');
            expect(sandbox.constructor.name).to.equal(global.constructor.name);

            sandbox.run('thisName = this.constructor.name');
            expect(sandbox.thisName).to.equal(sandbox.constructor.name);
        });

        it('should forward functions declared in global scope', () => {
            const sandbox = Contextify();
            const global = sandbox.getGlobal();

            sandbox.run("function testing () {}");

            expect(global.testing).not.to.be.undefined;
        });

        it('should support a run() function', () => {
            const global = Contextify().getGlobal();
            global.run("x = 5");

            expect(global.x).to.equal(5);
        });

        it('should support a getGlobal() function', () => {
            const global = Contextify().getGlobal();
            expect(global).to.equal(global.getGlobal());
        });

        //Make sure global can be a receiver for dispose().
        it('should support a dispose() function', () => {
            const sandbox = Contextify();
            const global = sandbox.getGlobal();

            expect(global.run).not.to.be.undefined;
            expect(global.getGlobal).not.to.be.undefined;
            expect(global.dispose).not.to.be.undefined;

            global.dispose();

            expect(() => sandbox.run()).to.throw(Error);
            expect(() => sandbox.getGlobal()).to.throw(Error);
            expect(() => sandbox.dispose()).to.throw(Error);
        });

        it('should permit deleting global', () => {
            const sandbox = Contextify({});

            sandbox.global = sandbox.getGlobal();
            sandbox.run('delete global.global;');

            expect(sandbox.global).to.be.undefined;

            sandbox.dispose();
        });

        it('should not permit deleting unwritable globals', () => {
            const sandbox = Contextify({});

            Object.defineProperty(sandbox, 'global', {
                enumerable: false,
                configurable: false,
                writable: false,
                value: sandbox.getGlobal()
            });

            sandbox.run('delete global.global;');

            expect(sandbox.global).not.to.be.undefined;
            expect(sandbox.global.global).not.to.be.undefined;

            sandbox.dispose();
        });
    });

    describe('Exceptional conditions', () => {
        it('should be thrown from the Contextified context', () => {
            const sandbox = Contextify();
            const ReferenceError = sandbox.run('ReferenceError');
            const SyntaxError    = sandbox.run('SyntaxError');

            expect(() => sandbox.run('doh')).to.throw(ReferenceError);
            expect(() => sandbox.run('x = y')).to.throw(ReferenceError);
            expect(() => sandbox.run('function ( { (( }{);')).to.throw(SyntaxError);
        });

        it('sandbox dispose() should not be callable twice', () => {
            const sandbox = Contextify();

            expect(() => sandbox.dispose()).not.to.throw();
            expect(() => sandbox.dispose()).to.throw('Called dispose() after dispose().');
        });

        it('global dispose() should not be callable twice', () => {
            const sandbox = Contextify();
            const global = sandbox.getGlobal();

            expect(() => global.dispose()).not.to.throw();
            expect(() => global.dispose()).to.throw('Called dispose() after dispose().');
        });

        it('sandbox run() should not be callable after dispose()', () => {
            const sandbox = Contextify();

            expect(() => sandbox.dispose()).not.to.throw();
            expect(() => sandbox.run('var x = 3')).to.throw('Called run() after dispose().');
        });

        it('sandbox getGlobal() should not be callable after dispose()', () => {
            const sandbox = Contextify();

            expect(() => sandbox.dispose()).not.to.throw();
            expect(() => { const g = sandbox.getGlobal(); }).to.throw('Called getGlobal() after dispose().');
        });
    });

    describe('Context-native eval', () => {
        it('should work in contexts', () => {
            const sandbox = Contextify();

            sandbox.run('eval("test1 = 1")');
            expect(sandbox.test1).to.equal(1);

            sandbox.run('(function() { eval("test2 = 2") })()');
            expect(sandbox.test2).to.equal(2);
        });

        it('shouldn\'t break global this', () => {
            const sandbox = Contextify();

            sandbox.run('e = eval ; e("test1 = 1")');
            expect(sandbox.test1).to.equal(1);

            sandbox.run('var t = 1 ; (function() { var t = 2; test2 = eval("t") })()');
            expect(sandbox.test2).to.equal(2);

            sandbox.run('t = 1 ; (function() { var t = 2; e = eval; test3 = e("t") })()');
            expect(sandbox.test3).to.equal(1);

            sandbox.run('var t = 1 ; global = this; (function() { var t = 2; e = eval; test4 = global.eval.call(global, "t") })()');
            expect(sandbox.test4).to.equal(1);

        });
    });

    describe('Asynchronous script behavior', () => {
        it('should write global variables in scripts on sandbox', (done) => {
            var sandbox = {
                setTimeout : setTimeout,
                prop1 : 'prop1',
                prop2 : 'prop2'
            };

            Contextify(sandbox);
            sandbox.run('setTimeout(function () {x = 3}, 0);');

            expect(sandbox.x).to.be.undefined;

            setTimeout(() => {
                expect(sandbox.x).to.equal(3);
                done();
            }, 0);
        });

        it('should expose sandbox properties as globals', (done) => {
            const sandbox = {
                setTimeout : setTimeout,
                prop1 : 'prop1',
                prop2 : 'prop2'
            };

            Contextify(sandbox);
            sandbox.run(`setTimeout(function () {
                                    test1 = (prop1 == 'prop1');
                                    test2 = (prop2 == 'prop2');
                                }, 0);`);

            expect(sandbox.test1).to.be.undefined;
            expect(sandbox.test2).to.be.undefined;

            setTimeout(() => {
                expect(sandbox.test1).to.be.true;
                expect(sandbox.test2).to.be.true;
                done();
            });
        });

        it('should support async execution after dispose()', (done) => {
            const sandbox = {
                done,
                expect,
                setTimeout,
                prop1: 'prop1',
                prop2: 'prop2'
            };
            Contextify(sandbox);

            sandbox.run(`setTimeout(function () {
                                    expect(prop1 == 'prop1').to.be.true;
                                    expect(prop2 == 'prop2').to.be.true;
                                    done();
                                }, 10);`);

            expect(sandbox.test1).to.be.undefined;
            expect(sandbox.test2).to.be.undefined;

            sandbox.dispose();
        });

        describe('createContext', () => {
            it('should expose sandbox properties as globals', (done) => {
                const sandbox = {
                    setTimeout,
                    prop1: 'prop1',
                    prop2: 'prop2'
                };
                const context = Contextify.createContext(sandbox);

                context.run(`setTimeout(() => {
                                        test1 = (prop1 == 'prop1');
                                        test2 = (prop2 == 'prop2');
                                    }, 0);`);

                expect(sandbox.test1).to.be.undefined;
                expect(sandbox.test2).to.be.undefined;

                setTimeout(() => {
                    expect(sandbox.test1).to.be.true;
                    expect(sandbox.test2).to.be.true;
                    done();
                }, 0);
            });
        });
    });
});