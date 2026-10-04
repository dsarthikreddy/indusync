import re
src = open('/home/claude/v82/classync_stage1_v8_2/classync_stage1_v8_2.ino').read()
early = src[src.index('#include "secrets.h"'):src.index('void v8Event(uint8_t type, int person')].replace('#include "secrets.h"', '')
blk = src[src.index('#define V8_EV_RING'):src.index('// ================= Serial Monitor commands =================')]
a = blk.index('int v9Http(const char* method'); b = blk.index('\n}\n', a) + 3
blk = blk[:a] + 'int v9Http(const char* method, const String& url, const String& body, String& reply, const char* auth = NULL, int timeout = 8000);\n' + blk[b:]
secrets = open('v82.h').read().split('#define TZ_OFF')[0] if False else ''
old = open('v82.h').read()
head = old[:old.index('#define TG_CHAT_ID')]
head += '#define TG_CHAT_ID "1234567890"\n#define SHEET_HOOK "https://script.google.com/macros/s/TEST/exec?key=K"\n'
defs = [l for l in src.splitlines() if l.startswith(('#define TZ_OFF', '#define DEVICE_ID'))]
open('v82.h', 'w').write("#define V9_HOST_TEST 1\n" + head + "\n".join(defs) + "\n" + early + "\nint v8LearnCard(const String& uid);\n" + blk)
print("extracted")
