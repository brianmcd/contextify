try {
    module.exports = require('../build/default/contextify').wrap;
} catch (e) {
    module.exports = require('../build/Release/contextify').wrap;
}
