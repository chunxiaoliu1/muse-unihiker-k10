"""Bundle the USB configuration UI into one HTML with hashed inline assets."""
import base64
import hashlib
from pathlib import Path
import re

SOURCE = Path(__file__).resolve().parent / 'usb-config'


def build_page():
    html = (SOURCE / 'index.html').read_text(encoding='utf-8')
    style = '\n' + (SOURCE / 'style.css').read_text(encoding='utf-8').strip() + '\n'
    client = (SOURCE / 'serial-client.mjs').read_text(encoding='utf-8')
    client = re.sub(r'^export ', '', client, flags=re.M)
    app = (SOURCE / 'app.mjs').read_text(encoding='utf-8')
    app = re.sub(r"^import .* from './serial-client\.mjs';\s*", '', app, flags=re.M)
    script = '\n(() => {\n"use strict";\n' + client.strip() + '\n' + app.strip() + '\n})();\n'
    if re.search(r'</(?:script|style)', script + style, re.I):
        raise ValueError('inline asset contains an HTML closing tag')
    def digest(text):
        return base64.b64encode(hashlib.sha256(text.encode('utf-8')).digest()).decode('ascii')
    policy = (f"default-src 'none'; script-src 'sha256-{digest(script)}'; "
              f"style-src 'sha256-{digest(style)}'; connect-src 'none'; "
              "object-src 'none'; base-uri 'none'; form-action 'none'")
    html = re.sub(r'(<meta http-equiv="Content-Security-Policy" content=")[^"]*(">)',
                  lambda match: match[1] + policy + match[2], html)
    html = html.replace('<link rel="stylesheet" href="style.css">', '<style>' + style + '</style>')
    html = html.replace('<script type="module" src="app.mjs"></script>', '')
    html = html.replace('</body>', '<script>' + script + '</script>\n</body>')
    output = SOURCE / 'k10-usb-config.html'
    output.write_text(html, encoding='utf-8', newline='\n')
    return output


if __name__ == '__main__': print(build_page())
