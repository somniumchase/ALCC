import sys
import os

def build_html(template_path, js_path, output_path):
    if not os.path.exists(template_path):
        print(f"Error: Template file not found: {template_path}")
        sys.exit(1)

    if not os.path.exists(js_path):
        print(f"Error: JS file not found: {js_path}")
        sys.exit(1)

    with open(template_path, 'r', encoding='utf-8') as f:
        html_content = f.read()

    with open(js_path, 'r', encoding='utf-8') as f:
        js_content = f.read()

    # Create a minimal Module object to intercept print/printErr before emscripten loads
    script_injection = """
<script>
    var Module = {
        print: (function() {
            return function(text) {
                if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
                var output = document.getElementById('output-text');
                if (output) {
                    output.value += text + "\\n";
                }
            };
        })(),
        printErr: function(text) {
            if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
            var output = document.getElementById('output-text');
            if (output) {
                output.value += "Error: " + text + "\\n";
            }
        },
        onRuntimeInitialized: function() {
            console.log("WASM Runtime initialized");
        }
    };
</script>
<script>
""" + js_content + """
</script>
"""

    # The HTML has a placeholder `// INSERT_WASM_HERE`
    if "// INSERT_WASM_HERE" in html_content:
        html_content = html_content.replace("// INSERT_WASM_HERE", script_injection)
    else:
        # Fallback to appending right before </body>
        html_content = html_content.replace("</body>", script_injection + "\n</body>")

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(html_content)
    print(f"Successfully generated {output_path}")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python build_html.py <template.html> <alcc_web.js> <output.html>")
        sys.exit(1)

    build_html(sys.argv[1], sys.argv[2], sys.argv[3])
