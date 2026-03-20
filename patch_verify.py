import re

with open('ALCC/verify_androlua.sh', 'r') as f:
    content = f.read()

content = content.replace(
'''    if [ -f "patches/androlua533_support.patch" ]; then''',
'''    if [ -f "patches/androlua533_lundump_size.patch" ]; then
        (cd ../androlua533_source && patch -p1 < ../ALCC/patches/androlua533_lundump_size.patch)
    fi
    if [ -f "patches/androlua533_support.patch" ]; then''')

with open('ALCC/verify_androlua.sh', 'w') as f:
    f.write(content)
