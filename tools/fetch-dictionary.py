import pathlib,urllib.request
req=urllib.request.Request('https://raw.githubusercontent.com/skywind3000/ECDICT/master/ecdict.csv',headers={'User-Agent':'EType-development'})
with urllib.request.urlopen(req,timeout=180) as r,open('third_party/ecdict.csv','wb') as f:
 while True:
  b=r.read(1024*1024)
  if not b:break
  f.write(b)
print('ECDICT downloaded:',pathlib.Path('third_party/ecdict.csv').stat().st_size,flush=True)
