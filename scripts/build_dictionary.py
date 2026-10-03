"""Prepare a deterministic offline single-word dictionary from ECDICT.

Retains common/exam words, derives attested inflections from exchange records,
and applies small reviewed candidate overrides for everyday input. Never guesses
word meanings or IPA. The source IPA is predominantly British (see NOTICE).
"""
import csv
import hashlib
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party' / 'ecdict.csv'
TOKEN = re.compile(r"[a-z]+(?:[-'][a-z]+)*\Z")
POS = {'n.':'名词','v.':'动词','vt.':'及物动词','vi.':'不及物动词',
       'a.':'形容词','adj.':'形容词','adv.':'副词','prep.':'介词',
       'pron.':'代词','conj.':'连词','num.':'数词','art.':'冠词','interj.':'感叹词',
       'aux.':'助动词','modal.':'情态动词'}
FORM = {'s':'复数','p':'过去式','d':'过去分词','i':'现在分词',
        '3':'第三人称单数','r':'比较级','t':'最高级'}
def rank(row):
    values = [int(row.get(k) or 0) for k in ('frq','bnc')]
    return min([n for n in values if n > 0] or [1000000])

def candidates(translation):
    result=[]
    for line in translation.replace('\\n','\n').splitlines():
        line=line.strip()
        if line.startswith('[') or '人名' in line or '过去式' in line or '过去分词' in line:
            continue
        m=re.match(r'^([a-z]+\.)\s*(.*)$',line)
        if not m or m[1] not in POS:
            continue
        for value in re.split(r'[,，;；、]',m[2]):
            value=value.strip()
            # Long explanations, domain labels and usage examples are unsuitable
            # as text-input candidates; keep the original in the upstream file.
            if not value or len(value)>14 or not re.search(r'[\u4e00-\u9fff]',value):continue
            if re.search(r'[\[\]{}()（）:：=<>…]|\.\.|\b[a-zA-Z]{2,}\b',value):continue
            pair=(value,POS[m[1]])
            if pair not in result:result.append(pair)
    return result

def main():
    rows={}
    with SOURCE.open(encoding='utf-8-sig',newline='') as f:
        for row in csv.DictReader(f):
            word=row['word'].lower()
            if not TOKEN.fullmatch(word) or len(word)>48:continue
            if not (rank(row)<=30000 or row.get('tag') or int(row.get('collins') or 0)>0 or row.get('oxford')=='1'):continue
            # Prefer the actual lowercase entry over a proper-name collision.
            if word not in rows or row['word']==word:rows[word]=row
    entries={}
    for word,row in sorted(rows.items()):
        cs=candidates(row['translation'])
        if cs:entries[word]={'word':word,'ipa':row['phonetic'],'root':'','form':'','rank':rank(row),'candidates':cs}
    overrides=json.loads((ROOT/'data'/'overrides.json').read_text(encoding='utf-8'))
    for word,value in overrides.items():
        previous=entries.get(word,{})
        source=rows.get(word,{})
        entries[word]={'word':word,'ipa':previous.get('ipa',source.get('phonetic','')),
                       'root':'','form':'','rank':previous.get('rank',value.get('rank',2000)),
                       'candidates':[tuple(x) for x in value['candidates']]}
    derived=0
    for base,row in sorted(rows.items()):
        if base not in entries:continue
        original=entries[base]
        for exchange in row.get('exchange','').split('/'):
            if ':' not in exchange:continue
            kind,word=exchange.split(':',1);word=word.lower()
            if kind not in FORM or word==base or not TOKEN.fullmatch(word):continue
            if word not in entries:
                entries[word]={'word':word,'ipa':rows.get(word,{}).get('phonetic',''),
                               'root':base,'form':FORM[kind],'rank':original['rank']+100,
                               'candidates':list(original['candidates'])}
                derived+=1
            else:
                e=entries[word]
                # Exact meanings remain first, so saw retains its noun senses.
                for candidate in original['candidates']:
                    if candidate not in e['candidates']:e['candidates'].append(candidate)
                if not e['root']:e['root']=base;e['form']=FORM[kind]
                elif e['root']==base and FORM[kind] not in e['form']:e['form']+='／'+FORM[kind]
    # Upstream attested irregular forms may occur only on the form's record.
    for word,row in sorted(rows.items()):
        fields=dict(x.split(':',1) for x in row.get('exchange','').split('/') if ':' in x)
        base=fields.get('0','').lower()
        if base==word or base not in entries:continue
        if word not in entries:
            entries[word]={'word':word,'ipa':row['phonetic'],'root':base,
                           'form':'／'.join(FORM[x] for x in fields.get('1','') if x in FORM),
                           'rank':entries[base]['rank']+100,'candidates':list(entries[base]['candidates'])}
            derived+=1
    # Reviewed everyday roots also define the primary candidates for their forms.
    # This prevents noisy legacy entries such as books -> 书评 from leading input.
    for word,e in entries.items():
        if word not in overrides and e['root'] in overrides:
            e['candidates']=list(entries[e['root']]['candidates'])
            e['rank']=min(e['rank'],entries[e['root']]['rank']+100)
    # Fetch existing IPA for derived forms even if that source record had no
    # frequency/exam tags. Missing IPA remains empty; it is never guessed.
    with SOURCE.open(encoding='utf-8-sig',newline='') as f:
        for row in csv.DictReader(f):
            word=row['word'].lower()
            if word in entries and not entries[word]['ipa'] and row['phonetic']:
                entries[word]['ipa']=row['phonetic']
    target=ROOT/'data'/'dictionary.tsv'
    with target.open('w',encoding='utf-8',newline='\n') as f:
        f.write('# EType dictionary v1; UTF-8; word\tIPA\troot\tform\trank\tChinese\tPOS\n')
        for word,e in sorted(entries.items()):
            for chinese,pos in e['candidates']:
                fields=[word,e['ipa'],e['root'],e['form'],str(e['rank']),chinese,pos]
                f.write('\t'.join(x.replace('\t',' ').replace('\n',' ') for x in fields)+'\n')
    manifest={'version':'1.0.0','source':'https://github.com/skywind3000/ECDICT',
              'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              'entry_count':len(entries),'derived_entry_count':derived,
              'candidate_count':sum(len(x['candidates']) for x in entries.values()),
              'dictionary_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
              'ipa_note':'Source phonetics are predominantly British; speech defaults to US English.'}
    (ROOT/'data'/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(manifest,ensure_ascii=False))
if __name__=='__main__':main()
