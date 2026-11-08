#!/usr/bin/env python3
"""把 VecExceptionGen.sv 复制成 VecExceptionGenX.sv，并把内部判据引到输出口上。"""
import sys, os

src, dst = sys.argv[1], sys.argv[2]
bools = ['instIllegal','villIllegal','eewIllegal','emulIllegal','regNumIllegal',
         'regOverlapIllegal','vstartIllegal','notDependVtypeInst']
wide  = ['vs1Emul','vs2Emul','vdEmul']

s = open(src).read().replace('module VecExceptionGen(', 'module VecExceptionGenX(', 1)
he = s.index('\n);')
head, rest = s[:he], s[he:]
head = head.rstrip() + '\n' + ''.join('  ,output o_%s\n' % n for n in bools) \
                       + ''.join('  ,output [2:0] o_%s\n' % n for n in wide)
assigns = ''.join('  assign o_%s = %s;\n' % (n, n) for n in bools + wide)
j = rest.rindex('endmodule')
rest = rest[:j] + assigns + rest[j:]
open(dst, 'w').write(head + rest)
print('wrote', dst)
