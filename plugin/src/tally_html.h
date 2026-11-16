#pragma once
// GET /tally   - persistent stream tally (OBS browser source)
//   sections=rune,orb,charm,gem  which blocks to show, in that order (default
//                          rune,orb,charm,gem; runes are listed highest to lowest)
//   charms=found|all       unique charms: only those found (default), or every
//                          unique charm in the mod with zeros
//   minrune=21             lowest rune shown (default 21 = Pul and up; 1 = all)
//   gems=found|all|perfect found: only gem types seen (default); all: every type;
//                          perfect: only the Perfect column
//   grail=1|0              holy grail progress at the top (default 1; read from the
//                          Grail On! plugin's file: uniques and set items found)
//   scale=1, align=left|right, title=Stream%20Loot (no title unless given)
// GET /control - reset dock (OBS: Docks > Custom Browser Docks)

namespace LootDrop {

inline constexpr const char* TallyHtml = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Loot Tally</title>
<style>
:root{--rune:#ffa800;--orb:#ff8c1a;--gold:#c7b377;--dim:#6d675a;--txt:#e8e1cc;--scale:1}
html,body{margin:0;background:transparent;overflow:hidden}
body{font-family:"Palatino Linotype","Book Antiqua",Palatino,Georgia,serif;font-size:calc(17px*var(--scale));color:var(--txt)}
#panel{position:fixed;top:calc(16px*var(--scale));left:calc(16px*var(--scale));min-width:calc(230px*var(--scale));padding:.6em .8em .7em;background:rgba(8,6,4,.84);
 border:1px solid rgba(199,179,119,.35);border-radius:5px;box-shadow:0 4px 18px rgba(0,0,0,.55)}
body.right #panel{left:auto;right:calc(16px*var(--scale))}
h1{margin:0 0 .35em;font-size:.82em;letter-spacing:.14em;text-transform:uppercase;color:var(--gold);font-weight:bold}
h2:first-child{margin-top:0}
h2{margin:.55em 0 .25em;font-size:.62em;letter-spacing:.16em;text-transform:uppercase;color:#a99f86;font-weight:normal;border-bottom:1px solid rgba(199,179,119,.18);padding-bottom:.15em}
.row{display:flex;justify-content:space-between;gap:1.2em;line-height:1.3}
.row .n{font-variant-numeric:tabular-nums;font-weight:bold}
.orb{color:var(--orb)} .rune{color:var(--rune)} .charm{color:var(--gold)}
.row.zero{color:var(--dim)} .zero .n{font-weight:normal}
.chips{display:flex;flex-wrap:wrap;gap:.25em .7em;color:var(--rune)}
.chips .n{font-weight:bold;margin-left:.25em}
table{border-collapse:collapse;font-size:.86em}
td,th{padding:.05em .4em;text-align:center;font-variant-numeric:tabular-nums}
th{font-weight:normal;font-size:.72em;color:#a99f86;letter-spacing:.06em}
td.t{text-align:left;padding-left:0}
td.z{color:var(--dim)}
.empty{color:var(--dim);font-size:.8em;font-style:italic}
.bump{display:inline-block;animation:bump .7s ease-out}
@keyframes bump{0%{transform:scale(1)}25%{transform:scale(1.45)}100%{transform:scale(1)}}
.grail{margin:0 0 .45em}
.grail .top{display:flex;justify-content:space-between;align-items:baseline;gap:1em}
.grail .lbl{font-size:.62em;letter-spacing:.16em;text-transform:uppercase;color:#a99f86}
.grail .pct{font-size:1.35em;font-weight:bold;color:var(--gold);font-variant-numeric:tabular-nums}
.grail .cnt{font-size:.72em;color:#a99f86;font-variant-numeric:tabular-nums;text-align:right}
.grail .bar{height:.32em;margin-top:.2em;background:rgba(199,179,119,.15);border-radius:2px;overflow:hidden}
.grail .fill{height:100%;background:var(--gold);transform-origin:left center;transition:transform .8s ease-out}
.gema{color:#c77dff}.gemt{color:#ffe14d}.gems{color:#6fa8ff}.geme{color:#3fd36b}.gemr{color:#ff5555}.gemd{color:#f2f2f2}.gemz{color:#c9c2b0}.gemo{color:#9b8cff}
</style></head>
<body><div id="panel"></div>
<script>
(function(){
const q=new URLSearchParams(location.search);
const charmMode=(q.get('charms')||'found').toLowerCase();
const sections=(q.get('sections')||'rune,orb,charm,gem').toLowerCase().split(',').map(s=>s.trim()).filter(Boolean);
const minRune=Math.max(1,Math.min(33,parseInt(q.get('minrune')||'21',10)||21));
const gemMode=(q.get('gems')||'found').toLowerCase();
const scale=Math.max(.4,Math.min(4,parseFloat(q.get('scale')||'1')||1));
document.documentElement.style.setProperty('--scale',scale);
if((q.get('align')||'').toLowerCase()==='right')document.body.classList.add('right');
const title=q.get('title')||'';
const showGrail=q.get('grail')!=='0';
const ORBS=[['Corruption',['ka3','1ka']],['Socketing',['oos','1os']],['Conversion',['ooc','1oc']],['Assemblage',['ooa','1oa']],['Infusion',['ooi','1oi']],['Shadows',['ooe','1oe']],['Renewal',['oor','1or']]];
const RUNES=['El','Eld','Tir','Nef','Eth','Ith','Tal','Ral','Ort','Thul','Amn','Sol','Shael','Dol','Hel','Io','Lum','Ko','Fal','Lem','Pul','Um','Mal','Ist','Gul','Vex','Ohm','Lo','Sur','Ber','Jah','Cham','Zod'];
const GEMS=[['gema','Amethyst'],['gemt','Topaz'],['gems','Sapphire'],['geme','Emerald'],['gemr','Ruby'],['gemd','Diamond'],['gemz','Skull'],['gemo','Chaos Onyx']];
const GRADES=[['gem0','C'],['gem1','F'],['gem2','S'],['gem3','Fl'],['gem4','P']];
const panel=document.getElementById('panel');
let prev={},prevGrail=null;
function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!==undefined)e.textContent=text;return e;}
function num(key,value){const s=el('span','n',String(value));s.dataset.k=key;return s;}
function render(t){
 const by={};for(const it of t.items)by[it.code]=it;
 const counts={};
 panel.replaceChildren();
 if(title)panel.appendChild(el('h1','',title));
 if(showGrail&&t.grail&&t.grail.total>0){
  const gr=t.grail,frac=Math.min(1,gr.found/gr.total);
  const box=el('div','grail'),top=el('div','top'),left=el('div','');
  left.appendChild(el('div','lbl','Holy Grail'));
  const pct=el('span','pct',(frac*100).toFixed(1)+'%');pct.dataset.k='grail';left.appendChild(pct);
  top.appendChild(left);top.appendChild(el('div','cnt',gr.found+' / '+gr.total));box.appendChild(top);
  const bar=el('div','bar'),fill=el('div','fill');
  fill.style.transform='scaleX('+(prevGrail===null?frac:prevGrail)+')';bar.appendChild(fill);box.appendChild(bar);
  panel.appendChild(box);
  if(prevGrail!==null&&frac!==prevGrail)requestAnimationFrame(()=>requestAnimationFrame(()=>{fill.style.transform='scaleX('+frac+')';}));
  counts['grail']=gr.found;prevGrail=frac;
 }
 for(const sec of sections){
  if(sec==='orb'){
   if(t.game&&t.game.orbs===false)continue; // the original game has no orbs
   panel.appendChild(el('h2','','Orbs'));
   for(const [name,codes] of ORBS){
    const n=codes.reduce((a,c)=>a+(by[c]?by[c].count:0),0);counts['orb:'+name]=n;
    const r=el('div','row orb'+(n?'':' zero'));r.appendChild(el('span','',name));r.appendChild(num('orb:'+name,n));panel.appendChild(r);
   }
  }else if(sec==='rune'){
   panel.appendChild(el('h2','',minRune>1?'Runes ('+RUNES[minRune-1]+'+)':'Runes'));
   const found=t.items.filter(i=>i.kind==='rune'&&i.rune>=minRune).sort((a,b)=>b.rune-a.rune);
   if(!found.length){panel.appendChild(el('div','empty','none yet'));}
   else{for(const r of found){const row=el('div','row rune');row.appendChild(el('span','',(RUNES[r.rune-1]||r.name)+' #'+r.rune));row.appendChild(num('rune:'+r.rune,r.count));counts['rune:'+r.rune]=r.count;panel.appendChild(row);}}
  }else if(sec==='charm'){
   panel.appendChild(el('h2','','Unique Charms'));
   const byRec={};for(const i of t.items)if(i.kind==='charm')byRec[i.record]=(byRec[i.record]||0)+i.count;
   let list;
   if(charmMode==='all'){list=(t.charmCatalog||[]).map(c=>({name:c.name,record:c.id,count:byRec[c.id]||0}));}
   else{list=t.items.filter(i=>i.kind==='charm').map(i=>({name:i.name,record:i.record,count:i.count})).sort((a,b)=>b.count-a.count||a.name.localeCompare(b.name));}
   if(!list.length){panel.appendChild(el('div','empty','none yet'));continue;}
   for(const c of list){const r=el('div','row charm'+(c.count?'':' zero'));r.appendChild(el('span','',c.name));r.appendChild(num('charm:'+c.record,c.count));counts['charm:'+c.record]=c.count;panel.appendChild(r);}
  }else if(sec==='gem'){
   panel.appendChild(el('h2','','Gems'));
   const grid={};let any=false;
   for(const i of t.items){if(i.kind!=='gem')continue;const g=grid[i.type]||(grid[i.type]={});const col=GRADES.some(x=>x[0]===i.type2)?i.type2:'other';g[col]=(g[col]||0)+i.count;any=true;}
   const cols=gemMode==='perfect'?[['gem4','P']]:GRADES.concat(Object.values(grid).some(g=>g.other)?[['other','+']]:[]);
   const rows=GEMS.filter(([ty])=>gemMode==='all'||(grid[ty]&&cols.some(([c])=>grid[ty][c])));
   if(!rows.length){panel.appendChild(el('div','empty','none yet'));continue;}
   const tb=el('table');const hr=el('tr');hr.appendChild(el('th',''));for(const [,h] of cols)hr.appendChild(el('th','',h));tb.appendChild(hr);
   for(const [ty,name] of rows){const tr=el('tr',ty);tr.appendChild(el('td','t',name));
    for(const [c] of cols){const n=(grid[ty]&&grid[ty][c])||0;counts['gem:'+ty+c]=n;const td=el('td',n?'':'z');td.appendChild(num('gem:'+ty+c,n||'·'));tr.appendChild(td);}
    tb.appendChild(tr);}
   panel.appendChild(tb);
  }
 }
 // Bump only the numbers that went up since the last update.
 for(const [k,v] of Object.entries(counts)){if(prev[k]!==undefined&&v>prev[k]){const s=panel.querySelector('[data-k="'+CSS.escape(k)+'"]');if(s){s.classList.add('bump');s.addEventListener('animationend',()=>s.classList.remove('bump'),{once:true});}}}
 prev=counts;
}
const es=new EventSource('/events');
es.addEventListener('tally',ev=>{try{render(JSON.parse(ev.data));}catch(e){}});
})();
</script></body></html>)HTML";

inline constexpr const char* ControlHtml = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Loot Overlay Control</title>
<style>
body{margin:0;padding:12px;background:#15130f;color:#e8e1cc;font:14px/1.4 "Segoe UI",system-ui,sans-serif}
h1{font-size:15px;margin:0 0 8px;color:#c7b377;letter-spacing:.05em}
.meta{color:#a99f86;font-size:12px;margin-bottom:10px}
button{background:#5a1d14;color:#fff;border:1px solid #a33a2a;border-radius:4px;padding:8px 14px;font-size:14px;cursor:pointer}
button:hover{background:#7a271b}
button.armed{background:#b3361f}
#msg{margin-top:8px;font-size:12px;color:#8fd18f;min-height:1em}
code{color:#c7b377}
</style></head>
<body>
<h1>Loot Tally</h1>
<div class="meta">Counting since <span id="since">...</span><br><span id="total">0</span> orbs, runes and gems this session</div>
<button id="reset">Reset tally</button>
<div id="msg"></div>
<div class="meta" style="margin-top:14px">Overlay: <code>/tally</code> &middot; drops: <code>/</code> &middot; <a href="/filter" style="color:#c7b377">choose which drops show</a></div>
<script>
const btn=document.getElementById('reset'),msg=document.getElementById('msg');
let armed=false,timer;
btn.addEventListener('click',async()=>{
 if(!armed){armed=true;btn.classList.add('armed');btn.textContent='Click again to reset';timer=setTimeout(()=>{armed=false;btn.classList.remove('armed');btn.textContent='Reset tally';},4000);return;}
 clearTimeout(timer);armed=false;btn.classList.remove('armed');btn.textContent='Reset tally';
 try{const r=await fetch('/api/reset',{method:'POST',headers:{'X-Loot-Overlay':'reset'}});msg.textContent=r.ok?'Tally reset.':'Reset failed ('+r.status+')';}
 catch(e){msg.textContent='Game not running?';}
});
const es=new EventSource('/events');
es.addEventListener('tally',ev=>{try{const t=JSON.parse(ev.data);
 document.getElementById('since').textContent=new Date(t.started).toLocaleString();
 document.getElementById('total').textContent=t.items.reduce((a,i)=>a+i.count,0);}catch(e){}});
</script></body></html>)HTML";

}
