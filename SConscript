from building import *

cwd = GetCurrentDir()
path = [cwd+'/inc']
src  = Glob('src/*.c')
src  += Glob('platform/*.c')
src  += Glob('algorithm/*.c')
 
group = DefineGroup('qboot', src, depend = ['PKG_USING_QBOOT'], CPPPATH = path)

Return('group')