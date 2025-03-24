const fs = require('fs');
const path = require('path');
const minify = require('html-minifier-terser').minify;
const { minify: terserMinify } = require('terser');
const CleanCSS = require('clean-css');

const terserOptions = {
  compress: {
    arrows: true,
    arguments: true,
    booleans_as_integers: true,
    booleans: true,
    collapse_vars: true,
    comparisons: true,
    computed_props: true,
    conditionals: true,
    dead_code: true,
    directives: true,
    drop_console: true,
    drop_debugger: true,
    evaluate: true,
    expression: true,
    global_defs: {},
    hoist_funs: true,
    hoist_props: true,
    hoist_vars: true,
    if_return: true,
    inline: true,
    join_vars: true,
    keep_classnames: false,
    keep_fargs: false,
    keep_fnames: false,
    keep_infinity: false,
    loops: true,
    module: true,
    negate_iife: true,
    passes: 3,
    properties: true,
    pure_funcs: null,
    pure_getters: true,
    reduce_vars: true,
    sequences: true,
    side_effects: true,
    switches: true,
    toplevel: true,
    typeofs: true,
    unsafe: true,
    unsafe_arrows: true,
    unsafe_comps: true,
    unsafe_Function: true,
    unsafe_math: true,
    unsafe_methods: true,
    unsafe_proto: true,
    unsafe_regexp: true,
    unsafe_undefined: true,
    unused: true,
  },
  mangle: {
    eval: true,
    keep_classnames: false,
    keep_fnames: false,
    module: true,
    properties: false,
    toplevel: true,
    safari10: false,
    reserved: ['ssid', 'rssi'],
  },
  format: {
    ascii_only: true,
    beautify: false,
    comments: false,
  },
  sourceMap: false,
  ecma: 2020,
  module: true,
};

const cleanCssOptions = {
  level: {
    1: {
      all: true,
      normalizeUrls: false
    },
    2: {
      restructureRules: true,
      mergeMedia: true,
      mergeNonAdjacentRules: true,
      mergeSemantically: true,
      // overrideProperties: true,
      removeEmpty: true,
      reduceNonAdjacentRules: true,
      removeDuplicateFontRules: true,
      removeDuplicateMediaBlocks: true,
      removeDuplicateRules: true,
      removeUnusedAtRules: true,
      restructureRules: true,
      mergeIntoShorthands: true
    }
  }
};

const cleanCss = new CleanCSS(cleanCssOptions);

const options = {
  collapseWhitespace: true,
  removeComments: true,
  removeRedundantAttributes: true,
  removeScriptTypeAttributes: true,
  removeStyleLinkTypeAttributes: true,
  useShortDoctype: true,
  minifyCSS: (text) => {
    try {
      const output = cleanCss.minify(text);
      if (output.errors.length > 0) {
        console.error('CSS minification errors:', output.errors);
        return text;
      }
      return output.styles;
    } catch (err) {
      console.error('CSS minification error:', err);
      return text;
    }
  },
  minifyJS: async (text) => {
    try {
      const result = await terserMinify(text, terserOptions);
      return result.code;
    } catch (err) {
      console.error('JS minification error:', err);
      return text;
    }
  }
};

async function minifyHtmlFiles() {
  const files = fs.readdirSync('.');
  const htmlFiles = files.filter(f => f.endsWith('.html'));

  for (const file of htmlFiles) {
    const content = fs.readFileSync(file, 'utf8');
    const minified = await minify(content, options);
    const outFile = path.join('lib', file.replace('.html', '.min.html'));
    fs.writeFileSync(outFile, minified);
  }
}

minifyHtmlFiles().catch(console.error);
