#pragma once
// GET /filter - choose what the drop overlay (/) shows, with a checkmark per item.
// Saved in the plugin's drop-filter.json and pushed to open overlays at once.
// An overlay URL with show= or minrune= ignores the saved filter.

namespace LootDrop {

inline constexpr const char* FilterHtml = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Drop Filter</title>
<style>
:root{--bg:#15130f;--panel:#1e1b16;--line:#3a342a;--txt:#e8e1cc;--dim:#8f8672;--gold:#c7b377;--set:#1ec81e;--rune:#ffa800;--orb:#ff8c1a}
*{box-sizing:border-box}
body{margin:0;padding:14px 16px 40px;background:var(--bg);color:var(--txt);font:14px/1.4 "Segoe UI",system-ui,sans-serif}
header{display:flex;align-items:baseline;justify-content:space-between;gap:12px;flex-wrap:wrap;margin-bottom:10px}
h1{font-size:17px;margin:0;color:var(--gold);letter-spacing:.04em}
#status{font-size:12px;color:var(--dim)}
#status.ok{color:#8fd18f}#status.err{color:#ff7b6b}
.note{color:var(--dim);font-size:12px;margin:0 0 12px}
section{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:10px 12px;margin-bottom:12px}
h2{font-size:14px;margin:0 0 8px;display:flex;align-items:center;gap:10px;flex-wrap:wrap}
h2 .count{font-weight:normal;font-size:12px;color:var(--dim)}
.u h2{color:var(--gold)}.s h2{color:var(--set)}.r h2{color:var(--rune)}.o h2{color:var(--orb)}
.modes{display:flex;gap:4px;font-weight:normal}
.modes label{padding:3px 9px;border:1px solid var(--line);border-radius:4px;cursor:pointer;color:var(--dim);font-size:12px}
.modes input{display:none}
.modes input:checked+span{color:var(--txt)}
.modes label:has(input:checked){background:#3a2f1a;border-color:var(--gold);color:var(--txt)}
.tools{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:6px}
input[type=search]{flex:1;min-width:160px;background:#0f0d0a;border:1px solid var(--line);color:var(--txt);border-radius:4px;padding:5px 8px;font:inherit}
button{background:#2a251d;color:var(--txt);border:1px solid var(--line);border-radius:4px;padding:5px 10px;font:inherit;font-size:12px;cursor:pointer}
button:hover{border-color:var(--gold)}
.list{max-height:320px;overflow:auto;border:1px solid var(--line);border-radius:4px;background:#0f0d0a}
.list.off{opacity:.45}
.row{display:flex;align-items:center;gap:8px;padding:3px 8px;cursor:pointer;border-bottom:1px solid #1d1a14}
.row:hover{background:#221e17}
.row .b{color:var(--dim);font-size:12px;margin-left:auto;text-align:right}
.hint{font-size:12px;color:var(--dim);margin:6px 0 0}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:2px 10px}
.grid label{display:flex;align-items:center;gap:6px;cursor:pointer;padding:2px 0}
input[type=checkbox]{accent-color:var(--gold);width:15px;height:15px;margin:0;flex:none}
.empty{padding:10px;color:var(--dim);font-style:italic}
</style></head>
<body>
<header><h1>Drop overlay filter</h1><span id="status">loading...</span></header>
<p class="note">Checked items pop up on the drop overlay. Changes save by themselves and reach OBS straight away.</p>

<section class="u"><h2>Uniques <span class="modes" data-k="unique"></span><span class="count" id="u-count"></span></h2>
 <div class="tools"><input type="search" id="u-search" placeholder="Search name or base, e.g. Annihilus, Grand Charm"><button data-act="u-all">Check shown</button><button data-act="u-none">Uncheck shown</button></div>
 <div class="list" id="u-list"></div><p class="hint" id="u-hint"></p></section>

<section class="s"><h2>Set items <span class="modes" data-k="set"></span><span class="count" id="s-count"></span></h2>
 <div class="tools"><input type="search" id="s-search" placeholder="Search name or base"><button data-act="s-all">Check shown</button><button data-act="s-none">Uncheck shown</button></div>
 <div class="list" id="s-list"></div><p class="hint" id="s-hint"></p></section>

<section class="r"><h2>Runes <span class="count" id="r-count"></span></h2>
 <div class="tools"><button data-act="r-all">All</button><button data-act="r-high">Pul and up</button><button data-act="r-none">None</button></div>
 <div class="grid" id="r-grid"></div></section>

<section class="o"><h2>Orbs</h2><div class="grid" id="o-grid"></div></section>

<section><h2>Other drops</h2><div class="grid" id="x-grid"></div></section>

<script>
(function(){
const RUNES=['El','Eld','Tir','Nef','Eth','Ith','Tal','Ral','Ort','Thul','Amn','Sol','Shael','Dol','Hel','Io','Lum','Ko','Fal','Lem','Pul','Um','Mal','Ist','Gul','Vex','Ohm','Lo','Sur','Ber','Jah','Cham','Zod'];
const ORBS=[['ka3','Corruption'],['oos','Socketing'],['ooc','Conversion'],['ooa','Assemblage'],['ooi','Infusion'],['ooe','Shadows'],['oor','Renewal']];
const OTHER=[['rare','Rare'],['magic','Magic'],['crafted','Crafted'],['base','Normal / superior']];
const MODES=[['all','All'],['picked','Only checked'],['off','None']];
const DEFAULT={v:1,unique:'all',uniques:[],set:'all',sets:[],runes:[21,22,23,24,25,26,27,28,29,30,31,32,33],orbs:ORBS.map(o=>o[0]),other:[]};
let f=null,catalog={u:[],s:[]},saveTimer=null;
const $=id=>document.getElementById(id);
const status=(t,c)=>{const s=$('status');s.textContent=t;s.className=c||'';};
function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!==undefined)e.textContent=text;return e;}
function box(checked,on){const i=el('input');i.type='checkbox';i.checked=checked;i.addEventListener('change',()=>on(i.checked));return i;}
function save(){
 clearTimeout(saveTimer);status('saving...');
 saveTimer=setTimeout(async()=>{
  try{const r=await fetch('/api/filter',{method:'POST',headers:{'X-Loot-Overlay':'filter'},body:JSON.stringify(f)});
   status(r.ok?'Saved':'Save failed ('+r.status+')',r.ok?'ok':'err');}
  catch(e){status('Game not running? Not saved','err');}
 },400);
}
// Uniques and set items: mode buttons, search, checkmark list.
function itemSection(p,k){
 const picked=new Set(f[k+'s']);
 const modes=document.querySelector('.modes[data-k="'+k+'"]');modes.replaceChildren();
 for(const [m,label] of MODES){const l=el('label'),i=el('input');i.type='radio';i.name=k+'-mode';i.checked=f[k]===m;
  i.addEventListener('change',()=>{f[k]=m;hint();save();});l.appendChild(i);l.appendChild(el('span','',label));modes.appendChild(l);}
 const list=$(p+'-list'),search=$(p+'-search');
 function hint(){
  list.classList.toggle('off',f[k]!=='picked');
  $(p+'-hint').textContent=f[k]==='all'?'All are shown. Choose "Only checked" to show just the checked ones.':f[k]==='off'?'None are shown.':'';
  $(p+'-count').textContent=picked.size+' checked';
 }
 function shown(){const s=search.value.trim().toLowerCase();return catalog[p].filter(it=>!s||it.name.toLowerCase().includes(s)||it.base.toLowerCase().includes(s));}
 function draw(){
  list.replaceChildren();const items=shown();
  if(!items.length){list.appendChild(el('div','empty','nothing matches'));return;}
  for(const it of items){const r=el('label','row');
   r.appendChild(box(picked.has(it.id),on=>{on?picked.add(it.id):picked.delete(it.id);f[k+'s']=[...picked].sort((a,b)=>a-b);hint();save();}));
   r.appendChild(el('span','',it.name));r.appendChild(el('span','b',it.base));list.appendChild(r);}
 }
 search.addEventListener('input',draw);
 document.querySelector('[data-act="'+p+'-all"]').onclick=()=>{for(const it of shown())picked.add(it.id);f[k+'s']=[...picked].sort((a,b)=>a-b);draw();hint();save();};
 document.querySelector('[data-act="'+p+'-none"]').onclick=()=>{for(const it of shown())picked.delete(it.id);f[k+'s']=[...picked].sort((a,b)=>a-b);draw();hint();save();};
 draw();hint();
}
function grid(id,entries,key){
 const g=$(id),set=new Set(f[key]);g.replaceChildren();
 for(const [v,label] of entries){const l=el('label');
  l.appendChild(box(set.has(v),on=>{on?set.add(v):set.delete(v);f[key]=[...set];if(key==='runes'){f.runes.sort((a,b)=>a-b);$('r-count').textContent=f.runes.length+' of 33';}save();}));
  l.appendChild(el('span','',label));g.appendChild(l);}
}
function runes(){grid('r-grid',RUNES.map((n,i)=>[i+1,n+' #'+(i+1)]),'runes');$('r-count').textContent=f.runes.length+' of 33';}
function setRunes(list){f.runes=list;runes();save();}
document.querySelector('[data-act="r-all"]').onclick=()=>setRunes(RUNES.map((_,i)=>i+1));
document.querySelector('[data-act="r-high"]').onclick=()=>setRunes(RUNES.map((_,i)=>i+1).filter(n=>n>=21));
document.querySelector('[data-act="r-none"]').onclick=()=>setRunes([]);
async function load(){
 try{
  const [c,saved]=await Promise.all([fetch('/catalog.json').then(r=>r.json()),fetch('/filter.json').then(r=>r.json())]);
  for(const it of c)(it.k==='s'?catalog.s:catalog.u).push(it);
  for(const p of ['u','s'])catalog[p].sort((a,b)=>a.name.localeCompare(b.name));
  f=Object.assign({},DEFAULT,saved&&typeof saved==='object'?saved:{});
  for(const k of ['uniques','sets','runes','orbs','other'])if(!Array.isArray(f[k]))f[k]=DEFAULT[k].slice();
  itemSection('u','unique');itemSection('s','set');runes();grid('o-grid',ORBS,'orbs');grid('x-grid',OTHER,'other');
  status(saved?'Saved filter loaded':'Defaults (nothing saved yet)');
 }catch(e){status('Game not running? Start it and reload this page','err');}
}
load();
})();
</script></body></html>)HTML";

}
