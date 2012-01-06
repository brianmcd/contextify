try {
    var ContextifyContext = require('../build/Release/contextify').ContextifyContext;
} catch (e) {
    console.log("Internal Contextify ERROR: Make sure Contextify is built " +
                "with your current Node version.\nTo rebuild, go to the " +
                "Contextify root folder and run 'node-waf distclean && " +
                "node-waf configure build'.");
    throw e;
}

module.exports = function Contextify (sandbox) {
    if (typeof sandbox != 'object') {
        sandbox = {};
    }
    var ctx = new ContextifyContext(sandbox);

    sandbox.run = function () {
        return ctx.run.apply(ctx, arguments);
    };

    sandbox.getGlobal = function () {
        return ctx.getGlobal();
    }

    sandbox.dispose = function () {
        sandbox.run = function () {
            throw new Error("Called run() after dispose().");
        };
        sandbox.getGlobal = function () {
            throw new Error("Called getGlobal() after dispose().");
        };
        sandbox.dispose = function () {
            throw new Error("Called dispose() after dispose().");
        };
        ctx = null;
    }
    return sandbox;
}
