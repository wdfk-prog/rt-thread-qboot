from building import *

cwd = GetCurrentDir()
path = [cwd+'/inc']
src  = Glob('src/*.c')
src  += Glob('platform/*.c')
src += ['algorithm/qboot_none.c']

if GetDepend('QBOOT_USING_AES'):
    src += ['algorithm/qboot_aes.c']
if GetDepend('QBOOT_USING_GZIP'):
    src += ['algorithm/qboot_gzip.c']
if GetDepend('QBOOT_USING_QUICKLZ'):
    src += ['algorithm/qboot_quicklz.c']
if GetDepend('QBOOT_USING_FASTLZ'):
    src += ['algorithm/qboot_fastlz.c']
if GetDepend('QBOOT_USING_HPATCHLITE'):
    src += ['algorithm/qboot_hpatchlite.c']
 
group = DefineGroup('qboot', src, depend = ['PKG_USING_QBOOT'], CPPPATH = path)

Return('group')