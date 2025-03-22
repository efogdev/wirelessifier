const fs = require('fs');
const path = require('path');
const minify = require('html-minifier-terser').minify;

const options = {
  collapseWhitespace: true,
  removeComments: true,
  removeRedundantAttributes: true,
  removeScriptTypeAttributes: true,
  removeStyleLinkTypeAttributes: true,
  useShortDoctype: true,
  minifyCSS: true,
  minifyJS: true
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
