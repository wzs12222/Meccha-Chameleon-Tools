# Fix i18n.py - add missing Chinese anti_esp key
with open('E:/dev/Python/Meccha-Chameleon-Tools-1.8.0.1/meccha_chameleon_tools/i18n.py', 'r', encoding='utf-8') as f:
    content = f.read()

zh_teammates = '"esp_show_teammates": "\u663e\u793a\u961f\u53cb"'
zh_anti_esp = '"anti_esp": "\u53cd\u900f\u89c6 (\u6df7\u6dc6\u540d\u79f0)"'

if zh_teammates in content and zh_anti_esp not in content:
    content = content.replace(zh_teammates, zh_teammates + ',\n        ' + zh_anti_esp)
    with open('E:/dev/Python/Meccha-Chameleon-Tools-1.8.0.1/meccha_chameleon_tools/i18n.py', 'w', encoding='utf-8') as f:
        f.write(content)
    print('Fixed: added Chinese anti_esp')
else:
    print('No fix needed')

import py_compile
py_compile.compile('E:/dev/Python/Meccha-Chameleon-Tools-1.8.0.1/meccha_chameleon_tools/i18n.py', doraise=True)
print('Syntax OK')
