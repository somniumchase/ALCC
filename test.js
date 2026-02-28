const fs = require('fs');
const html = fs.readFileSync('ALCC/index.html', 'utf8');
if (html.includes('<script>\ndocument.addEventListener')) {
    console.log("Found raw text leak.");
}
