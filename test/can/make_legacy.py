#!/usr/bin/env python3
"""Copy the library's CAN drivers, compiled without CAN_SHARED_SERVICE (the
existing drivers, unchanged), into OUT with the register model's sync points
added: the model applies a write's effect (UINC, RXFUL release, a mode
request) when q83_sync()/k80_sync() is called, where the PIC does it at once.
Also points the Q83 message RAM at the model's array.
    make_legacy.py LIBDIR OUT"""
import re, sys, os
lib, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
s = open(os.path.join(lib, "can18_can_2.c")).read()
s = s.replace("#define CAN1_BUFFERS_BASE_ADDRESS           0x3BB0",
              "#define CAN1_BUFFERS_BASE_ADDRESS           ((uintptr_t)q83_msgram)")
lines = []
for line in s.split("\n"):
    code = line.split("//")[0]
    if (re.search(r"(UINC\s*=\s*1|_UINC_MASK|_TXREQ_MASK)", code) and ";" in code and "=" in code
            and not code.strip().startswith("#")):
        i = line.index(";")
        line = line[:i + 1] + " q83_sync();" + line[i + 1:]
    lines.append(line)
s = "\n".join(lines).replace("        while (C1CONUbits.OPMOD != requestMode)\n        {",
                             "        while (C1CONUbits.OPMOD != requestMode)\n        {   q83_sync();")
open(os.path.join(out, "can18_can_2.c"), "w").write(s)
s = open(os.path.join(lib, "can18_ecan.c")).read()
s = s.replace("while (CANSTATbits.OPMODE2 == 0);", "while (CANSTATbits.OPMODE2 == 0) k80_sync();")
s = s.replace("while (CANSTATbits.OPMODE2 != 0);", "while (CANSTATbits.OPMODE2 != 0) k80_sync();")
s = s.replace("ptr[CON] &= 0x7f;", "ptr[CON] &= 0x7f; k80_sync();")
s = s.replace("p[CON] &= 0x7f;    // clear RXFUL bit", "p[CON] &= 0x7f; k80_sync();   // clear RXFUL bit")
open(os.path.join(out, "can18_ecan.c"), "w").write(s)
