"""Exercise the exact on-router Lua reducer with sample GL.iNet RPC replies.

Requires lupa in the Python environment (a temporary isolated install is fine).
"""
from pathlib import Path
import json,re
from lupa import LuaRuntime,LuaError,lua_type
root=Path(__file__).resolve().parents[1]
command=re.search(r'R"SH\((.*)\)SH";', (root/'NetworkMonitor/RouterCommand.h').read_text(),re.S)[1]
script=re.search(r'\| "\$nm_lua" -e \'(.*)\'\n',command,re.S)[1]
assert '"clients","get_list"' in command and 'ip -4 neigh' not in command and '/tmp/dhcp.leases' not in command

def reduce(response, module='cjson.safe'):
 runtime=LuaRuntime(unpack_returned_tuples=True);output=[]
 def native(v):
  if lua_type(v)=='table':return {k:native(value) for k,value in v.items()}
  return v
 runtime.globals().test_decode=lambda _:runtime.table_from(response,recursive=True)
 runtime.globals().test_encode=lambda v:json.dumps(native(v),ensure_ascii=False,separators=(',',':'))
 runtime.globals().print=lambda *args:output.append(' '.join(map(str,args)))
 runtime.execute('io.read=function() return "fixture" end')
 runtime.execute('package.preload["'+module+'"]=function() return {decode=test_decode,encode=test_encode,parse=test_decode,stringify=test_encode} end')
 runtime.execute(script)
 return output
clients=[dict(mac='aa:bb:cc:dd:ee:01',ip='192.168.1.2',name='Hostname',alias='My laptop',online=True,iface='5G',last_rx=list(range(60))),dict(mac='aa:bb:cc:dd:ee:02',ip='',alias='',name='Old phone',online=0,iface='cable')]
lines=reduce(dict(id=1,result=dict(clients=clients)))
assert lines[0]=='NM2_GLCLIENTS' and lines[-1]=='NM2_DONE 2'
rows=[json.loads(line) for line in lines[1:-1]]
assert rows[0]['name']=='My laptop' and rows[0]['online'] is True
assert rows[1]['name']=='Old phone' and rows[1]['online'] is False and rows[1]['ip']==''
assert all('last_rx' not in row for row in rows)
# MAC-keyed objects and each supported JSON library fallback.
for module in ('cjson.safe','cjson','luci.jsonc'):
 lines=reduce(dict(id=1,result=dict(clients={'aa:bb:cc:dd:ee:03':dict(ip='',online=False,name='雪 printer')})),module)
 assert json.loads(lines[1])['mac']=='aa:bb:cc:dd:ee:03' and json.loads(lines[1])['name']=='雪 printer'
assert reduce(dict(id=1,result=dict(clients=[])))[-1]=='NM2_DONE 0'
for bad in [dict(id=1,error=dict(code=-1)),dict(id=1,result={}),dict(id=1,result=dict(clients=[dict(mac='x',online='false')])),dict(id=9,result=dict(clients=[]))]:
 try:reduce(bad)
 except LuaError:pass
 else:raise AssertionError('Invalid API response accepted')
print('GL.iNet reducer checks passed: API envelope, aliases, online flags, MAC-keyed lists, Unicode, JSON-library fallbacks and traffic-history removal.')
